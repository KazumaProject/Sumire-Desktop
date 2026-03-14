#include "MozcSystemLexicon.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace
{
std::wstring Utf8ToWide(const std::string& value)
{
    if (value.empty())
    {
        return L"";
    }

    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0)
    {
        throw std::runtime_error("MozcSystemLexicon: invalid UTF-8 input");
    }

    std::wstring result(static_cast<size_t>(required), L'\0');
    const int converted = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), &result[0], required);
    if (converted != required)
    {
        throw std::runtime_error("MozcSystemLexicon: failed UTF-8 conversion");
    }

    return result;
}

std::wstring U16ToWide(const std::u16string& value)
{
    std::wstring result;
    result.reserve(value.size());
    for (char16_t ch : value)
    {
        result.push_back(static_cast<wchar_t>(ch));
    }

    return result;
}

std::u16string WideToU16(const std::wstring& value)
{
    std::u16string result;
    result.reserve(value.size());
    for (wchar_t ch : value)
    {
        result.push_back(static_cast<char16_t>(ch));
    }

    return result;
}

std::wstring HiraganaToKatakana(const std::wstring& value)
{
    std::wstring result;
    result.reserve(value.size());

    for (wchar_t ch : value)
    {
        if ((ch >= 0x3041 && ch <= 0x3096) || (ch >= 0x309D && ch <= 0x309F))
        {
            result.push_back(static_cast<wchar_t>(ch + 0x60));
        }
        else
        {
            result.push_back(ch);
        }
    }

    return result;
}

bool SplitFiveColumns(
    std::string_view line,
    std::string_view* c0,
    std::string_view* c1,
    std::string_view* c2,
    std::string_view* c3,
    std::string_view* c4)
{
    const size_t p0 = line.find('\t');
    if (p0 == std::string_view::npos)
    {
        return false;
    }

    const size_t p1 = line.find('\t', p0 + 1);
    if (p1 == std::string_view::npos)
    {
        return false;
    }

    const size_t p2 = line.find('\t', p1 + 1);
    if (p2 == std::string_view::npos)
    {
        return false;
    }

    const size_t p3 = line.find('\t', p2 + 1);
    if (p3 == std::string_view::npos)
    {
        return false;
    }

    *c0 = line.substr(0, p0);
    *c1 = line.substr(p0 + 1, p1 - (p0 + 1));
    *c2 = line.substr(p1 + 1, p2 - (p1 + 1));
    *c3 = line.substr(p2 + 1, p3 - (p2 + 1));
    *c4 = line.substr(p3 + 1);
    return true;
}

int ParseIntField(std::string_view field, const char* name)
{
    if (field.empty())
    {
        throw std::runtime_error(std::string("MozcSystemLexicon: empty int field: ") + name);
    }

    int value = 0;
    const auto parsed = std::from_chars(field.data(), field.data() + field.size(), value);
    if (parsed.ec != std::errc() || parsed.ptr != field.data() + field.size())
    {
        throw std::runtime_error(std::string("MozcSystemLexicon: invalid int field: ") + name);
    }

    return value;
}

bool IsCommentOrEmpty(const std::string& line)
{
    if (line.empty())
    {
        return true;
    }

    if (line[0] == '#')
    {
        return true;
    }

    if (line.size() >= 3 &&
        static_cast<unsigned char>(line[0]) == 0xEF &&
        static_cast<unsigned char>(line[1]) == 0xBB &&
        static_cast<unsigned char>(line[2]) == 0xBF)
    {
        return line.size() == 3 || line[3] == '#';
    }

    return false;
}

bool PathExists(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec) && !ec;
}

bool IsDirectory(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::is_directory(path, ec) && !ec;
}

std::filesystem::path NormalizePath(const std::filesystem::path& path)
{
    std::error_code ec;
    const std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
    if (ec)
    {
        return path.lexically_normal();
    }

    return normalized;
}

class BitVector
{
public:
    size_t size() const
    {
        return _nbits;
    }

    bool get(size_t index) const
    {
        if (index >= _nbits)
        {
            return false;
        }

        return ((_words[index >> 6] >> (index & 63)) & 1ULL) != 0;
    }

