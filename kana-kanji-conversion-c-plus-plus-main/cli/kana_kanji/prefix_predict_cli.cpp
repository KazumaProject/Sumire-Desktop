#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "louds/louds_utf16_reader.hpp"
#include "louds_with_term_id/louds_with_term_id_reader_utf16.hpp"
#include "louds_with_term_id/louds_with_term_id_utf16.hpp"
#include "token_array/token_array.hpp"

// -----------------------------
// UTF-8 -> UTF-16 (strict)
// -----------------------------
static bool utf8_next_codepoint(std::string_view s, size_t &i, char32_t &out_cp)
{
    if (i >= s.size())
        return false;
    const unsigned char c0 = static_cast<unsigned char>(s[i]);

    if (c0 < 0x80)
    {
        out_cp = c0;
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
        char32_t cp = (c0 & 0x1F);
        cp = (cp << 6) | (c1 & 0x3F);
        if (cp < 0x80)
            return false; // overlong
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
        char32_t cp = (c0 & 0x0F);
        cp = (cp << 6) | (c1 & 0x3F);
        cp = (cp << 6) | (c2 & 0x3F);
        if (cp < 0x800)
            return false; // overlong
        if (cp >= 0xD800 && cp <= 0xDFFF)
            return false; // surrogate
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
        char32_t cp = (c0 & 0x07);
        cp = (cp << 6) | (c1 & 0x3F);
        cp = (cp << 6) | (c2 & 0x3F);
        cp = (cp << 6) | (c3 & 0x3F);
        if (cp < 0x10000)
            return false; // overlong
        if (cp > 0x10FFFF)
            return false;
        out_cp = cp;
        i += 4;
        return true;
    }

    return false;
}

static bool utf8_to_u16(std::string_view s, std::u16string &out)
{
    out.clear();
    out.reserve(s.size());

    size_t i = 0;
    while (i < s.size())
    {
        char32_t cp = 0;
        if (!utf8_next_codepoint(s, i, cp))
            return false;

        if (cp <= 0xFFFF)
        {
            if (cp >= 0xD800 && cp <= 0xDFFF)
                return false;
            out.push_back(static_cast<char16_t>(cp));
        }
        else
        {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + ((cp >> 10) & 0x3FF)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        }
    }
    return true;
}

