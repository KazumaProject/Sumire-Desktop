#include "louds_with_term_id_reader_utf16.hpp"

LOUDSWithTermIdReaderUtf16::LOUDSWithTermIdReaderUtf16(const LOUDSWithTermIdUtf16 &trie)
    : LBS_(trie.LBS),
      isLeaf_(trie.isLeaf),
      labels_(trie.labels),
      termIdByNodeId_(trie.termIdByNodeId)
{
    // Build select0 table (positions of 0 bits in LBS).
    // LBS has about 1.8M zeros in your log; storing int positions is acceptable.
    zeroPos_.clear();
    zeroPos_.reserve(static_cast<size_t>(LBS_.size()) / 2 + 1);

    // 1-indexed: put a dummy at index 0
    zeroPos_.push_back(-1);

    const int n = static_cast<int>(LBS_.size());
    for (int i = 0; i < n; ++i)
    {
        if (!LBS_.get(i))
        {
            zeroPos_.push_back(i);
        }
    }
}

int LOUDSWithTermIdReaderUtf16::select0_1indexed(int k) const
{
    if (k <= 0)
        return -1;
    if (k >= static_cast<int>(zeroPos_.size()))
        return -1;
    return zeroPos_[k];
}

int LOUDSWithTermIdReaderUtf16::firstChild(int pos) const
{
    // LOUDS: first child of node at LBS position "pos".
    // Formula used in your implementation:
    //   firstChild(pos) = select0(rank1(pos)) + 1
    //
    // rank1(pos) is treated as 1-indexed count in the original code.
    const int r1 = static_cast<int>(LBS_.rank1(pos));
    const int z = select0_1indexed(r1);
    if (z < 0)
        return -1;
    return z + 1;
}

int LOUDSWithTermIdReaderUtf16::traverse(int pos, char16_t c) const
{
    int childPos = firstChild(pos);
    if (childPos < 0)
        return -1;

    // Children are encoded as a run of 1s terminated by 0.
    // Labels are aligned to rank1 positions.
    while (childPos < static_cast<int>(LBS_.size()) && LBS_.get(childPos))
    {
        // labels are indexed by rank1(position) and we have two dummy labels at indices 0 and 1.
        const int labelIndex = static_cast<int>(LBS_.rank1(childPos));
        if (labelIndex >= 0 && labelIndex < static_cast<int>(labels_.size()) && labels_[labelIndex] == c)
        {
            return childPos;
        }
        ++childPos;
    }
    return -1;
}

int LOUDSWithTermIdReaderUtf16::nodeIdFromPos(int pos) const
{
    // In your builder output:
    //   count0(LBS) = nodes including root
    //   termIdByNodeId.size() = count0(LBS) - 1  (root excluded)
    //
    // So we must subtract 1.
    const int raw = static_cast<int>(LBS_.rank1(pos));
    return raw - 1;
}

int32_t LOUDSWithTermIdReaderUtf16::getTermId(const std::u16string &key) const
{
    int pos = 0; // root
    for (char16_t ch : key)
    {
        pos = traverse(pos, ch);
        if (pos < 0)
            return -1;
    }

    const int nodeId = nodeIdFromPos(pos);
    if (nodeId < 0 || nodeId >= static_cast<int>(termIdByNodeId_.size()))
        return -1;

    const int32_t termId = termIdByNodeId_[nodeId];
    return (termId >= 0 ? termId : -1);
}

std::pair<size_t, int32_t> LOUDSWithTermIdReaderUtf16::longestPrefixTermId(const std::u16string &key) const
{
    int pos = 0;

    size_t bestLen = 0;
    int32_t bestTermId = -1;

    for (size_t i = 0; i < key.size(); ++i)
    {
        pos = traverse(pos, key[i]);
        if (pos < 0)
            break;

        const int nodeId = nodeIdFromPos(pos);
        if (nodeId >= 0 && nodeId < static_cast<int>(termIdByNodeId_.size()))
        {
            const int32_t termId = termIdByNodeId_[nodeId];
            if (termId >= 0)
            {
                bestLen = i + 1;
                bestTermId = termId;
            }
        }
    }

    return {bestLen, bestTermId};
}