    int rank0(int index) const
    {
        if (_nbits == 0 || index < 0)
        {
            return 0;
        }

        size_t current = static_cast<size_t>(index);
        if (current >= _nbits)
        {
            current = _nbits - 1;
        }

        return static_cast<int>(current + 1) - rank1Internal(current);
    }

    int rank1(int index) const
    {
        if (_nbits == 0 || index < 0)
        {
            return 0;
        }

        size_t current = static_cast<size_t>(index);
        if (current >= _nbits)
        {
            current = _nbits - 1;
        }

        return rank1Internal(current);
    }

    int select0(int ordinal) const
    {
        return selectInternal(false, ordinal);
    }

    int select1(int ordinal) const
    {
        return selectInternal(true, ordinal);
    }

    void assignFromWords(size_t nbits, std::vector<std::uint64_t> words)
    {
        _nbits = nbits;
        _words = std::move(words);
        if (((_nbits + 63) / 64) != _words.size())
        {
            throw std::runtime_error("MozcSystemLexicon: bit vector size mismatch");
        }
    }

private:
    static int Popcount64(std::uint64_t value)
    {
        int count = 0;
        while (value != 0)
        {
            value &= (value - 1);
            ++count;
        }

        return count;
    }

    int rank1Internal(size_t index) const
    {
        const size_t fullWords = index >> 6;
        const size_t bitInWord = index & 63;

        int count = 0;
        for (size_t word = 0; word < fullWords; ++word)
        {
            count += Popcount64(_words[word]);
        }

        const std::uint64_t mask = (bitInWord == 63) ? ~0ULL : ((1ULL << (bitInWord + 1)) - 1ULL);
        count += Popcount64(_words[fullWords] & mask);
        return count;
    }

    int selectInternal(bool value, int ordinal) const
    {
        if (ordinal <= 0)
        {
            return -1;
        }

        int count = 0;
        for (size_t index = 0; index < _nbits; ++index)
        {
            if (get(index) == value)
            {
                ++count;
                if (count == ordinal)
                {
                    return static_cast<int>(index);
                }
            }
        }

        return -1;
    }

    size_t _nbits = 0;
    std::vector<std::uint64_t> _words;
};

void ReadU64(std::istream& input, std::uint64_t* value)
{
    input.read(reinterpret_cast<char*>(value), sizeof(*value));
}

void ReadU32(std::istream& input, std::uint32_t* value)
{
    input.read(reinterpret_cast<char*>(value), sizeof(*value));
}

void ReadI32(std::istream& input, std::int32_t* value)
{
    input.read(reinterpret_cast<char*>(value), sizeof(*value));
}

void ReadU16(std::istream& input, std::uint16_t* value)
{
    input.read(reinterpret_cast<char*>(value), sizeof(*value));
}

BitVector ReadBitVector(std::istream& input)
{
    std::uint64_t nbits = 0;
    ReadU64(input, &nbits);

    std::uint64_t wordCount = 0;
    ReadU64(input, &wordCount);

    std::vector<std::uint64_t> words(static_cast<size_t>(wordCount));
    if (wordCount > 0)
    {
        input.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(wordCount * sizeof(std::uint64_t)));
    }

    BitVector bitVector;
    bitVector.assignFromWords(static_cast<size_t>(nbits), std::move(words));
    return bitVector;
}

struct TokenEntry
{
    std::uint16_t posIndex = 0;
    std::int16_t wordCost = 0;
    std::int32_t nodeIndex = 0;
};

class TokenArray
{
public:
    static constexpr std::int32_t kKatakanaSentinel = -1;
    static constexpr std::int32_t kHiraganaSentinel = -2;

    static TokenArray LoadFromFile(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error("MozcSystemLexicon: failed to open token array");
        }

        TokenArray result;

        std::uint32_t count = 0;
        ReadU32(input, &count);
        result._posIndex.resize(count);
        if (count > 0)
        {
            input.read(reinterpret_cast<char*>(result._posIndex.data()), static_cast<std::streamsize>(count * sizeof(std::uint16_t)));
        }

        ReadU32(input, &count);
        result._wordCost.resize(count);
        if (count > 0)
        {
            input.read(reinterpret_cast<char*>(result._wordCost.data()), static_cast<std::streamsize>(count * sizeof(std::int16_t)));
        }

