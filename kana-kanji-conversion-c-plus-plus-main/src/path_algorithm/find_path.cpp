// src/path_algorithm/find_path.cpp
#include "path_algorithm/find_path.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>
#include <unordered_set>

namespace kk
{

    // -----------------------------
    // ConnectionMatrix
    // -----------------------------
    ConnectionMatrix::ConnectionMatrix(std::vector<int16_t> v)
        : dim_(0), data_(std::move(v))
    {
        if (data_.empty())
            throw std::runtime_error("ConnectionMatrix: empty data");

        const double root = std::sqrt(static_cast<double>(data_.size()));
        const int n = static_cast<int>(root + 0.5);
        if (n <= 0 || static_cast<size_t>(n) * static_cast<size_t>(n) != data_.size())
            throw std::runtime_error("ConnectionMatrix: size is not a perfect square: " + std::to_string(data_.size()));

        dim_ = n;
    }

    int ConnectionMatrix::get(int leftId, int rightId) const
    {
        if (leftId < 0 || rightId < 0)
            return 0;
        if (leftId >= dim_ || rightId >= dim_)
            return 0;
        return static_cast<int>(data_[static_cast<size_t>(leftId) * static_cast<size_t>(dim_) + static_cast<size_t>(rightId)]);
    }

    // -----------------------------
    // Internal State for backward A*
    // -----------------------------
    struct State
    {
        const Node *node;            // points to a Node in graph
        int g;                       // accumulated cost from this node to EOS
        int total;                   // priority (g + f)
        std::shared_ptr<State> next; // successor state (towards EOS)

        State(const Node *n_, int g_, int total_, std::shared_ptr<State> next_)
            : node(n_), g(g_), total(total_), next(std::move(next_))
        {
        }
    };

    // priority: smaller total first, then smaller sPos, then smaller len, then pointer address
    struct StateLess
    {
        bool operator()(const std::shared_ptr<State> &a, const std::shared_ptr<State> &b) const
        {
            if (a->total != b->total)
                return a->total > b->total; // min-heap behavior via priority_queue (reverse)
            if (a->node->sPos != b->node->sPos)
                return a->node->sPos > b->node->sPos;
            if (a->node->len != b->node->len)
                return a->node->len > b->node->len;
            return a.get() > b.get();
        }
    };

    // u16string hash for dedup
    struct U16Hash
    {
        size_t operator()(const std::u16string &s) const noexcept
        {
            size_t h = 1469598103934665603ull;
            for (char16_t c : s)
            {
                h ^= static_cast<size_t>(c);
                h *= 1099511628211ull;
            }
            return h;
        }
    };

    // -----------------------------
    // Helpers: prev node list
    // -----------------------------
    static std::vector<const Node *> getPrevNodesForward(const Graph &graph, const Node &node, int endIndex, int length)
    {
        // Kotlin getPrevNodes:
        // index = if (node.tango == "EOS") length else endIndex - node.len
        int index = 0;
        if (node.tango == u"EOS")
            index = length;
        else
            index = endIndex - static_cast<int>(node.len);

        if (index == 0)
            return {&graph[0][0]}; // BOS
        if (index < 0 || static_cast<size_t>(index) >= graph.size())
            return {};

        const auto &v = graph[static_cast<size_t>(index)];
        std::vector<const Node *> out;
        out.reserve(v.size());
        for (const auto &x : v)
            out.push_back(&x);
        return out;
    }

    static std::vector<const Node *> getPrevNodesBackward(const Graph &graph, const Node &node, int length)
    {
        // Kotlin getPrevNodes2:
        // index = if (node.tango == "EOS") length else node.sPos
        int index = 0;
        if (node.tango == u"EOS")
            index = length;
        else
            index = node.sPos;

        if (index == 0)
            return {&graph[0][0]}; // BOS
        if (index < 0 || static_cast<size_t>(index) >= graph.size())
            return {};

        const auto &v = graph[static_cast<size_t>(index)];
        std::vector<const Node *> out;
        out.reserve(v.size());
        for (const auto &x : v)
            out.push_back(&x);
        return out;
    }

