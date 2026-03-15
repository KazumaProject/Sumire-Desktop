#pragma once
#include <cstdint>
#include <vector>
#include <algorithm>

#include "common/bit_vector_utf16.hpp"

// Kotlin の SuccinctBitVector 相当。
// - big block: 256 bits
// - small block: 8 bits
// - rank1/rank0: O(1) + 最大8bitの走査
// - select1/select0: 大ブロック二分探索 + 小ブロック線形 + 最大8bit走査
class SuccinctBitVector
{
public:
    explicit SuccinctBitVector(const BitVector &bv)
        : bv_(bv),
          n_(static_cast<int>(bv.size())),
          bigBlockRanks_(),
          smallBlockRanks_(),
          totalOnes_(0)
    {
        build();
    }

    int size() const { return n_; }
    int totalOnes() const { return totalOnes_; }

    // rank1(index): 0..index (inclusive) の 1 の数
    int rank1(int index) const
    {
        if (index < 0)
            return 0;
        if (n_ <= 0)
            return 0;
        if (index >= n_)
            return totalOnes_;

        const int bigIndex = index / bigBlockSize_;
        const int offsetInBig = index % bigBlockSize_;
        const int smallIndex = offsetInBig / smallBlockSize_;
        const int offsetInSmall = offsetInBig % smallBlockSize_;

        const int globalSmallIndex = bigIndex * numSmallBlocksPerBig_ + smallIndex;

        int rankBase =
            bigBlockRanks_[static_cast<size_t>(bigIndex)] +
            smallBlockRanks_[static_cast<size_t>(globalSmallIndex)];

        int additional = 0;
        const int smallStart = bigIndex * bigBlockSize_ + smallIndex * smallBlockSize_;
        for (int i = 0; i <= offsetInSmall; ++i)
        {
            const int pos = smallStart + i;
            if (pos >= n_)
                break;
            if (bv_.get(static_cast<size_t>(pos)))
                additional++;
        }
        return rankBase + additional;
    }

    // rank0(index): 0..index (inclusive) の 0 の数
    int rank0(int index) const
    {
        if (index < 0)
            return 0;
        if (n_ <= 0)
            return 0;
        if (index >= n_)
            return n_ - totalOnes_;
        return (index + 1) - rank1(index);
    }

    // select1(nodeId): nodeId番目(1-indexed)の 1 の位置
    int select1(int nodeId) const
    {
        if (nodeId < 1 || nodeId > totalOnes_)
            return -1;
        if (n_ <= 0)
            return -1;

        // bigBlockRanks[big] は big ブロック開始時点の累積 ones
        int lo = 0;
        int hi = static_cast<int>(bigBlockRanks_.size()) - 1;
        int bigBlock = 0;
        while (lo <= hi)
        {
            int mid = (lo + hi) / 2;
            if (bigBlockRanks_[static_cast<size_t>(mid)] < nodeId)
            {
                bigBlock = mid;
                lo = mid + 1;
            }
            else
            {
                hi = mid - 1;
            }
        }

        const int localTarget = nodeId - bigBlockRanks_[static_cast<size_t>(bigBlock)];

        const int baseSmallIndex = bigBlock * numSmallBlocksPerBig_;
        const int numSmallBlocks = static_cast<int>(smallBlockRanks_.size());
        const int smallBlocksInThisBig =
            std::min(numSmallBlocksPerBig_, std::max(0, numSmallBlocks - baseSmallIndex));

        int smallBlock = 0;
        while (smallBlock < smallBlocksInThisBig - 1)
        {
            const int nextIndex = baseSmallIndex + smallBlock + 1;
            if (smallBlockRanks_[static_cast<size_t>(nextIndex)] < localTarget)
            {
                smallBlock++;
            }
            else
            {
                break;
            }
        }

        const int globalSmallIndex = baseSmallIndex + smallBlock;
        const int offsetInSmallBlock =
            localTarget - smallBlockRanks_[static_cast<size_t>(globalSmallIndex)];

        const int smallStart = bigBlock * bigBlockSize_ + smallBlock * smallBlockSize_;
        int count = 0;
        for (int i = 0; i < smallBlockSize_; ++i)
        {
            const int pos = smallStart + i;
            if (pos >= n_)
                break;
            if (bv_.get(static_cast<size_t>(pos)))
            {
                ++count;
                if (count == offsetInSmallBlock)
                    return pos;
            }
        }
        return -1;
    }