        ReadU32(input, &count);
        result._nodeIndex.resize(count);
        if (count > 0)
        {
            input.read(reinterpret_cast<char*>(result._nodeIndex.data()), static_cast<std::streamsize>(count * sizeof(std::int32_t)));
        }

        result._postingsBits = ReadBitVector(input);
        return result;
    }

    std::vector<TokenEntry> GetTokensForTermId(std::int32_t termId) const
    {
        const int p0 = _postingsBits.select0(termId + 1);
        const int p1 = _postingsBits.select0(termId + 2);
        if (p0 < 0 || p1 < 0)
        {
            return {};
        }

        const int begin = _postingsBits.rank1(p0);
        const int end = _postingsBits.rank1(p1);

        std::vector<TokenEntry> result;
        result.reserve(static_cast<size_t>(std::max(0, end - begin)));
        for (int index = begin; index < end; ++index)
        {
            result.push_back(TokenEntry{
                _posIndex[static_cast<size_t>(index)],
                _wordCost[static_cast<size_t>(index)],
                _nodeIndex[static_cast<size_t>(index)]});
        }

        return result;
    }

private:
    std::vector<std::uint16_t> _posIndex;
    std::vector<std::int16_t> _wordCost;
    std::vector<std::int32_t> _nodeIndex;
    BitVector _postingsBits;
};

struct PosTable
{
    static PosTable LoadFromFile(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error("MozcSystemLexicon: failed to open pos table");
        }

        PosTable result;
        std::uint32_t count = 0;
        ReadU32(input, &count);

        result.leftIds.resize(count);
        result.rightIds.resize(count);

        if (count > 0)
        {
            input.read(reinterpret_cast<char*>(result.leftIds.data()), static_cast<std::streamsize>(count * sizeof(std::int16_t)));
            input.read(reinterpret_cast<char*>(result.rightIds.data()), static_cast<std::streamsize>(count * sizeof(std::int16_t)));
        }

        return result;
    }

    std::pair<std::int16_t, std::int16_t> GetLR(std::uint16_t posIndex) const
    {
        const size_t index = static_cast<size_t>(posIndex);
        if (index >= leftIds.size() || index >= rightIds.size())
        {
            return {0, 0};
        }

        return {leftIds[index], rightIds[index]};
    }

    std::vector<std::int16_t> leftIds;
    std::vector<std::int16_t> rightIds;
};

struct LoudsWithTermIdData
{
    static LoudsWithTermIdData LoadFromFile(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error("MozcSystemLexicon: failed to open yomi_termid");
        }

        LoudsWithTermIdData result;
        result.lbs = ReadBitVector(input);
        result.isLeaf = ReadBitVector(input);

        std::uint64_t labelCount = 0;
        ReadU64(input, &labelCount);
        result.labels.resize(static_cast<size_t>(labelCount));
        for (size_t i = 0; i < static_cast<size_t>(labelCount); ++i)
        {
            std::uint16_t value = 0;
            ReadU16(input, &value);
            result.labels[i] = static_cast<char16_t>(value);
        }

        std::uint64_t termCount = 0;
        ReadU64(input, &termCount);
        result.termIdByNodeId.resize(static_cast<size_t>(termCount));
        for (size_t i = 0; i < static_cast<size_t>(termCount); ++i)
        {
            ReadI32(input, &result.termIdByNodeId[i]);
        }

        return result;
    }

    BitVector lbs;
    BitVector isLeaf;
    std::vector<char16_t> labels;
    std::vector<std::int32_t> termIdByNodeId;
};

class LoudsWithTermIdReader
{
public:
    explicit LoudsWithTermIdReader(const LoudsWithTermIdData& trie)
        : _lbs(trie.lbs),
          _labels(trie.labels),
          _termIds(trie.termIdByNodeId)
    {
        _zeroPositions.push_back(-1);
        for (int index = 0; index < static_cast<int>(_lbs.size()); ++index)
        {
            if (!_lbs.get(static_cast<size_t>(index)))
            {
                _zeroPositions.push_back(index);
            }
        }
    }

