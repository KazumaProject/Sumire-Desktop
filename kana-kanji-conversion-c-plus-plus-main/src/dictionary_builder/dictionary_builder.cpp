// src/dictionary_builder/dictionary_builder.cpp
//
// Build a LOUDS (UTF-16) trie from Mozc dictionary "reading" keys.
// Also converts Mozc connection_single_column.txt into a binary short array.
//
// Input (TSV):
//   src/dictionary_builder/mozc_fetch/dictionary00.txt .. dictionary09.txt
// Input (connection):
//   src/dictionary_builder/mozc_fetch/connection_single_column.txt
//
// Output (binary):
//   build/mozc_reading.louds
//   build/connection_single_column.bin (default)
//
// Usage:
//   ./mozc_dic_fetch
//   ./dictionary_builder --out build/mozc_reading.louds
//
// Notes:
// - This builder only uses the first column (reading).
// - Connection file is converted in Big Endian short array format for Kotlin compatibility.

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "prefix_tree/prefix_tree_utf16.hpp"
#include "louds/louds_converter_utf16.hpp"
#include "louds/louds_utf16_writer.hpp"

#include "connection_id/connection_id_builder.hpp"

namespace fs = std::filesystem;

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
            char16_t hi = static_cast<char16_t>(0xD800 + ((cp >> 10) & 0x3FF));
            char16_t lo = static_cast<char16_t>(0xDC00 + (cp & 0x3FF));
            out.push_back(hi);
            out.push_back(lo);
        }
    }

    return true;
}

// -----------------------------
// TSV reading: extract first column "reading"
// -----------------------------
static void collect_readings_from_tsv(const fs::path &file, std::vector<std::string> &out)
{
    std::ifstream ifs(file);
    if (!ifs)
        throw std::runtime_error("Failed to open: " + file.string());

    std::string line;
    while (std::getline(ifs, line))
    {
        if (line.empty())
            continue;

        const size_t tab = line.find('\t');
        if (tab == std::string::npos)
            continue;
        if (tab == 0)
            continue;

        out.emplace_back(line.substr(0, tab));
    }
}

struct Options
{
    fs::path in_dir = "src/dictionary_builder/mozc_fetch";
    fs::path out_file = "build/mozc_reading.louds";
    fs::path conn_out_file = "build/connection_single_column.bin";
    int start_index = 0;
    int end_index = 9;
    bool verbose = true;

    bool build_connection_bin = true;
    bool conn_skip_first_line = true;
};

static void print_usage(const char *argv0)
{
    std::cout
        << "Usage: " << argv0
        << " [--in <dir>] [--out <file>] [--conn-out <file>]\n"
        << "             [--start <0..9>] [--end <0..9>] [--quiet]\n"
        << "             [--no-conn] [--conn-no-skip-first]\n"
        << "\n"
        << "Defaults:\n"
        << "  --in       src/dictionary_builder/mozc_fetch\n"
        << "  --out      build/mozc_reading.louds\n"
        << "  --conn-out build/connection_single_column.bin\n"
        << "  --start    0\n"
        << "  --end      9\n";
}

static Options parse_args(int argc, char **argv)
{
    Options opt;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--help" || a == "-h")
        {
            print_usage(argv[0]);
            std::exit(0);
        }
        else if (a == "--in" && i + 1 < argc)
        {
            opt.in_dir = argv[++i];
        }
        else if (a == "--out" && i + 1 < argc)
        {
            opt.out_file = argv[++i];
        }
        else if (a == "--conn-out" && i + 1 < argc)
        {
            opt.conn_out_file = argv[++i];
        }
        else if (a == "--start" && i + 1 < argc)
        {
            opt.start_index = std::stoi(argv[++i]);
        }
        else if (a == "--end" && i + 1 < argc)
        {
            opt.end_index = std::stoi(argv[++i]);
        }
        else if (a == "--quiet")
        {
            opt.verbose = false;
        }
        else if (a == "--no-conn")
        {
            opt.build_connection_bin = false;
        }
        else if (a == "--conn-no-skip-first")
        {
            opt.conn_skip_first_line = false;
        }
        else
        {
            throw std::runtime_error("Unknown or incomplete argument: " + a);
        }
    }

    if (opt.start_index < 0 || opt.start_index > 9 ||
        opt.end_index < 0 || opt.end_index > 9 ||
        opt.start_index > opt.end_index)
    {
        throw std::runtime_error("Invalid --start/--end (expected 0..9 and start<=end)");
    }

    return opt;
}

static std::string two_digits(int i)
{
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d", i);
    return buf;
}

