#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "./common/bit_vector_utf16.hpp"

// TokenArray is the per-yomi posting list used by the converter.
//
// Layout (aligned with Kotlin logic):
// - We maintain a bitvector where each yomi term starts with a 0 bit,
//   followed by N 1 bits for its tokens.
// - posIndex[i], wordCost[i], nodeIndex[i] are the token payload arrays.
//
// termId is 0-based and corresponds to the order of yomi keys
// in the sorted dictionary (length asc, then lex asc).

struct TokenEntry
{
    uint16_t posIndex;
    int16_t wordCost;
    int32_t nodeIndex; // tango LOUDS node index; may be -1/-2 sentinels
};

class TokenArray
{
public:
    static constexpr int32_t HIRAGANA_SENTINEL = -2;
    static constexpr int32_t KATAKANA_SENTINEL = -1;

    void clear();

    // Query tokens for a termId (0-based).
    std::vector<TokenEntry> getTokensForTermId(int32_t termId) const;

    void saveToFile(const std::string &path) const;
    static TokenArray loadFromFile(const std::string &path);

    // Public data (useful for debugging/inspection)
    std::vector<uint16_t> posIndex;
    std::vector<int16_t> wordCost;
    std::vector<int32_t> nodeIndex;
    BitVector postingsBits; // 0 then 1* for each term

private:
    static void write_u64(std::ostream &os, uint64_t v);
    static void write_u32(std::ostream &os, uint32_t v);
    static void write_i32(std::ostream &os, int32_t v);
    static void write_u16(std::ostream &os, uint16_t v);
    static void write_i16(std::ostream &os, int16_t v);

    static void read_u64(std::istream &is, uint64_t &v);
    static void read_u32(std::istream &is, uint32_t &v);
    static void read_i32(std::istream &is, int32_t &v);
    static void read_u16(std::istream &is, uint16_t &v);
    static void read_i16(std::istream &is, int16_t &v);

    static void writeBitVector(std::ostream &os, const BitVector &bv);
    static BitVector readBitVector(std::istream &is);
};
