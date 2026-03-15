#include "PersonNameLexicon.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string_view>

namespace
{
constexpr char kBinaryMagic[] = {'S', 'P', 'N', 'L', 'E', 'X', '1', '\0'};

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

void FinalizeEntries(
    std::unordered_map<std::wstring, std::vector<LexiconEntry>>* entriesByReading,
    std::vector<size_t>* readingLengths)
{
    std::set<size_t> lengths;
    for (const auto& pair : *entriesByReading)
    {
        lengths.insert(pair.first.size());
    }

    readingLengths->assign(lengths.begin(), lengths.end());
    std::sort(readingLengths->begin(), readingLengths->end(), std::greater<size_t>());

    for (auto& pair : *entriesByReading)
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
}

bool LoadTextEntries(
    const std::filesystem::path& path,
    std::unordered_map<std::wstring, std::vector<LexiconEntry>>* entriesByReading,
    std::vector<size_t>* readingLengths,
    std::wstring* errorMessage)
{
    if (entriesByReading == nullptr || readingLengths == nullptr)
    {
        return false;
    }

    entriesByReading->clear();
    readingLengths->clear();

    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = L"Failed to open source text dictionary.";
        }
        return false;
    }

    std::uint32_t nextEntryId = 0;
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
        entry.sourceEntryId = nextEntryId++;

        if (entry.reading.empty() || entry.surface.empty())
        {
            continue;
        }

        (*entriesByReading)[entry.reading].push_back(std::move(entry));
    }

    if (entriesByReading->empty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = L"No valid entries were found in the source text dictionary.";
        }
        return false;
    }

    FinalizeEntries(entriesByReading, readingLengths);
    return true;
}

void WriteUInt32(std::ofstream& output, std::uint32_t value)
{
    output.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

bool ReadUInt32(std::ifstream& input, std::uint32_t* value)
{
    input.read(reinterpret_cast<char*>(value), sizeof(*value));
    return static_cast<bool>(input);
}

void WriteWString(std::ofstream& output, const std::wstring& value)
{
    const std::uint32_t length = static_cast<std::uint32_t>(value.size());
    WriteUInt32(output, length);
    if (length != 0)
    {
        output.write(
            reinterpret_cast<const char*>(value.data()),
            static_cast<std::streamsize>(length * sizeof(wchar_t)));
    }
}

bool ReadWString(std::ifstream& input, std::wstring* value)
{
    std::uint32_t length = 0;
    if (!ReadUInt32(input, &length))
    {
        return false;
    }

    value->assign(length, L'\0');
    if (length != 0)
    {
        input.read(
            reinterpret_cast<char*>(&(*value)[0]),
            static_cast<std::streamsize>(length * sizeof(wchar_t)));
        if (!input)
        {
            return false;
        }
    }

    return true;
}
}

PersonNameLexicon::PersonNameLexicon(const std::filesystem::path& path)
{
    LoadFile(path);
}

