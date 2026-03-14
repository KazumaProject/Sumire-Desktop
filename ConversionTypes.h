#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class DictionaryKind
{
    System = 0,
    User = 1,
    PersonName = 2,
    PlaceName = 3,
};

struct LexiconEntry
{
    std::wstring reading;
    std::wstring surface;
    int wordCost = 0;
    int leftId = 0;
    int rightId = 0;
    std::uint16_t posId = 0;
    DictionaryKind dictionaryKind = DictionaryKind::System;
    std::uint32_t sourceEntryId = 0;
};

struct BunsetsuConversion
{
    int start = 0;
    int length = 0;
    std::wstring reading;
    std::wstring surface;
    std::vector<std::wstring> alternatives;
};

struct ConversionCandidate
{
    std::wstring surface;
    std::wstring reading;
    int totalCost = 0;
    std::vector<BunsetsuConversion> bunsetsu;
};

struct ConversionResult
{
    std::vector<ConversionCandidate> candidates;
    std::vector<int> bestBoundaries;
};
