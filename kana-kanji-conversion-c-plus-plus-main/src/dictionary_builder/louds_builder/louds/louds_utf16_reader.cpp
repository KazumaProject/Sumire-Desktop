#include "louds/louds_utf16_reader.hpp"

#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <cmath>

// ============================================================
// ctor
// ============================================================

LOUDSReaderUtf16::LOUDSReaderUtf16(const BitVector &lbs,
                                   const BitVector &isLeaf,
                                   std::vector<char16_t> labels)
    : LBS_(lbs),
      isLeaf_(isLeaf),
      labels_(std::move(labels)),
      lbsSucc_(LBS_)
{
}

// ============================================================
// LOUDS navigation
// ============================================================

int LOUDSReaderUtf16::firstChild(int pos) const
{
    // Safety: select0 expects 1-indexed; if rank1(pos)==0 => invalid
    const int r1 = lbsSucc_.rank1(pos);
    if (r1 <= 0)
        return -1;

    const int y = lbsSucc_.select0(r1) + 1;
    if (y < 0)
        return -1;
    if (static_cast<size_t>(y) >= LBS_.size())
        return -1;
    return (LBS_.get(static_cast<size_t>(y)) ? y : -1);
}

int LOUDSReaderUtf16::traverse(int pos, char16_t c) const
{
    int childPos = firstChild(pos);
    if (childPos == -1)
        return -1;

    while (static_cast<size_t>(childPos) < LBS_.size() &&
           LBS_.get(static_cast<size_t>(childPos)))
    {
        const int labelIndex = lbsSucc_.rank1(childPos);
        if (labelIndex >= 0 && static_cast<size_t>(labelIndex) < labels_.size())
        {
            if (labels_[static_cast<size_t>(labelIndex)] == c)
                return childPos;
        }
        childPos += 1;
    }
    return -1;
}

// ============================================================
// Common prefix search (existing behavior)
// ============================================================

std::vector<std::u16string> LOUDSReaderUtf16::commonPrefixSearch(const std::u16string &str) const
{
    std::vector<char16_t> resultTemp;
    std::vector<std::u16string> result;

    int n = 0;
    for (char16_t c : str)
    {
        n = traverse(n, c);
        if (n == -1)
            break;

        const int index = lbsSucc_.rank1(n);
        if (index < 0 || static_cast<size_t>(index) >= labels_.size())
            break;

        resultTemp.push_back(labels_[static_cast<size_t>(index)]);

        if (static_cast<size_t>(n) < isLeaf_.size() && isLeaf_.get(static_cast<size_t>(n)))
        {
            std::u16string tempStr(resultTemp.begin(), resultTemp.end());
            if (!result.empty())
            {
                result.push_back(result[0] + tempStr);
            }
            else
            {
                result.push_back(tempStr);
                resultTemp.clear();
            }
        }
    }
    return result;
}

// ============================================================
// Predictive search
// ============================================================

void LOUDSReaderUtf16::collectWords(int pos, std::u16string &prefix, std::vector<std::u16string> &out) const
{
    if (pos < 0)
        return;
    if (static_cast<size_t>(pos) >= LBS_.size())
        return;

    if (static_cast<size_t>(pos) < isLeaf_.size() && isLeaf_.get(static_cast<size_t>(pos)))
    {
        out.push_back(prefix);
    }

    int childPos = firstChild(pos);
    while (childPos >= 0 &&
           static_cast<size_t>(childPos) < LBS_.size() &&
           LBS_.get(static_cast<size_t>(childPos)))
    {
        const int labelIndex = lbsSucc_.rank1(childPos);
        if (labelIndex < 0 || static_cast<size_t>(labelIndex) >= labels_.size())
            break;

        prefix.push_back(labels_[static_cast<size_t>(labelIndex)]);
        collectWords(childPos, prefix, out);
        prefix.pop_back();

        childPos += 1;
    }
}

