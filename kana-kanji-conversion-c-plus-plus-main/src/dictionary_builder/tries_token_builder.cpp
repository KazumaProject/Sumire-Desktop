// src/dictionary_builder/tries_token_builder.cpp
//
// C++ equivalent of the Kotlin "buildTriesAndTokenArray" pipeline:
//  - Build yomi trie with termId (0-based, keys sorted by length then lex)
//  - Build tango trie (excluding kana-only tokens)
//  - Convert both to LOUDS and persist
//  - Build TokenArray posting lists keyed by termId and persist
//  - Build POS table (leftId/rightId pairs) and persist
//
// Build (Ubuntu):
//   g++ -std=c++20 -O2 -Isrc/dictionary_builder -Isrc/dictionary_builder/louds_builder \
//     src/dictionary_builder/tries_token_builder.cpp \
//     src/dictionary_builder/louds_builder/prefix_tree/prefix_tree_utf16.cpp \
//     src/dictionary_builder/louds_builder/prefix_tree_with_term_id/prefix_tree_with_term_id_utf16.cpp \
//     src/dictionary_builder/louds_builder/louds/louds_converter_utf16.cpp \
//     src/dictionary_builder/louds_builder/louds/louds_utf16_writer.cpp \
//     src/dictionary_builder/louds_builder/louds/louds_utf16_reader.cpp \
//     src/dictionary_builder/louds_builder/louds_with_term_id/louds_with_term_id_utf16.cpp \
//     src/dictionary_builder/louds_builder/louds_with_term_id/louds_converter_with_term_id_utf16.cpp \
//     src/dictionary_builder/token_array/token_array.cpp \
//     -o buildTriesToken
//
// Run:
//   ./buildTriesToken --in_dir src/dictionary_builder/mozc_fetch --out_dir build
//   ./buildTriesToken --in_dir ... --out_dir ... --quiet
//
// Dump registrations (VERY LARGE):
//   ./buildTriesToken --in_dir ... --out_dir ... --dump_all
//   ./buildTriesToken --in_dir ... --out_dir ... --dump_yomi えびふらい

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "louds/louds_converter_utf16.hpp"
#include "louds_builder/louds_with_term_id/louds_converter_with_term_id_utf16.hpp"
#include "louds/louds_utf16_reader.hpp"
#include "louds/louds_utf16_writer.hpp"
#include "louds_with_term_id/louds_with_term_id_utf16.hpp"
#include "prefix_tree/prefix_tree_utf16.hpp"
#include "prefix_tree_with_term_id/prefix_tree_with_term_id_utf16.hpp"
#include "token_array/token_array.hpp"

namespace fs = std::filesystem;

struct DicRow
{
    std::u16string yomi;
    int16_t left_id;
    int16_t right_id;
    int16_t cost;
    std::u16string tango;
};

// -----------------------------
// UTF-8 -> UTF-16 (strict, minimal, no ICU)
// -----------------------------
static bool utf8_to_u16(const std::string &s, std::u16string &out)
{
    out.clear();
    out.reserve(s.size());

    size_t i = 0;
    while (i < s.size())
    {
        const unsigned char c0 = static_cast<unsigned char>(s[i]);
        char32_t cp = 0;
        size_t extra = 0;
        if (c0 < 0x80)
        {
            cp = c0;
            extra = 0;
        }
        else if ((c0 & 0xE0) == 0xC0)
        {
            cp = c0 & 0x1F;
            extra = 1;
        }
        else if ((c0 & 0xF0) == 0xE0)
        {
            cp = c0 & 0x0F;
            extra = 2;
        }
        else if ((c0 & 0xF8) == 0xF0)
        {
            cp = c0 & 0x07;
            extra = 3;
        }
        else
        {
            return false;
        }

        if (i + extra >= s.size())
            return false;

        for (size_t k = 1; k <= extra; ++k)
        {
            const unsigned char cx = static_cast<unsigned char>(s[i + k]);
            if ((cx & 0xC0) != 0x80)
                return false;
            cp = (cp << 6) | (cx & 0x3F);
        }

        // Reject surrogates
        if (cp >= 0xD800 && cp <= 0xDFFF)
            return false;

        // Reject out of Unicode range
        if (cp > 0x10FFFF)
            return false;

        // Encode as UTF-16
        if (cp <= 0xFFFF)
        {
            out.push_back(static_cast<char16_t>(cp));
        }
        else
        {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        }

        i += 1 + extra;
    }

    return true;
}

