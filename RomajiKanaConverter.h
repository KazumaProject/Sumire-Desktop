// RomajiKanaConverter.h
#pragma once
#include <filesystem>
#include <string>
#include <unordered_map>

class RomajiKanaConverter
{
public:
    struct MapEntry
    {
        std::wstring kana;
        int consume;
        std::wstring pending;
    };

    RomajiKanaConverter();

    // Converts raw romaji text into the hiragana surface text.
    std::wstring ConvertFromRaw(const std::wstring& raw) const;

private:
    std::unordered_map<std::wstring, MapEntry> m_romajiToKana;
    int m_maxKeyLength;
    std::filesystem::path m_loadedMapPath;

    static wchar_t ToHalfWidth(wchar_t ch);
    static std::wstring FullWidthToHalfWidth(const std::wstring& src);
};
