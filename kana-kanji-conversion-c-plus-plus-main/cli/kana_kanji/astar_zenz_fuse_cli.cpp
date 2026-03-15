// cli/kana_kanji/astar_zenz_fuse_cli.cpp
// Fuse A* candidates + Zenz generation/evaluation.
// Modes:
//   --zenz_mode gen      : run Zenz generate and prepend result (dedup / move-to-front)
//   --zenz_mode eval     : evaluate A* candidates by Zenz and reorder:
//                          PASS exists -> move best PASS (max score) to top
//                          else        -> stable sort by FIX prefix length desc
//   --zenz_mode gen_eval : gen + then eval-sort including gen result
//   --zenz_mode off      : A* only
//
// Timing options (added):
//   --show_time          : print total elapsed time (ms) for each query line
//   --time_detail        : also print breakdown (A*, Zenz gen, Zenz eval)
// Notes:
//   - In --zenz_mode gen, Zenz generation runs async in parallel with A*.
//     time_ms_zenz_gen measures the generation task wall time, but it overlaps with A*.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <thread>
#include <chrono> // ★ added

#include "connection_id/connection_id_builder.hpp"
#include "graph_builder/graph.hpp"
#include "louds/louds_utf16_reader.hpp"
#include "louds_with_term_id/louds_with_term_id_reader_utf16.hpp"
#include "louds_with_term_id/louds_with_term_id_utf16.hpp"
#include "path_algorithm/find_path.hpp"
#include "token_array/token_array.hpp"

#include "llama.h"

// ============================================================
// Small chrono helpers
// ============================================================
static inline int64_t now_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static inline int64_t elapsed_ms(int64_t t0_ms)
{
    const int64_t t1 = now_ms();
    return t1 - t0_ms;
}

// ============================================================
// UTF-8 <-> UTF-16 (strict)  (copied from astar_bunsetsu_cli.cpp)
// ============================================================
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

static int utf8_codepoint_len(std::string_view s)
{
    size_t i = 0;
    int n = 0;
    char32_t cp = 0;
    while (i < s.size())
    {
        size_t prev = i;
        if (!utf8_next_codepoint(s, i, cp))
        {
            i = prev + 1;
        }
        n++;
    }
    return n;
}

// ============================================================
// Yomi mode
// ============================================================
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

// ============================================================
// Zenz helpers
// ============================================================
static void die(const std::string &msg)
{
    std::cerr << "error: " << msg << "\n";
    std::exit(1);
}

static int default_threads()
{
    int hw = (int)std::thread::hardware_concurrency();
    if (hw <= 0)
        hw = 4;
    int t = hw - 2;
    if (t < 1)
        t = 1;
    if (t > 8)
        t = 8;
    return t;
}

static std::string preprocess_text(std::string s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char ch : s)
    {
        if (ch == ' ')
        {
            out.append("\xE3\x80\x80"); // U+3000
        }
        else if (ch == '\n' || ch == '\r')
        {
            // drop
        }
        else
        {
            out.push_back(ch);
        }
    }
    return out;
}

