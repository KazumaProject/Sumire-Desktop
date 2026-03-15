#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <istream>
#include <utility>

#include "common/bit_vector_utf16.hpp"
#include "common/succinct_bit_vector_utf16.hpp"

class LOUDSReaderUtf16
{
public:
    struct OmissionSearchResult
    {
        std::u16string yomi;
        uint16_t replaceCount = 0;     // 置換が必要だった文字数
        bool omissionOccurred = false; // 置換が1回でも発生したか
    };

    // ★追加: KanaFlick typo の累積ペナルティ付き結果
    struct TypoSearchResult
    {
        std::u16string yomi;
        int penaltyUsed = 0; // Kotlin側 TypoCategory.penalty の合計
    };

public:
    LOUDSReaderUtf16(const BitVector &lbs,
                     const BitVector &isLeaf,
                     std::vector<char16_t> labels);

    std::vector<std::u16string> commonPrefixSearch(const std::u16string &str) const;

    // Added
    std::vector<std::u16string> predictiveSearch(const std::u16string &prefix) const;

    // Added
    std::vector<OmissionSearchResult> commonPrefixSearchWithOmission(const std::u16string &str) const;

    // ★追加: 12キー typo を考慮した common prefix search
    // maxPenalty: 累積ペナルティ上限（例: 2〜3）
    // maxOut    : 最大返却数（例: 128）
    std::vector<TypoSearchResult> commonPrefixSearchWithTypo(const std::u16string &str,
                                                             int maxPenalty,
                                                             int maxOut) const;

    // ルートから nodeIndex までのラベルを復元
    std::u16string getLetter(int nodeIndex) const;

    int getNodeIndex(const std::u16string &s) const;
    int getNodeId(const std::u16string &s) const;

    const std::vector<char16_t> &getAllLabels() const { return labels_; }

    static LOUDSReaderUtf16 loadFromFile(const std::string &path);

private:
    BitVector LBS_;
    BitVector isLeaf_;
    std::vector<char16_t> labels_;

    SuccinctBitVector lbsSucc_;

    int firstChild(int pos) const;
    int traverse(int pos, char16_t c) const;

    int search(int index, const std::u16string &chars, size_t wordOffset) const;

    // predictiveSearch helper
    void collectWords(int pos, std::u16string &prefix, std::vector<std::u16string> &out) const;

    // omission helpers
    static std::vector<char16_t> getCharVariations(char16_t ch);

    void searchRecursiveWithOmission(const std::u16string &originalStr,
                                     size_t strIndex,
                                     int currentNodeIndex,
                                     std::u16string &currentYomi,
                                     uint16_t replaceCount,
                                     bool omissionOccurred,
                                     std::vector<OmissionSearchResult> &out) const;

    // ★追加: typo helpers
    // (置換文字, 追加ペナルティ) の候補を返す
    static std::vector<std::pair<char16_t, int>> getTypoVariationsKanaFlick(char16_t ch);

    static void read_u64(std::istream &is, uint64_t &v);
    static void read_u16(std::istream &is, uint16_t &v);
    static std::vector<uint64_t> read_u64_vec(std::istream &is);
    static BitVector readBitVector(std::istream &is);
};