int main(int argc, char **argv)
{
    try
    {
        const Options opt = parse_args(argc, argv);

        if (opt.verbose)
        {
            std::cout << "[dictionary_builder] in_dir       = " << opt.in_dir.string() << "\n";
            std::cout << "[dictionary_builder] out_file     = " << opt.out_file.string() << "\n";
            std::cout << "[dictionary_builder] conn_out     = " << opt.conn_out_file.string() << "\n";
            std::cout << "[dictionary_builder] files        = dictionary" << two_digits(opt.start_index)
                      << ".txt .. dictionary" << two_digits(opt.end_index) << ".txt\n";
            std::cout << "[dictionary_builder] build_conn   = " << (opt.build_connection_bin ? "true" : "false") << "\n";
            std::cout << "[dictionary_builder] conn_skip_1st= " << (opt.conn_skip_first_line ? "true" : "false") << "\n";
        }

        // 1) Collect readings
        std::vector<std::string> readings;
        readings.reserve(900000);

        for (int i = opt.start_index; i <= opt.end_index; ++i)
        {
            const fs::path file = opt.in_dir / ("dictionary" + two_digits(i) + ".txt");
            if (!fs::exists(file))
            {
                throw std::runtime_error("Input file not found: " + file.string() + " (run mozc_dic_fetch first?)");
            }

            if (opt.verbose)
                std::cout << "Reading: " << file.string() << "\n";

            collect_readings_from_tsv(file, readings);
        }

        if (readings.empty())
            throw std::runtime_error("No readings collected (check input files)");

        if (opt.verbose)
            std::cout << "Collected readings (with duplicates): " << readings.size() << "\n";

        // 2) Unique readings
        std::sort(readings.begin(), readings.end());
        readings.erase(std::unique(readings.begin(), readings.end()), readings.end());

        if (opt.verbose)
            std::cout << "Unique readings: " << readings.size() << "\n";

        // 3) Build PrefixTree (UTF-16)
        PrefixTreeUtf16 trie;
        std::u16string buf;

        size_t bad_utf8 = 0;
        for (const auto &r : readings)
        {
            if (!utf8_to_u16(r, buf))
            {
                ++bad_utf8;
                continue;
            }
            trie.insert(buf);
        }

        if (bad_utf8 != 0)
        {
            std::cerr << "Warning: skipped " << bad_utf8 << " readings due to invalid UTF-8\n";
        }

        // 4) Convert to LOUDS
        ConverterUtf16 conv;
        LOUDSUtf16 louds = conv.convert(trie.getRoot());

        // 5) Save LOUDS
        fs::create_directories(opt.out_file.parent_path());
        louds.saveToFile(opt.out_file.string());

        if (opt.verbose)
        {
            const auto bytes = fs::file_size(opt.out_file);
            std::cout << "Wrote LOUDS: " << opt.out_file.string() << " (" << bytes << " bytes)\n";
        }

        // 6) Build connection_single_column.bin (optional)
        if (opt.build_connection_bin)
        {
            const fs::path conn_txt = opt.in_dir / "connection_single_column.txt";
            if (!fs::exists(conn_txt))
            {
                throw std::runtime_error(
                    "connection_single_column.txt not found: " + conn_txt.string() +
                    " (run mozc_dic_fetch first, or add download step there)");
            }

            if (opt.verbose)
                std::cout << "Reading connection file: " << conn_txt.string() << "\n";

            auto values = ConnectionIdBuilder::readSingleColumnText(conn_txt, opt.conn_skip_first_line);

            if (opt.verbose)
                std::cout << "Connection values: " << values.size() << "\n";

            fs::create_directories(opt.conn_out_file.parent_path());
            ConnectionIdBuilder::writeShortArrayAsBytesBE(values, opt.conn_out_file);

            if (opt.verbose)
            {
                const auto bytes = fs::file_size(opt.conn_out_file);
                std::cout << "Wrote connection bin: " << opt.conn_out_file.string()
                          << " (" << bytes << " bytes)\n";
            }

            // quick roundtrip check (cheap)
            auto back = ConnectionIdBuilder::readShortArrayFromBytesBE(opt.conn_out_file);
            if (back.size() != values.size())
                throw std::runtime_error("Connection roundtrip size mismatch");

            for (size_t i = 0; i < std::min<size_t>(10, values.size()); ++i)
            {
                if (back[i] != values[i])
                    throw std::runtime_error("Connection roundtrip mismatch at i=" + std::to_string(i));
            }

            if (opt.verbose)
                std::cout << "Connection roundtrip OK\n";
        }

        if (opt.verbose)
            std::cout << "Done.\n";

        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
