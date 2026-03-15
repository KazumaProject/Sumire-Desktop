// cli/kana_kanji/astar_bunsetsu_parallel_cli.cpp
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <future>
#include <limits>
#include <type_traits>
#include <concepts>

#include "connection_id/connection_id_builder.hpp"
#include "graph_builder/graph.hpp"
#include "louds/louds_utf16_reader.hpp"
#include "louds_with_term_id/louds_with_term_id_reader_utf16.hpp"
#include "louds_with_term_id/louds_with_term_id_utf16.hpp"
#include "path_algorithm/find_path.hpp"
#include "token_array/token_array.hpp"

// -----------------------------
// UTF-8 -> UTF-16 (strict)
// -----------------------------
static bool utf8_next_codepoint(std::string_view s, size_t &i, char32_t &out_cp)
{
    if (i >= s.size())
        return false;
    const unsigned char c0 = static_cast<unsigned char>(s[i]);

    if (c0 < 0x80)
    {
        out_cp = c0;
        ++i;
        return true;
    }

    if ((c0 & 0xE0) == 0xC0)
    {
        if (i + 1 >= s.size())
            return false;
        const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
        if ((c1 & 0xC0) != 0x80)
            return false;
        char32_t cp = (c0 & 0x1F);
        cp = (cp << 6) | (c1 & 0x3F);
        if (cp < 0x80)
            return false;
        out_cp = cp;
        i += 2;
        return true;
    }

    if ((c0 & 0xF0) == 0xE0)
    {
        if (i + 2 >= s.size())
            return false;
        const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
        const unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80)
            return false;
        char32_t cp = (c0 & 0x0F);
        cp = (cp << 6) | (c1 & 0x3F);
        cp = (cp << 6) | (c2 & 0x3F);
        if (cp < 0x800)
            return false;
        if (cp >= 0xD800 && cp <= 0xDFFF)
            return false;
        out_cp = cp;
        i += 3;
        return true;
    }

    if ((c0 & 0xF8) == 0xF0)
    {
        if (i + 3 >= s.size())
            return false;
        const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
        const unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
        const unsigned char c3 = static_cast<unsigned char>(s[i + 3]);
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80)
            return false;
        char32_t cp = (c0 & 0x07);
        cp = (cp << 6) | (c1 & 0x3F);
        cp = (cp << 6) | (c2 & 0x3F);
        cp = (cp << 6) | (c3 & 0x3F);
        if (cp < 0x10000)
            return false;
        if (cp > 0x10FFFF)
            return false;
        out_cp = cp;
        i += 4;
        return true;
    }

    return false;
}

static bool utf8_to_u16(std::string_view s, std::u16string &out)
{
    out.clear();
    out.reserve(s.size());

    size_t i = 0;
    while (i < s.size())
    {
        char32_t cp = 0;
        if (!utf8_next_codepoint(s, i, cp))
            return false;

        if (cp <= 0xFFFF)
        {
            if (cp >= 0xD800 && cp <= 0xDFFF)
                return false;
            out.push_back(static_cast<char16_t>(cp));
        }
        else
        {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + ((cp >> 10) & 0x3FF)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        }
    }
    return true;
}

