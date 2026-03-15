// cli/louds/termid_cli.cpp
//
// getTermId CLI for LOUDSWithTermId (UTF-16).
//
// Examples:
//   ./termid_cli --louds build/yomi_termid.louds --q とうきょう
//   echo -e "きょう\nとうきょう" | ./termid_cli --louds build/yomi_termid.louds --stdin
//

#include <iostream>
#include <string>
#include <string_view>

#include "louds_with_term_id/louds_with_term_id_utf16.hpp"
#include "louds_with_term_id/louds_with_term_id_reader_utf16.hpp"

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

static void usage(const char *argv0)
{
    std::cout
        << "Usage:\n"
        << "  " << argv0 << " --louds <yomi_termid.louds> --q <utf8>\n"
        << "  " << argv0 << " --louds <yomi_termid.louds> --stdin\n";
}

static void run_one(const LOUDSWithTermIdReaderUtf16 &reader, const std::string &q_utf8)
{
    std::u16string q16;
    if (!utf8_to_u16(q_utf8, q16))
    {
        std::cout << "[BAD_UTF8] " << q_utf8 << "\n";
        return;
    }

    const int32_t termId = reader.getTermId(q16);
    std::cout << q_utf8 << "\t" << termId << "\n";
}

int main(int argc, char **argv)
{
    try
    {
        std::string louds_path;
        bool stdin_mode = false;
        std::string q;

        for (int i = 1; i < argc; ++i)
        {
            const std::string a = argv[i];
            if (a == "--help" || a == "-h")
            {
                usage(argv[0]);
                return 0;
            }
            if (a == "--louds" && i + 1 < argc)
            {
                louds_path = argv[++i];
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
            throw std::runtime_error("Unknown/incomplete arg: " + a);
        }

        if (louds_path.empty() || (!stdin_mode && q.empty()))
        {
            usage(argv[0]);
            return 2;
        }

        const auto trie = LOUDSWithTermIdUtf16::loadFromFile(louds_path);
        const LOUDSWithTermIdReaderUtf16 reader(trie);

        if (!stdin_mode)
        {
            run_one(reader, q);
            return 0;
        }

        std::string line;
        while (std::getline(std::cin, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back(); // Windows対策
            if (line.empty())
                continue;
            run_one(reader, line);
        }
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