    std::vector<std::pair<std::u16string, std::int32_t>> CommonPrefixSearch(const std::u16string& key) const
    {
        std::vector<std::pair<std::u16string, std::int32_t>> result;
        std::u16string current;
        int position = 0;

        for (char16_t ch : key)
        {
            position = Traverse(position, ch);
            if (position < 0)
            {
                break;
            }

            current.push_back(ch);

            const int nodeId = NodeIdFromPos(position);
            if (nodeId >= 0 && nodeId < static_cast<int>(_termIds.size()))
            {
                const std::int32_t termId = _termIds[static_cast<size_t>(nodeId)];
                if (termId >= 0)
                {
                    result.push_back({current, termId});
                }
            }
        }

        return result;
    }

private:
    int FirstChild(int position) const
    {
        const int rank = _lbs.rank1(position);
        if (rank <= 0 || rank >= static_cast<int>(_zeroPositions.size()))
        {
            return -1;
        }

        return _zeroPositions[static_cast<size_t>(rank)] + 1;
    }

    int Traverse(int position, char16_t target) const
    {
        int child = FirstChild(position);
        if (child < 0)
        {
            return -1;
        }

        while (child < static_cast<int>(_lbs.size()) && _lbs.get(static_cast<size_t>(child)))
        {
            const int labelIndex = _lbs.rank1(child);
            if (labelIndex >= 0 &&
                labelIndex < static_cast<int>(_labels.size()) &&
                _labels[static_cast<size_t>(labelIndex)] == target)
            {
                return child;
            }

            ++child;
        }

        return -1;
    }

    int NodeIdFromPos(int position) const
    {
        return _lbs.rank1(position) - 1;
    }

    const BitVector& _lbs;
    const std::vector<char16_t>& _labels;
    const std::vector<std::int32_t>& _termIds;
    std::vector<int> _zeroPositions;
};

class LoudsReaderUtf16
{
public:
    static LoudsReaderUtf16 LoadFromFile(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error("MozcSystemLexicon: failed to open tango louds");
        }

        LoudsReaderUtf16 result;
        result._lbs = ReadBitVector(input);
        result._isLeaf = ReadBitVector(input);

        std::uint64_t labelCount = 0;
        ReadU64(input, &labelCount);
        result._labels.resize(static_cast<size_t>(labelCount));
        for (size_t i = 0; i < static_cast<size_t>(labelCount); ++i)
        {
            std::uint16_t value = 0;
            ReadU16(input, &value);
            result._labels[i] = static_cast<char16_t>(value);
        }

        return result;
    }

    std::u16string GetLetter(std::int32_t nodeIndex) const
    {
        if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= _lbs.size())
        {
            return u"";
        }

        std::u16string result;
        int current = nodeIndex;

        while (true)
        {
            const int nodeId = _lbs.rank1(current);
            if (nodeId < 0 || nodeId >= static_cast<int>(_labels.size()))
            {
                break;
            }

            const char16_t ch = _labels[static_cast<size_t>(nodeId)];
            if (ch != u' ')
            {
                result.push_back(ch);
            }

            if (nodeId == 0)
            {
                break;
            }

            const int rank0 = _lbs.rank0(current);
            current = _lbs.select1(rank0);
            if (current < 0)
            {
                break;
            }
        }

        std::reverse(result.begin(), result.end());
        return result;
    }

private:
    BitVector _lbs;
    BitVector _isLeaf;
    std::vector<char16_t> _labels;
};
}

struct MozcSystemLexicon::Impl
{
    struct BinaryDictionary
    {
        LoudsWithTermIdData yomiTermData;
        LoudsWithTermIdReader yomiTermReader;
        LoudsReaderUtf16 tangoReader;
        TokenArray tokenArray;
        PosTable posTable;
        mutable std::unordered_map<std::wstring, std::vector<LexiconEntry>> cache;

        BinaryDictionary(
            LoudsWithTermIdData data,
            LoudsReaderUtf16 tango,
            TokenArray tokens,
            PosTable pos)
            : yomiTermData(std::move(data)),
              yomiTermReader(yomiTermData),
              tangoReader(std::move(tango)),
              tokenArray(std::move(tokens)),
              posTable(std::move(pos))
        {
        }
    };

