#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <stdexcept>

class BitVector
{
public:
    BitVector() = default;

    size_t size() const { return nbits_; }

    bool get(size_t i) const
    {
        if (i >= nbits_)
            return false;
        return (words_[i >> 6] >> (i & 63)) & 1ULL;
    }

    void set(size_t i, bool v)
    {
        ensure_size(i + 1);
        const size_t w = i >> 6;
        const size_t b = i & 63;
        const uint64_t mask = 1ULL << b;
        if (v)
            words_[w] |= mask;
        else
            words_[w] &= ~mask;
    }

    void push_back(bool v) { set(nbits_, v); }

    // rank0(index): 0..index (inclusive) の 0 の数
    int rank0(int index) const
    {
        if (nbits_ == 0)
            return 0;
        if (index < 0)
            return 0;
        size_t idx = static_cast<size_t>(index);
        if (idx >= nbits_)
            idx = nbits_ - 1;

        int ones = rank1_internal(idx);
        return static_cast<int>(idx + 1) - ones;
    }

    // rank1(index): 0..index (inclusive) の 1 の数
    int rank1(int index) const
    {
        if (nbits_ == 0)
            return 0;
        if (index < 0)
            return 0;
        size_t idx = static_cast<size_t>(index);
        if (idx >= nbits_)
            idx = nbits_ - 1;
        return rank1_internal(idx);
    }

    // select0(nodeId): nodeId番目(1-indexed)の 0 の位置
    int select0(int nodeId) const { return select_internal(false, nodeId); }

    // select1(nodeId): nodeId番目(1-indexed)の 1 の位置
    int select1(int nodeId) const { return select_internal(true, nodeId); }

    const std::vector<uint64_t> &words() const { return words_; }

    void assign_from_words(size_t nbits, std::vector<uint64_t> w)
    {
        nbits_ = nbits;
        words_ = std::move(w);
        if ((nbits_ + 63) / 64 != words_.size())
        {
            throw std::runtime_error("BitVector: words size mismatch");
        }
    }

    bool equals(const BitVector &other) const
    {
        return nbits_ == other.nbits_ && words_ == other.words_;
    }

private:
    size_t nbits_{0};
    std::vector<uint64_t> words_;

    void ensure_size(size_t nbits)
    {
        if (nbits <= nbits_)
            return;
        nbits_ = nbits;
        const size_t need = (nbits_ + 63) / 64;
        if (words_.size() < need)
            words_.resize(need, 0ULL);
    }

    static int popcount64(uint64_t x)
    {
        return __builtin_popcountll(x);
    }

    int rank1_internal(size_t idx) const
    {
        const size_t full_words = idx >> 6;
        const size_t bit_in_word = idx & 63;

        int count = 0;
        for (size_t w = 0; w < full_words; ++w)
        {
            count += popcount64(words_[w]);
        }

        const uint64_t mask =
            (bit_in_word == 63) ? ~0ULL : ((1ULL << (bit_in_word + 1)) - 1ULL);

        count += popcount64(words_[full_words] & mask);
        return count;
    }

    int select_internal(bool value, int nodeId) const
    {
        if (nodeId <= 0)
            return -1;
        int count = 0;
        for (size_t i = 0; i < nbits_; ++i)
        {
            if (get(i) == value)
            {
                ++count;
                if (count == nodeId)
                    return static_cast<int>(i);
            }
        }
        return -1;
    }
};
