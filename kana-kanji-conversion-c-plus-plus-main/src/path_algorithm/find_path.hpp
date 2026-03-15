// src/path_algorithm/find_path.hpp
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "graph_builder/graph.hpp"

namespace kk
{

    struct Candidate
    {
        std::u16string string;
        std::u16string yomi; // reading (hiragana) for this candidate
        std::uint8_t type;   // 1: normal, 30: fullwidth numeric/symbol, 31: halfwidth numeric/symbol
        std::uint8_t length; // input length (clamped to 0..255)
        int score;           // total cost
        bool hasLR;
        int16_t leftId;
        int16_t rightId;
    };

    class ConnectionMatrix
    {
    public:
        ConnectionMatrix() : dim_(0) {}
        explicit ConnectionMatrix(std::vector<int16_t> v);

        int dim() const { return dim_; }
        size_t size() const { return data_.size(); }

        int get(int leftId, int rightId) const;

    private:
        int dim_;
        std::vector<int16_t> data_;
    };

    class FindPath
    {
    public:
        // Returns: (candidates, bestBunsetsuPositions)
        // bestBunsetsuPositions is computed from the 1-best candidate only (same as Kotlin).
        static std::pair<std::vector<Candidate>, std::vector<int>> backwardAStarWithBunsetsu(
            Graph &graph,
            int length,
            const ConnectionMatrix &conn,
            int nBest,
            int beamWidth = 20);

    private:
        static void forwardDp(Graph &graph, int length, const ConnectionMatrix &conn, int beamWidth);

        static bool isIndependentWord(int16_t id);
        static std::vector<int> getBunsetsuPositionsFromPath(const std::shared_ptr<struct State> &bosState);

        static bool isAllHalfWidthNumericSymbol(const std::u16string &s);
        static bool isAllFullWidthNumericSymbol(const std::u16string &s);
        static bool anyDigit(const std::u16string &s);
    };

} // namespace kk
