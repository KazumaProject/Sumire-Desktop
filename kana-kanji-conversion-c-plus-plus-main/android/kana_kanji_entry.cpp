// android/kana_kanji_entry.cpp

#include <jni.h>

#include <algorithm>
#include <cstdint>
#include <concepts>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "connection_id/connection_id_builder.hpp"
#include "graph_builder/graph.hpp"
#include "louds/louds_utf16_reader.hpp"
#include "louds_with_term_id/louds_with_term_id_reader_utf16.hpp"
#include "louds_with_term_id/louds_with_term_id_utf16.hpp"
#include "path_algorithm/find_path.hpp"
#include "token_array/token_array.hpp"

// ----------------------------------------
// Minimal UTF-8 <-> UTF-16 helpers
// ----------------------------------------
static bool utf8_next_codepoint(std::string_view s, size_t &i, char32_t &out_cp)
{
    if (i >= s.size()) return false;
    const unsigned char c0 = static_cast<unsigned char>(s[i]);

    if (c0 < 0x80) { out_cp = c0; ++i; return true; }

    if ((c0 & 0xE0) == 0xC0)
    {
        if (i + 1 >= s.size()) return false;
        const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
        if ((c1 & 0xC0) != 0x80) return false;
        char32_t cp = (c0 & 0x1F);
        cp = (cp << 6) | (c1 & 0x3F);
        if (cp < 0x80) return false;
        out_cp = cp;
        i += 2;
        return true;
    }

    if ((c0 & 0xF0) == 0xE0)
    {
        if (i + 2 >= s.size()) return false;
        const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
        const unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) return false;
        char32_t cp = (c0 & 0x0F);
        cp = (cp << 6) | (c1 & 0x3F);
        cp = (cp << 6) | (c2 & 0x3F);
        if (cp < 0x800) return false;
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;
        out_cp = cp;
        i += 3;
        return true;
    }

    if ((c0 & 0xF8) == 0xF0)
    {
        if (i + 3 >= s.size()) return false;
        const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
        const unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
        const unsigned char c3 = static_cast<unsigned char>(s[i + 3]);
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) return false;
        char32_t cp = (c0 & 0x07);
        cp = (cp << 6) | (c1 & 0x3F);
        cp = (cp << 6) | (c2 & 0x3F);
        cp = (cp << 6) | (c3 & 0x3F);
        if (cp < 0x10000) return false;
        if (cp > 0x10FFFF) return false;
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
        if (!utf8_next_codepoint(s, i, cp)) return false;

        if (cp <= 0xFFFF)
        {
            if (cp >= 0xD800 && cp <= 0xDFFF) return false;
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

static void append_utf8(std::string &out, char32_t cp)
{
    if (cp <= 0x7F) out.push_back(static_cast<char>(cp));
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
            if (i >= s.size()) return false;
            char16_t d = s[i++];
            if (!(d >= 0xDC00 && d <= 0xDFFF)) return false;
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

// ----------------------------------------
// Native engine: initialize once, query many times
// ----------------------------------------
namespace {

struct NativeOptions {
    int nBest = 10;
    int beamWidth = 20;
    bool showBunsetsu = false;

    // yomi_mode: 0=cps,1=cps_pred,2=cps_omit,3=all
    int yomiMode = 0;
    int predK = 1;

    bool showPred = false;
    bool showOmit = false;
    int predLenPenalty = 200;

    size_t yomiLimit = 200;
    size_t finalLimit = 200;

    bool dedup = true;
    bool globalDedup = true;
};

static kk::YomiSearchMode yomi_mode_from_int(int v)
{
    switch (v) {
    case 0: return kk::YomiSearchMode::CommonPrefixOnly;
    case 1: return kk::YomiSearchMode::CommonPrefixPlusPredictive;
    case 2: return kk::YomiSearchMode::CommonPrefixPlusOmission;
    case 3: return kk::YomiSearchMode::All;
    default: return kk::YomiSearchMode::CommonPrefixOnly;
    }
}

static kk::YomiSearchMode enable_omit_for_graph(kk::YomiSearchMode base)
{
    switch (base)
    {
    case kk::YomiSearchMode::CommonPrefixOnly:
        return kk::YomiSearchMode::CommonPrefixPlusOmission;
    case kk::YomiSearchMode::CommonPrefixPlusPredictive:
        return kk::YomiSearchMode::All;
    case kk::YomiSearchMode::CommonPrefixPlusOmission:
        return kk::YomiSearchMode::CommonPrefixPlusOmission;
    case kk::YomiSearchMode::All:
        return kk::YomiSearchMode::All;
    default:
        return kk::YomiSearchMode::CommonPrefixPlusOmission;
    }
}

struct U16Hash
{
    size_t operator()(const std::u16string &s) const noexcept
    {
        uint64_t h = 1469598103934665603ULL;
        for (char16_t c : s)
        {
            h ^= static_cast<uint16_t>(c);
            h *= 1099511628211ULL;
        }
        return static_cast<size_t>(h);
    }
};

struct FinalDedupKey
{
    std::u16string surface;
    std::u16string yomi;
    bool operator==(const FinalDedupKey &o) const noexcept { return surface == o.surface && yomi == o.yomi; }
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
    std::u16string yomi;
    int score = 0;
    int type = 0;
    bool hasLR = false;
    int16_t l = 0;
    int16_t r = 0;
    std::string source;
};

static bool is_graph_src(const std::string &src) { return (src == "graph" || src == "graph_omit"); }
static int graph_src_tiebreak_rank(const std::string &src)
{
    if (src == "graph") return 0;
    if (src == "graph_omit") return 1;
    return 9;
}
static int other_src_rank(const std::string &src)
{
    if (src == "cps") return 0;
    if (src == "pred") return 1;
    if (src == "omit") return 2;
    return 9;
}

static std::vector<std::u16string> get_cps_yomis(const LOUDSReaderUtf16 &yomiCps, const std::u16string &q16)
{
    return yomiCps.commonPrefixSearch(q16);
}

// extractor for omission results
template <class T>
static std::u16string extract_u16_yomi(const T &r)
{
    if constexpr (std::is_same_v<T, std::u16string>)
        return r;
    else if constexpr (requires { { r.yomi } -> std::convertible_to<std::u16string>; })
        return static_cast<std::u16string>(r.yomi);
    else if constexpr (requires { { r.text } -> std::convertible_to<std::u16string>; })
        return static_cast<std::u16string>(r.text);
    else if constexpr (requires { { r.prefix } -> std::convertible_to<std::u16string>; })
        return static_cast<std::u16string>(r.prefix);
    else if constexpr (requires { { r.key } -> std::convertible_to<std::u16string>; })
        return static_cast<std::u16string>(r.key);
    else if constexpr (requires { { r.surface } -> std::convertible_to<std::u16string>; })
        return static_cast<std::u16string>(r.surface);
    else
    {
        static_assert(!sizeof(T), "OmissionSearchResult does not expose a u16string field");
        return {};
    }
}

static std::vector<std::u16string> get_omit_yomis(const LOUDSReaderUtf16 &yomiCps, const std::u16string &q16)
{
    const auto results = yomiCps.commonPrefixSearchWithOmission(q16);
    std::vector<std::u16string> out;
    out.reserve(results.size());
    for (const auto &r : results) out.push_back(extract_u16_yomi(r));
    return out;
}

static bool starts_with_u16(const std::u16string &s, const std::u16string &prefix)
{
    if (prefix.size() > s.size()) return false;
    return s.compare(0, prefix.size(), prefix) == 0;
}

static std::vector<std::u16string> get_pred_yomis(const LOUDSReaderUtf16 &yomiCps, const std::u16string &q16, int predK)
{
    if (q16.empty()) return {};
    if (predK < 1) predK = 1;
    const size_t k = std::min(static_cast<size_t>(predK), q16.size());
    const std::u16string prefix = q16.substr(0, k);

    const auto preds = yomiCps.predictiveSearch(prefix);
    std::vector<std::u16string> out;
    out.reserve(preds.size());
    for (const auto &y : preds)
        if (starts_with_u16(y, q16)) out.push_back(y);
    return out;
}

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

static std::vector<CandidateRow> expand_yomi_candidates(
    const std::string &sourceName,
    const std::vector<std::u16string> &yomis,
    const LOUDSWithTermIdReaderUtf16 &yomiTerm,
    const TokenArray &tokens,
    const kk::PosTable &pos,
    const LOUDSReaderUtf16 &tango,
    bool dedup,
    size_t yomiLimit,
    size_t queryLen,
    int predLenPenalty)
{
    std::vector<CandidateRow> out;
    if (yomis.empty()) return out;

    const size_t lim = std::min(yomiLimit, yomis.size());

    auto calc_penalty = [&](const std::u16string &yomi) -> int {
        if (sourceName != "pred") return 0;
        if (predLenPenalty <= 0) return 0;
        if (yomi.size() <= queryLen) return 0;
        const size_t extra = yomi.size() - queryLen;
        const uint64_t pen = static_cast<uint64_t>(predLenPenalty) * static_cast<uint64_t>(extra);
        if (pen > static_cast<uint64_t>(INT32_MAX)) return INT32_MAX;
        return static_cast<int>(pen);
    };

    for (size_t i = 0; i < lim; ++i)
    {
        const auto &yomi = yomis[i];
        const int32_t termId = yomiTerm.getTermId(yomi);
        if (termId < 0) continue;

        const int penalty = calc_penalty(yomi);
        const auto listToken = tokens.getTokensForTermId(termId);
        for (const auto &t : listToken)
        {
            CandidateRow row;
            row.source = sourceName;
            row.yomi = yomi;
            row.score = static_cast<int>(t.wordCost) + penalty;

            std::u16string surface;
            if (t.nodeIndex == TokenArray::HIRAGANA_SENTINEL) surface = yomi;
            else if (t.nodeIndex == TokenArray::KATAKANA_SENTINEL) surface = hira_to_kata(yomi);
            else surface = tango.getLetter(t.nodeIndex);

            row.surface = std::move(surface);
            const auto [l, r] = pos.getLR(t.posIndex);
            row.hasLR = true;
            row.l = l;
            row.r = r;

            out.push_back(std::move(row));
        }
    }

    if (!dedup) return out;

    struct DedupKey {
        std::u16string surface;
        std::u16string yomi;
        std::string source;
        int16_t l = 0;
        int16_t r = 0;
        bool operator==(const DedupKey &o) const noexcept {
            return surface == o.surface && yomi == o.yomi && source == o.source && l == o.l && r == o.r;
        }
    };
    struct DedupKeyHash {
        size_t operator()(const DedupKey &k) const noexcept {
            U16Hash h16;
            size_t h = h16(k.surface);
            h ^= (h16(k.yomi) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
            h ^= (std::hash<std::string>{}(k.source) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
            h ^= (static_cast<size_t>(static_cast<uint16_t>(k.l)) << 16) ^ static_cast<size_t>(static_cast<uint16_t>(k.r));
            return h;
        }
    };

    std::unordered_map<DedupKey, CandidateRow, DedupKeyHash> best;
    best.reserve(out.size() * 2);

    for (auto &row : out)
    {
        DedupKey k{row.surface, row.yomi, row.source, row.l, row.r};
        auto it = best.find(k);
        if (it == best.end() || row.score < it->second.score)
            best[std::move(k)] = std::move(row);
    }

    std::vector<CandidateRow> ded;
    ded.reserve(best.size());
    for (auto &kv : best) ded.push_back(std::move(kv.second));
    return ded;
}

struct GraphResult {
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
    const NativeOptions &opt,
    kk::YomiSearchMode yomiMode)
{
    GraphResult gr;

    kk::Graph graph = kk::GraphBuilder::constructGraph(q16, yomiCps, yomiTerm, tokens, pos, tango, yomiMode, opt.predK);
    auto [cands, bunsetsu] = kk::FindPath::backwardAStarWithBunsetsu(
        graph,
        static_cast<int>(q16.size()),
        conn,
        opt.nBest,
        opt.beamWidth);

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
        if (c.hasLR) { row.l = c.leftId; row.r = c.rightId; }
        gr.rows.push_back(std::move(row));
    }

    return gr;
}

// Cross-source dedup: surface + LR (when available)
static int source_preference_rank_for_dedup(const std::string &src)
{
    if (src == "graph") return 0;
    if (src == "graph_omit") return 1;
    if (src == "cps") return 2;
    if (src == "pred") return 3;
    if (src == "omit") return 4;
    return 9;
}

struct GlobalDedupKey {
    std::u16string surface;
    bool hasLR = false;
    int16_t l = 0;
    int16_t r = 0;
    bool operator==(const GlobalDedupKey &o) const noexcept {
        return surface == o.surface && hasLR == o.hasLR && l == o.l && r == o.r;
    }
};
struct GlobalDedupKeyHash {
    size_t operator()(const GlobalDedupKey &k) const noexcept {
        U16Hash h16;
        size_t h = h16(k.surface);
        h ^= (static_cast<size_t>(k.hasLR ? 1 : 0) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
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
        GlobalDedupKey key{row.surface, row.hasLR, row.l, row.r};
        auto it = best.find(key);
        if (it == best.end()) { best.emplace(std::move(key), std::move(row)); continue; }

        CandidateRow &cur = it->second;
        if (row.score < cur.score) { cur = std::move(row); continue; }
        if (row.score > cur.score) continue;

        const int rNew = source_preference_rank_for_dedup(row.source);
        const int rCur = source_preference_rank_for_dedup(cur.source);
        if (rNew < rCur) { cur = std::move(row); continue; }
        if (rNew > rCur) continue;

        const bool newHasYomi = !row.yomi.empty();
        const bool curHasYomi = !cur.yomi.empty();
        if (newHasYomi && !curHasYomi) { cur = std::move(row); continue; }
        if (row.type < cur.type) { cur = std::move(row); continue; }
    }

    std::vector<CandidateRow> out;
    out.reserve(best.size());
    for (auto &kv : best) out.push_back(std::move(kv.second));
    return out;
}

static std::vector<CandidateRow> final_dedup_surface_yomi_keep_first(std::vector<CandidateRow> rows)
{
    std::unordered_set<FinalDedupKey, FinalDedupKeyHash> seen;
    seen.reserve(rows.size() * 2);
    std::vector<CandidateRow> out;
    out.reserve(rows.size());

    for (auto &r : rows)
    {
        FinalDedupKey k{r.surface, r.yomi};
        if (seen.insert(k).second) out.push_back(std::move(r));
    }
    return out;
}

static void sort_rows(std::vector<CandidateRow> &rows)
{
    std::sort(rows.begin(), rows.end(), [](const CandidateRow &a, const CandidateRow &b) {
        const bool ag = is_graph_src(a.source);
        const bool bg = is_graph_src(b.source);
        if (ag != bg) return ag > bg;

        if (ag && bg)
        {
            if (a.score != b.score) return a.score < b.score;
            const int ra = graph_src_tiebreak_rank(a.source);
            const int rb = graph_src_tiebreak_rank(b.source);
            if (ra != rb) return ra < rb;
            if (a.surface != b.surface) return a.surface < b.surface;
            if (a.type != b.type) return a.type < b.type;
            if (a.yomi != b.yomi) return a.yomi < b.yomi;
            return false;
        }

        const size_t la = a.yomi.size();
        const size_t lb = b.yomi.size();
        if (la != lb) return la > lb;
        if (a.score != b.score) return a.score < b.score;
        const int ra = other_src_rank(a.source);
        const int rb = other_src_rank(b.source);
        if (ra != rb) return ra < rb;
        if (a.surface != b.surface) return a.surface < b.surface;
        if (a.yomi != b.yomi) return a.yomi < b.yomi;
        return a.type < b.type;
    });
}

// --- Immutable data blob (non-assignable members must be constructed here) ---
struct EngineData {
    LOUDSReaderUtf16 yomiCps;
    LOUDSWithTermIdUtf16 yomiTrie;
    LOUDSReaderUtf16 tango;
    TokenArray tokens;
    kk::PosTable pos;
    kk::ConnectionMatrix conn;

    EngineData(const std::string &yomi_termid_path,
               const std::string &tango_path,
               const std::string &tokens_path,
               const std::string &pos_path,
               const std::string &conn_path)
        : yomiCps(LOUDSReaderUtf16::loadFromFile(yomi_termid_path))
        , yomiTrie(LOUDSWithTermIdUtf16::loadFromFile(yomi_termid_path))
        , tango(LOUDSReaderUtf16::loadFromFile(tango_path))
        , tokens(TokenArray::loadFromFile(tokens_path))
        , pos(kk::PosTable::loadFromFile(pos_path))
        , conn([](const std::string &p) {
            const auto connVec = ConnectionIdBuilder::readShortArrayFromBytesBE(p);
            return kk::ConnectionMatrix(std::vector<int16_t>(connVec.begin(), connVec.end()));
        }(conn_path))
    {}
};

class Engine {
public:
    void init(const std::string &yomi_termid_path,
              const std::string &tango_path,
              const std::string &tokens_path,
              const std::string &pos_path,
              const std::string &conn_path)
    {
        auto newData = std::make_shared<EngineData>(
            yomi_termid_path, tango_path, tokens_path, pos_path, conn_path);

        std::lock_guard<std::mutex> lock(mu_);
        data_ = std::move(newData);
    }

    std::vector<CandidateRow> convert_rows(const std::string &q_utf8, NativeOptions opt)
    {
        std::shared_ptr<const EngineData> data;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (!data_) throw std::runtime_error("Engine not initialized");
            data = data_;
        }

        LOUDSWithTermIdReaderUtf16 yomiTerm(data->yomiTrie);

        std::u16string q16;
        if (!utf8_to_u16(q_utf8, q16)) throw std::runtime_error("Bad UTF-8 in query");

        const bool queryIsSingleChar = (q16.size() == 1);
        if (queryIsSingleChar)
        {
            opt.yomiLimit = SIZE_MAX;
            opt.finalLimit = SIZE_MAX;
        }

        const kk::YomiSearchMode baseMode = yomi_mode_from_int(opt.yomiMode);

        GraphResult grBase = run_graph_astar(
            "graph",
            data->yomiCps, yomiTerm, data->tokens, data->pos, data->tango, data->conn,
            q16, opt, baseMode);

        GraphResult grOmit;
        const kk::YomiSearchMode omitMode = enable_omit_for_graph(baseMode);
        const bool runGraphOmit = opt.showOmit && (omitMode != baseMode);
        if (runGraphOmit)
        {
            grOmit = run_graph_astar(
                "graph_omit",
                data->yomiCps, yomiTerm, data->tokens, data->pos, data->tango, data->conn,
                q16, opt, omitMode);
        }

        const auto cpsYomis = get_cps_yomis(data->yomiCps, q16);
        std::vector<std::u16string> predYomis;
        std::vector<std::u16string> omitYomis;
        if (opt.showPred) predYomis = get_pred_yomis(data->yomiCps, q16, opt.predK);
        if (opt.showOmit) omitYomis = get_omit_yomis(data->yomiCps, q16);

        const size_t queryLen = q16.size();
        auto cpsRows = expand_yomi_candidates(
            "cps", cpsYomis, yomiTerm, data->tokens, data->pos, data->tango,
            opt.dedup, opt.yomiLimit, queryLen, opt.predLenPenalty);

        std::vector<CandidateRow> predRows;
        std::vector<CandidateRow> omitRows;
        if (opt.showPred)
        {
            predRows = expand_yomi_candidates(
                "pred", predYomis, yomiTerm, data->tokens, data->pos, data->tango,
                opt.dedup, opt.yomiLimit, queryLen, opt.predLenPenalty);
        }
        if (opt.showOmit)
        {
            omitRows = expand_yomi_candidates(
                "omit", omitYomis, yomiTerm, data->tokens, data->pos, data->tango,
                opt.dedup, opt.yomiLimit, queryLen, opt.predLenPenalty);
        }

        std::vector<CandidateRow> all;
        all.reserve(grBase.rows.size() + grOmit.rows.size() + cpsRows.size() + predRows.size() + omitRows.size());

        for (auto &r : grBase.rows) all.push_back(std::move(r));
        for (auto &r : grOmit.rows) all.push_back(std::move(r));
        for (auto &r : cpsRows) all.push_back(std::move(r));
        for (auto &r : predRows) all.push_back(std::move(r));
        for (auto &r : omitRows) all.push_back(std::move(r));

        if (opt.globalDedup) all = global_dedup_best(std::move(all));

        sort_rows(all);
        all = final_dedup_surface_yomi_keep_first(std::move(all));

        if (opt.finalLimit != SIZE_MAX && all.size() > opt.finalLimit) all.resize(opt.finalLimit);

        return all;
    }

private:
    mutable std::mutex mu_;
    std::shared_ptr<const EngineData> data_;
};

static Engine g_engine;

static std::string jstring_to_utf8(JNIEnv *env, jstring s)
{
    if (!s) return {};
    const char *p = env->GetStringUTFChars(s, nullptr);
    std::string out = p ? p : "";
    if (p) env->ReleaseStringUTFChars(s, p);
    return out;
}

// JNI candidate class cache
struct CandidateClassCache {
    jclass cls = nullptr;
    jmethodID ctor = nullptr;

    jfieldID surface = nullptr;
    jfieldID yomi = nullptr;
    jfieldID score = nullptr;
    jfieldID src = nullptr;
    jfieldID type = nullptr;
    jfieldID hasLR = nullptr;
    jfieldID l = nullptr;
    jfieldID r = nullptr;

    bool ready = false;
};

static std::mutex g_cls_mu;
static CandidateClassCache g_cache;

static jclass find_class_checked(JNIEnv *env, const char *name)
{
    jclass c = env->FindClass(name);
    if (!c)
    {
        if (env->ExceptionCheck()) env->ExceptionClear();
        throw std::runtime_error(std::string("FindClass failed: ") + name);
    }
    return c;
}

static jfieldID get_field_checked(JNIEnv *env, jclass cls, const char *name, const char *sig)
{
    jfieldID f = env->GetFieldID(cls, name, sig);
    if (!f)
    {
        if (env->ExceptionCheck()) env->ExceptionClear();
        throw std::runtime_error(std::string("GetFieldID failed: ") + name + " " + sig);
    }
    return f;
}

static jmethodID get_method_checked(JNIEnv *env, jclass cls, const char *name, const char *sig)
{
    jmethodID m = env->GetMethodID(cls, name, sig);
    if (!m)
    {
        if (env->ExceptionCheck()) env->ExceptionClear();
        throw std::runtime_error(std::string("GetMethodID failed: ") + name + " " + sig);
    }
    return m;
}

static void ensure_candidate_class(JNIEnv *env)
{
    std::lock_guard<std::mutex> lock(g_cls_mu);
    if (g_cache.ready) return;

    // Kotlin の実体: package com.kazumaproject.kana_kanji_converter
    // class NativeCandidate
    jclass local = find_class_checked(env, "com/kazumaproject/kana_kanji_converter/NativeCandidate");

    g_cache.cls = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
    if (!g_cache.cls)
    {
        if (env->ExceptionCheck()) env->ExceptionClear();
        throw std::runtime_error("NewGlobalRef(NativeCandidate) failed");
    }

    g_cache.ctor = get_method_checked(env, g_cache.cls, "<init>", "()V");

    g_cache.surface = get_field_checked(env, g_cache.cls, "surface", "Ljava/lang/String;");
    g_cache.yomi    = get_field_checked(env, g_cache.cls, "yomi", "Ljava/lang/String;");
    g_cache.score   = get_field_checked(env, g_cache.cls, "score", "I");
    g_cache.src     = get_field_checked(env, g_cache.cls, "src", "Ljava/lang/String;");
    g_cache.type    = get_field_checked(env, g_cache.cls, "type", "I");
    g_cache.hasLR   = get_field_checked(env, g_cache.cls, "hasLR", "Z");
    g_cache.l       = get_field_checked(env, g_cache.cls, "l", "I");
    g_cache.r       = get_field_checked(env, g_cache.cls, "r", "I");

    g_cache.ready = true;
}

static jstring new_jstring_utf8(JNIEnv *env, const std::string &s)
{
    jstring js = env->NewStringUTF(s.c_str());
    if (!js)
    {
        if (env->ExceptionCheck()) env->ExceptionClear();
        throw std::runtime_error("NewStringUTF failed");
    }
    return js;
}

} // namespace

// ----------------------------------------
// JNI API
// Kotlin 側に合わせる（重要）
//   package com.kazumaproject.kana_kanji_converter
//   object NativeKanaKanji
//
// よって JNI 名は:
//   Java_com_kazumaproject_kana_1kanji_1converter_NativeKanaKanji_nativeInit
//   Java_com_kazumaproject_kana_1kanji_1converter_NativeKanaKanji_nativeConvert
// ----------------------------------------

extern "C" JNIEXPORT void JNICALL
Java_com_kazumaproject_kana_1kanji_1converter_NativeKanaKanji_nativeInit(
    JNIEnv *env,
    jclass,
    jstring yomiTermidPath,
    jstring tangoPath,
    jstring tokensPath,
    jstring posPath,
    jstring connPath)
{
    try
    {
        g_engine.init(
            jstring_to_utf8(env, yomiTermidPath),
            jstring_to_utf8(env, tangoPath),
            jstring_to_utf8(env, tokensPath),
            jstring_to_utf8(env, posPath),
            jstring_to_utf8(env, connPath));
    }
    catch (const std::exception &e)
    {
        jclass ex = env->FindClass("java/lang/RuntimeException");
        if (ex) env->ThrowNew(ex, e.what());
    }
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_kazumaproject_kana_1kanji_1converter_NativeKanaKanji_nativeConvert(
    JNIEnv *env,
    jclass,
    jstring queryUtf8,
    jint nBest,
    jint beamWidth,
    jboolean showBunsetsu,
    jint yomiMode,
    jint predK,
    jboolean showPred,
    jboolean showOmit,
    jint predLenPenalty,
    jint yomiLimit,
    jint finalLimit,
    jboolean dedup,
    jboolean globalDedup)
{
    try
    {
        ensure_candidate_class(env);

        NativeOptions opt;
        opt.nBest = static_cast<int>(nBest);
        opt.beamWidth = static_cast<int>(beamWidth);
        opt.showBunsetsu = (showBunsetsu == JNI_TRUE);
        opt.yomiMode = static_cast<int>(yomiMode);
        opt.predK = static_cast<int>(predK);
        opt.showPred = (showPred == JNI_TRUE);
        opt.showOmit = (showOmit == JNI_TRUE);
        opt.predLenPenalty = static_cast<int>(predLenPenalty);
        opt.yomiLimit = static_cast<size_t>(yomiLimit < 0 ? 0 : yomiLimit);
        opt.finalLimit = static_cast<size_t>(finalLimit < 0 ? 0 : finalLimit);
        opt.dedup = (dedup == JNI_TRUE);
        opt.globalDedup = (globalDedup == JNI_TRUE);

        const std::string q = jstring_to_utf8(env, queryUtf8);
        std::vector<CandidateRow> rows = g_engine.convert_rows(q, opt);

        const jsize n = static_cast<jsize>(rows.size());
        jobjectArray arr = env->NewObjectArray(n, g_cache.cls, nullptr);
        if (!arr)
        {
            if (env->ExceptionCheck()) env->ExceptionClear();
            throw std::runtime_error("NewObjectArray failed");
        }

        for (jsize i = 0; i < n; ++i)
        {
            jobject obj = env->NewObject(g_cache.cls, g_cache.ctor);
            if (!obj)
            {
                if (env->ExceptionCheck()) env->ExceptionClear();
                throw std::runtime_error("NewObject(NativeCandidate) failed");
            }

            std::string surf8, y8;
            if (!u16_to_utf8(rows[i].surface, surf8)) surf8.clear();
            if (!rows[i].yomi.empty())
            {
                if (!u16_to_utf8(rows[i].yomi, y8)) y8.clear();
            }
            else
            {
                y8.clear();
            }

            jstring jSurface = new_jstring_utf8(env, surf8);
            jstring jYomi    = new_jstring_utf8(env, y8);
            jstring jSrc     = new_jstring_utf8(env, rows[i].source);

            env->SetObjectField(obj, g_cache.surface, jSurface);
            env->SetObjectField(obj, g_cache.yomi, jYomi);
            env->SetIntField(obj, g_cache.score, static_cast<jint>(rows[i].score));
            env->SetObjectField(obj, g_cache.src, jSrc);
            env->SetIntField(obj, g_cache.type, static_cast<jint>(rows[i].type));
            env->SetBooleanField(obj, g_cache.hasLR, rows[i].hasLR ? JNI_TRUE : JNI_FALSE);
            env->SetIntField(obj, g_cache.l, static_cast<jint>(rows[i].l));
            env->SetIntField(obj, g_cache.r, static_cast<jint>(rows[i].r));

            env->SetObjectArrayElement(arr, i, obj);

            // clean locals
            env->DeleteLocalRef(jSurface);
            env->DeleteLocalRef(jYomi);
            env->DeleteLocalRef(jSrc);
            env->DeleteLocalRef(obj);
        }

        return arr;
    }
    catch (const std::exception &e)
    {
        jclass ex = env->FindClass("java/lang/RuntimeException");
        if (ex) env->ThrowNew(ex, e.what());
        return nullptr;
    }
}