std::vector<std::u16string> LOUDSReaderUtf16::predictiveSearch(const std::u16string &prefix) const
{
    std::vector<std::u16string> out;

    int n = 0;
    std::u16string built;
    built.reserve(prefix.size());

    for (char16_t c : prefix)
    {
        n = traverse(n, c);
        if (n < 0)
            return out;

        const int idx = lbsSucc_.rank1(n);
        if (idx < 0 || static_cast<size_t>(idx) >= labels_.size())
            return out;

        built.push_back(labels_[static_cast<size_t>(idx)]);
    }

    collectWords(n, built, out);
    return out;
}

// ============================================================
// Omission-aware common prefix search
// ============================================================

std::vector<char16_t> LOUDSReaderUtf16::getCharVariations(char16_t ch)
{
    switch (ch)
    {
    case u'か':
        return {u'か', u'が'};
    case u'き':
        return {u'き', u'ぎ'};
    case u'く':
        return {u'く', u'ぐ'};
    case u'け':
        return {u'け', u'げ'};
    case u'こ':
        return {u'こ', u'ご'};

    case u'さ':
        return {u'さ', u'ざ'};
    case u'し':
        return {u'し', u'じ'};
    case u'す':
        return {u'す', u'ず'};
    case u'せ':
        return {u'せ', u'ぜ'};
    case u'そ':
        return {u'そ', u'ぞ'};

    case u'た':
        return {u'た', u'だ'};
    case u'ち':
        return {u'ち', u'ぢ'};
    case u'つ':
        return {u'つ', u'づ', u'っ'};
    case u'て':
        return {u'て', u'で'};
    case u'と':
        return {u'と', u'ど'};

    case u'は':
        return {u'は', u'ば', u'ぱ'};
    case u'ひ':
        return {u'ひ', u'び', u'ぴ'};
    case u'ふ':
        return {u'ふ', u'ぶ', u'ぷ'};
    case u'へ':
        return {u'へ', u'べ', u'ぺ'};
    case u'ほ':
        return {u'ほ', u'ぼ', u'ぽ'};

    case u'や':
        return {u'や', u'ゃ'};
    case u'ゆ':
        return {u'ゆ', u'ゅ'};
    case u'よ':
        return {u'よ', u'ょ'};

    case u'あ':
        return {u'あ', u'ぁ'};
    case u'い':
        return {u'い', u'ぃ'};
    case u'う':
        return {u'う', u'ぅ'};
    case u'え':
        return {u'え', u'ぇ'};
    case u'お':
        return {u'お', u'ぉ'};

    default:
        return {ch};
    }
}

void LOUDSReaderUtf16::searchRecursiveWithOmission(const std::u16string &originalStr,
                                                   size_t strIndex,
                                                   int currentNodeIndex,
                                                   std::u16string &currentYomi,
                                                   uint16_t replaceCount,
                                                   bool omissionOccurred,
                                                   std::vector<OmissionSearchResult> &out) const
{
    if (currentNodeIndex < 0)
        return;
    if (static_cast<size_t>(currentNodeIndex) >= LBS_.size())
        return;

    // prefix search: if leaf at current node, accept currentYomi
    if (static_cast<size_t>(currentNodeIndex) < isLeaf_.size() &&
        isLeaf_.get(static_cast<size_t>(currentNodeIndex)))
    {
        out.push_back(OmissionSearchResult{currentYomi, replaceCount, omissionOccurred});
    }

    if (strIndex >= originalStr.size())
        return;

    const char16_t ch = originalStr[strIndex];
    const auto vars = getCharVariations(ch);

    for (char16_t variant : vars)
    {
        const bool replaced = (variant != ch);
        const bool newOmission = omissionOccurred || replaced;

        // replaceCount は「置換が必要だった文字数」
        const uint16_t newReplaceCount = static_cast<uint16_t>(replaceCount + (replaced ? 1 : 0));

        int childPos = firstChild(currentNodeIndex);
        while (childPos >= 0 &&
               static_cast<size_t>(childPos) < LBS_.size() &&
               LBS_.get(static_cast<size_t>(childPos)))
        {
            const int labelIndex = lbsSucc_.rank1(childPos);
            if (labelIndex >= 0 && static_cast<size_t>(labelIndex) < labels_.size())
            {
                if (labels_[static_cast<size_t>(labelIndex)] == variant)
                {
                    currentYomi.push_back(variant);
                    searchRecursiveWithOmission(originalStr, strIndex + 1, childPos, currentYomi, newReplaceCount, newOmission, out);
                    currentYomi.pop_back();
                    break; // sibling labels are assumed unique
                }
            }
            childPos += 1;
        }
    }
}