    std::unordered_map<std::wstring, std::vector<LexiconEntry>> entriesByReading;
    std::vector<size_t> readingLengths;
    std::filesystem::path sourceDirectory;
    std::unique_ptr<BinaryDictionary> binary;
    bool loaded = false;
};

MozcSystemLexicon::MozcSystemLexicon(const std::filesystem::path& directory)
    : _impl(std::make_unique<Impl>())
{
    LoadDirectory(directory);
}

DictionaryKind MozcSystemLexicon::GetKind() const
{
    return DictionaryKind::System;
}

void MozcSystemLexicon::LookupPrefix(const std::wstring& reading, size_t start, std::vector<LexiconEntry>* out) const
{
    if (out == nullptr)
    {
        return;
    }

    out->clear();
    if (!_impl->loaded || start >= reading.size())
    {
        return;
    }

    if (_impl->binary)
    {
        const std::u16string remaining = WideToU16(reading.substr(start));
        const auto matches = _impl->binary->yomiTermReader.CommonPrefixSearch(remaining);
        for (const auto& match : matches)
        {
            const std::wstring key = U16ToWide(match.first);
            auto cached = _impl->binary->cache.find(key);
            if (cached == _impl->binary->cache.end())
            {
                std::vector<LexiconEntry> entries;
                const std::vector<TokenEntry> tokens = _impl->binary->tokenArray.GetTokensForTermId(match.second);
                entries.reserve(tokens.size());

                for (size_t index = 0; index < tokens.size(); ++index)
                {
                    const TokenEntry& token = tokens[index];
                    LexiconEntry entry;
                    entry.reading = key;
                    entry.wordCost = token.wordCost;
                    entry.posId = token.posIndex;

                    const auto lr = _impl->binary->posTable.GetLR(token.posIndex);
                    entry.leftId = lr.first;
                    entry.rightId = lr.second;
                    entry.dictionaryKind = DictionaryKind::System;
                    entry.sourceEntryId = static_cast<std::uint32_t>((static_cast<std::uint64_t>(match.second) << 16) | static_cast<std::uint64_t>(index));

                    if (token.nodeIndex == TokenArray::kHiraganaSentinel)
                    {
                        entry.surface = key;
                    }
                    else if (token.nodeIndex == TokenArray::kKatakanaSentinel)
                    {
                        entry.surface = HiraganaToKatakana(key);
                    }
                    else
                    {
                        entry.surface = U16ToWide(_impl->binary->tangoReader.GetLetter(token.nodeIndex));
                    }

                    if (!entry.surface.empty())
                    {
                        entries.push_back(std::move(entry));
                    }
                }

                std::sort(
                    entries.begin(),
                    entries.end(),
                    [](const LexiconEntry& lhs, const LexiconEntry& rhs)
                    {
                        if (lhs.wordCost != rhs.wordCost)
                        {
                            return lhs.wordCost < rhs.wordCost;
                        }

                        return lhs.surface < rhs.surface;
                    });

                cached = _impl->binary->cache.emplace(key, std::move(entries)).first;
            }

            out->insert(out->end(), cached->second.begin(), cached->second.end());
        }

        return;
    }

    const size_t remaining = reading.size() - start;
    for (size_t length : _impl->readingLengths)
    {
        if (length > remaining)
        {
            continue;
        }

        const std::wstring key = reading.substr(start, length);
        const auto it = _impl->entriesByReading.find(key);
        if (it == _impl->entriesByReading.end())
        {
            continue;
        }

        out->insert(out->end(), it->second.begin(), it->second.end());
    }
}

void MozcSystemLexicon::LookupExact(const std::wstring& reading, std::vector<LexiconEntry>* out) const
{
    if (out == nullptr)
    {
        return;
    }

    out->clear();
    if (!_impl->loaded)
    {
        return;
    }

    if (_impl->binary)
    {
        std::vector<LexiconEntry> entries;
        LookupPrefix(reading, 0, &entries);
        for (const LexiconEntry& entry : entries)
        {
            if (entry.reading == reading)
            {
                out->push_back(entry);
            }
        }

        return;
    }

    const auto it = _impl->entriesByReading.find(reading);
    if (it == _impl->entriesByReading.end())
    {
        return;
    }

    out->insert(out->end(), it->second.begin(), it->second.end());
}