    // -----------------------------
    // forwardDp (beam pruning)
    // -----------------------------
    void FindPath::forwardDp(Graph &graph, int length, const ConnectionMatrix &conn, int beamWidth)
    {
        const int INF = std::numeric_limits<int>::max() / 4;

        // initialize BOS f=0 (already 0), others keep initial f=word cost.
        for (int i = 1; i <= length + 1; ++i)
        {
            if (i < 0 || static_cast<size_t>(i) >= graph.size())
                continue;

            auto &nodes = graph[static_cast<size_t>(i)];
            if (nodes.empty())
                continue;

            for (auto &node : nodes)
            {
                const int nodeWordCost = node.score;

                int best = INF;
                const Node *bestPrev = nullptr;

                const auto prevs = getPrevNodesForward(graph, node, i, length);
                for (const Node *p : prevs)
                {
                    const int edge = conn.get(static_cast<int>(p->l), static_cast<int>(node.r));
                    const int prevCost = p->f;

                    const int temp = prevCost + nodeWordCost + edge;
                    if (temp < best)
                    {
                        best = temp;
                        bestPrev = p;
                    }
                }

                node.prev = bestPrev;
                node.f = best;
            }

            // pruning (do not prune EOS layer)
            if (i <= length && beamWidth > 0 && static_cast<int>(nodes.size()) > beamWidth)
            {
                std::sort(nodes.begin(), nodes.end(), [](const Node &a, const Node &b)
                          { return a.f < b.f; });
                nodes.resize(static_cast<size_t>(beamWidth));
            }
        }
    }

    // -----------------------------
    // numeric/symbol classifiers (approximation consistent enough for your scoring/type)
    // -----------------------------
    static bool is_ascii_digit(char16_t c) { return (c >= u'0' && c <= u'9'); }
    static bool is_fullwidth_digit(char16_t c) { return (c >= 0xFF10 && c <= 0xFF19); }

    bool FindPath::anyDigit(const std::u16string &s)
    {
        for (char16_t c : s)
        {
            if (is_ascii_digit(c) || is_fullwidth_digit(c))
                return true;
        }
        return false;
    }

    bool FindPath::isAllHalfWidthNumericSymbol(const std::u16string &s)
    {
        if (s.empty())
            return false;
        for (char16_t c : s)
        {
            // allow ASCII digits, punctuation, and space
            if (c == u' ')
                continue;
            if (c >= 0x21 && c <= 0x7E)
                continue;
            return false;
        }
        return true;
    }

    bool FindPath::isAllFullWidthNumericSymbol(const std::u16string &s)
    {
        if (s.empty())
            return false;
        for (char16_t c : s)
        {
            // allow fullwidth digits and common fullwidth ASCII variants + fullwidth space
            if (c == 0x3000) // IDEOGRAPHIC SPACE
                continue;
            if (is_fullwidth_digit(c))
                continue;
            if (c >= 0xFF01 && c <= 0xFF5E)
                continue;
            return false;
        }
        return true;
    }

    // -----------------------------
    // bunsetsu logic (same ranges as Kotlin)
    // -----------------------------
    bool FindPath::isIndependentWord(int16_t id)
    {
        const int x = static_cast<int>(id);

        // 副詞, 接続詞, 感動詞, 接頭詞, 連体詞
        if ((x >= 12 && x <= 28) || (x >= 2590 && x <= 2670))
            return true;

        // 動詞,自立
        if (x >= 577 && x <= 856)
            return true;

        // 形容詞,自立
        if (x >= 2390 && x <= 2471)
            return true;

        // 名詞 (ただし接尾(1937-2040)を除く)
        if (x >= 1842 && x <= 2195)
            return !(x >= 1937 && x <= 2040);

        return false;
    }

    static std::u16string buildStringFromBosState(const std::shared_ptr<State> &bosState)
    {
        std::u16string out;
        auto cur = bosState->next; // BOS -> first token
        while (cur && cur->node->tango != u"EOS")
        {
            out += cur->node->tango;
            cur = cur->next;
        }
        return out;
    }

    static std::u16string buildYomiFromBosState(const std::shared_ptr<State> &bosState)
    {
        std::u16string out;
        auto cur = bosState->next; // BOS -> first token
        while (cur && cur->node->tango != u"EOS")
        {
            out += cur->node->yomi;
            cur = cur->next;
        }
        return out;
    }

