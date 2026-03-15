// cli/kana_kanji/astar_bunsetsu_cli.cpp
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>
#include <unordered_map>
#include <algorithm>

#include "connection_id/connection_id_builder.hpp"
#include "graph_builder/graph.hpp"
#include "louds/louds_utf16_reader.hpp"
#include "louds_with_term_id/louds_with_term_id_reader_utf16.hpp"
#include "louds_with_term_id/louds_with_term_id_utf16.hpp"
#include "path_algorithm/find_path.hpp"
#include "token_array/token_array.hpp"

// -----------------------------
// UTF-8 -> UTF-16 (strict)  (same as prefix_predict_cli.cpp)
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

static void usage(const char *argv0)
{
    std::cout
        << "Usage:\n"
        << "  " << argv0
        << " --yomi_termid <yomi_termid.louds> --tango <tango.louds> --tokens <token_array.bin>\n"
        << "      --pos_table <pos_table.bin> --conn <connection_single_column.bin>\n"
        << "      --q <utf8> [--n N] [--beam W] [--show_bunsetsu] [--show_prediction]\n"
        << "      [--yomi_mode cps|cps_pred|cps_omit|all] [--pred_k K] [--pred_n M]\n"
        << "  " << argv0
        << " --yomi_termid <yomi_termid.louds> --tango <tango.louds> --tokens <token_array.bin>\n"
        << "      --pos_table <pos_table.bin> --conn <connection_single_column.bin>\n"
        << "      --stdin [--n N] [--beam W] [--show_bunsetsu] [--show_prediction]\n"
        << "      [--yomi_mode cps|cps_pred|cps_omit|all] [--pred_k K] [--pred_n M]\n"
        << "\n"
        << "Notes:\n"
        << "  --yomi_mode:\n"
        << "    cps      : yomiCps.commonPrefixSearch only (graph)\n"
        << "    cps_pred : graph still uses CPS only; predictive is available for --show_prediction\n"
        << "    cps_omit : commonPrefixSearch + commonPrefixSearchWithOmission (graph)\n"
        << "    all      : cps + omit (graph), predictive for --show_prediction\n"
        << "  --pred_k:\n"
        << "    --show_prediction uses prefix = q.substr(0, K), then keeps yomi that start with q.\n"
        << "  --pred_n:\n"
        << "    Max rows to print for prediction_candidates (default 50). Use 0 to suppress output.\n"
        << "  --show_prediction:\n"
        << "    Prints prediction candidates from LOUDSReaderUtf16::predictiveSearch without modifying the graph.\n";
}

// -----------------------------
// Hiragana -> Katakana (same as graph.cpp logic)
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
// u16 hash for unordered_map
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

struct PredRow
{
    std::u16string surface;
    std::u16string yomi;
    int score = 0;
    int16_t l = 0;
    int16_t r = 0;
};

// -----------------------------
// Build & print prediction candidates (NO graph modification)
// -----------------------------
static void print_prediction(
    const LOUDSReaderUtf16 &yomiCps,
    const LOUDSWithTermIdReaderUtf16 &yomiTerm,
    const TokenArray &tokens,
    const kk::PosTable &pos,
    const LOUDSReaderUtf16 &tango,
    const std::u16string &q16,
    int predK,
    int limitRows = 50)
{
    if (q16.empty())
        return;

    if (limitRows <= 0)
        return;

    if (predK < 1)
        predK = 1;

    const size_t k = std::min(static_cast<size_t>(predK), q16.size());
    const std::u16string prefix = q16.substr(0, k);

    // 1) get yomi candidates from predictiveSearch(prefix)
    const auto preds = yomiCps.predictiveSearch(prefix);

    // 2) Keep only yomi that start with q (so "あいあ" -> "あいあんと" remains)
    // 3) Expand into surface forms via termId -> tokens -> tango
    std::unordered_map<std::u16string, PredRow, U16Hash> bestBySurface;
    bestBySurface.reserve(1024);

    for (const auto &yomi : preds)
    {
        if (!starts_with_u16(yomi, q16))
            continue;

        const int32_t termId = yomiTerm.getTermId(yomi);
        if (termId < 0)
            continue;

        const auto listToken = tokens.getTokensForTermId(termId);

        for (const auto &t : listToken)
        {
            std::u16string surface;

            if (t.nodeIndex == TokenArray::HIRAGANA_SENTINEL)
            {
                surface = yomi;
            }
            else if (t.nodeIndex == TokenArray::KATAKANA_SENTINEL)
            {
                surface = hira_to_kata(yomi);
            }
            else
            {
                surface = tango.getLetter(t.nodeIndex);
            }

            const auto [l, r] = pos.getLR(t.posIndex);
            const int score = static_cast<int>(t.wordCost);

            auto it = bestBySurface.find(surface);
            if (it == bestBySurface.end() || score < it->second.score)
            {
                PredRow row;
                row.surface = surface;
                row.yomi = yomi;
                row.score = score;
                row.l = l;
                row.r = r;
                bestBySurface[surface] = std::move(row);
            }
        }
    }

    std::vector<PredRow> rows;
    rows.reserve(bestBySurface.size());
    for (auto &kv : bestBySurface)
        rows.push_back(std::move(kv.second));

    std::sort(rows.begin(), rows.end(), [](const PredRow &a, const PredRow &b)
              {
                  if (a.score != b.score)
                      return a.score < b.score;
                  // stable-ish tie-break
                  if (a.surface != b.surface)
                      return a.surface < b.surface;
                  return a.yomi < b.yomi; });

    if (rows.empty())
    {
        std::cout << "prediction_candidates: (none)\n";
        return;
    }

    std::cout << "prediction_candidates:\n";
    const int n = std::min<int>(limitRows, static_cast<int>(rows.size()));
    for (int i = 0; i < n; ++i)
    {
        std::string s8, y8;
        if (!u16_to_utf8(rows[i].surface, s8))
            s8 = "<BAD_U16>";
        if (!u16_to_utf8(rows[i].yomi, y8))
            y8 = "<BAD_U16>";

        std::cout << (i + 1) << "\t" << s8
                  << "\tyomi=" << y8
                  << "\tscore=" << rows[i].score
                  << "\tL=" << rows[i].l << "\tR=" << rows[i].r
                  << "\n";
    }
}

