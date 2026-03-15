#include "louds_with_term_id/louds_with_term_id_utf16.hpp"

LOUDSWithTermIdUtf16::LOUDSWithTermIdUtf16()
{
    // Keep compatibility with the existing LOUDSUtf16 dummy root behavior.
    LBSTemp = {true, false};
    labels = {u' ', u' '};
    isLeafTemp = {false, false};
    // Root node (nodeId=0) is always non-terminal.
    termIdByNodeIdTemp = {-1};
}

void LOUDSWithTermIdUtf16::convertListToBitVector()
{
    BitVector lbs;
    for (bool b : LBSTemp)
        lbs.push_back(b);
    LBS = std::move(lbs);
    LBSTemp.clear();

    BitVector leaf;
    for (bool b : isLeafTemp)
        leaf.push_back(b);
    isLeaf = std::move(leaf);
    isLeafTemp.clear();

    termIdByNodeId = std::move(termIdByNodeIdTemp);
    termIdByNodeIdTemp.clear();
}

void LOUDSWithTermIdUtf16::write_u64(std::ostream &os, uint64_t v)
{
    os.write(reinterpret_cast<const char *>(&v), sizeof(v));
}

void LOUDSWithTermIdUtf16::write_i32(std::ostream &os, int32_t v)
{
    os.write(reinterpret_cast<const char *>(&v), sizeof(v));
}

void LOUDSWithTermIdUtf16::write_u16(std::ostream &os, uint16_t v)
{
    os.write(reinterpret_cast<const char *>(&v), sizeof(v));
}

void LOUDSWithTermIdUtf16::read_u64(std::istream &is, uint64_t &v)
{
    is.read(reinterpret_cast<char *>(&v), sizeof(v));
}

void LOUDSWithTermIdUtf16::read_i32(std::istream &is, int32_t &v)
{
    is.read(reinterpret_cast<char *>(&v), sizeof(v));
}

void LOUDSWithTermIdUtf16::read_u16(std::istream &is, uint16_t &v)
{
    is.read(reinterpret_cast<char *>(&v), sizeof(v));
}

void LOUDSWithTermIdUtf16::write_u64_vec(std::ostream &os, const std::vector<uint64_t> &v)
{
    write_u64(os, static_cast<uint64_t>(v.size()));
    if (!v.empty())
    {
        os.write(reinterpret_cast<const char *>(v.data()),
                 static_cast<std::streamsize>(v.size() * sizeof(uint64_t)));
    }
}

std::vector<uint64_t> LOUDSWithTermIdUtf16::read_u64_vec(std::istream &is)
{
    uint64_t n = 0;
    read_u64(is, n);
    std::vector<uint64_t> v(static_cast<size_t>(n));
    if (n > 0)
    {
        is.read(reinterpret_cast<char *>(v.data()),
                static_cast<std::streamsize>(n * sizeof(uint64_t)));
    }
    return v;
}

void LOUDSWithTermIdUtf16::writeBitVector(std::ostream &os, const BitVector &bv) const
{
    write_u64(os, static_cast<uint64_t>(bv.size()));
    write_u64_vec(os, bv.words());
}

BitVector LOUDSWithTermIdUtf16::readBitVector(std::istream &is)
{
    uint64_t nbits = 0;
    read_u64(is, nbits);
    auto words = read_u64_vec(is);
    BitVector bv;
    bv.assign_from_words(static_cast<size_t>(nbits), std::move(words));
    return bv;
}

void LOUDSWithTermIdUtf16::saveToFile(const std::string &path) const
{
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs)
        throw std::runtime_error("failed to open file for write: " + path);

    writeBitVector(ofs, LBS);
    writeBitVector(ofs, isLeaf);

    // labels
    write_u64(ofs, static_cast<uint64_t>(labels.size()));
    for (char16_t ch : labels)
        write_u16(ofs, static_cast<uint16_t>(ch));

    // termIdByNodeId
    write_u64(ofs, static_cast<uint64_t>(termIdByNodeId.size()));
    for (int32_t v : termIdByNodeId)
        write_i32(ofs, v);
}

LOUDSWithTermIdUtf16 LOUDSWithTermIdUtf16::loadFromFile(const std::string &path)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs)
        throw std::runtime_error("failed to open file for read: " + path);

    LOUDSWithTermIdUtf16 l;
    l.LBS = readBitVector(ifs);
    l.isLeaf = readBitVector(ifs);

    uint64_t labelN = 0;
    read_u64(ifs, labelN);
    l.labels.resize(static_cast<size_t>(labelN));
    for (size_t i = 0; i < static_cast<size_t>(labelN); ++i)
    {
        uint16_t v = 0;
        read_u16(ifs, v);
        l.labels[i] = static_cast<char16_t>(v);
    }

    uint64_t termN = 0;
    read_u64(ifs, termN);
    l.termIdByNodeId.resize(static_cast<size_t>(termN));
    for (size_t i = 0; i < static_cast<size_t>(termN); ++i)
    {
        int32_t v = -1;
        read_i32(ifs, v);
        l.termIdByNodeId[i] = v;
    }
    return l;
}
