#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "louds_with_term_id_utf16.hpp"

// Reader for LOUDSWithTermIdUtf16.
//
// IMPORTANT (this repo's binary layout):
// - labels are indexed by rank1(position) and there are TWO dummy labels at indices 0 and 1.
// - termIdByNodeId.size() == popcount(1) of LBS (i.e., number of non-root nodes / edges).
// - Therefore, nodeId index for a node-position "pos" (where LBS[pos] == 1) is:
//       nodeId = rank1(pos) - 2
//   (because rank1(pos) itself is the label index, and real labels start at index 2)
class LOUDSWithTermIdReaderUtf16
{
public:
    explicit LOUDSWithTermIdReaderUtf16(const LOUDSWithTermIdUtf16 &trie);

    int firstChild(int pos) const;
    int traverse(int pos, char16_t c) const;

    int32_t getTermId(const std::u16string &key) const;
    std::pair<size_t, int32_t> longestPrefixTermId(const std::u16string &key) const;

private:
    // Convert LOUDS position (a 1-bit position returned by traverse) to nodeId index for termIdByNodeId_.
    // Returns -1 if pos is root/invalid.
    int nodeIdFromPos(int pos) const;

    // 1-indexed select0: returns the position of the k-th 0 bit in LBS.
    // Returns -1 if k is out of range.
    int select0_1indexed(int k) const;

private:
    const BitVector &LBS_;
    const BitVector &isLeaf_;
    const std::vector<char16_t> &labels_;
    const std::vector<int32_t> &termIdByNodeId_;

    // Precomputed 0-bit positions for select0 (1-indexed):
    // zeroPos_[k] = position of k-th 0 in LBS, zeroPos_[0] is dummy.
    std::vector<int> zeroPos_;
};
