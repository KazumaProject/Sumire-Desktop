// src/dictionary_builder/mozc_fetch/mozc_fetch.cpp
//
// Downloads Mozc dictionary00.txt..dictionary09.txt, parses TSV columns as:
// reading (string), left_id (int16), right_id (int16), score (int16), word (string)
// Adds a flag for word script: HiraganaOnly / KatakanaOnly / Other
// Builds reading -> list of (word, flag)
// Sort keys by: (1) shorter reading first (by Unicode codepoint count), (2) hiragana order (lexicographic by codepoints)
// Prints per-file entry count and text size, plus totals and random samples.
// Also downloads connection_single_column.txt.
//
// Build (Ubuntu):
//   sudo apt-get install -y g++ libcurl4-openssl-dev
//   g++ -std=c++20 -O2 src/dictionary_builder/mozc_fetch/mozc_fetch.cpp -lcurl -o mozc_dic_fetch
// Run:
//   ./mozc_dic_fetch

#include <curl/curl.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

enum class WordScript : std::uint8_t
{
    HiraganaOnly = 0,
    KatakanaOnly = 1,
    Other = 2,
};

static const char *word_script_name(WordScript s)
{
    switch (s)
    {
    case WordScript::HiraganaOnly:
        return "HIRAGANA";
    case WordScript::KatakanaOnly:
        return "KATAKANA";
    default:
        return "OTHER";
    }
}

struct Entry
{
    std::string reading;
    int16_t left_id;
    int16_t right_id;
    int16_t score;
    std::string word;
    WordScript word_script;
};

struct WordItem
{
    std::string word;
    WordScript script;
};

// --------------------
// libcurl: write callback
// --------------------
static size_t write_to_file(void *ptr, size_t size, size_t nmemb, void *stream)
{
    std::ofstream *out = static_cast<std::ofstream *>(stream);
    const size_t bytes = size * nmemb;
    out->write(static_cast<const char *>(ptr), static_cast<std::streamsize>(bytes));
    return bytes;
}

static void download_file(const std::string &url, const std::filesystem::path &out_path)
{
    CURL *curl = curl_easy_init();
    if (!curl)
        throw std::runtime_error("curl_easy_init failed");

    std::filesystem::create_directories(out_path.parent_path());

    std::ofstream out(out_path, std::ios::binary);
    if (!out)
    {
        curl_easy_cleanup(curl);
        throw std::runtime_error("Failed to open output file: " + out_path.string());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_file);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);

    // Reasonable timeouts
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);

    const CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_easy_cleanup(curl);
    out.close();

    if (res != CURLE_OK)
        throw std::runtime_error(std::string("Download failed: ") + curl_easy_strerror(res));
    if (http_code < 200 || http_code >= 300)
        throw std::runtime_error("HTTP error: " + std::to_string(http_code));
}

// --------------------
// parsing helpers
// --------------------
static int16_t parse_i16(std::string_view s, const char *field_name)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
        s.remove_suffix(1);

    int value = 0;
    const char *begin = s.data();
    const char *end = s.data() + s.size();
    auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc() || ptr != end)
        throw std::runtime_error(std::string("Invalid int field (") + field_name + "): " + std::string(s));
    if (value < std::numeric_limits<int16_t>::min() || value > std::numeric_limits<int16_t>::max())
        throw std::runtime_error(std::string("Out of int16 range (") + field_name + "): " + std::to_string(value));
    return static_cast<int16_t>(value);
}

static bool split5_tabs(std::string_view line,
                        std::string_view &c0,
                        std::string_view &c1,
                        std::string_view &c2,
                        std::string_view &c3,
                        std::string_view &c4)
{
    size_t p0 = line.find('\t');
    if (p0 == std::string_view::npos)
        return false;
    size_t p1 = line.find('\t', p0 + 1);
    if (p1 == std::string_view::npos)
        return false;
    size_t p2 = line.find('\t', p1 + 1);
    if (p2 == std::string_view::npos)
        return false;
    size_t p3 = line.find('\t', p2 + 1);
    if (p3 == std::string_view::npos)
        return false;

    c0 = line.substr(0, p0);
    c1 = line.substr(p0 + 1, p1 - (p0 + 1));
    c2 = line.substr(p1 + 1, p2 - (p1 + 1));
    c3 = line.substr(p2 + 1, p3 - (p2 + 1));
    c4 = line.substr(p3 + 1);
    return true;
}

