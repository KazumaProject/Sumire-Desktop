// RomajiKanaConverter.h
#pragma once
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

class RomajiKanaConverter
{
public:
    struct ConversionSpan
    {
        size_t rawStart = 0;
        size_t rawEnd = 0;
        std::wstring text;
    };

    struct MapEntry
    {
        std::wstring kana;
        int consume;
        std::wstring pending;
    };

    RomajiKanaConverter();
    void ReloadFromSettings();

    // Converts raw romaji text into the hiragana surface text.
    std::wstring ConvertFromRaw(const std::wstring& raw) const;
    std::vector<ConversionSpan> BuildConversionSpans(const std::wstring& raw) const;

private:
    std::unordered_map<std::wstring, MapEntry> m_romajiToKana;
    int m_maxKeyLength;
    std::filesystem::path m_loadedMapPath;

    static wchar_t ToHalfWidth(wchar_t ch);
    static std::wstring FullWidthToHalfWidth(const std::wstring& src);
};
