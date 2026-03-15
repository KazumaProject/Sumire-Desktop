#include "token_array/token_array.hpp"

#include <fstream>
#include <stdexcept>

void TokenArray::clear()
{
    posIndex.clear();
    wordCost.clear();
    nodeIndex.clear();
    postingsBits = BitVector{};
}

std::vector<TokenEntry> TokenArray::getTokensForTermId(int32_t termId) const
{
    // postingsBits stores: 0, then 1* for term0 tokens, 0, 1* for term1 tokens...
    // Our BitVector/SuccinctBitVector select0 is 1-indexed, while termId is 0-based.
    const int p0 = postingsBits.select0(termId + 1);
    const int p1 = postingsBits.select0(termId + 2);
    if (p0 < 0 || p1 < 0)
        return {};

    const int b = postingsBits.rank1(p0);
    const int c = postingsBits.rank1(p1);

    std::vector<TokenEntry> out;
    out.reserve(static_cast<size_t>(std::max(0, c - b)));
    for (int i = b; i < c; ++i)
    {
        out.push_back(TokenEntry{posIndex[static_cast<size_t>(i)],
                                 wordCost[static_cast<size_t>(i)],
                                 nodeIndex[static_cast<size_t>(i)]});
    }
    return out;
}

void TokenArray::write_u64(std::ostream &os, uint64_t v)
{
    os.write(reinterpret_cast<const char *>(&v), sizeof(v));
}
void TokenArray::write_u32(std::ostream &os, uint32_t v)
{
    os.write(reinterpret_cast<const char *>(&v), sizeof(v));
}
void TokenArray::write_i32(std::ostream &os, int32_t v)
{
    os.write(reinterpret_cast<const char *>(&v), sizeof(v));
}
void TokenArray::write_u16(std::ostream &os, uint16_t v)
{
    os.write(reinterpret_cast<const char *>(&v), sizeof(v));
}
void TokenArray::write_i16(std::ostream &os, int16_t v)
{
    os.write(reinterpret_cast<const char *>(&v), sizeof(v));
}

void TokenArray::read_u64(std::istream &is, uint64_t &v)
{
    is.read(reinterpret_cast<char *>(&v), sizeof(v));
}
void TokenArray::read_u32(std::istream &is, uint32_t &v)
{
    is.read(reinterpret_cast<char *>(&v), sizeof(v));
}
void TokenArray::read_i32(std::istream &is, int32_t &v)
{
    is.read(reinterpret_cast<char *>(&v), sizeof(v));
}
void TokenArray::read_u16(std::istream &is, uint16_t &v)
{
    is.read(reinterpret_cast<char *>(&v), sizeof(v));
}
void TokenArray::read_i16(std::istream &is, int16_t &v)
{
    is.read(reinterpret_cast<char *>(&v), sizeof(v));
}

void TokenArray::writeBitVector(std::ostream &os, const BitVector &bv)
{
    write_u64(os, static_cast<uint64_t>(bv.size()));
    const auto &w = bv.words();
    write_u64(os, static_cast<uint64_t>(w.size()));
    if (!w.empty())
    {
        os.write(reinterpret_cast<const char *>(w.data()),
                 static_cast<std::streamsize>(w.size() * sizeof(uint64_t)));
    }
}

BitVector TokenArray::readBitVector(std::istream &is)
{
    uint64_t nbits = 0;
    read_u64(is, nbits);
    uint64_t wsize = 0;
    read_u64(is, wsize);
    std::vector<uint64_t> w(static_cast<size_t>(wsize));
    if (wsize > 0)
    {
        is.read(reinterpret_cast<char *>(w.data()),
                static_cast<std::streamsize>(wsize * sizeof(uint64_t)));
    }
    BitVector bv;
    bv.assign_from_words(static_cast<size_t>(nbits), std::move(w));
    return bv;
}

void TokenArray::saveToFile(const std::string &path) const
{
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs)
        throw std::runtime_error("failed to open file for write: " + path);

    // posIndex
    write_u32(ofs, static_cast<uint32_t>(posIndex.size()));
    if (!posIndex.empty())
        ofs.write(reinterpret_cast<const char *>(posIndex.data()),
                  static_cast<std::streamsize>(posIndex.size() * sizeof(uint16_t)));

    // wordCost
    write_u32(ofs, static_cast<uint32_t>(wordCost.size()));
    if (!wordCost.empty())
        ofs.write(reinterpret_cast<const char *>(wordCost.data()),
                  static_cast<std::streamsize>(wordCost.size() * sizeof(int16_t)));

    // nodeIndex
    write_u32(ofs, static_cast<uint32_t>(nodeIndex.size()));
    if (!nodeIndex.empty())
        ofs.write(reinterpret_cast<const char *>(nodeIndex.data()),
                  static_cast<std::streamsize>(nodeIndex.size() * sizeof(int32_t)));

    // postingsBits
    writeBitVector(ofs, postingsBits);
}

TokenArray TokenArray::loadFromFile(const std::string &path)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs)
        throw std::runtime_error("failed to open file for read: " + path);

    TokenArray t;

    uint32_t n = 0;
    read_u32(ifs, n);
    t.posIndex.resize(n);
    if (n > 0)
        ifs.read(reinterpret_cast<char *>(t.posIndex.data()),
                 static_cast<std::streamsize>(n * sizeof(uint16_t)));

    read_u32(ifs, n);
    t.wordCost.resize(n);
    if (n > 0)
        ifs.read(reinterpret_cast<char *>(t.wordCost.data()),
                 static_cast<std::streamsize>(n * sizeof(int16_t)));

    read_u32(ifs, n);
    t.nodeIndex.resize(n);
    if (n > 0)
        ifs.read(reinterpret_cast<char *>(t.nodeIndex.data()),
                 static_cast<std::streamsize>(n * sizeof(int32_t)));

    t.postingsBits = readBitVector(ifs);
    return t;
}