std::vector<LOUDSReaderUtf16::OmissionSearchResult>
LOUDSReaderUtf16::commonPrefixSearchWithOmission(const std::u16string &str) const
{
    std::vector<OmissionSearchResult> raw;
    raw.reserve(128);

    std::u16string current;
    current.reserve(str.size());

    searchRecursiveWithOmission(str, 0, 0, current, /*replaceCount=*/0, /*omissionOccurred=*/false, raw);

    // de-dup by yomi:
    // - omissionOccurred: OR (true wins)
    // - replaceCount: min (少ない置換数を優先)
    std::vector<OmissionSearchResult> out;
    out.reserve(raw.size());

    for (const auto &r : raw)
    {
        bool found = false;
        for (auto &e : out)
        {
            if (e.yomi == r.yomi)
            {
                e.omissionOccurred = (e.omissionOccurred || r.omissionOccurred);
                if (r.replaceCount < e.replaceCount)
                    e.replaceCount = r.replaceCount;
                found = true;
                break;
            }
        }
        if (!found)
            out.push_back(r);
    }

    // sort:
    // - replaceCount 少ない順
    // - yomi 長さ短い順（commonPrefix用途なので短い方が先）
    // - lex
    std::sort(out.begin(), out.end(),
              [](const OmissionSearchResult &a, const OmissionSearchResult &b)
              {
                  if (a.replaceCount != b.replaceCount)
                      return a.replaceCount < b.replaceCount;
                  if (a.yomi.size() != b.yomi.size())
                      return a.yomi.size() < b.yomi.size();
                  return a.yomi < b.yomi;
              });

    return out;
}

// ============================================================
// ★KanaFlick typo-aware common prefix search
//   - Dijkstra over (nodeIndex, strIndex) with cumulative penalty
// ============================================================

namespace
{
    enum class KFGroup : uint8_t
    {
        A,
        KA,
        SA,
        TA,
        NA,
        HA,
        MA,
        YA,
        RA,
        WA,
        N_GROUPS
    };
    enum class KFDir : uint8_t
    {
        CENTER,
        LEFT,
        UP,
        RIGHT,
        DOWN,
        N_DIRS
    };
    struct KFPos
    {
        int x;
        int y;
    };
    struct KFKey
    {
        KFGroup g;
        KFDir d;
    };

    static inline int iabs(int x) { return x < 0 ? -x : x; }

    static KFPos kf_pos[(int)KFGroup::N_GROUPS] = {
        {0, 0},
        {1, 0},
        {2, 0},
        {0, 1},
        {1, 1},
        {2, 1},
        {0, 2},
        {1, 2},
        {2, 2},
        {0, 3},
    };

    static char16_t kf_table[(int)KFGroup::N_GROUPS][(int)KFDir::N_DIRS] = {
        // A
        {u'あ', u'い', u'う', u'え', u'お'},
        // KA
        {u'か', u'き', u'く', u'け', u'こ'},
        // SA
        {u'さ', u'し', u'す', u'せ', u'そ'},
        // TA
        {u'た', u'ち', u'つ', u'て', u'と'},
        // NA
        {u'な', u'に', u'ぬ', u'ね', u'の'},
        // HA
        {u'は', u'ひ', u'ふ', u'へ', u'ほ'},
        // MA
        {u'ま', u'み', u'む', u'め', u'も'},
        // YA（あなたの現状を踏襲）
        {u'や', u'（', u'ゆ', u'）', u'よ'},
        // RA
        {u'ら', u'り', u'る', u'れ', u'ろ'},
        // WA
        {u'わ', u'を', u'ん', u'ー', u'〜'},
    };