// --------------------
// UTF-8 helpers (decode, compare, and script classification)
// --------------------
static bool utf8_next_codepoint(const std::string &s, size_t &i, char32_t &out_cp)
{
    if (i >= s.size())
        return false;

    const unsigned char c0 = static_cast<unsigned char>(s[i]);

    if (c0 < 0x80)
    {
        out_cp = static_cast<char32_t>(c0);
        ++i;
        return true;
    }

    if ((c0 & 0xE0) == 0xC0)
    {
        if (i + 1 >= s.size())
            return false;
        const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
        if ((c1 & 0xC0) != 0x80)
            return false;

        char32_t cp = static_cast<char32_t>(c0 & 0x1F);
        cp = (cp << 6) | static_cast<char32_t>(c1 & 0x3F);
        if (cp < 0x80)
            return false;

        out_cp = cp;
        i += 2;
        return true;
    }

    if ((c0 & 0xF0) == 0xE0)
    {
        if (i + 2 >= s.size())
            return false;
        const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
        const unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80)
            return false;

        char32_t cp = static_cast<char32_t>(c0 & 0x0F);
        cp = (cp << 6) | static_cast<char32_t>(c1 & 0x3F);
        cp = (cp << 6) | static_cast<char32_t>(c2 & 0x3F);
        if (cp < 0x800)
            return false;
        if (cp >= 0xD800 && cp <= 0xDFFF)
            return false;

        out_cp = cp;
        i += 3;
        return true;
    }

    if ((c0 & 0xF8) == 0xF0)
    {
        if (i + 3 >= s.size())
            return false;
        const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
        const unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
        const unsigned char c3 = static_cast<unsigned char>(s[i + 3]);
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80)
            return false;

        char32_t cp = static_cast<char32_t>(c0 & 0x07);
        cp = (cp << 6) | static_cast<char32_t>(c1 & 0x3F);
        cp = (cp << 6) | static_cast<char32_t>(c2 & 0x3F);
        cp = (cp << 6) | static_cast<char32_t>(c3 & 0x3F);
        if (cp < 0x10000)
            return false;
        if (cp > 0x10FFFF)
            return false;

        out_cp = cp;
        i += 4;
        return true;
    }

    return false;
}

static size_t utf8_codepoint_count(const std::string &s)
{
    size_t i = 0;
    size_t count = 0;
    char32_t cp = 0;
    while (i < s.size())
    {
        const size_t prev = i;
        if (!utf8_next_codepoint(s, i, cp))
            i = prev + 1;
        ++count;
    }
    return count;
}

static bool utf8_lex_less_by_codepoint(const std::string &a, const std::string &b)
{
    size_t ia = 0, ib = 0;
    char32_t ca = 0, cb = 0;

    while (ia < a.size() && ib < b.size())
    {
        size_t pa = ia, pb = ib;
        if (!utf8_next_codepoint(a, ia, ca))
        {
            ca = static_cast<unsigned char>(a[pa]);
            ia = pa + 1;
        }
        if (!utf8_next_codepoint(b, ib, cb))
        {
            cb = static_cast<unsigned char>(b[pb]);
            ib = pb + 1;
        }

        if (ca < cb)
            return true;
        if (ca > cb)
            return false;
    }
    return a.size() < b.size();
}

struct ReadingKeyLess
{
    bool operator()(const std::string &a, const std::string &b) const
    {
        const size_t la = utf8_codepoint_count(a);
        const size_t lb = utf8_codepoint_count(b);
        if (la != lb)
            return la < lb;
        return utf8_lex_less_by_codepoint(a, b);
    }
};

static bool is_hiragana_cp(char32_t cp)
{
    return (cp >= 0x3040 && cp <= 0x309F);
}

static bool is_katakana_cp(char32_t cp)
{
    if (cp >= 0x30A0 && cp <= 0x30FF)
        return true;
    if (cp >= 0x31F0 && cp <= 0x31FF)
        return true;
    if (cp >= 0xFF65 && cp <= 0xFF9F)
        return true;
    return false;
}

static bool is_common_allowed_for_both(char32_t cp)
{
    return (cp == 0x30FC); // "ー"
}

