#include "PersonNameLexicon.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <charconv>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string_view>

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
        throw std::runtime_error("PersonNameLexicon: invalid UTF-8 input");
    }

    std::wstring result(static_cast<size_t>(required), L'\0');
    const int converted = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), &result[0], required);
    if (converted != required)
    {
        throw std::runtime_error("PersonNameLexicon: failed UTF-8 conversion");
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
    int value = 0;
    const auto parsed = std::from_chars(field.data(), field.data() + field.size(), value);
    if (parsed.ec != std::errc() || parsed.ptr != field.data() + field.size())
    {
        throw std::runtime_error(std::string("PersonNameLexicon: invalid int field: ") + name);
    }

    return value;
}

bool PathExists(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec) && !ec;
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
}

PersonNameLexicon::PersonNameLexicon(const std::filesystem::path& path)
{
    LoadFile(path);
}

DictionaryKind PersonNameLexicon::GetKind() const
{
    return DictionaryKind::PersonName;
}

void PersonNameLexicon::LookupPrefix(const std::wstring& reading, size_t start, std::vector<LexiconEntry>* out) const
{
    if (out == nullptr)
    {
        return;
    }

    out->clear();
    if (!_loaded || start >= reading.size())
    {
        return;
    }

    const size_t remaining = reading.size() - start;
    for (size_t length : _readingLengths)
    {
        if (length > remaining)
        {
            continue;
        }

        const std::wstring key = reading.substr(start, length);
        const auto it = _entriesByReading.find(key);
        if (it == _entriesByReading.end())
        {
            continue;
        }

        out->insert(out->end(), it->second.begin(), it->second.end());
    }
}

void PersonNameLexicon::LookupExact(const std::wstring& reading, std::vector<LexiconEntry>* out) const
{
    if (out == nullptr)
    {
        return;
    }

    out->clear();
    if (!_loaded)
    {
        return;
    }

    const auto it = _entriesByReading.find(reading);
    if (it == _entriesByReading.end())
    {
        return;
    }

    out->insert(out->end(), it->second.begin(), it->second.end());
}

bool PersonNameLexicon::IsLoaded() const
{
    return _loaded;
}

const std::filesystem::path& PersonNameLexicon::GetSourcePath() const
{
    return _sourcePath;
}

void PersonNameLexicon::LoadFile(const std::filesystem::path& path)
{
    _entriesByReading.clear();
    _readingLengths.clear();
    _sourcePath.clear();
    _loaded = false;

    if (path.empty() || !PathExists(path))
    {
        return;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("PersonNameLexicon: failed to open source file");
    }

    std::set<size_t> lengths;
    std::string line;
    while (std::getline(input, line))
    {
        if (line.empty())
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

        if (line.empty() || line[0] == '#')
        {
            continue;
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
        entry.wordCost = std::max(100, ParseIntField(costField, "cost") - 6000);
        entry.dictionaryKind = DictionaryKind::PersonName;
        entry.sourceEntryId = static_cast<std::uint32_t>(_entriesByReading.size());

        if (entry.reading.empty() || entry.surface.empty())
        {
            continue;
        }

        lengths.insert(entry.reading.size());
        _entriesByReading[entry.reading].push_back(std::move(entry));
    }

    if (_entriesByReading.empty())
    {
        return;
    }

    _readingLengths.assign(lengths.begin(), lengths.end());
    std::sort(_readingLengths.begin(), _readingLengths.end(), std::greater<size_t>());

    for (auto& pair : _entriesByReading)
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

    _sourcePath = NormalizePath(path);
    _loaded = true;
}
