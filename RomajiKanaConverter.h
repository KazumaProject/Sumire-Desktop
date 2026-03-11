// RomajiKanaConverter.h
#pragma once
#include <string>
#include <unordered_map>

class RomajiKanaConverter
{
public:
    struct MapEntry
    {
        std::wstring kana;
        int consume;
    };

    RomajiKanaConverter();

    // RawText（全角アルファベットを含む）から SurfaceText（かな）を生成
    std::wstring ConvertFromRaw(const std::wstring& raw) const;

private:
    std::unordered_map<std::wstring, MapEntry> m_romajiToKana;
    int m_maxKeyLength;

    static wchar_t ToHalfWidth(wchar_t ch);
    static std::wstring FullWidthToHalfWidth(const std::wstring& src);
};
