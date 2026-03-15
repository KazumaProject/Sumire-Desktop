// src/graph_builder/graph.hpp
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "louds/louds_utf16_reader.hpp"
#include "louds_with_term_id/louds_with_term_id_reader_utf16.hpp"
#include "token_array/token_array.hpp"

namespace kk
{

    // pos_table.bin format (written by tries_token_builder):
    //   uint32_t n
    //   int16_t leftIds[n]
    //   int16_t rightIds[n]
    struct PosTable
    {
        std::vector<int16_t> leftIds;
        std::vector<int16_t> rightIds;

        static PosTable loadFromFile(const std::string &path);

        // Returns (l, r). If out of range, returns (0, 0).
        std::pair<int16_t, int16_t> getLR(uint16_t posIndex) const;
    };

    struct Node
    {
        int16_t l;
        int16_t r;
        int score; // word cost
        int f;     // forward DP: best cost from BOS to this node
        int g;     // backward A*: cost from this node to EOS (accumulated)
        std::u16string tango;
        std::u16string yomi; // reading (hiragana) for this node (may differ from input substring when omit/rewrite is used)
        int16_t len;         // reading length
        int sPos;            // start position in input

        // forward best path pointer (optional; used by forward DP)
        const Node *prev;

        Node();
        Node(int16_t l_,
             int16_t r_,
             int score_,
             int f_,
             int g_,
             std::u16string tango_,
             std::u16string yomi_,
             int16_t len_,
             int sPos_);
    };

    // graph[endIndex] = list of nodes whose end position is endIndex
    using Graph = std::vector<std::vector<Node>>;

    // yomiCps の探索モード
    enum class YomiSearchMode
    {
        CommonPrefixOnly = 0,           // commonPrefixSearch のみ
        CommonPrefixPlusPredictive = 1, // commonPrefixSearch + predictiveSearch
        CommonPrefixPlusOmission = 2,   // commonPrefixSearch + commonPrefixSearchWithOmission
        All = 3                         // 3つすべて
    };

    // ★追加: KanaFlick typo のオプション
    struct TypoOptions
    {
        bool enable = false;
        int maxPenalty = 3; // 2〜3 推奨
        int maxOut = 128;   // yomiCps.commonPrefixSearchWithTypo の最大返却数
        int weight = 1500;  // wordCost に加算する係数（penaltyUsed * weight）
    };

    class GraphBuilder
    {
    public:
        static Graph constructGraph(
            const std::u16string &str,
            const LOUDSReaderUtf16 &yomiCps,
            const LOUDSWithTermIdReaderUtf16 &yomiTerm,
            const TokenArray &tokens,
            const PosTable &pos,
            const LOUDSReaderUtf16 &tango,
            YomiSearchMode mode = YomiSearchMode::CommonPrefixOnly,
            int predictivePrefixLen = 1,
            TypoOptions typo = {} // ★追加（既存呼び出しを壊さない）
        );
    };

} // namespace kk
