#include "louds/louds_utf16_writer.hpp"
#include <stdexcept>

LOUDSUtf16::LOUDSUtf16()
{
    // 既存実装互換のためのダミー要素
    LBSTemp = {true, false};
    labels = {u' ', u' '};
    isLeafTemp = {false, false};
}

void LOUDSUtf16::convertListToBitVector()
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
}

int LOUDSUtf16::firstChild(int pos) const
{
    const int y = LBS.select0(LBS.rank1(pos)) + 1;
    if (y < 0)
        return -1;
    if (static_cast<size_t>(y) >= LBS.size())
        return -1;
    return (LBS.get(static_cast<size_t>(y)) ? y : -1);
}

int LOUDSUtf16::traverse(int pos, char16_t c) const
{
    int childPos = firstChild(pos);
    if (childPos == -1)
        return -1;

    while (static_cast<size_t>(childPos) < LBS.size() &&
           LBS.get(static_cast<size_t>(childPos)))
    {
        const int labelIndex = LBS.rank1(childPos);
        if (labelIndex >= 0 && static_cast<size_t>(labelIndex) < labels.size())
        {
            if (labels[static_cast<size_t>(labelIndex)] == c)
                return childPos;
        }
        childPos += 1;
    }
    return -1;
}

std::vector<std::u16string> LOUDSUtf16::commonPrefixSearch(const std::u16string &str) const
{
    std::vector<char16_t> resultTemp;
    std::vector<std::u16string> result;

    int n = 0;
    for (char16_t c : str)
    {
        n = traverse(n, c);
        if (n == -1)
            break;

        const int index = LBS.rank1(n);
        if (index < 0 || static_cast<size_t>(index) >= labels.size())
            return result;

        resultTemp.push_back(labels[static_cast<size_t>(index)]);

        if (static_cast<size_t>(n) < isLeaf.size() && isLeaf.get(static_cast<size_t>(n)))
        {
            std::u16string tempStr(resultTemp.begin(), resultTemp.end());
            if (!result.empty())
            {
                result.push_back(result[0] + tempStr);
            }
            else
            {
                result.push_back(tempStr);
                resultTemp.clear();
            }
        }
    }
    return result;
}

bool LOUDSUtf16::equals(const LOUDSUtf16 &other) const
{
    return LBS.equals(other.LBS) &&
           isLeaf.equals(other.isLeaf) &&
           labels == other.labels;
}

void LOUDSUtf16::write_u64(std::ostream &os, uint64_t v)
{
    os.write(reinterpret_cast<const char *>(&v), sizeof(v));
}

void LOUDSUtf16::write_u16(std::ostream &os, uint16_t v)
{
    os.write(reinterpret_cast<const char *>(&v), sizeof(v));
}

void LOUDSUtf16::read_u64(std::istream &is, uint64_t &v)
{
    is.read(reinterpret_cast<char *>(&v), sizeof(v));
}

void LOUDSUtf16::read_u16(std::istream &is, uint16_t &v)
{
    is.read(reinterpret_cast<char *>(&v), sizeof(v));
}

void LOUDSUtf16::write_u64_vec(std::ostream &os, const std::vector<uint64_t> &v)
{
    write_u64(os, static_cast<uint64_t>(v.size()));
    if (!v.empty())
    {
        os.write(reinterpret_cast<const char *>(v.data()),
                 static_cast<std::streamsize>(v.size() * sizeof(uint64_t)));
    }
}

std::vector<uint64_t> LOUDSUtf16::read_u64_vec(std::istream &is)
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

void LOUDSUtf16::writeBitVector(std::ostream &os, const BitVector &bv) const
{
    write_u64(os, static_cast<uint64_t>(bv.size()));
    write_u64_vec(os, bv.words());
}

BitVector LOUDSUtf16::readBitVector(std::istream &is)
{
    uint64_t nbits = 0;
    read_u64(is, nbits);
    auto words = read_u64_vec(is);
    BitVector bv;
    bv.assign_from_words(static_cast<size_t>(nbits), std::move(words));
    return bv;
}

void LOUDSUtf16::saveToFile(const std::string &path) const
{
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs)
        throw std::runtime_error("failed to open file for write: " + path);

    writeBitVector(ofs, LBS);
    writeBitVector(ofs, isLeaf);

    write_u64(ofs, static_cast<uint64_t>(labels.size()));
    for (char16_t ch : labels)
    {
        write_u16(ofs, static_cast<uint16_t>(ch));
    }
}

LOUDSUtf16 LOUDSUtf16::loadFromFile(const std::string &path)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs)
        throw std::runtime_error("failed to open file for read: " + path);

    LOUDSUtf16 l;
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
    return l;
}
