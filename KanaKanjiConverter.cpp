#include "KanaKanjiConverter.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <limits>
#include <memory>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <cmath>
#include <utility>
#include <vector>

#include "BuiltInSystemLexicon.h"
#include "ILexicon.h"
#include "LexiconRegistry.h"
#include "MozcSystemLexicon.h"
#include "PersonNameLexicon.h"

namespace
{
void AppendUnique(std::vector<std::wstring>* candidates, const std::wstring& candidate)
{
    if (candidate.empty())
    {
        return;
    }

    for (const std::wstring& existing : *candidates)
    {
        if (existing == candidate)
        {
            return;
        }
    }

    candidates->push_back(candidate);
}

struct PathStep
{
    size_t start = 0;
    int bestCost = 0;
    int prevEnd = -1;
    int prevIndex = -1;
    LexiconEntry entry;
};

LexiconEntry MakeUnknownEntry(const std::wstring& reading, size_t start)
{
    LexiconEntry entry;
    entry.reading = reading.substr(start, 1);
    entry.surface = entry.reading;
    entry.wordCost = 10000;
    entry.dictionaryKind = DictionaryKind::System;
    return entry;
}

std::vector<std::wstring> BuildAlternatives(
    const LexiconRegistry& lexicons,
    const std::wstring& reading,
    const std::wstring& bestSurface)
{
    std::vector<std::wstring> alternatives;
    std::vector<LexiconEntry> entries;
    lexicons.LookupExact(reading, &entries);

    AppendUnique(&alternatives, bestSurface);

    for (const LexiconEntry& entry : entries)
    {
        AppendUnique(&alternatives, entry.surface);
    }

    AppendUnique(&alternatives, reading);
    return alternatives;
}

bool LoadConnectionMatrix(
    const std::filesystem::path& path,
    std::vector<std::int16_t>* values,
    int* dimension)
{
    if (values == nullptr || dimension == nullptr)
    {
        return false;
    }

    values->clear();
    *dimension = 0;

    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return false;
    }

    input.seekg(0, std::ios::end);
    const std::streamoff byteCount = input.tellg();
    input.seekg(0, std::ios::beg);
    if (byteCount <= 0 || (byteCount % 2) != 0)
    {
        return false;
    }

    const size_t shortCount = static_cast<size_t>(byteCount / 2);
    const int dim = static_cast<int>(std::sqrt(static_cast<double>(shortCount)) + 0.5);
    if (dim <= 0 || static_cast<size_t>(dim) * static_cast<size_t>(dim) != shortCount)
    {
        return false;
    }

    values->resize(shortCount);
    for (size_t index = 0; index < shortCount; ++index)
    {
        unsigned char bytes[2] = {};
        input.read(reinterpret_cast<char*>(bytes), 2);
        if (!input)
        {
            values->clear();
            *dimension = 0;
            return false;
        }

        const std::uint16_t raw = static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[0]) << 8) | bytes[1]);
        (*values)[index] = static_cast<std::int16_t>(raw);
    }

    *dimension = dim;
    return true;
}

int GetConnectionCost(
    const std::vector<std::int16_t>& matrix,
    int dimension,
    int leftId,
    int rightId)
{
    if (dimension <= 0 || matrix.empty())
    {
        return 0;
    }

    if (leftId < 0 || rightId < 0 || leftId >= dimension || rightId >= dimension)
    {
        return 0;
    }

    return matrix[static_cast<size_t>(leftId) * static_cast<size_t>(dimension) + static_cast<size_t>(rightId)];
}

bool IsRawReadingCandidate(const LexiconEntry& entry)
{
    return entry.surface == entry.reading && entry.reading.size() > 1;
}

bool IsParticleReading(const std::wstring& reading)
{
    return reading == L"\u306f" ||
        reading == L"\u304c" ||
        reading == L"\u3092" ||
        reading == L"\u306b" ||
        reading == L"\u3067" ||
        reading == L"\u3068" ||
        reading == L"\u3078" ||
        reading == L"\u3082" ||
        reading == L"\u3084" ||
        reading == L"\u306e" ||
        reading == L"\u304b";
}