    std::vector<int> FindPath::getBunsetsuPositionsFromPath(const std::shared_ptr<State> &bosState)
    {
        std::vector<int> positions;
        int currentPos = 0;

        auto cur = bosState->next;
        while (cur && cur->node->tango != u"EOS")
        {
            if (currentPos > 0 && isIndependentWord(cur->node->l))
                positions.push_back(currentPos);

            currentPos += static_cast<int>(cur->node->len);
            cur = cur->next;
        }
        return positions;
    }

    // -----------------------------
    // backwardAStarWithBunsetsu
    // -----------------------------
    std::pair<std::vector<Candidate>, std::vector<int>> FindPath::backwardAStarWithBunsetsu(
        Graph &graph,
        int length,
        const ConnectionMatrix &conn,
        int nBest,
        int beamWidth)
    {
        if (nBest <= 0)
            return {{}, {}};

        // 1) forward DP (fills node.f)
        forwardDp(graph, length, conn, beamWidth);

        // EOS node
        if (static_cast<size_t>(length + 1) >= graph.size() || graph[static_cast<size_t>(length + 1)].empty())
            return {{}, {}};

        const Node *eos = &graph[static_cast<size_t>(length + 1)][0];

        std::priority_queue<std::shared_ptr<State>, std::vector<std::shared_ptr<State>>, StateLess> pq;
        pq.push(std::make_shared<State>(eos, /*g=*/0, /*total=*/0, /*next=*/nullptr));

        std::vector<Candidate> results;
        results.reserve(static_cast<size_t>(nBest));

        std::vector<int> bestBunsetsuPositions;

        std::unordered_set<std::u16string, U16Hash> seen;
        seen.reserve(static_cast<size_t>(nBest) * 4);

        while (!pq.empty())
        {
            auto cur = pq.top();
            pq.pop();

            const Node *curNode = cur->node;

            if (curNode->tango == u"BOS")
            {
                const std::u16string s = buildStringFromBosState(cur);
                const std::u16string y = buildYomiFromBosState(cur);

                if (seen.insert(s).second)
                {
                    if (results.empty())
                        bestBunsetsuPositions = getBunsetsuPositionsFromPath(cur);

                    Candidate c;
                    c.string = s;
                    c.yomi = y;
                    c.type = isAllFullWidthNumericSymbol(s) ? 30 : (isAllHalfWidthNumericSymbol(s) ? 31 : 1);

                    const int lenClamped = (length < 0) ? 0 : (length > 255 ? 255 : length);
                    c.length = static_cast<std::uint8_t>(lenClamped);

                    // Kotlin: score = node.second (+2000 if any digit)
                    int sc = cur->total;
                    if (anyDigit(s))
                        sc += 2000;
                    c.score = sc;

                    // Kotlin: leftId/rightId = bos.next?.l/r
                    c.hasLR = false;
                    c.leftId = 0;
                    c.rightId = 0;
                    if (cur->next && cur->next->node && cur->next->node->tango != u"EOS")
                    {
                        c.hasLR = true;
                        c.leftId = cur->next->node->l;
                        c.rightId = cur->next->node->r;
                    }

                    results.push_back(std::move(c));
                    if (static_cast<int>(results.size()) >= nBest)
                        return {results, bestBunsetsuPositions};
                }

                continue;
            }

            // expand to previous nodes (nodes ending at curNode->sPos)
            const auto prevs = getPrevNodesBackward(graph, *curNode, length);
            for (const Node *p : prevs)
            {
                const int edge = conn.get(static_cast<int>(p->l), static_cast<int>(curNode->r));
                const int newG = cur->g + edge + curNode->score;
                const int newTotal = newG + p->f;

                auto st = std::make_shared<State>(p, newG, newTotal, cur);
                pq.push(std::move(st));
            }
        }

        // If we exhausted, return what we got (sorted by score like Kotlin's final line)
        std::sort(results.begin(), results.end(), [](const Candidate &a, const Candidate &b)
                  { return a.score < b.score; });
        return {results, bestBunsetsuPositions};
    }

} // namespace kk