static bool is_katakana_extra_allowed(char32_t cp)
{
    return (cp == 0x30FB); // "・"
}

static WordScript classify_word_script_utf8(const std::string &word)
{
    bool has_hira = false;
    bool has_kata = false;
    bool has_other = false;

    size_t i = 0;
    char32_t cp = 0;

    while (i < word.size())
    {
        const size_t prev = i;
        if (!utf8_next_codepoint(word, i, cp))
        {
            has_other = true;
            i = prev + 1;
            continue;
        }

        if (is_common_allowed_for_both(cp))
            continue;

        if (is_hiragana_cp(cp))
        {
            has_hira = true;
            continue;
        }

        if (is_katakana_cp(cp) || is_katakana_extra_allowed(cp))
        {
            has_kata = true;
            continue;
        }

        has_other = true;
    }

    if (!has_other && has_hira && !has_kata)
        return WordScript::HiraganaOnly;
    if (!has_other && has_kata && !has_hira)
        return WordScript::KatakanaOnly;

    return WordScript::Other;
}

// --------------------
// read TSV
// --------------------
static std::vector<Entry> read_dictionary_tsv(const std::filesystem::path &path)
{
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("Failed to open: " + path.string());

    std::vector<Entry> entries;
    std::string line;
    entries.reserve(300000);

    while (std::getline(in, line))
    {
        if (line.empty())
            continue;

        std::string_view sv(line);
        if (!sv.empty() && sv.front() == '#')
            continue;

        std::string_view reading, left, right, score, word;
        if (!split5_tabs(sv, reading, left, right, score, word))
            throw std::runtime_error("Invalid TSV line (expected 5 columns): " + line);

        Entry e;
        e.reading = std::string(reading);
        e.left_id = parse_i16(left, "left_id");
        e.right_id = parse_i16(right, "right_id");
        e.score = parse_i16(score, "score");
        e.word = std::string(word);
        e.word_script = classify_word_script_utf8(e.word);

        entries.emplace_back(std::move(e));
    }

    return entries;
}

static std::string human_bytes(std::uintmax_t bytes)
{
    const double b = static_cast<double>(bytes);
    const double kib = 1024.0;
    const double mib = kib * 1024.0;
    const double gib = mib * 1024.0;

    char buf[64];
    if (b >= gib)
        std::snprintf(buf, sizeof(buf), "%.2f GiB (%ju bytes)", b / gib, bytes);
    else if (b >= mib)
        std::snprintf(buf, sizeof(buf), "%.2f MiB (%ju bytes)", b / mib, bytes);
    else if (b >= kib)
        std::snprintf(buf, sizeof(buf), "%.2f KiB (%ju bytes)", b / kib, bytes);
    else
        std::snprintf(buf, sizeof(buf), "%ju bytes", bytes);
    return buf;
}

static std::string two_digits(int i)
{
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d", i);
    return buf;
}

static void print_random_samples(
    const std::vector<std::string> &sorted_keys,
    const std::unordered_map<std::string, std::vector<WordItem>> &dict,
    size_t sample_count,
    size_t max_words_per_key)
{
    if (sorted_keys.empty())
    {
        std::cout << "\n[Samples] (empty)\n";
        return;
    }

    std::random_device rd;
    std::mt19937_64 rng(rd());
    std::uniform_int_distribution<size_t> dist(0, sorted_keys.size() - 1);

    std::cout << "\n=== RANDOM SAMPLES ===\n";
    for (size_t s = 0; s < sample_count; ++s)
    {
        const auto &key = sorted_keys[dist(rng)];
        auto it = dict.find(key);
        if (it == dict.end())
            continue;

        const auto &items = it->second;

        std::cout << "[" << (s + 1) << "] reading=\"" << key << "\""
                  << " (words=" << items.size() << ")\n";

        const size_t n = std::min(max_words_per_key, items.size());
        for (size_t i = 0; i < n; ++i)
        {
            std::cout << "  - " << items[i].word << " [" << word_script_name(items[i].script) << "]\n";
        }
        if (items.size() > n)
        {
            std::cout << "  ... (" << (items.size() - n) << " more)\n";
        }
    }
}