bool StartsWithParticleReading(const std::wstring& reading)
{
    if (reading.size() <= 1)
    {
        return false;
    }

    const std::wstring head(1, reading[0]);
    return IsParticleReading(head);
}

bool IsKatakanaFallbackCandidate(const LexiconEntry& entry)
{
    if (entry.reading.empty() || entry.surface.size() != entry.reading.size())
    {
        return false;
    }

    for (size_t index = 0; index < entry.reading.size(); ++index)
    {
        const wchar_t hira = entry.reading[index];
        const wchar_t kata = entry.surface[index];
        if ((hira >= 0x3041 && hira <= 0x3096) || (hira >= 0x309D && hira <= 0x309F))
        {
            if (kata != static_cast<wchar_t>(hira + 0x60))
            {
                return false;
            }
        }
        else if (kata != hira)
        {
            return false;
        }
    }

    return true;
}

int GetAdjustedWordCost(const LexiconEntry& entry)
{
    int cost = entry.wordCost;
    if (IsRawReadingCandidate(entry))
    {
        cost += 2500;
    }

    if (IsKatakanaFallbackCandidate(entry))
    {
        cost += 1500;
    }

    if (IsParticleReading(entry.reading) && entry.surface != entry.reading)
    {
        cost += 2500;
    }

    if (entry.dictionaryKind == DictionaryKind::PersonName && StartsWithParticleReading(entry.reading))
    {
        cost += 4500;
    }

    return cost;
}

std::wstring ReadEnvVar(const wchar_t* name)
{
    size_t required = 0;
    _wgetenv_s(&required, nullptr, 0, name);
    if (required == 0)
    {
        return L"";
    }

    std::wstring value(required, L'\0');
    _wgetenv_s(&required, &value[0], value.size(), name);
    if (!value.empty() && value.back() == L'\0')
    {
        value.pop_back();
    }

    return value;
}

std::filesystem::path GetModuleDirectory()
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&GetModuleDirectory),
        &module))
    {
        return std::filesystem::current_path();
    }

    std::wstring path(MAX_PATH, L'\0');
    DWORD length = 0;
    for (;;)
    {
        length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0)
        {
            return std::filesystem::current_path();
        }

        if (length < path.size() - 1)
        {
            path.resize(length);
            break;
        }

        path.resize(path.size() * 2);
    }

    return std::filesystem::path(path).parent_path();
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

void AppendDirectoryVariants(std::vector<std::filesystem::path>* out, const std::filesystem::path& base)
{
    if (base.empty())
    {
        return;
    }

    std::filesystem::path current = base;
    for (int depth = 0; depth < 5; ++depth)
    {
        out->push_back(current / L"dictionaries" / L"mozc");
        out->push_back(current / L"mozc");
        out->push_back(current / L"mozc_fetch");

        if (!current.has_parent_path())
        {
            break;
        }

        const std::filesystem::path parent = current.parent_path();
        if (parent == current)
        {
            break;
        }

        current = parent;
    }
}

std::vector<std::filesystem::path> GetMozcDictionaryDirectories()
{
    std::vector<std::filesystem::path> candidates;

    const std::wstring envDir = ReadEnvVar(L"SUMIRE_MOZC_DICT_DIR");
    if (!envDir.empty())
    {
        candidates.push_back(std::filesystem::path(envDir));
    }

    AppendDirectoryVariants(&candidates, GetModuleDirectory());
    AppendDirectoryVariants(&candidates, std::filesystem::current_path());

    std::vector<std::filesystem::path> unique;
    for (const std::filesystem::path& candidate : candidates)
    {
        if (candidate.empty())
        {
            continue;
        }

        bool exists = false;
        const std::filesystem::path normalizedCandidate = NormalizePath(candidate);
        for (const std::filesystem::path& current : unique)
        {
            if (NormalizePath(current) == normalizedCandidate)
            {
                exists = true;
                break;
            }
        }

        if (!exists)
        {
            unique.push_back(candidate);
        }
    }

    return unique;
}