bool PersonNameLexicon::BuildBinaryFromText(
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& outputPath,
    std::wstring* errorMessage)
{
    std::unordered_map<std::wstring, std::vector<LexiconEntry>> entriesByReading;
    std::vector<size_t> readingLengths;
    if (!LoadTextEntries(sourcePath, &entriesByReading, &readingLengths, errorMessage))
    {
        return false;
    }

    std::error_code ec;
    const std::filesystem::path parentPath = outputPath.parent_path();
    if (!parentPath.empty())
    {
        std::filesystem::create_directories(parentPath, ec);
    }

    if (ec)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = L"Failed to create output directory.";
        }
        return false;
    }

    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = L"Failed to open output binary dictionary.";
        }
        return false;
    }

    output.write(kBinaryMagic, sizeof(kBinaryMagic));

    std::vector<std::wstring> readings;
    readings.reserve(entriesByReading.size());
    std::uint32_t entryCount = 0;
    for (const auto& pair : entriesByReading)
    {
        readings.push_back(pair.first);
        entryCount += static_cast<std::uint32_t>(pair.second.size());
    }
    std::sort(readings.begin(), readings.end());

    WriteUInt32(output, entryCount);
    for (const std::wstring& reading : readings)
    {
        const auto it = entriesByReading.find(reading);
        if (it == entriesByReading.end())
        {
            continue;
        }

        for (const LexiconEntry& entry : it->second)
        {
            WriteWString(output, entry.reading);
            WriteWString(output, entry.surface);
            output.write(reinterpret_cast<const char*>(&entry.leftId), sizeof(entry.leftId));
            output.write(reinterpret_cast<const char*>(&entry.rightId), sizeof(entry.rightId));
            output.write(reinterpret_cast<const char*>(&entry.wordCost), sizeof(entry.wordCost));
            output.write(reinterpret_cast<const char*>(&entry.posId), sizeof(entry.posId));
            output.write(reinterpret_cast<const char*>(&entry.sourceEntryId), sizeof(entry.sourceEntryId));
        }
    }

    if (!output)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = L"Failed while writing the binary dictionary.";
        }
        return false;
    }

    return true;
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

    std::ifstream probe(path, std::ios::binary);
    if (!probe)
    {
        throw std::runtime_error("PersonNameLexicon: failed to open source file");
    }

    char magic[sizeof(kBinaryMagic)] = {};
    probe.read(magic, sizeof(magic));
    probe.close();

    if (std::memcmp(magic, kBinaryMagic, sizeof(kBinaryMagic)) == 0)
    {
        LoadBinaryFile(path);
        return;
    }

    LoadTextFile(path);
}

void PersonNameLexicon::LoadTextFile(const std::filesystem::path& path)
{
    std::wstring errorMessage;
    if (!LoadTextEntries(path, &_entriesByReading, &_readingLengths, &errorMessage))
    {
        return;
    }

    _sourcePath = NormalizePath(path);
    _loaded = true;
}

void PersonNameLexicon::LoadBinaryFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("PersonNameLexicon: failed to open source file");
    }

    char magic[sizeof(kBinaryMagic)] = {};
    input.read(magic, sizeof(magic));
    if (!input || std::memcmp(magic, kBinaryMagic, sizeof(kBinaryMagic)) != 0)
    {
        throw std::runtime_error("PersonNameLexicon: invalid binary dictionary");
    }

    std::uint32_t entryCount = 0;
    if (!ReadUInt32(input, &entryCount))
    {
        throw std::runtime_error("PersonNameLexicon: invalid binary header");
    }

    for (std::uint32_t index = 0; index < entryCount; ++index)
    {
        LexiconEntry entry;
        if (!ReadWString(input, &entry.reading) ||
            !ReadWString(input, &entry.surface))
        {
            throw std::runtime_error("PersonNameLexicon: invalid binary string entry");
        }

        input.read(reinterpret_cast<char*>(&entry.leftId), sizeof(entry.leftId));
        input.read(reinterpret_cast<char*>(&entry.rightId), sizeof(entry.rightId));
        input.read(reinterpret_cast<char*>(&entry.wordCost), sizeof(entry.wordCost));
        input.read(reinterpret_cast<char*>(&entry.posId), sizeof(entry.posId));
        input.read(reinterpret_cast<char*>(&entry.sourceEntryId), sizeof(entry.sourceEntryId));
        if (!input)
        {
            throw std::runtime_error("PersonNameLexicon: invalid binary payload");
        }

        entry.dictionaryKind = DictionaryKind::PersonName;
        if (entry.reading.empty() || entry.surface.empty())
        {
            continue;
        }

        _entriesByReading[entry.reading].push_back(std::move(entry));
    }

    if (_entriesByReading.empty())
    {
        return;
    }

    FinalizeEntries(&_entriesByReading, &_readingLengths);
    _sourcePath = NormalizePath(path);
    _loaded = true;
}