    // select0(nodeId): nodeId番目(1-indexed)の 0 の位置
    int select0(int nodeId) const
    {
        if (n_ <= 0)
            return -1;
        const int totalZeros = n_ - totalOnes_;
        if (nodeId < 1 || nodeId > totalZeros)
            return -1;

        int lo = 0;
        int hi = static_cast<int>(bigBlockRanks_.size()) - 1;
        int bigBlock = 0;

        while (lo <= hi)
        {
            int mid = (lo + hi) / 2;
            const int blockStart = mid * bigBlockSize_;
            const int zerosBefore = blockStart - bigBlockRanks_[static_cast<size_t>(mid)];
            if (zerosBefore < nodeId)
            {
                bigBlock = mid;
                lo = mid + 1;
            }
            else
            {
                hi = mid - 1;
            }
        }

        const int zerosBeforeBlock =
            bigBlock * bigBlockSize_ - bigBlockRanks_[static_cast<size_t>(bigBlock)];
        const int localTarget = nodeId - zerosBeforeBlock;

        const int baseSmallIndex = bigBlock * numSmallBlocksPerBig_;
        const int numSmallBlocks = static_cast<int>(smallBlockRanks_.size());
        const int smallBlocksInThisBig =
            std::min(numSmallBlocksPerBig_, std::max(0, numSmallBlocks - baseSmallIndex));

        int smallBlock = 0;
        while (smallBlock < smallBlocksInThisBig - 1)
        {
            const int nextSmall = smallBlock + 1;
            const int nextGlobal = baseSmallIndex + nextSmall;
            const int onesBeforeNextSmall = smallBlockRanks_[static_cast<size_t>(nextGlobal)];
            const int bitsBeforeNextSmall = nextSmall * smallBlockSize_;
            const int nextZeros = bitsBeforeNextSmall - onesBeforeNextSmall;

            if (nextZeros < localTarget)
            {
                smallBlock++;
            }
            else
            {
                break;
            }
        }

        const int globalSmallIndex = baseSmallIndex + smallBlock;
        const int onesBeforeSmall = smallBlockRanks_[static_cast<size_t>(globalSmallIndex)];
        const int bitsBeforeSmall = smallBlock * smallBlockSize_;
        const int zerosBeforeSmall = bitsBeforeSmall - onesBeforeSmall;
        const int offsetInSmallBlock = localTarget - zerosBeforeSmall;

        const int smallStart = bigBlock * bigBlockSize_ + smallBlock * smallBlockSize_;
        int count = 0;
        for (int i = 0; i < smallBlockSize_; ++i)
        {
            const int pos = smallStart + i;
            if (pos >= n_)
                break;
            if (!bv_.get(static_cast<size_t>(pos)))
            {
                ++count;
                if (count == offsetInSmallBlock)
                    return pos;
            }
        }
        return -1;
    }

private:
    const BitVector &bv_;
    int n_;

    static constexpr int bigBlockSize_ = 256;
    static constexpr int smallBlockSize_ = 8;
    static constexpr int numSmallBlocksPerBig_ = bigBlockSize_ / smallBlockSize_;

    std::vector<int> bigBlockRanks_;
    std::vector<int> smallBlockRanks_;
    int totalOnes_;

    void build()
    {
        if (n_ <= 0)
        {
            bigBlockRanks_.clear();
            smallBlockRanks_.clear();
            totalOnes_ = 0;
            return;
        }

        const int numBigBlocks = (n_ + bigBlockSize_ - 1) / bigBlockSize_;
        bigBlockRanks_.assign(static_cast<size_t>(numBigBlocks), 0);

        const int numSmallBlocks = (n_ + smallBlockSize_ - 1) / smallBlockSize_;
        smallBlockRanks_.assign(static_cast<size_t>(numSmallBlocks), 0);

        int rank = 0;
        for (int big = 0; big < numBigBlocks; ++big)
        {
            const int bigStart = big * bigBlockSize_;
            bigBlockRanks_[static_cast<size_t>(big)] = rank;

            for (int small = 0; small < numSmallBlocksPerBig_; ++small)
            {
                const int globalSmall = big * numSmallBlocksPerBig_ + small;
                if (globalSmall >= numSmallBlocks)
                    break;

                smallBlockRanks_[static_cast<size_t>(globalSmall)] =
                    rank - bigBlockRanks_[static_cast<size_t>(big)];

                const int smallStart = bigStart + small * smallBlockSize_;
                for (int j = 0; j < smallBlockSize_; ++j)
                {
                    const int pos = smallStart + j;
                    if (pos >= n_)
                        break;
                    if (bv_.get(static_cast<size_t>(pos)))
                        rank++;
                }
            }
        }

        totalOnes_ = rank;
    }
};
