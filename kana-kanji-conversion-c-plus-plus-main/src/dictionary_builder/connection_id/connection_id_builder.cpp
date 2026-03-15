#include "connection_id/connection_id_builder.hpp"

#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

void ConnectionIdBuilder::write_u16_be(std::ostream &os, std::uint16_t v)
{
    const unsigned char b0 = static_cast<unsigned char>((v >> 8) & 0xFF);
    const unsigned char b1 = static_cast<unsigned char>(v & 0xFF);
    os.put(static_cast<char>(b0));
    os.put(static_cast<char>(b1));
}

bool ConnectionIdBuilder::read_u16_be(std::istream &is, std::uint16_t &v)
{
    const int c0 = is.get();
    if (c0 == EOF)
        return false; // EOF
    const int c1 = is.get();
    if (c1 == EOF)
        throw std::runtime_error("Unexpected EOF: binary length is odd (not multiple of 2 bytes)");

    const auto b0 = static_cast<unsigned char>(c0);
    const auto b1 = static_cast<unsigned char>(c1);

    v = static_cast<std::uint16_t>((static_cast<std::uint16_t>(b0) << 8) |
                                   static_cast<std::uint16_t>(b1));
    return true;
}

std::vector<std::int16_t> ConnectionIdBuilder::readSingleColumnText(
    const std::filesystem::path &path,
    bool skip_first_line)
{
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("Failed to open: " + path.string());

    std::vector<std::int16_t> out;
    out.reserve(200000);

    std::string line;

    if (skip_first_line)
        std::getline(in, line);

    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;

        long long v = 0;
        try
        {
            v = std::stoll(line);
        }
        catch (...)
        {
            throw std::runtime_error("Invalid integer line: " + line);
        }

        if (v < std::numeric_limits<std::int16_t>::min() ||
            v > std::numeric_limits<std::int16_t>::max())
        {
            throw std::runtime_error("Out of int16 range: " + std::to_string(v));
        }

        out.push_back(static_cast<std::int16_t>(v));
    }

    return out;
}

void ConnectionIdBuilder::writeShortArrayAsBytesBE(
    const std::vector<std::int16_t> &v,
    const std::filesystem::path &out_path)
{
    std::filesystem::create_directories(out_path.parent_path());

    std::ofstream os(out_path, std::ios::binary);
    if (!os)
        throw std::runtime_error("Failed to open: " + out_path.string());

    for (std::int16_t s : v)
    {
        const std::uint16_t u = static_cast<std::uint16_t>(s); // bitpattern保持
        write_u16_be(os, u);
    }

    os.flush();
    if (!os)
        throw std::runtime_error("Write failed: " + out_path.string());
}

std::vector<std::int16_t> ConnectionIdBuilder::readShortArrayFromBytesBE(
    const std::filesystem::path &path)
{
    std::ifstream is(path, std::ios::binary);
    if (!is)
        throw std::runtime_error("Failed to open: " + path.string());
    return readShortArrayFromBytesBE(is);
}

std::vector<std::int16_t> ConnectionIdBuilder::readShortArrayFromBytesBE(std::istream &is)
{
    std::vector<std::int16_t> out;

    while (true)
    {
        std::uint16_t u = 0;
        const bool ok = read_u16_be(is, u);
        if (!ok)
            break; // EOF

        out.push_back(static_cast<std::int16_t>(u));
    }

    return out;
}