// -----------------------------
// UTF-16 -> UTF-8 (for printing)
// -----------------------------
static void append_utf8(std::string &out, char32_t cp)
{
    if (cp <= 0x7F)
        out.push_back(static_cast<char>(cp));
    else if (cp <= 0x7FF)
    {
        out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else if (cp <= 0xFFFF)
    {
        out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else
    {
        out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

static bool u16_to_utf8(const std::u16string &s, std::string &out)
{
    out.clear();
    out.reserve(s.size());

    for (size_t i = 0; i < s.size();)
    {
        char32_t cp = 0;
        char16_t c = s[i++];

        if (c >= 0xD800 && c <= 0xDBFF)
        {
            if (i >= s.size())
                return false;
            char16_t d = s[i++];
            if (!(d >= 0xDC00 && d <= 0xDFFF))
                return false;
            cp = 0x10000 + (((static_cast<char32_t>(c) - 0xD800) << 10) |
                            (static_cast<char32_t>(d) - 0xDC00));
        }
        else if (c >= 0xDC00 && c <= 0xDFFF)
        {
            return false;
        }
        else
        {
            cp = static_cast<char32_t>(c);
        }

        append_utf8(out, cp);
    }

    return true;
}

// -----------------------------
// Hiragana -> Katakana (basic mapping)
// -----------------------------
static std::u16string hira_to_kata(const std::u16string &hira)
{
    std::u16string out;
    out.reserve(hira.size());

    for (char16_t ch : hira)
    {
        // Most hiragana <-> katakana pairs differ by +0x60.
        if ((ch >= 0x3041 && ch <= 0x3096) || (ch >= 0x309D && ch <= 0x309F))
            out.push_back(static_cast<char16_t>(ch + 0x0060));
        else
            out.push_back(ch);
    }
    return out;
}

static void usage(const char *argv0)
{
    std::cout
        << "Usage:\n"
        << "  " << argv0 << " --yomi_termid <yomi_termid.louds> --tango <tango.louds> --tokens <token_array.bin> --q <utf8> [--limit N] [--no_dedup]\n"
        << "  " << argv0 << " --yomi_termid <yomi_termid.louds> --tango <tango.louds> --tokens <token_array.bin> --stdin [--limit N] [--no_dedup]\n";
}

static void run_one(const LOUDSReaderUtf16 &yomiCps,
                    const LOUDSWithTermIdReaderUtf16 &yomiTerm,
                    const TokenArray &tokens,
                    const LOUDSReaderUtf16 &tango,
                    const std::string &q_utf8,
                    int limit,
                    bool dedup)
{
    std::u16string q16;
    if (!utf8_to_u16(q_utf8, q16))
    {
        std::cout << "[BAD_UTF8] " << q_utf8 << "\n";
        return;
    }

    const auto yomi_hits = yomiCps.commonPrefixSearch(q16);
    std::cout << "query=" << q_utf8 << " yomi_hits=" << yomi_hits.size() << "\n";

    for (const auto &yomi : yomi_hits)
    {
        const int32_t termId = yomiTerm.getTermId(yomi);

        std::string yomi8;
        if (!u16_to_utf8(yomi, yomi8))
            yomi8 = "<BAD_U16>";

        if (termId < 0)
        {
            std::cout << "  [yomi] " << yomi8 << " termId=-1 (non-terminal)\n";
            continue;
        }

        auto list = tokens.getTokensForTermId(termId);
        std::sort(list.begin(), list.end(), [](const TokenEntry &a, const TokenEntry &b)
                  { return a.wordCost < b.wordCost; });

        std::unordered_set<std::string> seen;
        if (dedup)
            seen.reserve(list.size() * 2);

        std::cout << "  [yomi] " << yomi8 << " termId=" << termId << " tokens=" << list.size() << "\n";

        int printed = 0;
        for (const auto &t : list)
        {
            if (limit >= 0 && printed >= limit)
                break;

            std::u16string surface;
            if (t.nodeIndex == TokenArray::KATAKANA_SENTINEL)
                surface = hira_to_kata(yomi);
            else if (t.nodeIndex == TokenArray::HIRAGANA_SENTINEL)
                surface = yomi;
            else
                surface = tango.getLetter(t.nodeIndex);

            std::string s8;
            if (!u16_to_utf8(surface, s8))
                s8 = "<BAD_U16>";

            if (dedup)
            {
                if (seen.find(s8) != seen.end())
                    continue;
                seen.insert(s8);
            }

            std::cout << "    - " << s8
                      << "\tcost=" << t.wordCost
                      << "\tnodeIndex=" << t.nodeIndex
                      << "\n";
            ++printed;
        }
    }
}

int main(int argc, char **argv)
{
    try
    {
        std::string yomi_termid_path;
        std::string tango_path;
        std::string tokens_path;
        std::string q;
        bool stdin_mode = false;
        int limit = 20;
        bool dedup = true;

        for (int i = 1; i < argc; ++i)
        {
            const std::string a = argv[i];
            if (a == "--help" || a == "-h")
            {
                usage(argv[0]);
                return 0;
            }
            if (a == "--yomi_termid" && i + 1 < argc)
            {
                yomi_termid_path = argv[++i];
                continue;
            }
            if (a == "--tango" && i + 1 < argc)
            {
                tango_path = argv[++i];
                continue;
            }
            if (a == "--tokens" && i + 1 < argc)
            {
                tokens_path = argv[++i];
                continue;
            }
            if (a == "--q" && i + 1 < argc)
            {
                q = argv[++i];
                continue;
            }
            if (a == "--stdin")
            {
                stdin_mode = true;
                continue;
            }
            if (a == "--limit" && i + 1 < argc)
            {
                limit = std::stoi(argv[++i]);
                continue;
            }
            if (a == "--no_dedup")
            {
                dedup = false;
                continue;
            }
            throw std::runtime_error("Unknown/incomplete arg: " + a);
        }

        if (yomi_termid_path.empty() || tango_path.empty() || tokens_path.empty() || (!stdin_mode && q.empty()))
        {
            usage(argv[0]);
            return 2;
        }

        // yomi_termid.louds: load twice (plain LOUDS for CPS, and WithTermId for getTermId)
        const auto yomiCps = LOUDSReaderUtf16::loadFromFile(yomi_termid_path);
        const auto yomiTrie = LOUDSWithTermIdUtf16::loadFromFile(yomi_termid_path);
        const LOUDSWithTermIdReaderUtf16 yomiTerm(yomiTrie);

        const auto tango = LOUDSReaderUtf16::loadFromFile(tango_path);
        const auto tokens = TokenArray::loadFromFile(tokens_path);

        if (!stdin_mode)
        {
            run_one(yomiCps, yomiTerm, tokens, tango, q, limit, dedup);
            return 0;
        }

        std::string line;
        while (std::getline(std::cin, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;
            run_one(yomiCps, yomiTerm, tokens, tango, line, limit, dedup);
        }

        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
