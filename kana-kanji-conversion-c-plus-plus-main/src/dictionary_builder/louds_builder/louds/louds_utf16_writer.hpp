#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <fstream>
#include <ostream>
#include <istream>

#include "common/bit_vector_utf16.hpp"

// UTF-16 writer は char32_t 版の LOUDS と同名にすると
// リンカで ODR/ABI 衝突するため、別名にしています。
class LOUDSUtf16
{
public:
    std::vector<bool> LBSTemp;
    std::vector<bool> isLeafTemp;

    BitVector LBS;
    BitVector isLeaf;

    std::vector<char16_t> labels;

    LOUDSUtf16();

    void convertListToBitVector();

    std::vector<std::u16string> commonPrefixSearch(const std::u16string &str) const;

    void saveToFile(const std::string &path) const;
    static LOUDSUtf16 loadFromFile(const std::string &path);

    bool equals(const LOUDSUtf16 &other) const;

private:
    int firstChild(int pos) const;
    int traverse(int pos, char16_t c) const;

    static void write_u64(std::ostream &os, uint64_t v);
    static void write_u16(std::ostream &os, uint16_t v);

    static void read_u64(std::istream &is, uint64_t &v);
    static void read_u16(std::istream &is, uint16_t &v);

    static void write_u64_vec(std::ostream &os, const std::vector<uint64_t> &v);
    static std::vector<uint64_t> read_u64_vec(std::istream &is);

    void writeBitVector(std::ostream &os, const BitVector &bv) const;
    static BitVector readBitVector(std::istream &is);
};