    static int manhattan(KFGroup a, KFGroup b)
    {
        const auto pa = kf_pos[(int)a];
        const auto pb = kf_pos[(int)b];
        return iabs(pa.x - pb.x) + iabs(pa.y - pb.y);
    }

    static std::once_flag kf_once;
    static std::unordered_map<char16_t, KFKey> kf_reverse;

    static void kf_init_once()
    {
        kf_reverse.reserve(128);
        for (int g = 0; g < (int)KFGroup::N_GROUPS; g++)
        {
            for (int d = 0; d < (int)KFDir::N_DIRS; d++)
            {
                const char16_t ch = kf_table[g][d];
                kf_reverse.emplace(ch, KFKey{(KFGroup)g, (KFDir)d});
            }
        }
    }

    static inline const KFKey *kf_key_of(char16_t ch)
    {
        std::call_once(kf_once, kf_init_once);
        auto it = kf_reverse.find(ch);
        if (it == kf_reverse.end())
            return nullptr;
        return &it->second;
    }

    static inline char16_t kf_char_of(KFGroup g, KFDir d)
    {
        return kf_table[(int)g][(int)d];
    }
} // namespace

std::vector<std::pair<char16_t, int>>
LOUDSReaderUtf16::getTypoVariationsKanaFlick(char16_t ch)
{
    // Kotlinの TypoCategory を拡張して「キー違い + 方向違い」も許可する版
    // - Exact(0)
    // - TapKeyInFlick(1)              : 同一キー内で方向違い
    // - DistanceNear(1)/Middle(2)/Far(7) : 同方向でキー違い
    // - Cross (distance + 1)          : 近距離キーで方向も違う（例: か->お は 2）

    std::vector<std::pair<char16_t, int>> out;
    out.reserve(1 + 4 + 16 + 32);

    out.push_back({ch, 0}); // Exact

    const KFKey *key = kf_key_of(ch);
    if (!key)
        return out;

    // ---- (1) 同一キー内: 方向ミス ----
    for (int d = 0; d < (int)KFDir::N_DIRS; d++)
    {
        if ((KFDir)d == key->d)
            continue;
        const char16_t v = kf_char_of(key->g, (KFDir)d);
        out.push_back({v, 1});
    }

    // ---- (2) 同方向: キー違い（従来） ----
    for (int g2 = 0; g2 < (int)KFGroup::N_GROUPS; g2++)
    {
        const KFGroup gg = (KFGroup)g2;
        if (gg == key->g)
            continue;

        const int dist = manhattan(key->g, gg);
        int pen = 0;
        if (dist == 1)
            pen = 1;
        else if (dist == 2)
            pen = 2;
        else
            pen = 7;

        const char16_t v = kf_char_of(gg, key->d);
        out.push_back({v, pen});
    }

    // ---- (3) ★追加: 近距離キーで「方向も自由」な置換 ----
    // 探索爆発を防ぐため dist<=2 のみ（必要なら 1 に絞ってもOK）
    for (int g2 = 0; g2 < (int)KFGroup::N_GROUPS; g2++)
    {
        const KFGroup gg = (KFGroup)g2;
        if (gg == key->g)
            continue;

        const int dist = manhattan(key->g, gg);
        if (dist > 2)
            continue;

        int distPen = (dist == 1) ? 1 : 2; // dist==2
        for (int d2 = 0; d2 < (int)KFDir::N_DIRS; d2++)
        {
            const KFDir dd = (KFDir)d2;
            if (dd == key->d)
                continue;         // 同方向は(2)で入っているので省略
            const int dirPen = 1; // 方向違い
            const int pen = distPen + dirPen;

            const char16_t v = kf_char_of(gg, dd);
            out.push_back({v, pen});
        }
    }

    // dedup by char, keep min penalty
    std::unordered_map<char16_t, int> best;
    best.reserve(out.size());
    for (auto &p : out)
    {
        auto it = best.find(p.first);
        if (it == best.end() || p.second < it->second)
            best[p.first] = p.second;
    }

    std::vector<std::pair<char16_t, int>> uniq;
    uniq.reserve(best.size());
    for (auto &kv : best)
        uniq.push_back({kv.first, kv.second});

    std::sort(uniq.begin(), uniq.end(), [](const auto &a, const auto &b)
              {
        if (a.second != b.second) return a.second < b.second;
        return a.first < b.first; });

    return uniq;
}

