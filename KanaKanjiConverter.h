#pragma once

#include <string>
#include <unordered_map>
#include <vector>

class KanaKanjiConverter
{
public:
    KanaKanjiConverter();

    std::vector<std::wstring> GenerateCandidates(
        const std::wstring& reading,
        const std::wstring& katakana,
        const std::wstring& halfwidthRoman,
        const std::wstring& fullwidthRoman) const;

private:
    std::unordered_map<std::wstring, std::vector<std::wstring>> _dictionary;
};