void AppendPersonFileVariants(std::vector<std::filesystem::path>* out, const std::filesystem::path& base)
{
    if (base.empty())
    {
        return;
    }

    std::filesystem::path current = base;
    for (int depth = 0; depth < 5; ++depth)
    {
        out->push_back(current / L"dictionaries" / L"names" / L"filtered_names.txt");
        out->push_back(current / L"dictionaries" / L"filtered_names.txt");
        out->push_back(current / L"filtered_names.txt");

        if (!current.has_parent_path())
        {
            break;
        }

        const std::filesystem::path parent = current.parent_path();
        if (parent == current)
        {
            break;
        }

        current = parent;
    }
}

std::vector<std::filesystem::path> GetPersonNameDictionaryFiles()
{
    std::vector<std::filesystem::path> candidates;

    const std::wstring envFile = ReadEnvVar(L"SUMIRE_PERSON_NAMES_PATH");
    if (!envFile.empty())
    {
        candidates.push_back(std::filesystem::path(envFile));
    }

    AppendPersonFileVariants(&candidates, GetModuleDirectory());
    AppendPersonFileVariants(&candidates, std::filesystem::current_path());

    const std::wstring userProfile = ReadEnvVar(L"USERPROFILE");
    if (!userProfile.empty())
    {
        candidates.push_back(std::filesystem::path(userProfile) / L"Downloads" / L"filtered_names.txt");
    }

    std::vector<std::filesystem::path> unique;
    for (const std::filesystem::path& candidate : candidates)
    {
        if (candidate.empty())
        {
            continue;
        }

        bool exists = false;
        const std::filesystem::path normalizedCandidate = NormalizePath(candidate);
        for (const std::filesystem::path& current : unique)
        {
            if (NormalizePath(current) == normalizedCandidate)
            {
                exists = true;
                break;
            }
        }

        if (!exists)
        {
            unique.push_back(candidate);
        }
    }

    return unique;
}
}

KanaKanjiConverter::KanaKanjiConverter()
    : _lexicons(std::make_shared<LexiconRegistry>())
{
    for (const std::filesystem::path& directory : GetMozcDictionaryDirectories())
    {
        std::shared_ptr<MozcSystemLexicon> mozcLexicon = std::make_shared<MozcSystemLexicon>(directory);
        if (mozcLexicon->IsLoaded())
        {
            AddLexicon(mozcLexicon);
            LoadConnectionMatrix(directory / L"connection_single_column.bin", &_connectionMatrix, &_connectionDimension);
            break;
        }
    }

    for (const std::filesystem::path& path : GetPersonNameDictionaryFiles())
    {
        std::shared_ptr<PersonNameLexicon> personLexicon = std::make_shared<PersonNameLexicon>(path);
        if (personLexicon->IsLoaded())
        {
            AddLexicon(personLexicon);
            break;
        }
    }

    AddLexicon(std::make_shared<BuiltInSystemLexicon>());
}

void KanaKanjiConverter::AddLexicon(const std::shared_ptr<ILexicon>& lexicon)
{
    _lexicons->AddLexicon(lexicon);
}