std::vector<LOUDSReaderUtf16::TypoSearchResult>
LOUDSReaderUtf16::commonPrefixSearchWithTypo(const std::u16string &str, int maxPenalty, int maxOut) const
{
    std::vector<TypoSearchResult> results;
    if (maxOut <= 0)
        return results;
    if (maxPenalty < 0)
        return results;

    struct State
    {
        int penalty = 0;
        int node = 0;
        size_t idx = 0;
        std::u16string built;
    };
    struct Cmp
    {
        bool operator()(const State &a, const State &b) const
        {
            return a.penalty > b.penalty; // min-heap
        }
    };

    // best[(node,idx)] = min penalty
    std::unordered_map<uint64_t, int> best;
    best.reserve(2048);

    auto pack_key = [](int node, size_t idx) -> uint64_t
    {
        return (uint64_t)(uint32_t)node << 32 | (uint64_t)(uint32_t)idx;
    };

    std::priority_queue<State, std::vector<State>, Cmp> pq;
    pq.push(State{0, 0, 0, std::u16string{}});

    while (!pq.empty() && (int)results.size() < maxOut)
    {
        State cur = std::move(pq.top());
        pq.pop();

        if (cur.node < 0)
            continue;
        if ((size_t)cur.node >= LBS_.size())
            continue;
        if (cur.penalty > maxPenalty)
            continue;

        const uint64_t k = pack_key(cur.node, cur.idx);
        auto it = best.find(k);
        if (it != best.end() && cur.penalty > it->second)
            continue;
        best[k] = cur.penalty;

        // leaf => accept current prefix
        if ((size_t)cur.node < isLeaf_.size() && isLeaf_.get((size_t)cur.node))
        {
            results.push_back(TypoSearchResult{cur.built, cur.penalty});
            // longer prefixも探索継続（commonPrefix用途）
        }

        if (cur.idx >= str.size())
            continue;

        const char16_t ch = str[cur.idx];
        const auto vars = getTypoVariationsKanaFlick(ch);

        for (const auto &vp : vars)
        {
            const char16_t vch = vp.first;
            const int addPen = vp.second;
            const int newPen = cur.penalty + addPen;
            if (newPen > maxPenalty)
                continue;

            const int next = traverse(cur.node, vch);
            if (next < 0)
                continue;

            State ns;
            ns.penalty = newPen;
            ns.node = next;
            ns.idx = cur.idx + 1;
            ns.built = cur.built;
            ns.built.push_back(vch);
            pq.push(std::move(ns));
        }
    }

    // dedup by yomi, keep min penalty
    std::vector<TypoSearchResult> dedup;
    dedup.reserve(results.size());

    for (auto &r : results)
    {
        bool found = false;
        for (auto &e : dedup)
        {
            if (e.yomi == r.yomi)
            {
                if (r.penaltyUsed < e.penaltyUsed)
                    e.penaltyUsed = r.penaltyUsed;
                found = true;
                break;
            }
        }
        if (!found)
            dedup.push_back(std::move(r));
    }

    std::sort(dedup.begin(), dedup.end(), [](const TypoSearchResult &a, const TypoSearchResult &b)
              {
        if (a.penaltyUsed != b.penaltyUsed) return a.penaltyUsed < b.penaltyUsed;
        if (a.yomi.size() != b.yomi.size()) return a.yomi.size() < b.yomi.size();
        return a.yomi < b.yomi; });

    return dedup;
}

// ============================================================
// Letter restore
// ============================================================