// Hiragana -> Katakana in UTF-8
static std::string hira_to_kata_utf8(const std::string &utf8)
{
    std::string out;
    out.reserve(utf8.size());

    const unsigned char *p = (const unsigned char *)utf8.data();
    const size_t n = utf8.size();
    size_t i = 0;

    auto append_u8 = [&](uint32_t cp)
    {
        if (cp <= 0x7F)
        {
            out.push_back((char)cp);
        }
        else if (cp <= 0x7FF)
        {
            out.push_back((char)(0xC0 | ((cp >> 6) & 0x1F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
        else if (cp <= 0xFFFF)
        {
            out.push_back((char)(0xE0 | ((cp >> 12) & 0x0F)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
        else
        {
            out.push_back((char)(0xF0 | ((cp >> 18) & 0x07)));
            out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
    };

    auto decode_one = [&](uint32_t &cp, size_t &adv) -> bool
    {
        adv = 0;
        if (i >= n)
            return false;
        unsigned char c0 = p[i];
        if (c0 < 0x80)
        {
            cp = c0;
            adv = 1;
            return true;
        }
        else if ((c0 & 0xE0) == 0xC0)
        {
            if (i + 1 >= n)
                return false;
            unsigned char c1 = p[i + 1];
            if ((c1 & 0xC0) != 0x80)
                return false;
            cp = ((c0 & 0x1F) << 6) | (c1 & 0x3F);
            adv = 2;
            return true;
        }
        else if ((c0 & 0xF0) == 0xE0)
        {
            if (i + 2 >= n)
                return false;
            unsigned char c1 = p[i + 1];
            unsigned char c2 = p[i + 2];
            if (((c1 & 0xC0) != 0x80) || ((c2 & 0xC0) != 0x80))
                return false;
            cp = ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
            adv = 3;
            return true;
        }
        else if ((c0 & 0xF8) == 0xF0)
        {
            if (i + 3 >= n)
                return false;
            unsigned char c1 = p[i + 1];
            unsigned char c2 = p[i + 2];
            unsigned char c3 = p[i + 3];
            if (((c1 & 0xC0) != 0x80) || ((c2 & 0xC0) != 0x80) || ((c3 & 0xC0) != 0x80))
                return false;
            cp = ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
            adv = 4;
            return true;
        }
        return false;
    };

    while (i < n)
    {
        uint32_t cp = 0;
        size_t adv = 0;
        if (!decode_one(cp, adv) || adv == 0)
        {
            out.push_back((char)p[i]);
            i += 1;
            continue;
        }
        if (0x3041 <= cp && cp <= 0x3096)
            cp += 0x60;
        append_u8(cp);
        i += adv;
    }

    return out;
}

static std::vector<llama_token> tokenize(
    const llama_vocab *vocab,
    const std::string &text_utf8,
    bool add_bos,
    bool add_eos)
{
    const int32_t utf8_len = (int32_t)text_utf8.size();
    int32_t cap = utf8_len + (add_bos ? 1 : 0) + 8;
    if (cap < 16)
        cap = 16;

    std::vector<llama_token> tmp((size_t)cap);
    const int32_t n = llama_tokenize(
        vocab,
        text_utf8.c_str(),
        utf8_len,
        tmp.data(),
        cap,
        add_bos,
        /*parse_special*/ false);

    std::vector<llama_token> out;
    if (n < 0)
    {
        out.push_back(llama_vocab_bos(vocab));
    }
    else
    {
        out.assign(tmp.begin(), tmp.begin() + n);
    }
    if (add_eos)
        out.push_back(llama_vocab_eos(vocab));
    return out;
}

static std::string token_to_piece(const llama_vocab *vocab, llama_token tok)
{
    char buf8[8];
    std::memset(buf8, 0, sizeof(buf8));
    int32_t n = llama_token_to_piece(vocab, tok, buf8, (int32_t)sizeof(buf8), /*lstrip*/ 0, /*special*/ false);
    if (n < 0)
    {
        const int32_t need = -n;
        std::vector<char> big((size_t)need);
        std::memset(big.data(), 0, big.size());
        int32_t n2 = llama_token_to_piece(vocab, tok, big.data(), need, 0, false);
        if (n2 <= 0)
            return {};
        return std::string(big.data(), big.data() + n2);
    }
    else
    {
        if (n <= 0)
            return {};
        return std::string(buf8, buf8 + n);
    }
}

static void batch_add(
    llama_batch &batch,
    llama_token id,
    llama_pos pos,
    llama_seq_id seq_id,
    bool logits)
{
    const int32_t i = batch.n_tokens;
    batch.token[i] = id;
    batch.pos[i] = pos;
    batch.n_seq_id[i] = 1;
    batch.seq_id[i][0] = seq_id;
    batch.logits[i] = logits ? 1 : 0;
    batch.n_tokens += 1;
}

struct KvState
{
    std::vector<llama_token> prev_tokens;
};

static const float *get_logits_kv(
    llama_context *ctx,
    llama_seq_id seq_id,
    const std::vector<llama_token> &tokens,
    int logits_start_index,
    KvState &st)
{
    int common = 0;
    const int m = (int)std::min(st.prev_tokens.size(), tokens.size());
    while (common < m && st.prev_tokens[(size_t)common] == tokens[(size_t)common])
        common++;

    const int prefix_cache = std::min(common, logits_start_index);
    llama_kv_cache_seq_rm(ctx, seq_id, (llama_pos)prefix_cache, -1);

    const int32_t need = (int32_t)(tokens.size() - (size_t)prefix_cache);
    if (need <= 0)
        return llama_get_logits(ctx);

    const int32_t cap = std::max<int32_t>(512, need);
    llama_batch batch = llama_batch_init(cap, /*embd*/ 0, /*n_seq_max*/ 1);
    if (!batch.token)
    {
        llama_batch_free(batch);
        return nullptr;
    }

    for (int i = prefix_cache; i < (int)tokens.size(); i++)
    {
        const bool want_logits = (i >= logits_start_index);
        batch_add(batch, tokens[(size_t)i], (llama_pos)i, seq_id, want_logits);
    }

    const int rc = llama_decode(ctx, batch);
    llama_batch_free(batch);
    if (rc != 0)
        return nullptr;

    st.prev_tokens = tokens;
    return llama_get_logits(ctx);
}

static std::string build_prompt_v3(
    const std::string &left_context_utf8,
    const std::string &input_hira_utf8,
    const std::string &profile_utf8,
    const std::string &topic_utf8,
    const std::string &style_utf8,
    const std::string &preference_utf8,
    int max_left_len_chars)
{
    const std::string inputTag = "\xEE\xB8\x80";   // U+EE00
    const std::string outputTag = "\xEE\xB8\x81";  // U+EE01
    const std::string contextTag = "\xEE\xB8\x82"; // U+EE02
    const std::string profTag = "\xEE\xB8\x83";    // U+EE03
    const std::string topicTag = "\xEE\xB8\x84";   // U+EE04
    const std::string styleTag = "\xEE\xB8\x85";   // U+EE05
    const std::string prefTag = "\xEE\xB8\x86";    // U+EE06

    auto suffix_bytes = [](const std::string &s, size_t max_bytes) -> std::string
    {
        if (s.size() <= max_bytes)
            return s;
        return s.substr(s.size() - max_bytes);
    };

    std::string conditions;
    if (!profile_utf8.empty())
        conditions += profTag + suffix_bytes(profile_utf8, 25);
    if (!topic_utf8.empty())
        conditions += topicTag + suffix_bytes(topic_utf8, 25);
    if (!style_utf8.empty())
        conditions += styleTag + suffix_bytes(style_utf8, 25);
    if (!preference_utf8.empty())
        conditions += prefTag + suffix_bytes(preference_utf8, 25);

    std::string left_trim = left_context_utf8;
    if (!left_trim.empty())
    {
        const size_t max_bytes = (size_t)std::max(1, max_left_len_chars) * 4;
        if (left_trim.size() > max_bytes)
        {
            left_trim = left_trim.substr(left_trim.size() - max_bytes);
        }
    }

    const std::string input_kata = hira_to_kata_utf8(input_hira_utf8);

    std::string prompt;
    if (!left_trim.empty())
    {
        prompt = conditions + contextTag + left_trim + inputTag + input_kata + outputTag;
    }
    else
    {
        prompt = conditions + inputTag + input_kata + outputTag;
    }
    return preprocess_text(prompt);
}

static std::string greedy_generate(
    llama_context *ctx,
    const llama_vocab *vocab,
    std::vector<llama_token> prompt_tokens,
    int max_new_chars,
    int min_len,
    KvState &kv,
    llama_seq_id seq_id = 0)
{
    const llama_token eos = llama_vocab_eos(vocab);
    const std::unordered_set<std::string> stop_chars = {"、", "。", "！", "？"};

    std::string out;
    for (int step = 0; step < max_new_chars; step++)
    {
        const int startOffset = (int)prompt_tokens.size() - 1;
        const float *logits = get_logits_kv(ctx, seq_id, prompt_tokens, startOffset, kv);
        if (!logits)
            die("llama_decode failed while decoding prompt");

        const int32_t n_vocab = llama_vocab_n_tokens(vocab);

        llama_token best_tok = 0;
        float best_val = logits[0];
        for (int32_t t = 1; t < n_vocab; t++)
        {
            if (logits[t] > best_val)
            {
                best_val = logits[t];
                best_tok = (llama_token)t;
            }
        }
        if (best_tok == eos)
            break;

        const std::string piece = token_to_piece(vocab, best_tok);
        if (piece.empty())
            break;

        if ((int)out.size() >= min_len && stop_chars.find(piece) != stop_chars.end())
            break;

        out += piece;

        const std::string piece_pp = preprocess_text(piece);
        std::vector<llama_token> appended = tokenize(vocab, piece_pp, false, false);
        if (appended.empty())
            break;
        prompt_tokens.insert(prompt_tokens.end(), appended.begin(), appended.end());
    }
    return out;
}

// ============================================================
// Zenz eval core (PASS/FIX/WHOLE)
// ============================================================
static float logsumexp(const float *logits, int n)
{
    float m = logits[0];
    for (int i = 1; i < n; i++)
        m = std::max(m, logits[i]);
    double sum = 0.0;
    for (int i = 0; i < n; i++)
        sum += std::exp((double)logits[i] - (double)m);
    return (float)((double)m + std::log(sum));
}

static std::string detokenize_range(
    const llama_vocab *vocab,
    const std::vector<llama_token> &tokens,
    size_t begin,
    size_t end)
{
    std::string s;
    for (size_t i = begin; i < end; i++)
    {
        s += token_to_piece(vocab, tokens[i]);
    }
    return s;
}

enum class EvalKind
{
    Pass,
    FixRequired,
    WholeResult,
    Error
};

struct EvalResult
{
    EvalKind kind = EvalKind::Error;
    float score = 0.0f;            // PASS
    std::string fix_prefix_utf8;   // FIX
    std::string whole_result_utf8; // WHOLE
};

// safer version using llama_get_logits_ith(ctx, slice_idx)
static EvalResult evaluate_candidate_core(
    llama_context *ctx,
    const llama_vocab *vocab,
    const std::vector<llama_token> &prompt_tokens,
    const std::string &candidate_text_utf8,
    KvState &kv,
    llama_seq_id seq_id = 0,
    bool verbose = false)
{
    EvalResult r;

    const std::vector<llama_token> cand_tokens =
        tokenize(vocab, candidate_text_utf8, /*add_bos*/ false, /*add_eos*/ false);

    std::vector<llama_token> tokens;
    tokens.reserve(prompt_tokens.size() + cand_tokens.size());
    tokens.insert(tokens.end(), prompt_tokens.begin(), prompt_tokens.end());
    tokens.insert(tokens.end(), cand_tokens.begin(), cand_tokens.end());

    if (tokens.size() < 2)
        return r;

    const int32_t n_vocab = llama_vocab_n_tokens(vocab);
    const llama_token eos = llama_vocab_eos(vocab);

    const int startOffset = (int)prompt_tokens.size() - 1;
    if (startOffset < 0)
        return r;

    const float *dummy = get_logits_kv(ctx, seq_id, tokens, startOffset, kv);
    if (!dummy)
        return r;

    float sum_score = 0.0f;

    for (size_t i = (size_t)startOffset + 1; i < tokens.size(); i++)
    {
        const int dist_pos = (int)i - 1;
        const int slice_idx = dist_pos - startOffset;
        if (slice_idx < 0)
            continue;

        const float *dist = llama_get_logits_ith(ctx, slice_idx);
        if (!dist)
            return r;

        const float lse = logsumexp(dist, (int)n_vocab);

        llama_token best_tok = 0;
        float best_val = dist[0];
        for (int t = 1; t < n_vocab; t++)
        {
            if (dist[t] > best_val)
            {
                best_val = dist[t];
                best_tok = (llama_token)t;
            }
        }

        const llama_token actual_tok = tokens[i];
        const float actual_logp = dist[(int)actual_tok] - lse;
        const float best_logp = best_val - lse;

        if (best_tok != actual_tok)
        {
            if (verbose)
            {
                std::cerr << "mismatch at i=" << i
                          << " best=" << (int)best_tok
                          << " actual=" << (int)actual_tok
                          << " best_logp=" << best_logp
                          << " actual_logp=" << actual_logp
                          << "\n";
            }

            if (best_tok == eos)
            {
                const std::string decoded = detokenize_range(vocab, tokens, 0, i);
                const std::string prompt_decoded = detokenize_range(vocab, prompt_tokens, 0, prompt_tokens.size());
                std::string whole = decoded;
                if (whole.size() >= prompt_decoded.size())
                {
                    whole = whole.substr(prompt_decoded.size());
                }
                r.kind = EvalKind::WholeResult;
                r.whole_result_utf8 = whole;
                return r;
            }
            else
            {
                const std::string decoded_prefix = detokenize_range(vocab, tokens, 0, i);
                const std::string prompt_decoded = detokenize_range(vocab, prompt_tokens, 0, prompt_tokens.size());
                std::string prefix = decoded_prefix;
                if (prefix.size() >= prompt_decoded.size())
                {
                    prefix = prefix.substr(prompt_decoded.size());
                }
                else
                {
                    prefix.clear();
                }
                prefix += token_to_piece(vocab, best_tok);

                r.kind = EvalKind::FixRequired;
                r.fix_prefix_utf8 = prefix;
                return r;
            }
        }
        else
        {
            sum_score += actual_logp;
        }
    }

    r.kind = EvalKind::Pass;
    r.score = sum_score;
    return r;
}

// ============================================================
// Thread-safe(ish) runner (mutex guarded)
// ============================================================
class ZenzRunner
{
public:
    struct Options
    {
        std::string model_path;
        int n_ctx = 512;
        int n_batch = 512;
        int threads = -1;
        int gpu_layers = 0;
        bool use_mmap = true;
        bool offload_kqv = false;

        int max_new = 64;
        int min_len = 1;

        std::string left;
        std::string profile;
        std::string topic;
        std::string style;
        std::string preference;

        bool verbose = false;
    };

    struct EvalOut
    {
        EvalKind kind = EvalKind::Error;
        float pass_score = 0.0f;
        int fix_prefix_len = 0;
        std::string fix_prefix_utf8;
    };

    explicit ZenzRunner(const Options &opt) : opt_(opt)
    {
        if (opt_.threads <= 0)
            opt_.threads = default_threads();

        llama_backend_init();

        llama_model_params mparams = llama_model_default_params();
        mparams.use_mmap = opt_.use_mmap;
        mparams.n_gpu_layers = opt_.gpu_layers;

        model_ = llama_model_load_from_file(opt_.model_path.c_str(), mparams);
        if (!model_)
        {
            llama_backend_free();
            die("could not load model: " + opt_.model_path);
        }

        llama_context_params cparams = llama_context_default_params();
        cparams.n_ctx = opt_.n_ctx;
        cparams.n_batch = opt_.n_batch;
        cparams.n_threads = opt_.threads;
        cparams.n_threads_batch = opt_.threads;
        cparams.offload_kqv = opt_.offload_kqv;

        ctx_ = llama_init_from_model(model_, cparams);
        if (!ctx_)
        {
            llama_model_free(model_);
            llama_backend_free();
            die("could not init context");
        }

        vocab_ = llama_model_get_vocab(model_);
        if (!vocab_)
        {
            llama_free(ctx_);
            llama_model_free(model_);
            llama_backend_free();
            die("could not get vocab");
        }
    }

    ~ZenzRunner()
    {
        if (ctx_)
            llama_free(ctx_);
        if (model_)
            llama_model_free(model_);
        llama_backend_free();
    }

    std::string generate(const std::string &input_hira_utf8)
    {
        std::lock_guard<std::mutex> lock(mu_);

        const std::string prompt = build_prompt_v3(
            opt_.left,
            input_hira_utf8,
            opt_.profile,
            opt_.topic,
            opt_.style,
            opt_.preference,
            /*max_left_len_chars*/ 40);

        if (opt_.verbose)
        {
            std::cerr << "zenz prompt(preprocessed)=" << prompt << "\n";
        }

        const std::vector<llama_token> prompt_tokens = tokenize(vocab_, prompt, true, false);
        if (prompt_tokens.empty())
            return {};

        llama_kv_cache_seq_rm(ctx_, /*seq_id*/ 0, 0, -1);
        kv_.prev_tokens.clear();

        std::string out = greedy_generate(
            ctx_, vocab_,
            prompt_tokens,
            opt_.max_new,
            opt_.min_len,
            kv_,
            /*seq_id*/ 0);
        return out;
    }

    std::vector<EvalOut> eval_candidates(
        const std::string &input_hira_utf8,
        const std::vector<std::string> &candidates_utf8)
    {
        std::lock_guard<std::mutex> lock(mu_);

        const std::string prompt = build_prompt_v3(
            opt_.left,
            input_hira_utf8,
            opt_.profile,
            opt_.topic,
            opt_.style,
            opt_.preference,
            /*max_left_len_chars*/ 40);

        const std::vector<llama_token> prompt_tokens = tokenize(vocab_, prompt, true, false);
        if (prompt_tokens.empty())
            return {};

        std::vector<EvalOut> outs;
        outs.reserve(candidates_utf8.size());

        for (const auto &cand_raw : candidates_utf8)
        {
            const std::string cand_pp = preprocess_text(cand_raw);

            llama_kv_cache_seq_rm(ctx_, /*seq_id*/ 0, 0, -1);
            kv_.prev_tokens.clear();

            EvalResult er = evaluate_candidate_core(
                ctx_, vocab_,
                prompt_tokens,
                cand_pp,
                kv_,
                /*seq_id*/ 0,
                /*verbose*/ opt_.verbose);

            EvalOut eo;
            eo.kind = er.kind;
            if (er.kind == EvalKind::Pass)
            {
                eo.pass_score = er.score;
            }
            else if (er.kind == EvalKind::FixRequired)
            {
                eo.fix_prefix_utf8 = er.fix_prefix_utf8;
                eo.fix_prefix_len = utf8_codepoint_len(er.fix_prefix_utf8);
            }
            outs.push_back(std::move(eo));
        }

        return outs;
    }

private:
    Options opt_;
    llama_model *model_ = nullptr;
    llama_context *ctx_ = nullptr;
    const llama_vocab *vocab_ = nullptr;
    KvState kv_;
    std::mutex mu_;
};

// ============================================================
// A* candidate struct (print-compatible)
// ============================================================
struct OutCand
{
    std::string text_utf8;
    std::string yomi_utf8;
    int score = 0;
    int type = 0;
    bool hasLR = false;
    int16_t L = 0;
    int16_t R = 0;
    bool isZenz = false;
};

// ============================================================
// Zenz mode
// ============================================================
enum class ZenzMode
{
    Off,
    Gen,
    Eval,
    GenEval,
};

static ZenzMode parse_zenz_mode(const std::string &s)
{
    if (s == "off")
        return ZenzMode::Off;
    if (s == "gen")
        return ZenzMode::Gen;
    if (s == "eval")
        return ZenzMode::Eval;
    if (s == "gen_eval")
        return ZenzMode::GenEval;
    throw std::runtime_error("Unknown --zenz_mode: " + s + " (expected: off|gen|eval|gen_eval)");
}

// ============================================================
// Usage
// ============================================================
static void usage(const char *argv0)
{
    std::cerr
        << "Usage:\n"
        << "  " << argv0
        << " --yomi_termid <yomi_termid.louds> --tango <tango.louds> --tokens <token_array.bin>\n"
        << "      --pos_table <pos_table.bin> --conn <connection_single_column.bin>\n"
        << "      --zenz_model <zenz.gguf>\n"
        << "      --q <utf8> [--stdin]\n"
        << "      [--n N] [--beam W] [--show_bunsetsu]\n"
        << "      [--yomi_mode cps|cps_pred|cps_omit|all] [--pred_k K]\n"
        << "      [--zenz_mode off|gen|eval|gen_eval]   (default: gen)\n"
        << "      [--zenz_show_eval]                   (append eval info per line)\n"
        << "      [--show_yomi]                        (append yomi=... per line)\n"
        << "      [--zenz_max_new 64] [--zenz_min_len 1]\n"
        << "      [--zenz_left \"...\"] [--zenz_profile \"...\"] [--zenz_topic \"...\"] [--zenz_style \"...\"] [--zenz_preference \"...\"]\n"
        << "      [--zenz_n_ctx 512] [--zenz_n_batch 512] [--zenz_threads N]\n"
        << "      [--zenz_gpu_layers 0] [--zenz_no_mmap] [--zenz_offload_kqv]\n"
        // ★追加: typo args
        << "      [--typo on|off] [--typo_max_penalty N] [--typo_weight W] [--typo_max_out M]\n"
        // ★追加: time args
        << "      [--show_time] [--time_detail]\n"
        << "      [--verbose]\n"
        << "\n"
        << "Behavior:\n"
        << "  - A* always runs.\n"
        << "  - --zenz_mode gen      : run Zenz generate in parallel and prepend/move-to-front (dedup).\n"
        << "  - --zenz_mode eval     : evaluate A* candidates and reorder:\n"
        << "                           * if any PASS -> move best PASS (max score) to top\n"
        << "                           * else        -> stable sort by FIX prefix length desc\n"
        << "  - --zenz_mode gen_eval : apply gen, then eval-sort including gen output.\n"
        << "                           NOTE: only in gen_eval, Zenz gen query uses A* top-1 yomi (omit-aware).\n"
        << "  - --zenz_mode off      : disable Zenz completely (A* only).\n"
        << "  - --typo on            : enable KanaFlick 12-key typo-aware yomi search (adds penalty to word cost).\n"
        << "  - --show_time          : print elapsed time for each query (ms). In gen mode, gen overlaps with A*.\n"
        << "  - --time_detail        : also print breakdown: A*, Zenz gen, Zenz eval (ms).\n";
}

// ============================================================
// Core: run one query
// ============================================================
struct GenResult
{
    std::string out;
    int64_t gen_ms = -1;
};

static void run_one_fused(
    const LOUDSReaderUtf16 &yomiCps,
    const LOUDSWithTermIdReaderUtf16 &yomiTerm,
    const TokenArray &tokens,
    const kk::PosTable &pos,
    const LOUDSReaderUtf16 &tango,
    const kk::ConnectionMatrix &conn,
    ZenzRunner *zenz, // nullable when zenz_mode=off
    const std::string &q_utf8,
    int nBest,
    int beamWidth,
    bool showBunsetsu,
    kk::YomiSearchMode yomiMode,
    int predK,
    ZenzMode zenzMode,
    bool zenzShowEval,
    bool showYomi,
    bool verbose,
    kk::TypoOptions typoOpt,
    bool showTime,     // ★ added
    bool timeDetail    // ★ added
)
{
    const int64_t t_total0 = now_ms(); // ★ timing

    std::u16string q16;
    if (!utf8_to_u16(q_utf8, q16))
    {
        std::cout << "[BAD_UTF8] " << q_utf8 << "\n";
        return;
    }

    // Zenz gen async (only when zenzMode == Gen)
    std::future<GenResult> fut_zenz;
    const bool need_gen = (zenzMode == ZenzMode::Gen || zenzMode == ZenzMode::GenEval);

    if (need_gen && zenzMode == ZenzMode::Gen)
    {
        if (!zenz)
            die("internal: zenzMode requires zenz runner but zenz is null");

        const std::string q_copy = q_utf8;
        fut_zenz = std::async(std::launch::async, [&, q_copy]() -> GenResult
                              {
                                  GenResult gr;
                                  const int64_t t0 = now_ms();
                                  gr.out = zenz->generate(q_copy); // gen uses original query
                                  gr.gen_ms = elapsed_ms(t0);
                                  return gr;
                              });
    }

    // A* graph + search timing
    int64_t time_ms_astar = -1;
    kk::Graph graph;
    std::vector<kk::Candidate> cands;
    std::vector<int> bunsetsu;

    {
        const int64_t t0 = now_ms();
        graph = kk::GraphBuilder::constructGraph(
            q16, yomiCps, yomiTerm, tokens, pos, tango, yomiMode, predK, typoOpt);

        auto ret = kk::FindPath::backwardAStarWithBunsetsu(
            graph,
            static_cast<int>(q16.size()),
            conn,
            nBest,
            beamWidth);
        cands = std::move(ret.first);
        bunsetsu = std::move(ret.second);

        time_ms_astar = elapsed_ms(t0);
    }

    // Collect A* output
    std::vector<OutCand> out;
    out.reserve(cands.size() + 1);

    for (size_t i = 0; i < cands.size(); ++i)
    {
        std::string s8;
        if (!u16_to_utf8(cands[i].string, s8))
            s8 = "<BAD_U16>";
        std::string y8;
        if (!u16_to_utf8(cands[i].yomi, y8))
            y8 = "<BAD_U16>";

        OutCand oc;
        oc.text_utf8 = std::move(s8);
        oc.yomi_utf8 = std::move(y8);
        oc.score = cands[i].score;
        oc.type = static_cast<int>(cands[i].type);
        oc.hasLR = cands[i].hasLR;
        if (oc.hasLR)
        {
            oc.L = cands[i].leftId;
            oc.R = cands[i].rightId;
        }
        out.push_back(std::move(oc));
    }

    // Apply Zenz generate prepend/move-to-front
    std::string zenz_out;
    std::string zenz_query_yomi = q_utf8;
    int64_t time_ms_zenz_gen = -1;

    if (need_gen)
    {
        if (!zenz)
            die("internal: zenzMode requires zenz runner but zenz is null");

        if (zenzMode == ZenzMode::GenEval)
        {
            if (!out.empty() && !out[0].yomi_utf8.empty() && out[0].yomi_utf8 != "<BAD_U16>")
            {
                zenz_query_yomi = out[0].yomi_utf8;
            }
            else
            {
                zenz_query_yomi = q_utf8;
            }

            const int64_t t0 = now_ms();
            zenz_out = zenz->generate(zenz_query_yomi);
            time_ms_zenz_gen = elapsed_ms(t0);
        }
        else
        {
            try
            {
                GenResult gr = fut_zenz.get();
                zenz_out = std::move(gr.out);
                time_ms_zenz_gen = gr.gen_ms;
            }
            catch (...)
            {
                zenz_out.clear();
                time_ms_zenz_gen = -1;
            }
            zenz_query_yomi = q_utf8;
        }

        if (!zenz_out.empty())
        {
            while (!zenz_out.empty() && (zenz_out.back() == '\n' || zenz_out.back() == '\r'))
            {
                zenz_out.pop_back();
            }
        }

        if (!zenz_out.empty())
        {
            auto it = std::find_if(out.begin(), out.end(), [&](const OutCand &oc)
                                   { return oc.text_utf8 == zenz_out; });

            if (it != out.end())
            {
                OutCand tmp = *it;
                tmp.isZenz = true;
                tmp.yomi_utf8 = zenz_query_yomi;
                out.erase(it);
                out.insert(out.begin(), std::move(tmp));
            }
            else
            {
                OutCand z;
                z.text_utf8 = zenz_out;
                z.yomi_utf8 = zenz_query_yomi;
                z.score = -1;
                z.type = 99;
                z.hasLR = false;
                z.isZenz = true;
                out.insert(out.begin(), std::move(z));
            }
        }
    }

    // Eval-sort (only for eval/gen_eval)
    int64_t time_ms_zenz_eval = -1;
    std::vector<ZenzRunner::EvalOut> evals;
    if (zenzMode == ZenzMode::Eval || zenzMode == ZenzMode::GenEval)
    {
        if (!zenz)
            die("internal: zenzMode requires zenz runner but zenz is null");

        const int64_t t0 = now_ms();

        evals.assign(out.size(), {});
        std::unordered_map<std::string, std::vector<size_t>> groups;
        groups.reserve(out.size());
        for (size_t i = 0; i < out.size(); ++i)
        {
            groups[out[i].yomi_utf8].push_back(i);
        }
        for (auto &kv : groups)
        {
            const std::string &yomi8 = kv.first;
            const std::vector<size_t> &idxs = kv.second;
            std::vector<std::string> texts;
            texts.reserve(idxs.size());
            for (size_t idx : idxs)
                texts.push_back(out[idx].text_utf8);
            auto sub = zenz->eval_candidates(yomi8, texts);
            if (sub.size() == idxs.size())
            {
                for (size_t j = 0; j < idxs.size(); ++j)
                    evals[idxs[j]] = sub[j];
            }
            else
            {
                for (size_t idx : idxs)
                    evals[idx].kind = EvalKind::Error;
            }
        }

        int best_pass = -1;
        float best_score = -std::numeric_limits<float>::infinity();
        if (evals.size() == out.size())
        {
            for (int i = 0; i < (int)evals.size(); i++)
            {
                if (evals[(size_t)i].kind == EvalKind::Pass)
                {
                    if (best_pass < 0 || evals[(size_t)i].pass_score > best_score)
                    {
                        best_pass = i;
                        best_score = evals[(size_t)i].pass_score;
                    }
                }
            }
        }

        if (best_pass >= 0)
        {
            OutCand top = out[(size_t)best_pass];
            out.erase(out.begin() + best_pass);
            out.insert(out.begin(), std::move(top));

            if (zenzShowEval && evals.size() == out.size())
            {
                auto e0 = evals[(size_t)best_pass];
                evals.erase(evals.begin() + best_pass);
                evals.insert(evals.begin(), std::move(e0));
            }
        }
        else if (evals.size() == out.size())
        {
            std::vector<int> idx((int)out.size());
            std::iota(idx.begin(), idx.end(), 0);

            std::stable_sort(idx.begin(), idx.end(), [&](int a, int b)
                             {
                const int la = evals[(size_t)a].fix_prefix_len;
                const int lb = evals[(size_t)b].fix_prefix_len;
                if (la != lb) return la > lb;
                return a < b; });

            std::vector<OutCand> sorted;
            sorted.reserve(out.size());
            for (int i : idx)
                sorted.push_back(std::move(out[(size_t)i]));
            out = std::move(sorted);

            if (zenzShowEval)
            {
                std::vector<ZenzRunner::EvalOut> es;
                es.reserve(evals.size());
                for (int i : idx)
                    es.push_back(std::move(evals[(size_t)i]));
                evals = std::move(es);
            }
        }

        time_ms_zenz_eval = elapsed_ms(t0);
    }

    const int64_t time_ms_total = elapsed_ms(t_total0);

    // Print header
    std::cout
        << "query=" << q_utf8
        << " len=" << q16.size()
        << " n=" << nBest
        << " beam=" << beamWidth
        << " yomi_mode=" << static_cast<int>(yomiMode)
        << " pred_k=" << predK
        << " zenz_mode=";

    switch (zenzMode)
    {
    case ZenzMode::Off:
        std::cout << "off";
        break;
    case ZenzMode::Gen:
        std::cout << "gen";
        break;
    case ZenzMode::Eval:
        std::cout << "eval";
        break;
    case ZenzMode::GenEval:
        std::cout << "gen_eval";
        break;
    }

    std::cout << " zenz_gen=" << (!zenz_out.empty() ? "1" : "0")
              << " typo=" << (typoOpt.enable ? "on" : "off")
              << " typo_max_penalty=" << typoOpt.maxPenalty
              << " typo_weight=" << typoOpt.weight
              << " typo_max_out=" << typoOpt.maxOut;

    // ★ timing print
    if (showTime)
    {
        std::cout << " time_ms=" << time_ms_total;
        if (timeDetail)
        {
            std::cout << " time_ms_astar=" << time_ms_astar
                      << " time_ms_zenz_gen=" << time_ms_zenz_gen
                      << " time_ms_zenz_eval=" << time_ms_zenz_eval;
        }
    }

    std::cout << "\n";

    if (showBunsetsu)
    {
        std::cout << "best_bunsetsu_positions:";
        for (int p : bunsetsu)
            std::cout << " " << p;
        std::cout << "\n";
    }

    // Print candidates
    for (size_t i = 0; i < out.size(); ++i)
    {
        const auto &oc = out[i];

        std::cout << (i + 1) << "\t" << oc.text_utf8
                  << "\tscore=" << oc.score
                  << "\ttype=" << oc.type;

        if (oc.isZenz)
        {
            std::cout << "\tfrom=zenz_gen";
        }

        if (oc.hasLR)
        {
            std::cout << "\tL=" << oc.L << "\tR=" << oc.R;
        }

        if (zenzShowEval && evals.size() == out.size())
        {
            const auto &ev = evals[i];
            if (ev.kind == EvalKind::Pass)
            {
                std::cout << "\tzenz=PASS(score=" << ev.pass_score << ")";
            }
            else if (ev.kind == EvalKind::FixRequired)
            {
                std::cout << "\tzenz=FIX(len=" << ev.fix_prefix_len << ")";
            }
            else if (ev.kind == EvalKind::WholeResult)
            {
                std::cout << "\tzenz=WHOLE";
            }
            else
            {
                std::cout << "\tzenz=ERROR";
            }
        }

        if (showYomi)
        {
            std::cout << "\tyomi=" << oc.yomi_utf8;
        }

        std::cout << "\n";
    }

    if (verbose && !zenz_out.empty())
    {
        std::cerr << "[debug] zenz_out=" << zenz_out << "\n";
        if (need_gen)
        {
            std::cerr << "[debug] zenz_query_yomi=" << zenz_query_yomi << "\n";
        }
    }
}

// ============================================================
// main
// ============================================================
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
        int beamWidth = 50;
        bool showBunsetsu = false;

        std::string yomi_mode_str = "cps";
        int predK = 1;

        bool verbose = false;

        // ★追加: typo options
        kk::TypoOptions typoOpt;
        typoOpt.enable = false;
        typoOpt.maxPenalty = 3;
        typoOpt.maxOut = 128;
        typoOpt.weight = 1500;

        // ★追加: time options
        bool showTime = false;
        bool timeDetail = false;

        // Zenz options
        ZenzRunner::Options zopt;
        zopt.n_ctx = 512;
        zopt.n_batch = 512;
        zopt.threads = -1;
        zopt.gpu_layers = 0;
        zopt.use_mmap = true;
        zopt.offload_kqv = false;
        zopt.max_new = 64;
        zopt.min_len = 1;
        zopt.verbose = false;

        ZenzMode zenzMode = ZenzMode::Gen;
        bool zenzShowEval = false;
        bool showYomi = false;

        for (int i = 1; i < argc; ++i)
        {
            const std::string a = argv[i];

            auto need = [&](const char *name) -> std::string
            {
                if (i + 1 >= argc)
                    throw std::runtime_error(std::string("missing value for ") + name);
                return std::string(argv[++i]);
            };

            if (a == "--help" || a == "-h")
            {
                usage(argv[0]);
                return 0;
            }

            // A* paths
            if (a == "--yomi_termid")
            {
                yomi_termid_path = need("--yomi_termid");
                continue;
            }
            if (a == "--tango")
            {
                tango_path = need("--tango");
                continue;
            }
            if (a == "--tokens")
            {
                tokens_path = need("--tokens");
                continue;
            }
            if (a == "--pos_table")
            {
                pos_path = need("--pos_table");
                continue;
            }
            if (a == "--conn")
            {
                conn_path = need("--conn");
                continue;
            }

            // query
            if (a == "--q")
            {
                q = need("--q");
                continue;
            }
            if (a == "--stdin")
            {
                stdin_mode = true;
                continue;
            }

            // A* knobs
            if (a == "--n")
            {
                nBest = std::stoi(need("--n"));
                continue;
            }
            if (a == "--beam")
            {
                beamWidth = std::stoi(need("--beam"));
                continue;
            }
            if (a == "--show_bunsetsu")
            {
                showBunsetsu = true;
                continue;
            }
            if (a == "--yomi_mode")
            {
                yomi_mode_str = need("--yomi_mode");
                continue;
            }
            if (a == "--pred_k")
            {
                predK = std::stoi(need("--pred_k"));
                continue;
            }

            // ★追加: typo args
            if (a == "--typo")
            {
                const std::string v = need("--typo");
                if (v == "on")
                    typoOpt.enable = true;
                else if (v == "off")
                    typoOpt.enable = false;
                else
                    throw std::runtime_error("Unknown --typo: " + v + " (expected: on|off)");
                continue;
            }
            if (a == "--typo_max_penalty")
            {
                typoOpt.maxPenalty = std::stoi(need("--typo_max_penalty"));
                continue;
            }
            if (a == "--typo_weight")
            {
                typoOpt.weight = std::stoi(need("--typo_weight"));
                continue;
            }
            if (a == "--typo_max_out")
            {
                typoOpt.maxOut = std::stoi(need("--typo_max_out"));
                continue;
            }

            // ★追加: timing args
            if (a == "--show_time")
            {
                showTime = true;
                continue;
            }
            if (a == "--time_detail")
            {
                timeDetail = true;
                continue;
            }

            // Zenz behavior
            if (a == "--zenz_mode")
            {
                zenzMode = parse_zenz_mode(need("--zenz_mode"));
                continue;
            }
            if (a == "--zenz_show_eval")
            {
                zenzShowEval = true;
                continue;
            }
            if (a == "--show_yomi")
            {
                showYomi = true;
                continue;
            }

            // Zenz model
            if (a == "--zenz_model")
            {
                zopt.model_path = need("--zenz_model");
                continue;
            }

            // Zenz knobs
            if (a == "--zenz_left")
            {
                zopt.left = need("--zenz_left");
                continue;
            }
            if (a == "--zenz_profile")
            {
                zopt.profile = need("--zenz_profile");
                continue;
            }
            if (a == "--zenz_topic")
            {
                zopt.topic = need("--zenz_topic");
                continue;
            }
            if (a == "--zenz_style")
            {
                zopt.style = need("--zenz_style");
                continue;
            }
            if (a == "--zenz_preference")
            {
                zopt.preference = need("--zenz_preference");
                continue;
            }

            if (a == "--zenz_n_ctx")
            {
                zopt.n_ctx = std::stoi(need("--zenz_n_ctx"));
                continue;
            }
            if (a == "--zenz_n_batch")
            {
                zopt.n_batch = std::stoi(need("--zenz_n_batch"));
                continue;
            }
            if (a == "--zenz_threads")
            {
                zopt.threads = std::stoi(need("--zenz_threads"));
                continue;
            }
            if (a == "--zenz_gpu_layers")
            {
                zopt.gpu_layers = std::stoi(need("--zenz_gpu_layers"));
                continue;
            }
            if (a == "--zenz_no_mmap")
            {
                zopt.use_mmap = false;
                continue;
            }
            if (a == "--zenz_offload_kqv")
            {
                zopt.offload_kqv = true;
                continue;
            }

            if (a == "--zenz_max_new")
            {
                zopt.max_new = std::stoi(need("--zenz_max_new"));
                continue;
            }
            if (a == "--zenz_min_len")
            {
                zopt.min_len = std::stoi(need("--zenz_min_len"));
                continue;
            }

            if (a == "--verbose")
            {
                verbose = true;
                zopt.verbose = true;
                continue;
            }

            throw std::runtime_error("Unknown/incomplete arg: " + a);
        }

        // validate args
        if (yomi_termid_path.empty() || tango_path.empty() || tokens_path.empty() ||
            pos_path.empty() || conn_path.empty() ||
            (!stdin_mode && q.empty()))
        {
            usage(argv[0]);
            return 2;
        }

        // zenz_model required unless zenz_mode=off
        if (zenzMode != ZenzMode::Off && zopt.model_path.empty())
        {
            std::cerr << "Error: --zenz_model is required when --zenz_mode != off\n";
            return 2;
        }

        const kk::YomiSearchMode yomiMode = parse_yomi_mode(yomi_mode_str);

        // Load A* assets once
        const auto yomiCps = LOUDSReaderUtf16::loadFromFile(yomi_termid_path);
        const auto yomiTrie = LOUDSWithTermIdUtf16::loadFromFile(yomi_termid_path);
        const LOUDSWithTermIdReaderUtf16 yomiTerm(yomiTrie);

        const auto tango = LOUDSReaderUtf16::loadFromFile(tango_path);
        const auto tokens = TokenArray::loadFromFile(tokens_path);
        const auto pos = kk::PosTable::loadFromFile(pos_path);

        const auto connVec = ConnectionIdBuilder::readShortArrayFromBytesBE(conn_path);
        const kk::ConnectionMatrix conn(std::vector<int16_t>(connVec.begin(), connVec.end()));

        // Init Zenz once (unless off)
        std::unique_ptr<ZenzRunner> zenz;
        if (zenzMode != ZenzMode::Off)
        {
            zenz = std::make_unique<ZenzRunner>(zopt);
        }

        if (!stdin_mode)
        {
            run_one_fused(
                yomiCps, yomiTerm, tokens, pos, tango, conn,
                zenz ? zenz.get() : nullptr,
                q,
                nBest, beamWidth, showBunsetsu,
                yomiMode, predK,
                zenzMode,
                zenzShowEval,
                showYomi,
                verbose,
                typoOpt,
                showTime,
                timeDetail);
            return 0;
        }

        std::string line;
        while (std::getline(std::cin, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;

            run_one_fused(
                yomiCps, yomiTerm, tokens, pos, tango, conn,
                zenz ? zenz.get() : nullptr,
                line,
                nBest, beamWidth, showBunsetsu,
                yomiMode, predK,
                zenzMode,
                zenzShowEval,
                showYomi,
                verbose,
                typoOpt,
                showTime,
                timeDetail);
        }

        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