ConversionResult KanaKanjiConverter::Convert(const std::wstring& reading) const
{
    ConversionResult result;
    if (reading.empty())
    {
        return result;
    }

    const int kInf = std::numeric_limits<int>::max() / 8;
    const size_t length = reading.size();

    std::vector<LexiconEntry> entries;
    std::vector<std::vector<PathStep>> lattice(length + 1);

    for (size_t start = 0; start < length; ++start)
    {
        entries.clear();
        _lexicons->LookupPrefix(reading, start, &entries);
        if (entries.empty())
        {
            entries.push_back(MakeUnknownEntry(reading, start));
        }

        for (const LexiconEntry& entry : entries)
        {
            const size_t entryLength = entry.reading.size();
            if (entryLength == 0 || start + entryLength > length)
            {
                continue;
            }

            const size_t end = start + entryLength;
            PathStep node;
            node.start = start;
            node.bestCost = kInf;
            node.prevEnd = -1;
            node.prevIndex = -1;
            node.entry = entry;
            lattice[end].push_back(std::move(node));
        }
    }

    for (size_t end = 1; end <= length; ++end)
    {
        std::vector<PathStep>& nodes = lattice[end];
        for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
        {
            PathStep& node = nodes[nodeIndex];
            const int wordCost = GetAdjustedWordCost(node.entry);

            if (node.start == 0)
            {
                node.bestCost = wordCost + GetConnectionCost(_connectionMatrix, _connectionDimension, 0, node.entry.rightId);
                continue;
            }

            const std::vector<PathStep>& previousNodes = lattice[node.start];
            for (size_t prevIndex = 0; prevIndex < previousNodes.size(); ++prevIndex)
            {
                const PathStep& previous = previousNodes[prevIndex];
                if (previous.bestCost >= kInf)
                {
                    continue;
                }

                const int edgeCost = GetConnectionCost(_connectionMatrix, _connectionDimension, previous.entry.leftId, node.entry.rightId);
                const int candidateCost = previous.bestCost + edgeCost + wordCost;
                if (candidateCost < node.bestCost)
                {
                    node.bestCost = candidateCost;
                    node.prevEnd = static_cast<int>(node.start);
                    node.prevIndex = static_cast<int>(prevIndex);
                }
            }
        }
    }

    int bestTerminalIndex = -1;
    int bestTerminalCost = kInf;
    for (size_t nodeIndex = 0; nodeIndex < lattice[length].size(); ++nodeIndex)
    {
        const PathStep& node = lattice[length][nodeIndex];
        if (node.bestCost >= kInf)
        {
            continue;
        }

        const int totalCost = node.bestCost + GetConnectionCost(_connectionMatrix, _connectionDimension, node.entry.leftId, 0);
        if (totalCost < bestTerminalCost)
        {
            bestTerminalCost = totalCost;
            bestTerminalIndex = static_cast<int>(nodeIndex);
        }
    }

    if (bestTerminalIndex < 0)
    {
        return result;
    }

    ConversionCandidate bestCandidate;
    bestCandidate.reading = reading;
    bestCandidate.totalCost = bestTerminalCost;

    std::vector<BunsetsuConversion> bunsetsuReversed;
    for (int end = static_cast<int>(length), nodeIndex = bestTerminalIndex; end > 0 && nodeIndex >= 0;)
    {
        const PathStep& step = lattice[static_cast<size_t>(end)][static_cast<size_t>(nodeIndex)];

        BunsetsuConversion bunsetsu;
        bunsetsu.start = static_cast<int>(step.start);
        bunsetsu.length = end - static_cast<int>(step.start);
        bunsetsu.reading = step.entry.reading;
        bunsetsu.surface = step.entry.surface;
        bunsetsu.alternatives = BuildAlternatives(*_lexicons, bunsetsu.reading, bunsetsu.surface);
        bunsetsuReversed.push_back(bunsetsu);

        end = step.prevEnd;
        nodeIndex = step.prevIndex;
    }

    for (std::vector<BunsetsuConversion>::reverse_iterator it = bunsetsuReversed.rbegin();
        it != bunsetsuReversed.rend();
        ++it)
    {
        bestCandidate.surface += it->surface;
        bestCandidate.bunsetsu.push_back(*it);
    }

    for (size_t index = 0; index < bestCandidate.bunsetsu.size(); ++index)
    {
        const BunsetsuConversion& bunsetsu = bestCandidate.bunsetsu[index];
        const int boundary = bunsetsu.start + bunsetsu.length;
        if (index + 1 < bestCandidate.bunsetsu.size())
        {
            result.bestBoundaries.push_back(boundary);
        }
    }

    result.candidates.push_back(bestCandidate);
    return result;
}
