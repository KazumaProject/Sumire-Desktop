#include "KanaKanjiConverter.h"

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
}

KanaKanjiConverter::KanaKanjiConverter()
{
    _dictionary[L"\u308f\u305f\u3057"] = { L"\u79c1" };
    _dictionary[L"\u306b\u307b\u3093"] = { L"\u65e5\u672c" };
    _dictionary[L"\u306b\u307b\u3093\u3054"] = { L"\u65e5\u672c\u8a9e" };
    _dictionary[L"\u304d\u3087\u3046"] = { L"\u4eca\u65e5", L"\u4eac\u90fd" };
    _dictionary[L"\u3042\u3057\u305f"] = { L"\u660e\u65e5" };
    _dictionary[L"\u3068\u3046\u304d\u3087\u3046"] = { L"\u6771\u4eac" };
    _dictionary[L"\u304b\u3093\u3058"] = { L"\u6f22\u5b57" };
    _dictionary[L"\u3078\u3093\u304b\u3093"] = { L"\u5909\u63db" };
    _dictionary[L"\u304b\u306a"] = { L"\u4eee\u540d", L"\u304b\u306a" };
    _dictionary[L"\u304c\u3063\u3053\u3046"] = { L"\u5b66\u6821" };
    _dictionary[L"\u305b\u3093\u305b\u3044"] = { L"\u5148\u751f" };
    _dictionary[L"\u3058\u304b\u3093"] = { L"\u6642\u9593" };
    _dictionary[L"\u3058\u3057\u3087"] = { L"\u8f9e\u66f8" };
    _dictionary[L"\u3066\u3093\u304d"] = { L"\u5929\u6c17" };
    _dictionary[L"\u3044\u3044"] = { L"\u826f\u3044", L"\u3044\u3044" };
    _dictionary[L"\u3072\u3089\u304c\u306a"] = { L"\u5e73\u4eee\u540d", L"\u3072\u3089\u304c\u306a" };
    _dictionary[L"\u304b\u305f\u304b\u306a"] = { L"\u7247\u4eee\u540d", L"\u30ab\u30bf\u30ab\u30ca" };
}

std::vector<std::wstring> KanaKanjiConverter::GenerateCandidates(
    const std::wstring& reading,
    const std::wstring& katakana,
    const std::wstring& halfwidthRoman,
    const std::wstring& fullwidthRoman) const
{
    std::vector<std::wstring> result;
    if (reading.empty())
    {
        return result;
    }

    auto it = _dictionary.find(reading);
    if (it != _dictionary.end())
    {
        for (const std::wstring& candidate : it->second)
        {
            AppendUnique(&result, candidate);
        }
    }

    AppendUnique(&result, reading);
    AppendUnique(&result, katakana);
    AppendUnique(&result, halfwidthRoman);
    AppendUnique(&result, fullwidthRoman);

    return result;
}