static std::string u16_to_utf8(const std::u16string &s)
{
    std::string out;
    out.reserve(s.size() * 3);

    for (size_t i = 0; i < s.size(); ++i)
    {
        uint32_t cp = static_cast<uint16_t>(s[i]);

        // surrogate pair
        if (cp >= 0xD800 && cp <= 0xDBFF)
        {
            if (i + 1 >= s.size())
                break;
            const uint32_t lo = static_cast<uint16_t>(s[i + 1]);
            if (lo < 0xDC00 || lo > 0xDFFF)
                break;
            cp = 0x10000 + (((cp - 0xD800) << 10) | (lo - 0xDC00));
            ++i;
        }
        else if (cp >= 0xDC00 && cp <= 0xDFFF)
        {
            // stray low surrogate
            continue;
        }

        if (cp <= 0x7F)
        {
            out.push_back(static_cast<char>(cp));
        }
        else if (cp <= 0x7FF)
        {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else if (cp <= 0xFFFF)
        {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    return out;
}

static bool is_hiragana_only_u16(const std::u16string &s);
static bool is_katakana_only_u16(const std::u16string &s);
static bool is_hira_or_kata_only_u16(const std::u16string &s);

static const char *token_kind_label(const std::u16string &yomi, const std::u16string &tango)
{
    // Mirrors the TokenArray nodeIndex decision used later:
    //   - hiragana-only (or tango==yomi) => HIRAGANA_SENTINEL
    //   - katakana-only => KATAKANA_SENTINEL
    //   - otherwise => tango LOUDS nodeIndex
    if (tango == yomi || is_hiragana_only_u16(tango))
        return "HIRAGANA_SENTINEL";
    if (is_katakana_only_u16(tango))
        return "KATAKANA_SENTINEL";
    return "TANGO_LOUDS_NODE";
}

static void dump_all_registrations(const std::vector<std::u16string> &keys,
                                   const std::unordered_map<std::u16string, std::vector<DicRow>> &grouped,
                                   const std::u16string *onlyYomiOrNull)
{
    // WARNING: This can be extremely large (millions of lines) when dumping the full Mozc dictionary.
    // Use --dump_yomi <yomi> to narrow.
    for (size_t termId = 0; termId < keys.size(); ++termId)
    {
        const auto &yomi = keys[termId];
        if (onlyYomiOrNull && yomi != *onlyYomiOrNull)
            continue;

        auto it = grouped.find(yomi);
        if (it == grouped.end())
            continue;

        const auto &list = it->second;
        std::cout << "[yomi] termId=" << termId
                  << " yomi=" << u16_to_utf8(yomi)
                  << " tokens=" << list.size()
                  << "\n";

        for (const auto &row : list)
        {
            const bool isKanaOnly = is_hira_or_kata_only_u16(row.tango);
            const bool isInsertedIntoTangoLOUDS = !isKanaOnly;

            std::cout << "  - tango=" << u16_to_utf8(row.tango)
                      << " cost=" << row.cost
                      << " left_id=" << row.left_id
                      << " right_id=" << row.right_id
                      << " kind=" << token_kind_label(yomi, row.tango)
                      << " insert_tango_louds=" << (isInsertedIntoTangoLOUDS ? "1" : "0")
                      << "\n";
        }
    }
}

static bool is_hiragana_only_u16(const std::u16string &s)
{
    if (s.empty())
        return false;
    for (char16_t ch : s)
    {
        // Hiragana block: U+3040..U+309F
        if (ch < 0x3040 || ch > 0x309F)
            return false;
    }
    return true;
}

static bool is_katakana_only_u16(const std::u16string &s)
{
    if (s.empty())
        return false;
    for (char16_t ch : s)
    {
        // Katakana block: U+30A0..U+30FF
        if (ch < 0x30A0 || ch > 0x30FF)
            return false;
    }
    return true;
}

static bool is_hira_or_kata_only_u16(const std::u16string &s)
{
    return is_hiragana_only_u16(s) || is_katakana_only_u16(s);
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

static std::vector<DicRow> read_mozc_tsv(const fs::path &path)
{
    std::ifstream ifs(path);
    if (!ifs)
        throw std::runtime_error("Failed to open: " + path.string());

    std::vector<DicRow> rows;
    std::string line;
    while (std::getline(ifs, line))
    {
        if (line.empty() || line[0] == '#')
            continue;

        std::string_view c0, c1, c2, c3, c4;
        if (!split5_tabs(line, c0, c1, c2, c3, c4))
            continue;

        DicRow r;
        const std::string yomi_utf8(c0);
        const std::string tango_utf8(c4);

        if (!utf8_to_u16(yomi_utf8, r.yomi))
            continue;
        if (!utf8_to_u16(tango_utf8, r.tango))
            continue;

        r.left_id = parse_i16(c1, "left_id");
        r.right_id = parse_i16(c2, "right_id");
        r.cost = parse_i16(c3, "cost");
        rows.push_back(std::move(r));
    }
    return rows;
}

static void write_pos_table(const std::string &path,
                            const std::vector<int16_t> &leftIds,
                            const std::vector<int16_t> &rightIds)
{
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs)
        throw std::runtime_error("failed to open file for write: " + path);

    uint32_t n = static_cast<uint32_t>(leftIds.size());
    ofs.write(reinterpret_cast<const char *>(&n), sizeof(n));
    ofs.write(reinterpret_cast<const char *>(leftIds.data()), static_cast<std::streamsize>(leftIds.size() * sizeof(int16_t)));
    ofs.write(reinterpret_cast<const char *>(rightIds.data()), static_cast<std::streamsize>(rightIds.size() * sizeof(int16_t)));
}

int main(int argc, char **argv)
{
    try
    {
        fs::path in_dir = "src/dictionary_builder/mozc_fetch";
        fs::path out_dir = "build";
        bool dump_all = false;
        bool dump_yomi = false;
        std::u16string dump_yomi_u16;

        for (int i = 1; i < argc; ++i)
        {
            std::string a = argv[i];
            if (a == "--in_dir" && i + 1 < argc)
                in_dir = argv[++i];
            else if (a == "--out_dir" && i + 1 < argc)
                out_dir = argv[++i];
            else if (a == "--dump_all")
                dump_all = true;
            else if (a == "--dump_yomi" && i + 1 < argc)
            {
                dump_yomi = true;
                const std::string y = argv[++i];
                if (!utf8_to_u16(y, dump_yomi_u16))
                    throw std::runtime_error("Invalid UTF-8 for --dump_yomi");
            }
        }

        fs::create_directories(out_dir);

        // 1) Load dictionaries and group by yomi
        std::unordered_map<std::u16string, std::vector<DicRow>> grouped;
        grouped.reserve(200000);

        for (int k = 0; k < 10; ++k)
        {
            const fs::path f = in_dir / ("dictionary0" + std::to_string(k) + ".txt");
            if (!fs::exists(f))
                throw std::runtime_error("Missing file: " + f.string());
            auto rows = read_mozc_tsv(f);
            for (auto &r : rows)
            {
                grouped[r.yomi].push_back(std::move(r));
            }
            std::cerr << "Loaded " << f.filename().string() << "\n";
        }

        // 2) Sort keys by (length asc, lex asc) and assign termId
        std::vector<std::u16string> keys;
        keys.reserve(grouped.size());
        for (const auto &kv : grouped)
            keys.push_back(kv.first);

        std::sort(keys.begin(), keys.end(), [](const std::u16string &a, const std::u16string &b)
                  {
                      if (a.size() != b.size())
                          return a.size() < b.size();
                      return a < b; });

        std::cerr << "Distinct yomi keys: " << keys.size() << "\n";

        if (dump_all || dump_yomi)
        {
            const std::u16string *only = dump_yomi ? &dump_yomi_u16 : nullptr;
            dump_all_registrations(keys, grouped, only);
            // If the user only wants the dump, exiting early avoids doing heavy LOUDS/token builds.
            return 0;
        }

        // 3) Build POS table (match Kotlin: reverse encounter order)
        std::unordered_map<uint32_t, int> pairCounter;
        pairCounter.reserve(8192);
        int counter = 0;
        auto pack_pair = [](int16_t l, int16_t r) -> uint32_t
        {
            return (static_cast<uint16_t>(l) << 16) | static_cast<uint16_t>(r);
        };

        for (const auto &key : keys)
        {
            const auto &list = grouped.at(key);
            for (const auto &row : list)
            {
                const uint32_t pk = pack_pair(row.left_id, row.right_id);
                if (pairCounter.find(pk) == pairCounter.end())
                {
                    pairCounter.emplace(pk, counter++);
                }
            }
        }

        // Sort pairs by counter DESC
        std::vector<std::pair<uint32_t, int>> pairs;
        pairs.reserve(pairCounter.size());
        for (const auto &kv : pairCounter)
            pairs.emplace_back(kv.first, kv.second);
        std::sort(pairs.begin(), pairs.end(), [](const auto &a, const auto &b)
                  { return a.second > b.second; });

        std::vector<int16_t> leftIds;
        std::vector<int16_t> rightIds;
        leftIds.reserve(pairs.size());
        rightIds.reserve(pairs.size());

        std::unordered_map<uint32_t, uint16_t> posIndexByPair;
        posIndexByPair.reserve(pairs.size() * 2);

        for (size_t i = 0; i < pairs.size(); ++i)
        {
            const uint32_t pk = pairs[i].first;
            const int16_t l = static_cast<int16_t>(pk >> 16);
            const int16_t r = static_cast<int16_t>(pk & 0xFFFF);
            leftIds.push_back(l);
            rightIds.push_back(r);
            posIndexByPair[pk] = static_cast<uint16_t>(i);
        }

        const fs::path posPath = out_dir / "pos_table.bin";
        write_pos_table(posPath.string(), leftIds, rightIds);
        std::cerr << "POS pairs: " << pairs.size() << "\n";

        // 4) Build tries
        PrefixTreeWithTermIdUtf16 yomiTree;
        PrefixTreeUtf16 tangoTree;

        for (size_t termId = 0; termId < keys.size(); ++termId)
        {
            const auto &key = keys[termId];
            yomiTree.insert(key, static_cast<int32_t>(termId));

            const auto &list = grouped.at(key);
            for (const auto &row : list)
            {
                if (!is_hira_or_kata_only_u16(row.tango))
                {
                    tangoTree.insert(row.tango);
                }
            }
        }

        // 5) Convert to LOUDS and persist
        const auto yomiLOUDS = ConverterWithTermIdUtf16().convert(yomiTree.root());
        const auto tangoLOUDS = ConverterUtf16().convert(tangoTree.getRoot());

        const fs::path yomiPath = out_dir / "yomi_termid.louds";
        const fs::path tangoPath = out_dir / "tango.louds";
        yomiLOUDS.saveToFile(yomiPath.string());
        tangoLOUDS.saveToFile(tangoPath.string());

        // 6) Reload tango LOUDS for nodeIndex lookup
        const auto tangoReader = LOUDSReaderUtf16::loadFromFile(tangoPath.string());

        // 7) Build TokenArray
        TokenArray tokens;
        tokens.posIndex.reserve(3000000);
        tokens.wordCost.reserve(3000000);
        tokens.nodeIndex.reserve(3000000);

        for (size_t termId = 0; termId < keys.size(); ++termId)
        {
            const auto &key = keys[termId];
            tokens.postingsBits.push_back(false);

            const auto &list = grouped.at(key);
            for (const auto &row : list)
            {
                tokens.postingsBits.push_back(true);

                const uint32_t pk = pack_pair(row.left_id, row.right_id);
                const auto it = posIndexByPair.find(pk);
                if (it == posIndexByPair.end())
                    throw std::runtime_error("posIndex missing");

                tokens.posIndex.push_back(it->second);
                tokens.wordCost.push_back(row.cost);

                // nodeIndex: kana-only -> sentinel, otherwise tango LOUDS nodeIndex
                int32_t nodeIdx = 0;
                if (row.tango == key || is_hiragana_only_u16(row.tango))
                {
                    nodeIdx = TokenArray::HIRAGANA_SENTINEL;
                }
                else if (is_katakana_only_u16(row.tango))
                {
                    nodeIdx = TokenArray::KATAKANA_SENTINEL;
                }
                else
                {
                    nodeIdx = tangoReader.getNodeIndex(row.tango);
                }
                tokens.nodeIndex.push_back(nodeIdx);
            }
        }

        const fs::path tokenPath = out_dir / "token_array.bin";
        tokens.saveToFile(tokenPath.string());

        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}