std::u16string LOUDSReaderUtf16::getLetter(int nodeIndex) const
{
    if (nodeIndex < 0)
        return u"";
    if (static_cast<size_t>(nodeIndex) >= LBS_.size())
        return u"";

    std::u16string out;
    int current = nodeIndex;

    while (true)
    {
        const int nodeId = lbsSucc_.rank1(current);
        if (nodeId < 0 || static_cast<size_t>(nodeId) >= labels_.size())
            break;

        const char16_t ch = labels_[static_cast<size_t>(nodeId)];
        if (ch != u' ')
            out.push_back(ch);

        if (nodeId == 0)
            break;

        const int r0 = lbsSucc_.rank0(current);
        current = lbsSucc_.select1(r0);
        if (current < 0)
            break;
    }

    std::reverse(out.begin(), out.end());
    return out;
}

// ============================================================
// Node lookup
// ============================================================

int LOUDSReaderUtf16::getNodeIndex(const std::u16string &s) const
{
    return search(2, s, 0);
}

int LOUDSReaderUtf16::getNodeId(const std::u16string &s) const
{
    const int idx = getNodeIndex(s);
    if (idx < 0)
        return -1;
    return lbsSucc_.rank0(idx);
}

int LOUDSReaderUtf16::search(int index, const std::u16string &chars, size_t wordOffset) const
{
    int currentIndex = index;
    if (chars.empty())
        return -1;
    if (currentIndex < 0)
        return -1;

    while (static_cast<size_t>(currentIndex) < LBS_.size() &&
           LBS_.get(static_cast<size_t>(currentIndex)))
    {
        if (wordOffset >= chars.size())
            return currentIndex;

        int charIndex = lbsSucc_.rank1(currentIndex);
        if (charIndex < 0 || static_cast<size_t>(charIndex) >= labels_.size())
            return -1;

        const char16_t currentLabel = labels_[static_cast<size_t>(charIndex)];
        const char16_t currentChar = chars[wordOffset];

        if (currentChar == currentLabel)
        {
            if (wordOffset + 1 == chars.size())
                return currentIndex;

            const int nextIndex = lbsSucc_.select0(charIndex) + 1;
            if (nextIndex < 0)
                return -1;
            return search(nextIndex, chars, wordOffset + 1);
        }

        currentIndex++;
    }
    return -1;
}

// ============================================================
// File I/O
// ============================================================

void LOUDSReaderUtf16::read_u64(std::istream &is, uint64_t &v)
{
    is.read(reinterpret_cast<char *>(&v), sizeof(v));
}

void LOUDSReaderUtf16::read_u16(std::istream &is, uint16_t &v)
{
    is.read(reinterpret_cast<char *>(&v), sizeof(v));
}

std::vector<uint64_t> LOUDSReaderUtf16::read_u64_vec(std::istream &is)
{
    uint64_t n = 0;
    read_u64(is, n);
    std::vector<uint64_t> v(static_cast<size_t>(n));
    if (n > 0)
    {
        is.read(reinterpret_cast<char *>(v.data()),
                static_cast<std::streamsize>(n * sizeof(uint64_t)));
    }
    return v;
}

BitVector LOUDSReaderUtf16::readBitVector(std::istream &is)
{
    uint64_t nbits = 0;
    read_u64(is, nbits);
    auto words = read_u64_vec(is);
    BitVector bv;
    bv.assign_from_words(static_cast<size_t>(nbits), std::move(words));
    return bv;
}

LOUDSReaderUtf16 LOUDSReaderUtf16::loadFromFile(const std::string &path)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs)
        throw std::runtime_error("failed to open file for read: " + path);

    BitVector lbs = readBitVector(ifs);
    BitVector isLeaf = readBitVector(ifs);

    uint64_t labelN = 0;
    read_u64(ifs, labelN);

    std::vector<char16_t> labels;
    labels.resize(static_cast<size_t>(labelN));

    for (size_t i = 0; i < static_cast<size_t>(labelN); ++i)
    {
        uint16_t v = 0;
        read_u16(ifs, v);
        labels[i] = static_cast<char16_t>(v);
    }

    return LOUDSReaderUtf16(lbs, isLeaf, std::move(labels));
}
