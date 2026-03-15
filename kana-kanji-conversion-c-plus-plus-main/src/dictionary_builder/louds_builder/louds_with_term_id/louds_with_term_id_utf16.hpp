#pragma once

#include <cstdint>
#include <fstream>
#include <istream>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "common/bit_vector_utf16.hpp"

// LOUDS trie (UTF-16) with an additional "termId" per node.
//
// termIdByNodeId_ is indexed by nodeId (rank0 in LBS), and stores the
// application-defined terminal id for that node (or -1 if not terminal).

class LOUDSWithTermIdUtf16
{
public:
    // Temporary vectors used by the converter (before calling convertListToBitVector)
    std::vector<bool> LBSTemp;
    std::vector<bool> isLeafTemp;
    std::vector<int32_t> termIdByNodeIdTemp; // one entry per node (per 0-delimiter)

    // Final succinct bitvectors
    BitVector LBS;
    BitVector isLeaf;

    // Labels are aligned with rank1 positions.
    std::vector<char16_t> labels;

    // Final termId mapping aligned with nodeId (rank0).
    std::vector<int32_t> termIdByNodeId;

    LOUDSWithTermIdUtf16();

    void convertListToBitVector();

    void saveToFile(const std::string &path) const;
    static LOUDSWithTermIdUtf16 loadFromFile(const std::string &path);

private:
    static void write_u64(std::ostream &os, uint64_t v);
    static void write_i32(std::ostream &os, int32_t v);
    static void write_u16(std::ostream &os, uint16_t v);

    static void read_u64(std::istream &is, uint64_t &v);
    static void read_i32(std::istream &is, int32_t &v);
    static void read_u16(std::istream &is, uint16_t &v);

    static void write_u64_vec(std::ostream &os, const std::vector<uint64_t> &v);
    static std::vector<uint64_t> read_u64_vec(std::istream &is);

    void writeBitVector(std::ostream &os, const BitVector &bv) const;
    static BitVector readBitVector(std::istream &is);
};
