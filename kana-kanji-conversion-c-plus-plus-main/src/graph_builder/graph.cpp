#include "graph_builder/graph.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <unordered_set>
#include <unordered_map>
#include <limits>

namespace kk
{

    // -----------------------------
    // PosTable
    // -----------------------------
    PosTable PosTable::loadFromFile(const std::string &path)
    {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs)
            throw std::runtime_error("PosTable: failed to open: " + path);

        uint32_t n = 0;
        ifs.read(reinterpret_cast<char *>(&n), sizeof(n));
        if (!ifs)
            throw std::runtime_error("PosTable: failed to read n: " + path);

        PosTable t;
        t.leftIds.resize(n);
        t.rightIds.resize(n);

        if (n > 0)
        {
            ifs.read(reinterpret_cast<char *>(t.leftIds.data()),
                     static_cast<std::streamsize>(n * sizeof(int16_t)));
            if (!ifs)
                throw std::runtime_error("PosTable: failed to read leftIds: " + path);

            ifs.read(reinterpret_cast<char *>(t.rightIds.data()),
                     static_cast<std::streamsize>(n * sizeof(int16_t)));
            if (!ifs)
                throw std::runtime_error("PosTable: failed to read rightIds: " + path);
        }

        return t;
    }

    std::pair<int16_t, int16_t> PosTable::getLR(uint16_t posIndex) const
    {
        const size_t i = static_cast<size_t>(posIndex);
        if (i >= leftIds.size() || i >= rightIds.size())
            return {0, 0};
        return {leftIds[i], rightIds[i]};
    }

    // -----------------------------
    // Node
    // -----------------------------
    Node::Node()
        : l(0), r(0), score(0), f(0), g(0), tango(), yomi(), len(0), sPos(0), prev(nullptr)
    {
    }

    Node::Node(int16_t l_,
               int16_t r_,
               int score_,
               int f_,
               int g_,
               std::u16string tango_,
               std::u16string yomi_,
               int16_t len_,
               int sPos_)
        : l(l_),
          r(r_),
          score(score_),
          f(f_),
          g(g_),
          tango(std::move(tango_)),
          yomi(std::move(yomi_)),
          len(len_),
          sPos(sPos_),
          prev(nullptr)
    {
    }

    // -----------------------------
    // Hiragana -> Katakana
    // -----------------------------
    static std::u16string hira_to_kata(const std::u16string &hira)
    {
        std::u16string out;
        out.reserve(hira.size());

        for (char16_t ch : hira)
        {
            if ((ch >= 0x3041 && ch <= 0x3096) || (ch >= 0x309D && ch <= 0x309F))
                out.push_back(static_cast<char16_t>(ch + 0x0060));
            else
                out.push_back(ch);
        }
        return out;
    }

    // -----------------------------
    // BOS / EOS
    // -----------------------------
    static Node make_bos()
    {
        return Node(
            /*l=*/0,
            /*r=*/0,
            /*score=*/0,
            /*f=*/0,
            /*g=*/0,
            /*tango=*/u"BOS",
            /*yomi=*/u"",
            /*len=*/0,
            /*sPos=*/0);
    }

    static Node make_eos(int eosPos)
    {
        return Node(
            /*l=*/0,
            /*r=*/0,
            /*score=*/0,
            /*f=*/0,
            /*g=*/0,
            /*tango=*/u"EOS",
            /*yomi=*/u"",
            /*len=*/0,
            /*sPos=*/eosPos);
    }

    // -----------------------------
    // addOrUpdateNode (Kotlin: tango,l,r fully match => keep lower score)
    // -----------------------------
    static void addOrUpdateNode(Graph &graph, int endIndex, const Node &newNode)
    {
        if (endIndex < 0)
            return;

        if (static_cast<size_t>(endIndex) >= graph.size())
            graph.resize(static_cast<size_t>(endIndex) + 1);

        auto &nodes = graph[static_cast<size_t>(endIndex)];

        const auto it = std::find_if(nodes.begin(), nodes.end(),
                                     [&](const Node &n)
                                     {
                                         return (n.l == newNode.l) &&
                                                (n.r == newNode.r) &&
                                                (n.tango == newNode.tango) &&
                                                (n.yomi == newNode.yomi);
                                     });

        if (it != nodes.end())
        {
            if (newNode.score < it->score)
                *it = newNode;
        }
        else
        {
            nodes.push_back(newNode);
        }
    }

    // -----------------------------
    // u16 hash for unordered_set / unordered_map
    // -----------------------------
    struct U16Hash
    {
        size_t operator()(const std::u16string &s) const noexcept
        {
            uint64_t h = 1469598103934665603ULL; // FNV-1a 64-bit
            for (char16_t c : s)
            {
                h ^= static_cast<uint16_t>(c);
                h *= 1099511628211ULL;
            }
            return static_cast<size_t>(h);
        }
    };

    // -----------------------------
    // GraphBuilder::constructGraph
    // -----------------------------
    Graph GraphBuilder::constructGraph(
        const std::u16string &str,
        const LOUDSReaderUtf16 &yomiCps,
        const LOUDSWithTermIdReaderUtf16 &yomiTerm,
        const TokenArray &tokens,
        const PosTable &pos,
        const LOUDSReaderUtf16 &tango,
        YomiSearchMode mode,
        int predictivePrefixLen,
        TypoOptions typo)
    {
        const int n = static_cast<int>(str.size());

        Graph graph;
        graph.resize(static_cast<size_t>(n) + 2);

        graph[0].push_back(make_bos());
        graph[static_cast<size_t>(n) + 1].push_back(make_eos(n + 1));

        if (predictivePrefixLen < 1)
            predictivePrefixLen = 1;

        for (int i = 0; i < n; ++i)
        {
            const std::u16string subStr = str.substr(static_cast<size_t>(i));
            bool foundInAnyDictionary = false;

            // 集約（重複排除）:
            std::vector<std::u16string> yomiHits;
            yomiHits.reserve(256);

            std::unordered_set<std::u16string, U16Hash> seen;
            seen.reserve(256);

            // yomi -> penalty (min)
            std::unordered_map<std::u16string, int, U16Hash> penaltyByYomi;
            penaltyByYomi.reserve(256);

            // (A) commonPrefixSearch は常に含める
            {
                const auto cps = yomiCps.commonPrefixSearch(subStr);
                if (!cps.empty())
                    foundInAnyDictionary = true;

                for (const auto &y : cps)
                {
                    if (seen.insert(y).second)
                    {
                        yomiHits.push_back(y);
                    }
                    auto itp = penaltyByYomi.find(y);
                    if (itp == penaltyByYomi.end())
                        penaltyByYomi.emplace(y, 0);
                    else
                        itp->second = std::min(itp->second, 0);
                }
            }

            // (B) predictiveSearch（必要なら）
            if (mode == YomiSearchMode::CommonPrefixPlusPredictive || mode == YomiSearchMode::All)
            {
                // prefix 長を制限して predictive を掛ける例（必要なら調整）
                const size_t k = static_cast<size_t>(std::min<int>(predictivePrefixLen, (int)subStr.size()));
                const std::u16string pref = subStr.substr(0, k);

                const auto preds = yomiCps.predictiveSearch(pref);
                if (!preds.empty())
                    foundInAnyDictionary = true;

                for (const auto &y : preds)
                {
                    if (y.size() > subStr.size())
                        continue;

                    if (seen.insert(y).second)
                        yomiHits.push_back(y);

                    auto itp = penaltyByYomi.find(y);
                    if (itp == penaltyByYomi.end())
                        penaltyByYomi.emplace(y, 0);
                    else
                        itp->second = std::min(itp->second, 0);
                }
            }

            // (C) omission（濁点/半濁点/小文字など）
            if (mode == YomiSearchMode::CommonPrefixPlusOmission || mode == YomiSearchMode::All)
            {
                const auto omits = yomiCps.commonPrefixSearchWithOmission(subStr);
                if (!omits.empty())
                    foundInAnyDictionary = true;

                for (const auto &r : omits)
                {
                    if (r.yomi.size() > subStr.size())
                        continue;

                    if (seen.insert(r.yomi).second)
                        yomiHits.push_back(r.yomi);

                    const int p = static_cast<int>(r.replaceCount);
                    auto itp = penaltyByYomi.find(r.yomi);
                    if (itp == penaltyByYomi.end())
                        penaltyByYomi.emplace(r.yomi, p);
                    else
                        itp->second = std::min(itp->second, p);
                }
            }

            // (D) ★KanaFlick typo
            if (typo.enable)
            {
                const auto ty = yomiCps.commonPrefixSearchWithTypo(subStr, typo.maxPenalty, typo.maxOut);
                if (!ty.empty())
                    foundInAnyDictionary = true;

                for (const auto &r : ty)
                {
                    if (r.yomi.size() > subStr.size())
                        continue;

                    if (seen.insert(r.yomi).second)
                        yomiHits.push_back(r.yomi);

                    const int p = r.penaltyUsed;
                    auto itp = penaltyByYomi.find(r.yomi);
                    if (itp == penaltyByYomi.end())
                        penaltyByYomi.emplace(r.yomi, p);
                    else
                        itp->second = std::min(itp->second, p);
                }
            }

            // --- add tokens for each yomi ---
            for (const auto &yomiStr : yomiHits)
            {
                const int32_t termId = yomiTerm.getTermId(yomiStr);
                if (termId < 0)
                    continue;

                const auto listToken = tokens.getTokensForTermId(termId);
                const int endIndex = i + static_cast<int>(yomiStr.size());

                int pen = 0;
                {
                    auto itp = penaltyByYomi.find(yomiStr);
                    if (itp != penaltyByYomi.end())
                        pen = itp->second;
                }

                // penalty の係数: typo.weight（typo.disable時でも omission/cps の penalty が入っているなら機能する）
                const int penaltyCost = pen * typo.weight;

                for (const auto &t : listToken)
                {
                    std::u16string surface;
                    if (t.nodeIndex == TokenArray::HIRAGANA_SENTINEL)
                    {
                        surface = yomiStr;
                    }
                    else if (t.nodeIndex == TokenArray::KATAKANA_SENTINEL)
                    {
                        surface = hira_to_kata(yomiStr);
                    }
                    else
                    {
                        surface = tango.getLetter(t.nodeIndex);
                    }

                    const auto [l, r] = pos.getLR(t.posIndex);

                    const int cost = static_cast<int>(t.wordCost) + penaltyCost;

                    Node node(
                        /*l=*/l,
                        /*r=*/r,
                        /*score=*/cost,
                        /*f=*/cost,
                        /*g=*/cost,
                        /*tango=*/std::move(surface),
                        /*yomi=*/yomiStr,
                        /*len=*/static_cast<int16_t>(yomiStr.size()),
                        /*sPos=*/i);

                    addOrUpdateNode(graph, endIndex, node);
                }
            }

            // Unknown fallback: 1-char
            if (!foundInAnyDictionary && !subStr.empty())
            {
                const std::u16string yomi1 = subStr.substr(0, 1);
                const int endIndex = i + 1;

                Node unknownNode(
                    /*l=*/0,
                    /*r=*/0,
                    /*score=*/10000,
                    /*f=*/10000,
                    /*g=*/10000,
                    /*tango=*/yomi1,
                    /*yomi=*/yomi1,
                    /*len=*/1,
                    /*sPos=*/i);

                if (static_cast<size_t>(endIndex) >= graph.size())
                    graph.resize(static_cast<size_t>(endIndex) + 1);

                graph[static_cast<size_t>(endIndex)].push_back(std::move(unknownNode));
            }
        }

        return graph;
    }

} // namespace kk