static void run_one(const LOUDSReaderUtf16 &yomiCps,
                    const LOUDSWithTermIdReaderUtf16 &yomiTerm,
                    const TokenArray &tokens,
                    const kk::PosTable &pos,
                    const LOUDSReaderUtf16 &tango,
                    const kk::ConnectionMatrix &conn,
                    const std::string &q_utf8,
                    int nBest,
                    int beamWidth,
                    bool showBunsetsu,
                    bool showPrediction,
                    kk::YomiSearchMode yomiMode,
                    int predK,
                    int predN)
{
    std::u16string q16;
    if (!utf8_to_u16(q_utf8, q16))
    {
        std::cout << "[BAD_UTF8] " << q_utf8 << "\n";
        return;
    }

    // 1) build graph (mode switch here)
    kk::Graph graph = kk::GraphBuilder::constructGraph(q16, yomiCps, yomiTerm, tokens, pos, tango, yomiMode, predK);

    // 2) search
    auto [cands, bunsetsu] = kk::FindPath::backwardAStarWithBunsetsu(
        graph,
        static_cast<int>(q16.size()),
        conn,
        nBest,
        beamWidth);

    std::cout << "query=" << q_utf8 << " len=" << q16.size() << " n=" << nBest << " beam=" << beamWidth
              << " yomi_mode=" << static_cast<int>(yomiMode) << " pred_k=" << predK << "\n";

    if (showBunsetsu)
    {
        std::cout << "best_bunsetsu_positions:";
        for (int p : bunsetsu)
            std::cout << " " << p;
        std::cout << "\n";
    }

    for (size_t i = 0; i < cands.size(); ++i)
    {
        std::string out8;
        if (!u16_to_utf8(cands[i].string, out8))
            out8 = "<BAD_U16>";

        std::cout << (i + 1) << "\t" << out8
                  << "\tscore=" << cands[i].score
                  << "\ttype=" << static_cast<int>(cands[i].type);

        if (cands[i].hasLR)
        {
            std::cout << "\tL=" << cands[i].leftId << "\tR=" << cands[i].rightId;
        }

        std::cout << "\n";
    }

    if (showPrediction)
    {
        // Prediction output is independent from the graph.
        print_prediction(yomiCps, yomiTerm, tokens, pos, tango, q16, predK, predN);
    }
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
        bool showPrediction = false;

        std::string yomi_mode_str = "cps";
        int predK = 1;

        // Added: prediction output rows
        int predN = 50;

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
            if (a == "--show_prediction")
            {
                showPrediction = true;
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

            // Added: --pred_n
            if (a == "--pred_n" && i + 1 < argc)
            {
                predN = std::stoi(argv[++i]);
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

        const kk::YomiSearchMode yomiMode = parse_yomi_mode(yomi_mode_str);

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
            run_one(
                yomiCps, yomiTerm, tokens, pos, tango, conn,
                q, nBest, beamWidth, showBunsetsu, showPrediction,
                yomiMode, predK, predN);
            return 0;
        }

        std::string line;
        while (std::getline(std::cin, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;

            run_one(
                yomiCps, yomiTerm, tokens, pos, tango, conn,
                line, nBest, beamWidth, showBunsetsu, showPrediction,
                yomiMode, predK, predN);
        }

        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