static bool file_exists_nonempty(const std::filesystem::path &p)
{
    if (!std::filesystem::exists(p))
        return false;
    std::error_code ec;
    const auto sz = std::filesystem::file_size(p, ec);
    return (!ec && sz > 0);
}

int main()
{
    try
    {
        const std::string base_url =
            "https://raw.githubusercontent.com/google/mozc/master/src/data/dictionary_oss/";
        const std::filesystem::path out_dir = "src/dictionary_builder/mozc_fetch";

        curl_global_init(CURL_GLOBAL_DEFAULT);

        std::uintmax_t total_bytes = 0;
        std::uint64_t total_entries = 0;

        std::unordered_map<std::string, std::vector<WordItem>> reading_to_words;
        reading_to_words.reserve(800000);

        for (int i = 0; i <= 9; ++i)
        {
            const std::string fname = "dictionary" + two_digits(i) + ".txt";
            const std::string url = base_url + fname;
            const std::filesystem::path out_path = out_dir / fname;

            if (!file_exists_nonempty(out_path))
            {
                std::cout << "Downloading " << fname << "...\n";
                download_file(url, out_path);
            }
            else
            {
                std::cout << "Skip download (exists): " << fname << "\n";
            }

            const auto bytes = std::filesystem::file_size(out_path);
            std::cout << "Parsing " << fname << "...\n";
            auto entries = read_dictionary_tsv(out_path);

            for (const auto &e : entries)
            {
                reading_to_words[e.reading].push_back(WordItem{e.word, e.word_script});
            }

            std::cout << "  " << fname
                      << " | entries=" << entries.size()
                      << " | size=" << human_bytes(bytes) << "\n";

            total_entries += static_cast<std::uint64_t>(entries.size());
            total_bytes += bytes;
        }

        // ---- NEW: download connection_single_column.txt ----
        {
            const std::string conn = "connection_single_column.txt";
            const std::string url = base_url + conn;
            const std::filesystem::path out_path = out_dir / conn;

            if (!file_exists_nonempty(out_path))
            {
                std::cout << "Downloading " << conn << "...\n";
                download_file(url, out_path);
            }
            else
            {
                std::cout << "Skip download (exists): " << conn << "\n";
            }

            std::cout << "  " << conn << " | size=" << human_bytes(std::filesystem::file_size(out_path)) << "\n";
        }

        curl_global_cleanup();

        // Sort + unique per reading
        std::uint64_t total_before = 0;
        std::uint64_t total_after = 0;

        for (auto &kv : reading_to_words)
        {
            auto &items = kv.second;
            total_before += static_cast<std::uint64_t>(items.size());

            std::sort(items.begin(), items.end(),
                      [](const WordItem &a, const WordItem &b)
                      { return a.word < b.word; });

            items.erase(std::unique(items.begin(), items.end(),
                                    [](const WordItem &a, const WordItem &b)
                                    { return a.word == b.word; }),
                        items.end());

            total_after += static_cast<std::uint64_t>(items.size());
        }

        // Sort keys
        std::vector<std::string> sorted_keys;
        sorted_keys.reserve(reading_to_words.size());
        for (const auto &kv : reading_to_words)
            sorted_keys.push_back(kv.first);

        std::sort(sorted_keys.begin(), sorted_keys.end(), ReadingKeyLess{});

        // Totals
        std::cout << "\n=== TOTAL ===\n";
        std::cout << "Total entries: " << total_entries << "\n";
        std::cout << "Total text size: " << human_bytes(total_bytes) << "\n";
        std::cout << "Unique readings: " << reading_to_words.size() << "\n";
        std::cout << "Total word-list items (before unique): " << total_before << "\n";
        std::cout << "Total word-list items (after  unique): " << total_after << "\n";

        print_random_samples(sorted_keys, reading_to_words,
                             /*sample_count=*/10,
                             /*max_words_per_key=*/8);

        std::cout << "\n=== FIRST 30 SORTED KEYS (short -> hiragana order) ===\n";
        for (size_t i = 0; i < std::min<size_t>(30, sorted_keys.size()); ++i)
        {
            const auto &k = sorted_keys[i];
            auto it = reading_to_words.find(k);
            const size_t n = (it == reading_to_words.end()) ? 0 : it->second.size();
            std::cout << "  " << k << " (words=" << n << ")\n";
        }

        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