// -----------------------------
// UTF-16 -> UTF-8 (printing)
// -----------------------------
static void append_utf8(std::string &out, char32_t cp)
{
    if (cp <= 0x7F)
        out.push_back(static_cast<char>(cp));
    else if (cp <= 0x7FF)
    {
        out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else if (cp <= 0xFFFF)
    {
        out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else
    {
        out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

static bool u16_to_utf8(const std::u16string &s, std::string &out)
{
    out.clear();
    out.reserve(s.size());

    for (size_t i = 0; i < s.size();)
    {
        char32_t cp = 0;
        char16_t c = s[i++];

        if (c >= 0xD800 && c <= 0xDBFF)
        {
            if (i >= s.size())
                return false;
            char16_t d = s[i++];
            if (!(d >= 0xDC00 && d <= 0xDFFF))
                return false;
            cp = 0x10000 + (((static_cast<char32_t>(c) - 0xD800) << 10) |
                            (static_cast<char32_t>(d) - 0xDC00));
        }
        else if (c >= 0xDC00 && c <= 0xDFFF)
        {
            return false;
        }
        else
        {
            cp = static_cast<char32_t>(c);
        }

        append_utf8(out, cp);
    }

    return true;
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

static bool starts_with_u16(const std::u16string &s, const std::u16string &prefix)
{
    if (prefix.size() > s.size())
        return false;
    return s.compare(0, prefix.size(), prefix) == 0;
}

// -----------------------------
// Modes
// -----------------------------
static kk::YomiSearchMode parse_yomi_mode(const std::string &s)
{
    if (s == "cps")
        return kk::YomiSearchMode::CommonPrefixOnly;
    if (s == "cps_pred")
        return kk::YomiSearchMode::CommonPrefixPlusPredictive;
    if (s == "cps_omit")
        return kk::YomiSearchMode::CommonPrefixPlusOmission;
    if (s == "all")
        return kk::YomiSearchMode::All;
    throw std::runtime_error("Unknown --yomi_mode: " + s + " (expected: cps|cps_pred|cps_omit|all)");
}

static kk::YomiSearchMode enable_omit_for_graph(kk::YomiSearchMode base)
{
    // --show_omit 時に「追加で」回す graph の mode を決める
    switch (base)
    {
    case kk::YomiSearchMode::CommonPrefixOnly:
        return kk::YomiSearchMode::CommonPrefixPlusOmission;
    case kk::YomiSearchMode::CommonPrefixPlusPredictive:
        // pred も活かしたいなら All が自然
        return kk::YomiSearchMode::All;
    case kk::YomiSearchMode::CommonPrefixPlusOmission:
        return kk::YomiSearchMode::CommonPrefixPlusOmission;
    case kk::YomiSearchMode::All:
        return kk::YomiSearchMode::All;
    default:
        return kk::YomiSearchMode::CommonPrefixPlusOmission;
    }
}

static void usage(const char *argv0)
{
    std::cout
        << "Usage:\n"
        << "  " << argv0
        << " --yomi_termid <yomi_termid.louds> --tango <tango.louds> --tokens <token_array.bin>\n"
        << "      --pos_table <pos_table.bin> --conn <connection_single_column.bin>\n"
        << "      --q <utf8> [--n N] [--beam W] [--show_bunsetsu]\n"
        << "      [--yomi_mode cps|cps_pred|cps_omit|all]\n"
        << "      [--pred_k K] [--show_pred] [--show_omit]\n"
        << "      [--pred_len_penalty P]\n"
        << "      [--yomi_n N] [--final_n N] [--no_dedup] [--no_global_dedup]\n"
        << "  " << argv0
        << " --yomi_termid <yomi_termid.louds> --tango <tango.louds> --tokens <token_array.bin>\n"
        << "      --pos_table <pos_table.bin> --conn <connection_single_column.bin>\n"
        << "      --stdin [--n N] [--beam W] [--show_bunsetsu]\n"
        << "      [--yomi_mode cps|cps_pred|cps_omit|all]\n"
        << "      [--pred_k K] [--show_pred] [--show_omit]\n"
        << "      [--pred_len_penalty P]\n"
        << "      [--yomi_n N] [--final_n N] [--no_dedup] [--no_global_dedup]\n"
        << "\n"
        << "Notes:\n"
        << "  - commonPrefixSearch candidates are ALWAYS included.\n"
        << "  - --show_pred: also include predictiveSearch-derived candidates.\n"
        << "  - --show_omit: also include omissionSearch-derived candidates.\n"
        << "  - Graph/A* is executed in parallel with cps/pred/omit.\n"
        << "  - When --show_omit is enabled, an additional graph/A* run (src=graph_omit)\n"
        << "    is performed with omissionSearch enabled in graph construction.\n"
        << "  - Global dedup (default ON) removes duplicates across sources by surface (+LR when available).\n"
        << "    Use --no_global_dedup to disable.\n"
        << "  - If query length is 1 hiragana, limits are auto-disabled (may be huge).\n"
        << "  - --pred_len_penalty: if --show_pred, add (yomi_len - query_len)*P to wordCost for src=pred.\n";
}

// -----------------------------
// Hash for u16string
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
// Final dedup key for printing (surface + yomi, ignoring src and L/R)
// -----------------------------
struct FinalDedupKey
{
    std::u16string surface;
    std::u16string yomi;

    bool operator==(const FinalDedupKey &o) const noexcept
    {
        return surface == o.surface && yomi == o.yomi;
    }
};

struct FinalDedupKeyHash
{
    size_t operator()(const FinalDedupKey &k) const noexcept
    {
        U16Hash h16;
        size_t h = h16(k.surface);
        h ^= (h16(k.yomi) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
        return h;
    }
};

struct CandidateRow
{
    std::u16string surface;
    std::u16string yomi; // empty for some graph candidates if unknown
    int score = 0;
    int type = 0; // graph candidate type or 0 for yomi-derived
    bool hasLR = false;
    int16_t l = 0;
    int16_t r = 0;
    std::string source; // "graph" | "graph_omit" | "cps" | "pred" | "omit"
};

// Dedup key (surface + yomi + source + L/R)  ※yomi展開側で使う（既存）
struct DedupKey
{
    std::u16string surface;
    std::u16string yomi;
    std::string source;
    int16_t l = 0;
    int16_t r = 0;

    bool operator==(const DedupKey &o) const noexcept
    {
        return surface == o.surface && yomi == o.yomi && source == o.source && l == o.l && r == o.r;
    }
};

struct DedupKeyHash
{
    size_t operator()(const DedupKey &k) const noexcept
    {
        U16Hash h16;
        size_t h = h16(k.surface);
        h ^= (h16(k.yomi) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
        std::hash<std::string> hs;
        h ^= (hs(k.source) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
        h ^= (static_cast<size_t>(static_cast<uint16_t>(k.l)) << 16) ^ static_cast<size_t>(static_cast<uint16_t>(k.r));
        return h;
    }
};

// -----------------------------
// Global (cross-source) dedup by surface (+LR when available)
// -----------------------------
static int source_preference_rank_for_dedup(const std::string &src)
{
    // 同scoreの場合にどれを残すか（小さいほど優先）
    if (src == "graph")
        return 0;
    if (src == "graph_omit")
        return 1;
    if (src == "cps")
        return 2;
    if (src == "pred")
        return 3;
    if (src == "omit")
        return 4;
    return 9;
}

struct GlobalDedupKey
{
    std::u16string surface;
    bool hasLR = false;
    int16_t l = 0;
    int16_t r = 0;

    bool operator==(const GlobalDedupKey &o) const noexcept
    {
        return surface == o.surface && hasLR == o.hasLR && l == o.l && r == o.r;
    }
};

struct GlobalDedupKeyHash
{
    size_t operator()(const GlobalDedupKey &k) const noexcept
    {
        U16Hash h16;
        size_t h = h16(k.surface);
        h ^= (static_cast<size_t>(k.hasLR ? 1 : 0) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
        // LRはhasLRのときだけ意味があるが、キーとしてはそのまま混ぜる
        h ^= (static_cast<size_t>(static_cast<uint16_t>(k.l)) << 16) ^ static_cast<size_t>(static_cast<uint16_t>(k.r));
        return h;
    }
};

static std::vector<CandidateRow> global_dedup_best(std::vector<CandidateRow> rows)
{
    std::unordered_map<GlobalDedupKey, CandidateRow, GlobalDedupKeyHash> best;
    best.reserve(rows.size() * 2);

    for (auto &row : rows)
    {
        GlobalDedupKey key;
        key.surface = row.surface;
        key.hasLR = row.hasLR;
        key.l = row.l;
        key.r = row.r;

        auto it = best.find(key);
        if (it == best.end())
        {
            best.emplace(std::move(key), std::move(row));
            continue;
        }

        CandidateRow &cur = it->second;

        // (1) scoreが小さい方を残す
        if (row.score < cur.score)
        {
            cur = std::move(row);
            continue;
        }
        if (row.score > cur.score)
            continue;

        // (2) 同点なら source 優先順位
        const int rNew = source_preference_rank_for_dedup(row.source);
        const int rCur = source_preference_rank_for_dedup(cur.source);
        if (rNew < rCur)
        {
            cur = std::move(row);
            continue;
        }
        if (rNew > rCur)
            continue;

        // (3) さらに同点なら、yomiが埋まっている方を優先（デバッグ時に情報が残る）
        const bool newHasYomi = !row.yomi.empty();
        const bool curHasYomi = !cur.yomi.empty();
        if (newHasYomi && !curHasYomi)
        {
            cur = std::move(row);
            continue;
        }

        // (4) 最後の安定化：typeが小さい方（任意）
        if (row.type < cur.type)
        {
            cur = std::move(row);
            continue;
        }
    }

    std::vector<CandidateRow> out;
    out.reserve(best.size());
    for (auto &kv : best)
        out.push_back(std::move(kv.second));
    return out;
}

// -----------------------------
// Expand yomi list -> surface candidates via termId/tokens/tango
// -----------------------------
static std::vector<CandidateRow> expand_yomi_candidates(
    const std::string &sourceName,
    const std::vector<std::u16string> &yomis,
    const LOUDSWithTermIdReaderUtf16 &yomiTerm,
    const TokenArray &tokens,
    const kk::PosTable &pos,
    const LOUDSReaderUtf16 &tango,
    bool dedup,
    bool queryIsSingleChar,
    size_t yomiLimit,     // max yomis to process; SIZE_MAX for no limit
    size_t queryLen,      // q16.size()
    int predLenPenalty    // per extra char penalty for src=pred
)
{
    std::vector<CandidateRow> out;
    if (yomis.empty())
        return out;

    const size_t lim = std::min(yomiLimit, yomis.size());

    auto calc_penalty = [&](const std::u16string &yomi) -> int
    {
        if (sourceName != "pred")
            return 0;
        if (predLenPenalty <= 0)
            return 0;
        if (yomi.size() <= queryLen)
            return 0;
        const size_t extra = yomi.size() - queryLen;
        // int overflow guard (practically small, but still)
        const uint64_t p = static_cast<uint64_t>(predLenPenalty);
        const uint64_t e = static_cast<uint64_t>(extra);
        const uint64_t pen = p * e;
        if (pen > static_cast<uint64_t>(std::numeric_limits<int>::max()))
            return std::numeric_limits<int>::max();
        return static_cast<int>(pen);
    };

    if (!dedup)
    {
        for (size_t i = 0; i < lim; ++i)
        {
            const auto &yomi = yomis[i];
            const int32_t termId = yomiTerm.getTermId(yomi);
            if (termId < 0)
                continue;

            const int penalty = calc_penalty(yomi);

            const auto listToken = tokens.getTokensForTermId(termId);
            for (const auto &t : listToken)
            {
                CandidateRow row;
                row.source = sourceName;
                row.yomi = yomi;
                row.score = static_cast<int>(t.wordCost) + penalty;
                row.type = 0;

                std::u16string surface;
                if (t.nodeIndex == TokenArray::HIRAGANA_SENTINEL)
                    surface = yomi;
                else if (t.nodeIndex == TokenArray::KATAKANA_SENTINEL)
                    surface = hira_to_kata(yomi);
                else
                    surface = tango.getLetter(t.nodeIndex);

                row.surface = std::move(surface);

                const auto [l, r] = pos.getLR(t.posIndex);
                row.hasLR = true;
                row.l = l;
                row.r = r;

                out.push_back(std::move(row));
            }
        }
        (void)queryIsSingleChar;
        return out;
    }

    std::unordered_map<DedupKey, CandidateRow, DedupKeyHash> best;
    best.reserve(4096);

    for (size_t i = 0; i < lim; ++i)
    {
        const auto &yomi = yomis[i];
        const int32_t termId = yomiTerm.getTermId(yomi);
        if (termId < 0)
            continue;

        const int penalty = calc_penalty(yomi);

        const auto listToken = tokens.getTokensForTermId(termId);
        for (const auto &t : listToken)
        {
            std::u16string surface;
            if (t.nodeIndex == TokenArray::HIRAGANA_SENTINEL)
                surface = yomi;
            else if (t.nodeIndex == TokenArray::KATAKANA_SENTINEL)
                surface = hira_to_kata(yomi);
            else
                surface = tango.getLetter(t.nodeIndex);

            const auto [l, r] = pos.getLR(t.posIndex);

            DedupKey key;
            key.surface = surface;
            key.yomi = yomi;
            key.source = sourceName;
            key.l = l;
            key.r = r;

            CandidateRow row;
            row.source = sourceName;
            row.surface = std::move(surface);
            row.yomi = yomi;
            row.score = static_cast<int>(t.wordCost) + penalty;
            row.type = 0;
            row.hasLR = true;
            row.l = l;
            row.r = r;

            auto it = best.find(key);
            if (it == best.end() || row.score < it->second.score)
                best[std::move(key)] = std::move(row);

            (void)queryIsSingleChar;
        }
    }

    out.reserve(best.size());
    for (auto &kv : best)
        out.push_back(std::move(kv.second));
    return out;
}

// -----------------------------
// Build yomi lists for each search type
// -----------------------------
static std::vector<std::u16string> get_cps_yomis(const LOUDSReaderUtf16 &yomiCps, const std::u16string &q16)
{
    return yomiCps.commonPrefixSearch(q16);
}

// --- OmissionSearchResult -> u16string extractor (C++20) ---
template <class T>
static std::u16string extract_u16_yomi(const T &r)
{
    if constexpr (std::is_same_v<T, std::u16string>)
    {
        return r;
    }
    else if constexpr (requires { { r.yomi } -> std::convertible_to<std::u16string>; })
    {
        return static_cast<std::u16string>(r.yomi);
    }
    else if constexpr (requires { { r.text } -> std::convertible_to<std::u16string>; })
    {
        return static_cast<std::u16string>(r.text);
    }
    else if constexpr (requires { { r.prefix } -> std::convertible_to<std::u16string>; })
    {
        return static_cast<std::u16string>(r.prefix);
    }
    else if constexpr (requires { { r.key } -> std::convertible_to<std::u16string>; })
    {
        return static_cast<std::u16string>(r.key);
    }
    else if constexpr (requires { { r.surface } -> std::convertible_to<std::u16string>; })
    {
        return static_cast<std::u16string>(r.surface);
    }
    else
    {
        static_assert(!sizeof(T),
                      "OmissionSearchResult does not expose a u16string field "
                      "(expected one of: yomi/text/prefix/key/surface). "
                      "Update extract_u16_yomi() to match your struct.");
        return {};
    }
}

static std::vector<std::u16string> get_omit_yomis(const LOUDSReaderUtf16 &yomiCps, const std::u16string &q16)
{
    const auto results = yomiCps.commonPrefixSearchWithOmission(q16);

    std::vector<std::u16string> out;
    out.reserve(results.size());
    for (const auto &r : results)
        out.push_back(extract_u16_yomi(r));

    return out;
}

static std::vector<std::u16string> get_pred_yomis(const LOUDSReaderUtf16 &yomiCps, const std::u16string &q16, int predK)
{
    if (q16.empty())
        return {};

    if (predK < 1)
        predK = 1;
    const size_t k = std::min(static_cast<size_t>(predK), q16.size());
    const std::u16string prefix = q16.substr(0, k);

    const auto preds = yomiCps.predictiveSearch(prefix);

    std::vector<std::u16string> out;
    out.reserve(preds.size());
    for (const auto &y : preds)
    {
        if (starts_with_u16(y, q16))
            out.push_back(y);
    }
    return out;
}

// -----------------------------
// Graph + A* (N-best + bunsetsu positions)
// -----------------------------
struct GraphResult
{
    std::vector<CandidateRow> rows;
    std::vector<int> bunsetsu;
};

static GraphResult run_graph_astar(
    const std::string &sourceName,
    const LOUDSReaderUtf16 &yomiCps,
    const LOUDSWithTermIdReaderUtf16 &yomiTerm,
    const TokenArray &tokens,
    const kk::PosTable &pos,
    const LOUDSReaderUtf16 &tango,
    const kk::ConnectionMatrix &conn,
    const std::u16string &q16,
    int nBest,
    int beamWidth,
    kk::YomiSearchMode yomiMode,
    int predK)
{
    GraphResult gr;

    kk::Graph graph = kk::GraphBuilder::constructGraph(q16, yomiCps, yomiTerm, tokens, pos, tango, yomiMode, predK);

    auto [cands, bunsetsu] = kk::FindPath::backwardAStarWithBunsetsu(
        graph,
        static_cast<int>(q16.size()),
        conn,
        nBest,
        beamWidth);

    gr.bunsetsu = std::move(bunsetsu);

    gr.rows.reserve(cands.size());
    for (const auto &c : cands)
    {
        CandidateRow row;
        row.source = sourceName;
        row.surface = c.string;
        row.yomi = c.yomi;
        row.score = c.score;
        row.type = static_cast<int>(c.type);
        row.hasLR = c.hasLR;
        if (c.hasLR)
        {
            row.l = c.leftId;
            row.r = c.rightId;
        }
        gr.rows.push_back(std::move(row));
    }
    return gr;
}

// -----------------------------
// Sorting & printing
// -----------------------------
static bool is_graph_src(const std::string &src)
{
    return (src == "graph" || src == "graph_omit");
}

static int graph_src_tiebreak_rank(const std::string &src)
{
    // graph と graph_omit が同スコア時だけ使う
    if (src == "graph")
        return 0;
    if (src == "graph_omit")
        return 1;
    return 9;
}

static int other_src_rank(const std::string &src)
{
    // graph系より下のグループ内での安定化用
    if (src == "cps")
        return 0;
    if (src == "pred")
        return 1;
    if (src == "omit")
        return 2;
    return 9;
}

static void print_rows(
    const std::string &qUtf8,
    const std::u16string &q16,
    const std::vector<int> &bunsetsu,
    bool showBunsetsu,
    std::vector<CandidateRow> rows,
    size_t finalLimit // SIZE_MAX for no limit
)
{
    std::sort(rows.begin(), rows.end(),
              [](const CandidateRow &a, const CandidateRow &b)
              {
                  const bool ag = is_graph_src(a.source);
                  const bool bg = is_graph_src(b.source);

                  // (1) graph / graph_omit は最上段グループ
                  if (ag != bg)
                      return ag > bg; // true first

                  // (2) graph group: score順（sourceで分けない）
                  if (ag && bg)
                  {
                      if (a.score != b.score)
                          return a.score < b.score;

                      // 同点時だけ安定化（graph -> graph_omit）
                      const int ra = graph_src_tiebreak_rank(a.source);
                      const int rb = graph_src_tiebreak_rank(b.source);
                      if (ra != rb)
                          return ra < rb;

                      if (a.surface != b.surface)
                          return a.surface < b.surface;
                      if (a.type != b.type)
                          return a.type < b.type;
                      if (a.yomi != b.yomi)
                          return a.yomi < b.yomi;
                      return false;
                  }

                  // (3) それ以外: yomi長さ順（長い順）→ 同長ならスコア順
                  const size_t la = a.yomi.size();
                  const size_t lb = b.yomi.size();
                  if (la != lb)
                      return la > lb; // 長い順

                  if (a.score != b.score)
                      return a.score < b.score;

                  // 以降は安定化
                  const int ra = other_src_rank(a.source);
                  const int rb = other_src_rank(b.source);
                  if (ra != rb)
                      return ra < rb;

                  if (a.surface != b.surface)
                      return a.surface < b.surface;

                  if (a.yomi != b.yomi)
                      return a.yomi < b.yomi;

                  return a.type < b.type;
              });

    // -----------------------------
    // Final dedup AFTER sorting:
    //   - remove duplicates like "わたし" appearing multiple times due to POS/LR differences
    //   - remove duplicates between graph and graph_omit when surface+yomi identical
    // Keep the first one (best-ranked) after sort.
    // -----------------------------
    {
        std::unordered_set<FinalDedupKey, FinalDedupKeyHash> seen;
        seen.reserve(rows.size() * 2);

        std::vector<CandidateRow> uniq;
        uniq.reserve(rows.size());

        for (auto &r : rows)
        {
            FinalDedupKey key{r.surface, r.yomi};
            if (seen.insert(key).second)
            {
                uniq.push_back(std::move(r));
            }
        }
        rows = std::move(uniq);
    }

    std::cout << "query=" << qUtf8 << " len=" << q16.size() << "\n";

    if (showBunsetsu)
    {
        std::cout << "best_bunsetsu_positions:";
        for (int p : bunsetsu)
            std::cout << " " << p;
        std::cout << "\n";
    }

    const size_t n = std::min(finalLimit, rows.size());
    std::cout << "candidates_sorted: " << n << "/" << rows.size() << "\n";
    for (size_t i = 0; i < n; ++i)
    {
        std::string surf8, y8;
        if (!u16_to_utf8(rows[i].surface, surf8))
            surf8 = "<BAD_U16>";
        if (!rows[i].yomi.empty())
        {
            if (!u16_to_utf8(rows[i].yomi, y8))
                y8 = "<BAD_U16>";
        }
        else
        {
            y8 = "-";
        }

        std::cout << (i + 1) << "\t" << surf8
                  << "\tyomi=" << y8
                  << "\tscore=" << rows[i].score
                  << "\tsrc=" << rows[i].source
                  << "\ttype=" << rows[i].type;

        if (rows[i].hasLR)
            std::cout << "\tL=" << rows[i].l << "\tR=" << rows[i].r;

        std::cout << "\n";
    }
}

// -----------------------------
// One query execution (parallel)
// -----------------------------
static void run_one_parallel(
    const LOUDSReaderUtf16 &yomiCps,
    const LOUDSWithTermIdReaderUtf16 &yomiTerm,
    const TokenArray &tokens,
    const kk::PosTable &pos,
    const LOUDSReaderUtf16 &tango,
    const kk::ConnectionMatrix &conn,
    const std::string &q_utf8,
    int nBest,
    int beamWidth,
    bool showBunsetsu,
    kk::YomiSearchMode yomiMode,
    int predK,
    bool showPred,
    bool showOmit,
    int predLenPenalty,
    size_t yomiLimit,
    size_t finalLimit,
    bool dedup,
    bool globalDedup)
{
    std::u16string q16;
    if (!utf8_to_u16(q_utf8, q16))
    {
        std::cout << "[BAD_UTF8] " << q_utf8 << "\n";
        return;
    }

    const bool queryIsSingleChar = (q16.size() == 1);

    if (queryIsSingleChar)
    {
        if (yomiLimit != 0)
            yomiLimit = std::numeric_limits<size_t>::max();
        if (finalLimit != 0)
            finalLimit = std::numeric_limits<size_t>::max();
    }

    // 1) parallel tasks
    auto futGraphBase = std::async(std::launch::async, [&]()
                                   { return run_graph_astar("graph", yomiCps, yomiTerm, tokens, pos, tango, conn, q16, nBest, beamWidth, yomiMode, predK); });

    // --show_omit の場合、omission を必ず有効化した graph を「追加で」回す
    std::future<GraphResult> futGraphOmit;
    bool runGraphOmit = false;
    kk::YomiSearchMode omitGraphMode = enable_omit_for_graph(yomiMode);
    if (showOmit && (omitGraphMode != yomiMode))
    {
        runGraphOmit = true;
        futGraphOmit = std::async(std::launch::async, [&]()
                                  { return run_graph_astar("graph_omit", yomiCps, yomiTerm, tokens, pos, tango, conn, q16, nBest, beamWidth, omitGraphMode, predK); });
    }

    auto futCps = std::async(std::launch::async, [&]()
                             { return get_cps_yomis(yomiCps, q16); });

    std::future<std::vector<std::u16string>> futPred;
    if (showPred)
    {
        futPred = std::async(std::launch::async, [&]()
                             { return get_pred_yomis(yomiCps, q16, predK); });
    }

    std::future<std::vector<std::u16string>> futOmit;
    if (showOmit)
    {
        futOmit = std::async(std::launch::async, [&]()
                             { return get_omit_yomis(yomiCps, q16); });
    }

    // 2) collect results
    GraphResult grBase = futGraphBase.get();
    GraphResult grOmit;
    if (runGraphOmit)
        grOmit = futGraphOmit.get();

    // bunsetsu は基本 graph のものを採用
    const auto cpsYomis = futCps.get();

    std::vector<std::u16string> predYomis;
    if (showPred)
        predYomis = futPred.get();

    std::vector<std::u16string> omitYomis;
    if (showOmit)
        omitYomis = futOmit.get();

    // 3) expand yomi candidates (also parallel)
    const size_t queryLen = q16.size();

    auto futCpsRows = std::async(std::launch::async, [&]()
                                 { return expand_yomi_candidates("cps", cpsYomis, yomiTerm, tokens, pos, tango, dedup, queryIsSingleChar, yomiLimit, queryLen, predLenPenalty); });

    std::future<std::vector<CandidateRow>> futPredRows;
    if (showPred)
    {
        futPredRows = std::async(std::launch::async, [&]()
                                 { return expand_yomi_candidates("pred", predYomis, yomiTerm, tokens, pos, tango, dedup, queryIsSingleChar, yomiLimit, queryLen, predLenPenalty); });
    }

    std::future<std::vector<CandidateRow>> futOmitRows;
    if (showOmit)
    {
        futOmitRows = std::async(std::launch::async, [&]()
                                 { return expand_yomi_candidates("omit", omitYomis, yomiTerm, tokens, pos, tango, dedup, queryIsSingleChar, yomiLimit, queryLen, predLenPenalty); });
    }

    auto cpsRows = futCpsRows.get();
    std::vector<CandidateRow> predRows;
    if (showPred)
        predRows = futPredRows.get();

    std::vector<CandidateRow> omitRows;
    if (showOmit)
        omitRows = futOmitRows.get();

    // 4) merge all rows
    std::vector<CandidateRow> all;
    all.reserve(grBase.rows.size() + grOmit.rows.size() + cpsRows.size() + predRows.size() + omitRows.size());

    // graph (base)
    for (auto &r : grBase.rows)
        all.push_back(std::move(r));

    // graph_omit (additional)
    for (auto &r : grOmit.rows)
        all.push_back(std::move(r));

    // cps always
    for (auto &r : cpsRows)
        all.push_back(std::move(r));
    // optional
    for (auto &r : predRows)
        all.push_back(std::move(r));
    for (auto &r : omitRows)
        all.push_back(std::move(r));

    // 4.5) GLOBAL dedup across sources (surface + LR)
    if (globalDedup)
        all = global_dedup_best(std::move(all));

    // 5) print
    print_rows(q_utf8, q16, grBase.bunsetsu, showBunsetsu, std::move(all),
               (finalLimit == 0 ? 0 : finalLimit));
}

int main(int argc, char **argv)
{
    try
    {
        std::string yomi_termid_path;
        std::string tango_path;
        std::string tokens_path;
        std::string pos_path;
        std::string conn_path;

        std::string q;
        bool stdin_mode = false;

        int nBest = 10;
        int beamWidth = 20;
        bool showBunsetsu = false;

        std::string yomi_mode_str = "cps";
        int predK = 1;

        bool showPred = false;
        bool showOmit = false;

        // 追加: pred候補の「入力より長い分」へのペナルティ
        int predLenPenalty = 300;

        size_t yomiLimit = 200;
        size_t finalLimit = 200;

        bool dedup = true;
        bool globalDedup = true;

        for (int i = 1; i < argc; ++i)
        {
            const std::string a = argv[i];
            if (a == "--help" || a == "-h")
            {
                usage(argv[0]);
                return 0;
            }
            if (a == "--yomi_termid" && i + 1 < argc)
            {
                yomi_termid_path = argv[++i];
                continue;
            }
            if (a == "--tango" && i + 1 < argc)
            {
                tango_path = argv[++i];
                continue;
            }
            if (a == "--tokens" && i + 1 < argc)
            {
                tokens_path = argv[++i];
                continue;
            }
            if (a == "--pos_table" && i + 1 < argc)
            {
                pos_path = argv[++i];
                continue;
            }
            if (a == "--conn" && i + 1 < argc)
            {
                conn_path = argv[++i];
                continue;
            }
            if (a == "--q" && i + 1 < argc)
            {
                q = argv[++i];
                continue;
            }
            if (a == "--stdin")
            {
                stdin_mode = true;
                continue;
            }
            if (a == "--n" && i + 1 < argc)
            {
                nBest = std::stoi(argv[++i]);
                continue;
            }
            if (a == "--beam" && i + 1 < argc)
            {
                beamWidth = std::stoi(argv[++i]);
                continue;
            }
            if (a == "--show_bunsetsu")
            {
                showBunsetsu = true;
                continue;
            }
            if (a == "--yomi_mode" && i + 1 < argc)
            {
                yomi_mode_str = argv[++i];
                continue;
            }
            if (a == "--pred_k" && i + 1 < argc)
            {
                predK = std::stoi(argv[++i]);
                continue;
            }

            if (a == "--show_pred")
            {
                showPred = true;
                continue;
            }
            if (a == "--show_omit")
            {
                showOmit = true;
                continue;
            }

            // 追加
            if (a == "--pred_len_penalty" && i + 1 < argc)
            {
                predLenPenalty = std::stoi(argv[++i]);
                continue;
            }

            if (a == "--yomi_n" && i + 1 < argc)
            {
                yomiLimit = static_cast<size_t>(std::stoul(argv[++i]));
                continue;
            }
            if (a == "--final_n" && i + 1 < argc)
            {
                finalLimit = static_cast<size_t>(std::stoul(argv[++i]));
                continue;
            }
            if (a == "--no_dedup")
            {
                dedup = false;
                continue;
            }
            if (a == "--no_global_dedup")
            {
                globalDedup = false;
                continue;
            }

            throw std::runtime_error("Unknown/incomplete arg: " + a);
        }

        if (yomi_termid_path.empty() || tango_path.empty() || tokens_path.empty() ||
            pos_path.empty() || conn_path.empty() ||
            (!stdin_mode && q.empty()))
        {
            usage(argv[0]);
            return 2;
        }

        if (predLenPenalty < 0)
            predLenPenalty = 0;

        const kk::YomiSearchMode yomiMode = parse_yomi_mode(yomi_mode_str);

        // load resources
        const auto yomiCps = LOUDSReaderUtf16::loadFromFile(yomi_termid_path);
        const auto yomiTrie = LOUDSWithTermIdUtf16::loadFromFile(yomi_termid_path);
        const LOUDSWithTermIdReaderUtf16 yomiTerm(yomiTrie);

        const auto tango = LOUDSReaderUtf16::loadFromFile(tango_path);
        const auto tokens = TokenArray::loadFromFile(tokens_path);
        const auto pos = kk::PosTable::loadFromFile(pos_path);

        const auto connVec = ConnectionIdBuilder::readShortArrayFromBytesBE(conn_path);
        const kk::ConnectionMatrix conn(std::vector<int16_t>(connVec.begin(), connVec.end()));

        if (!stdin_mode)
        {
            run_one_parallel(
                yomiCps, yomiTerm, tokens, pos, tango, conn,
                q, nBest, beamWidth, showBunsetsu,
                yomiMode, predK,
                showPred, showOmit,
                predLenPenalty,
                yomiLimit, finalLimit,
                dedup, globalDedup);
            return 0;
        }

        std::string line;
        while (std::getline(std::cin, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;

            run_one_parallel(
                yomiCps, yomiTerm, tokens, pos, tango, conn,
                line, nBest, beamWidth, showBunsetsu,
                yomiMode, predK,
                showPred, showOmit,
                predLenPenalty,
                yomiLimit, finalLimit,
                dedup, globalDedup);
        }

        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
