// cli/zenz/zenz_convert_cli.cpp
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>    // <-- FIX: std::exp / std::log
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>
#include <thread>   // <-- FIX: for std::thread::hardware_concurrency()

#include "llama.h"

// -----------------------------
// Small helpers
// -----------------------------
static void die(const std::string & msg) {
    std::cerr << "error: " << msg << "\n";
    std::exit(1);
}

static int default_threads() {
    int hw = (int)std::thread::hardware_concurrency();
    if (hw <= 0) hw = 4;
    int t = hw - 2;
    if (t < 1) t = 1;
    if (t > 8) t = 8;
    return t;
}

static std::string preprocess_text(std::string s) {
    // Swift: replace space into ideographic space (\u3000), replace newline into ""
    // U+3000 in UTF-8: E3 80 80
    std::string out;
    out.reserve(s.size() + 8);
    for (char ch : s) {
        if (ch == ' ') {
            out.append("\xE3\x80\x80");
        } else if (ch == '\n' || ch == '\r') {
            // drop
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

// Convert Hiragana (U+3041..U+3096) to Katakana by +0x60.
// Input/Output are UTF-8.
static std::string hira_to_kata_utf8(const std::string & utf8) {
    std::string out;
    out.reserve(utf8.size());

    const unsigned char * p = (const unsigned char *)utf8.data();
    const size_t n = utf8.size();
    size_t i = 0;

    auto append_utf8 = [&](uint32_t cp) {
        if (cp <= 0x7F) {
            out.push_back((char)cp);
        } else if (cp <= 0x7FF) {
            out.push_back((char)(0xC0 | ((cp >> 6) & 0x1F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back((char)(0xE0 | ((cp >> 12) & 0x0F)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else {
            out.push_back((char)(0xF0 | ((cp >> 18) & 0x07)));
            out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
    };

    auto decode_one = [&](uint32_t & cp, size_t & adv) -> bool {
        adv = 0;
        if (i >= n) return false;
        unsigned char c0 = p[i];
        if (c0 < 0x80) {
            cp = c0; adv = 1; return true;
        } else if ((c0 & 0xE0) == 0xC0) {
            if (i + 1 >= n) return false;
            unsigned char c1 = p[i + 1];
            if ((c1 & 0xC0) != 0x80) return false;
            cp = ((c0 & 0x1F) << 6) | (c1 & 0x3F);
            adv = 2; return true;
        } else if ((c0 & 0xF0) == 0xE0) {
            if (i + 2 >= n) return false;
            unsigned char c1 = p[i + 1];
            unsigned char c2 = p[i + 2];
            if (((c1 & 0xC0) != 0x80) || ((c2 & 0xC0) != 0x80)) return false;
            cp = ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
            adv = 3; return true;
        } else if ((c0 & 0xF8) == 0xF0) {
            if (i + 3 >= n) return false;
            unsigned char c1 = p[i + 1];
            unsigned char c2 = p[i + 2];
            unsigned char c3 = p[i + 3];
            if (((c1 & 0xC0) != 0x80) || ((c2 & 0xC0) != 0x80) || ((c3 & 0xC0) != 0x80)) return false;
            cp = ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
            adv = 4; return true;
        }
        return false;
    };

    while (i < n) {
        uint32_t cp = 0;
        size_t adv = 0;
        if (!decode_one(cp, adv) || adv == 0) {
            out.push_back((char)p[i]);
            i += 1;
            continue;
        }
        if (0x3041 <= cp && cp <= 0x3096) {
            cp += 0x60;
        }
        append_utf8(cp);
        i += adv;
    }

    return out;
}

// -----------------------------
// Tokenize / detokenize (Swift-compatible)
// -----------------------------
static std::vector<llama_token> tokenize(
    const llama_vocab * vocab,
    const std::string & text_utf8,
    bool add_bos,
    bool add_eos
) {
    const int32_t utf8_len = (int32_t)text_utf8.size();
    int32_t cap = utf8_len + (add_bos ? 1 : 0) + 8;
    if (cap < 16) cap = 16;

    std::vector<llama_token> tmp((size_t)cap);
    const int32_t n = llama_tokenize(
        vocab,
        text_utf8.c_str(),
        utf8_len,
        tmp.data(),
        cap,
        add_bos,
        /*parse_special*/ false
    );

    std::vector<llama_token> out;
    if (n < 0) {
        out.push_back(llama_vocab_bos(vocab));
    } else {
        out.assign(tmp.begin(), tmp.begin() + n);
    }
    if (add_eos) out.push_back(llama_vocab_eos(vocab));
    return out;
}

static std::string token_to_piece(const llama_vocab * vocab, llama_token tok) {
    char buf8[8];
    std::memset(buf8, 0, sizeof(buf8));
    int32_t n = llama_token_to_piece(vocab, tok, buf8, (int32_t)sizeof(buf8), /*lstrip*/ 0, /*special*/ false);
    if (n < 0) {
        const int32_t need = -n;
        std::vector<char> big((size_t)need);
        std::memset(big.data(), 0, big.size());
        int32_t n2 = llama_token_to_piece(vocab, tok, big.data(), need, 0, false);
        if (n2 <= 0) return {};
        return std::string(big.data(), big.data() + n2);
    } else {
        if (n <= 0) return {};
        return std::string(buf8, buf8 + n);
    }
}

// -----------------------------
// Batch add (Swift-compatible)
// -----------------------------
static void batch_add(
    llama_batch & batch,
    llama_token id,
    llama_pos pos,
    llama_seq_id seq_id,
    bool logits
) {
    const int32_t i = batch.n_tokens;
    batch.token   [i] = id;
    batch.pos     [i] = pos;
    batch.n_seq_id[i] = 1;
    batch.seq_id  [i][0] = seq_id;
    batch.logits  [i] = logits ? 1 : 0;
    batch.n_tokens += 1;
}

// -----------------------------
// Minimal KV-managed logits getter (close to Swift get_logits)
// -----------------------------
struct KvState {
    std::vector<llama_token> prev_tokens;
};

static const float * get_logits_kv(
    llama_context * ctx,
    llama_seq_id seq_id,
    const std::vector<llama_token> & tokens,
    int logits_start_index,
    KvState & st
) {
    int common = 0;
    const int m = (int)std::min(st.prev_tokens.size(), tokens.size());
    while (common < m && st.prev_tokens[(size_t)common] == tokens[(size_t)common]) common++;

    const int prefix_cache = std::min(common, logits_start_index);
    llama_kv_cache_seq_rm(ctx, seq_id, (llama_pos)prefix_cache, -1);

    const int32_t need = (int32_t)(tokens.size() - (size_t)prefix_cache);
    if (need <= 0) return llama_get_logits(ctx); // no-op; logits may be stale but caller uses only when adding

    const int32_t cap = std::max<int32_t>(512, need);
    llama_batch batch = llama_batch_init(cap, /*embd*/ 0, /*n_seq_max*/ 1);
    if (!batch.token) {
        llama_batch_free(batch);
        return nullptr;
    }

    for (int i = prefix_cache; i < (int)tokens.size(); i++) {
        const bool want_logits = (i >= logits_start_index);
        batch_add(batch, tokens[(size_t)i], (llama_pos)i, seq_id, want_logits);
    }

    const int rc = llama_decode(ctx, batch);
    llama_batch_free(batch);
    if (rc != 0) return nullptr;

    st.prev_tokens = tokens;
    return llama_get_logits(ctx);
}

// -----------------------------
// Prompt builder (Zenz v3 style from Swift)
// -----------------------------
// Tags:
//   inputTag   = \u{EE00}
//   outputTag  = \u{EE01}
//   contextTag = \u{EE02}
//   profile    = \u{EE03}
//   topic      = \u{EE04}
//   style      = \u{EE05}
//   preference = \u{EE06}
static std::string build_prompt_v3(
    const std::string & left_context_utf8,
    const std::string & input_hira_utf8,
    const std::string & profile_utf8,
    const std::string & topic_utf8,
    const std::string & style_utf8,
    const std::string & preference_utf8,
    int max_left_len_chars
) {
    const std::string inputTag   = "\xEE\xB8\x80"; // U+EE00
    const std::string outputTag  = "\xEE\xB8\x81"; // U+EE01
    const std::string contextTag = "\xEE\xB8\x82"; // U+EE02
    const std::string profTag    = "\xEE\xB8\x83"; // U+EE03
    const std::string topicTag   = "\xEE\xB8\x84"; // U+EE04
    const std::string styleTag   = "\xEE\xB8\x85"; // U+EE05
    const std::string prefTag    = "\xEE\xB8\x86"; // U+EE06

    auto suffix_bytes = [](const std::string & s, size_t max_bytes) -> std::string {
        if (s.size() <= max_bytes) return s;
        return s.substr(s.size() - max_bytes);
    };

    std::string conditions;
    if (!profile_utf8.empty())    conditions += profTag  + suffix_bytes(profile_utf8,  25);
    if (!topic_utf8.empty())      conditions += topicTag + suffix_bytes(topic_utf8,    25);
    if (!style_utf8.empty())      conditions += styleTag + suffix_bytes(style_utf8,    25);
    if (!preference_utf8.empty()) conditions += prefTag  + suffix_bytes(preference_utf8,25);

    std::string left_trim = left_context_utf8;
    if (!left_trim.empty()) {
        const size_t max_bytes = (size_t)std::max(1, max_left_len_chars) * 4; // rough UTF-8 upper
        if (left_trim.size() > max_bytes) {
            left_trim = left_trim.substr(left_trim.size() - max_bytes);
        }
    }

    const std::string input_kata = hira_to_kata_utf8(input_hira_utf8);

    std::string prompt;
    if (!left_trim.empty()) {
        prompt = conditions + contextTag + left_trim + inputTag + input_kata + outputTag;
    } else {
        prompt = conditions + inputTag + input_kata + outputTag;
    }
    return preprocess_text(prompt);
}

// -----------------------------
// logsumexp + logprob for one position
// -----------------------------
static float logsumexp(const float * logits, int n) {
    float m = logits[0];
    for (int i = 1; i < n; i++) m = std::max(m, logits[i]);
    double sum = 0.0;
    for (int i = 0; i < n; i++) sum += std::exp((double)logits[i] - (double)m);
    return (float)((double)m + std::log(sum));
}

static std::string detokenize_range(
    const llama_vocab * vocab,
    const std::vector<llama_token> & tokens,
    size_t begin,
    size_t end
) {
    std::string s;
    for (size_t i = begin; i < end; i++) {
        s += token_to_piece(vocab, tokens[i]);
    }
    return s;
}

// -----------------------------
// evaluate_candidate() equivalent (core)
// -----------------------------
enum class EvalKind {
    Pass,        // score available
    FixRequired, // prefix bytes (UTF-8) available
    WholeResult, // model suggests early EOS; return full continuation
    Error
};

struct EvalResult {
    EvalKind kind = EvalKind::Error;
    float score = 0.0f;                 // for Pass
    std::string fix_prefix_utf8;        // for FixRequired (candidate replacement prefix)
    std::string whole_result_utf8;      // for WholeResult
};

// Swift notes:
// - prompt ends with outputTag. They evaluate tokens = prompt_tokens + candidate_tokens
// - startOffset = prompt_tokens.count - 1 (+ addressed_tokens.count ... we omit constraints)
// - for each i in tokens.dropFirst(startOffset+1): compute logp distribution at (i-1), pick argmax
// - if argmax != actual token_id:
//     if argmax == eos -> return wholeResult (decoded continuation)
//     else -> return fixRequired(prefixConstraint: prefix + piece(argmax))
// - else: score += logp(actual)
static EvalResult evaluate_candidate_core(
    llama_context * ctx,
    const llama_vocab * vocab,
    const std::vector<llama_token> & prompt_tokens,
    const std::string & candidate_text_utf8,  // already preprocess_text applied if needed
    KvState & kv,
    llama_seq_id seq_id = 0,
    bool verbose = false
) {
    EvalResult r;
    r.kind = EvalKind::Error;

    const std::vector<llama_token> cand_tokens =
        tokenize(vocab, candidate_text_utf8, /*add_bos*/ false, /*add_eos*/ false);

    std::vector<llama_token> tokens;
    tokens.reserve(prompt_tokens.size() + cand_tokens.size());
    tokens.insert(tokens.end(), prompt_tokens.begin(), prompt_tokens.end());
    tokens.insert(tokens.end(), cand_tokens.begin(), cand_tokens.end());

    if (tokens.size() < 2) {
        return r;
    }

    const int32_t n_vocab = llama_vocab_n_tokens(vocab);
    const llama_token eos = llama_vocab_eos(vocab);

    const int startOffset = (int)prompt_tokens.size() - 1;
    if (startOffset < 0) return r;

    // Decode all tokens, requesting logits from startOffset onward.
    // We do ONE decode call by marking logits for i>=startOffset,
    // but llama_get_logits gives us a contiguous buffer only for requested positions.
    // To keep it simple and robust: we call get_logits_kv once with logits_start_index=startOffset.
    const float * logits_all = get_logits_kv(ctx, seq_id, tokens, startOffset, kv);
    if (!logits_all) return r;

    // In llama.cpp, when multiple logits positions are requested, llama_get_logits returns
    // a buffer of size (n_vocab * n_logits_positions) in sequence order.
    // Our logits positions are i >= startOffset. The first logits slice corresponds to i=startOffset.
    // For scoring token at position i (actual token tokens[i]), we need distribution at i-1,
    // i.e. slice index (i-1 - startOffset).
    auto slice_ptr = [&](int slice_idx) -> const float * {
        return logits_all + (size_t)slice_idx * (size_t)n_vocab;
    };

    float sum_score = 0.0f;

    // i ranges over tokens positions we "consume" (actual token to score), starting at startOffset+1
    for (size_t i = (size_t)startOffset + 1; i < tokens.size(); i++) {
        const int dist_pos = (int)i - 1;
        const int slice_idx = dist_pos - startOffset;
        if (slice_idx < 0) continue;

        const float * dist = slice_ptr(slice_idx);
        const float lse = logsumexp(dist, (int)n_vocab);

        // argmax
        llama_token best_tok = 0;
        float best_val = dist[0];
        for (int t = 1; t < n_vocab; t++) {
            if (dist[t] > best_val) {
                best_val = dist[t];
                best_tok = (llama_token)t;
            }
        }

        const llama_token actual_tok = tokens[i];
        const float actual_logp = dist[(int)actual_tok] - lse;
        const float best_logp   = best_val - lse;

        if (best_tok != actual_tok) {
            if (verbose) {
                std::cerr << "mismatch at i=" << i
                          << " best=" << (int)best_tok
                          << " actual=" << (int)actual_tok
                          << " best_logp=" << best_logp
                          << " actual_logp=" << actual_logp
                          << "\n";
            }

            if (best_tok == eos) {
                // wholeResult: decode prefix tokens[..<i] then drop prompt bytes
                const std::string decoded = detokenize_range(vocab, tokens, 0, i);
                // prompt_text bytes to drop: use detokenized prompt (not original prompt string)
                const std::string prompt_decoded = detokenize_range(vocab, prompt_tokens, 0, prompt_tokens.size());
                std::string whole = decoded;
                if (whole.size() >= prompt_decoded.size()) {
                    whole = whole.substr(prompt_decoded.size());
                }
                r.kind = EvalKind::WholeResult;
                r.whole_result_utf8 = whole;
                return r;
            } else {
                // fixRequired: prefix of candidate that model prefers
                // Swift builds prefix bytes = tokens[..<i] + piece(best_tok), then drops prompt bytes.
                const std::string decoded_prefix = detokenize_range(vocab, tokens, 0, i);
                const std::string prompt_decoded = detokenize_range(vocab, prompt_tokens, 0, prompt_tokens.size());
                std::string prefix = decoded_prefix;
                if (prefix.size() >= prompt_decoded.size()) {
                    prefix = prefix.substr(prompt_decoded.size());
                } else {
                    prefix.clear();
                }
                prefix += token_to_piece(vocab, best_tok);

                r.kind = EvalKind::FixRequired;
                r.fix_prefix_utf8 = prefix;
                return r;
            }
        } else {
            sum_score += actual_logp;
        }
    }

    r.kind = EvalKind::Pass;
    r.score = sum_score;
    return r;
}

// -----------------------------
// Greedy generation (for --mode generate)
// -----------------------------
static std::string greedy_generate(
    llama_context * ctx,
    const llama_vocab * vocab,
    std::vector<llama_token> prompt_tokens,
    int max_new_chars,
    int min_len,
    KvState & kv,
    llama_seq_id seq_id = 0
) {
    const llama_token eos = llama_vocab_eos(vocab);
    const std::unordered_set<std::string> stop_chars = {"、", "。", "！", "？"};

    std::string out;
    for (int step = 0; step < max_new_chars; step++) {
        const int startOffset = (int)prompt_tokens.size() - 1;
        const float * logits = get_logits_kv(ctx, seq_id, prompt_tokens, startOffset, kv);
        if (!logits) die("llama_decode failed while decoding prompt");

        const int32_t n_vocab = llama_vocab_n_tokens(vocab);

        llama_token best_tok = 0;
        float best_val = logits[0];
        for (int32_t t = 1; t < n_vocab; t++) {
            if (logits[t] > best_val) {
                best_val = logits[t];
                best_tok = (llama_token)t;
            }
        }
        if (best_tok == eos) break;

        const std::string piece = token_to_piece(vocab, best_tok);
        if (piece.empty()) break;

        if ((int)out.size() >= min_len && stop_chars.find(piece) != stop_chars.end()) break;

        out += piece;

        const std::string piece_pp = preprocess_text(piece);
        std::vector<llama_token> appended = tokenize(vocab, piece_pp, false, false);
        if (appended.empty()) break;
        prompt_tokens.insert(prompt_tokens.end(), appended.begin(), appended.end());
    }
    return out;
}

// -----------------------------
// Args
// -----------------------------
struct Args {
    std::string model_path;

    // generate mode
    std::string q;
    std::string left;
    std::string profile;
    std::string topic;
    std::string style;
    std::string preference;

    // eval mode
    std::string input;               // hira reading (will be kata inside prompt)
    std::vector<std::string> candidates;

    int n_ctx = 512;
    int n_batch = 512;
    int threads = -1;
    int gpu_layers = 0;
    bool use_mmap = true;
    bool offload_kqv = false;

    int max_new = 64;
    int min_len = 1;

    bool verbose = false;

    enum class Mode { Generate, Eval } mode = Mode::Generate;
};

static void usage() {
    std::cerr <<
R"(zenz_convert_cli

Mode A) generate (greedy)
  zenz_convert_cli --model PATH --q "ひらがな読み"
    [--left "..."] [--profile "..."] [--topic "..."] [--style "..."] [--preference "..."]
    [--max_new 64] [--min_len 1]

Mode B) eval (candidate scoring / validate)
  zenz_convert_cli --model PATH --mode eval --input "ひらがな読み"
    --candidate "候補1" --candidate "候補2" ...
    [--left "..."] [--profile "..."] [--topic "..."] [--style "..."] [--preference "..."]

Common:
  [--n_ctx 512] [--n_batch 512] [--threads N]
  [--gpu_layers 0] [--no_mmap] [--offload_kqv]
  [--verbose]

Output:
  - generate: prints generated string only.
  - eval: prints one line per candidate:
      PASS  score=<logprob>  text=<candidate>
      FIX   prefix=<model_preferred_prefix>
      WHOLE text=<model_whole_result>
)";
}

static Args parse_args(int argc, char ** argv) {
    Args a;
    for (int i = 1; i < argc; i++) {
        std::string k = argv[i];
        auto need = [&](const char * name) -> std::string {
            if (i + 1 >= argc) die(std::string("missing value for ") + name);
            return std::string(argv[++i]);
        };

        if (k == "--help" || k == "-h") {
            usage();
            std::exit(0);
        } else if (k == "--model") {
            a.model_path = need("--model");
        } else if (k == "--mode") {
            const std::string m = need("--mode");
            if (m == "generate") a.mode = Args::Mode::Generate;
            else if (m == "eval") a.mode = Args::Mode::Eval;
            else die("unknown --mode: " + m);
        } else if (k == "--q") {
            a.q = need("--q");
        } else if (k == "--input") {
            a.input = need("--input");
        } else if (k == "--candidate") {
            a.candidates.push_back(need("--candidate"));
        } else if (k == "--left") {
            a.left = need("--left");
        } else if (k == "--profile") {
            a.profile = need("--profile");
        } else if (k == "--topic") {
            a.topic = need("--topic");
        } else if (k == "--style") {
            a.style = need("--style");
        } else if (k == "--preference") {
            a.preference = need("--preference");
        } else if (k == "--n_ctx") {
            a.n_ctx = std::stoi(need("--n_ctx"));
        } else if (k == "--n_batch") {
            a.n_batch = std::stoi(need("--n_batch"));
        } else if (k == "--threads") {
            a.threads = std::stoi(need("--threads"));
        } else if (k == "--gpu_layers") {
            a.gpu_layers = std::stoi(need("--gpu_layers"));
        } else if (k == "--no_mmap") {
            a.use_mmap = false;
        } else if (k == "--offload_kqv") {
            a.offload_kqv = true;
        } else if (k == "--max_new") {
            a.max_new = std::stoi(need("--max_new"));
        } else if (k == "--min_len") {
            a.min_len = std::stoi(need("--min_len"));
        } else if (k == "--verbose") {
            a.verbose = true;
        } else {
            die("unknown argument: " + k);
        }
    }

    if (a.model_path.empty()) die("missing --model");
    if (a.threads <= 0) a.threads = default_threads();

    if (a.mode == Args::Mode::Generate) {
        if (a.q.empty()) die("missing --q (generate mode)");
    } else {
        if (a.input.empty()) die("missing --input (eval mode)");
        if (a.candidates.empty()) die("missing --candidate ... (eval mode)");
    }

    return a;
}

// -----------------------------
// main
// -----------------------------
int main(int argc, char ** argv) {
    Args args = parse_args(argc, argv);

    if (args.verbose) {
        std::cerr << "model=" << args.model_path
                  << " n_ctx=" << args.n_ctx
                  << " n_batch=" << args.n_batch
                  << " threads=" << args.threads
                  << " gpu_layers=" << args.gpu_layers
                  << " mmap=" << (args.use_mmap ? "1" : "0")
                  << " offload_kqv=" << (args.offload_kqv ? "1" : "0")
                  << "\n";
    }

    llama_backend_init();

    llama_model_params mparams = llama_model_default_params();
    mparams.use_mmap = args.use_mmap;
    mparams.n_gpu_layers = args.gpu_layers;

    llama_model * model = llama_model_load_from_file(args.model_path.c_str(), mparams);
    if (!model) {
        llama_backend_free();
        die("could not load model: " + args.model_path);
    }

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx = args.n_ctx;
    cparams.n_batch = args.n_batch;
    cparams.n_threads = args.threads;
    cparams.n_threads_batch = args.threads;
    cparams.offload_kqv = args.offload_kqv;

    llama_context * ctx = llama_init_from_model(model, cparams);
    if (!ctx) {
        llama_model_free(model);
        llama_backend_free();
        die("could not init context");
    }

    const llama_vocab * vocab = llama_model_get_vocab(model);
    if (!vocab) {
        llama_free(ctx);
        llama_model_free(model);
        llama_backend_free();
        die("could not get vocab");
    }

    // Build prompt same as iOS v3
    const std::string & input_hira = (args.mode == Args::Mode::Generate) ? args.q : args.input;

    const std::string prompt = build_prompt_v3(
        args.left,
        input_hira,
        args.profile,
        args.topic,
        args.style,
        args.preference,
        /*max_left_len_chars*/ 40
    );

    if (args.verbose) {
        std::cerr << "prompt(preprocessed)=" << prompt << "\n";
    }

    // prompt tokens: add_bos=true, add_eos=false
    const std::vector<llama_token> prompt_tokens = tokenize(vocab, prompt, true, false);
    if (prompt_tokens.empty()) {
        llama_free(ctx);
        llama_model_free(model);
        llama_backend_free();
        die("prompt tokenization empty");
    }

    KvState kv;

    if (args.mode == Args::Mode::Generate) {
        std::string out = greedy_generate(
            ctx, vocab,
            prompt_tokens,
            args.max_new,
            args.min_len,
            kv,
            /*seq_id*/ 0
        );
        std::cout << out << "\n";
    } else {
        // Eval mode: evaluate each candidate
        for (const std::string & cand : args.candidates) {
            // Swift: candidate text also preprocess (spaces/newlines normalization)
            const std::string cand_pp = preprocess_text(cand);

            EvalResult er = evaluate_candidate_core(
                ctx, vocab,
                prompt_tokens,
                cand_pp,
                kv,
                /*seq_id*/ 0,
                /*verbose*/ args.verbose
            );

            if (er.kind == EvalKind::Pass) {
                std::cout << "PASS score=" << er.score << " text=" << cand << "\n";
            } else if (er.kind == EvalKind::FixRequired) {
                std::cout << "FIX prefix=" << er.fix_prefix_utf8 << "\n";
            } else if (er.kind == EvalKind::WholeResult) {
                std::cout << "WHOLE text=" << er.whole_result_utf8 << "\n";
            } else {
                std::cout << "ERROR text=" << cand << "\n";
            }

            // NOTE: Swift は prevInputBySeq/prevPromptBySeq を管理しているが、
            // CLI の eval は candidate ごとに条件が異なるため、キャッシュを汚染しやすい。
            // 安全のため candidate ごとにKVをリセットする（速度より正しさ優先）。
            llama_kv_cache_seq_rm(ctx, /*seq_id*/ 0, 0, -1);
            kv.prev_tokens.clear();
        }
    }

    llama_free(ctx);
    llama_model_free(model);
    llama_backend_free();
    return 0;
}