bool MozcSystemLexicon::IsLoaded() const
{
    return _impl->loaded;
}

const std::filesystem::path& MozcSystemLexicon::GetSourceDirectory() const
{
    return _impl->sourceDirectory;
}

void MozcSystemLexicon::LoadDirectory(const std::filesystem::path& directory)
{
    _impl->entriesByReading.clear();
    _impl->readingLengths.clear();
    _impl->sourceDirectory.clear();
    _impl->binary.reset();
    _impl->loaded = false;

    if (directory.empty() || !PathExists(directory) || !IsDirectory(directory))
    {
        return;
    }

    const std::filesystem::path yomiPath = directory / L"yomi_termid.louds";
    const std::filesystem::path tangoPath = directory / L"tango.louds";
    const std::filesystem::path tokenPath = directory / L"token_array.bin";
    const std::filesystem::path posPath = directory / L"pos_table.bin";

    if (PathExists(yomiPath) && PathExists(tangoPath) && PathExists(tokenPath) && PathExists(posPath))
    {
        LoudsWithTermIdData yomi = LoudsWithTermIdData::LoadFromFile(yomiPath);
        LoudsReaderUtf16 tango = LoudsReaderUtf16::LoadFromFile(tangoPath);
        TokenArray tokens = TokenArray::LoadFromFile(tokenPath);
        PosTable pos = PosTable::LoadFromFile(posPath);

        _impl->binary = std::make_unique<Impl::BinaryDictionary>(std::move(yomi), std::move(tango), std::move(tokens), std::move(pos));
        _impl->sourceDirectory = NormalizePath(directory);
        _impl->loaded = true;
        return;
    }

    std::set<size_t> lengths;
    for (int index = 0; index <= 9; ++index)
    {
        wchar_t fileName[32] = {};
        swprintf_s(fileName, L"dictionary%02d.txt", index);

        const std::filesystem::path filePath = directory / fileName;
        if (!PathExists(filePath))
        {
            return;
        }

        std::ifstream input(filePath, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error("MozcSystemLexicon: failed to open dictionary file");
        }

        std::string line;
        while (std::getline(input, line))
        {
            if (IsCommentOrEmpty(line))
            {
                continue;
            }

            if (line.size() >= 3 &&
                static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF)
            {
                line.erase(0, 3);
            }

            std::string_view readingField;
            std::string_view leftField;
            std::string_view rightField;
            std::string_view costField;
            std::string_view surfaceField;

            if (!SplitFiveColumns(line, &readingField, &leftField, &rightField, &costField, &surfaceField))
            {
                continue;
            }

            LexiconEntry entry;
            entry.reading = Utf8ToWide(std::string(readingField));
            entry.surface = Utf8ToWide(std::string(surfaceField));
            entry.leftId = ParseIntField(leftField, "left_id");
            entry.rightId = ParseIntField(rightField, "right_id");
            entry.wordCost = ParseIntField(costField, "cost");
            entry.dictionaryKind = DictionaryKind::System;
            entry.sourceEntryId = static_cast<std::uint32_t>(_impl->entriesByReading.size());

            if (entry.reading.empty() || entry.surface.empty())
            {
                continue;
            }

            lengths.insert(entry.reading.size());
            _impl->entriesByReading[entry.reading].push_back(std::move(entry));
        }
    }

    if (_impl->entriesByReading.empty())
    {
        return;
    }

    _impl->readingLengths.assign(lengths.begin(), lengths.end());
    std::sort(_impl->readingLengths.begin(), _impl->readingLengths.end(), std::greater<size_t>());

    for (auto& pair : _impl->entriesByReading)
    {
        std::sort(
            pair.second.begin(),
            pair.second.end(),
            [](const LexiconEntry& lhs, const LexiconEntry& rhs)
            {
                if (lhs.wordCost != rhs.wordCost)
                {
                    return lhs.wordCost < rhs.wordCost;
                }

                return lhs.surface < rhs.surface;
            });
    }

    _impl->sourceDirectory = NormalizePath(directory);
    _impl->loaded = true;
}
