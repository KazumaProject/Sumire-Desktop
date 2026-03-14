#include "SumireSettingsStore.h"

#include <algorithm>

namespace
{
constexpr wchar_t kSettingsSubKey[] = L"Software\\Sumire\\Settings";
constexpr wchar_t kInstallSubKey[] = L"Software\\Sumire";
constexpr wchar_t kLiveConversionValue[] = L"LiveConversionEnabled";
constexpr wchar_t kCandidatePageSizeValue[] = L"CandidatePageSize";
constexpr wchar_t kRomajiMapPathValue[] = L"RomajiMapPath";
constexpr int kDefaultCandidatePageSize = 9;
constexpr int kMinCandidatePageSize = 1;
constexpr int kMaxCandidatePageSize = 50;

DWORD ReadDwordValue(HKEY key, const wchar_t* valueName, DWORD fallback)
{
    DWORD type = 0;
    DWORD value = fallback;
    DWORD size = sizeof(value);
    if (RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(&value), &size) != ERROR_SUCCESS ||
        type != REG_DWORD)
    {
        return fallback;
    }

    return value;
}

std::wstring ReadStringValue(HKEY key, const wchar_t* valueName)
{
    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &size) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) ||
        size == 0)
    {
        return L"";
    }

    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(
            key,
            valueName,
            nullptr,
            &type,
            reinterpret_cast<LPBYTE>(&value[0]),
            &size) != ERROR_SUCCESS)
    {
        return L"";
    }

    while (!value.empty() && value.back() == L'\0')
    {
        value.pop_back();
    }

    return value;
}

void WriteDwordValue(HKEY key, const wchar_t* valueName, DWORD value)
{
    RegSetValueExW(
        key,
        valueName,
        0,
        REG_DWORD,
        reinterpret_cast<const BYTE*>(&value),
        sizeof(value));
}

void WriteStringValue(HKEY key, const wchar_t* valueName, const std::wstring& value)
{
    RegSetValueExW(
        key,
        valueName,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

int ClampCandidatePageSize(int value)
{
    return (std::max)(kMinCandidatePageSize, (std::min)(kMaxCandidatePageSize, value));
}
}

namespace SumireSettingsStore
{
Settings Load()
{
    Settings settings;

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsSubKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
    {
        return settings;
    }

    settings.liveConversionEnabled = ReadDwordValue(key, kLiveConversionValue, 1) != 0;
    settings.candidatePageSize = ClampCandidatePageSize(
        static_cast<int>(ReadDwordValue(key, kCandidatePageSizeValue, kDefaultCandidatePageSize)));
    settings.romajiMapPath = ReadStringValue(key, kRomajiMapPathValue);

    RegCloseKey(key);
    return settings;
}

bool Save(const Settings& settings)
{
    HKEY key = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER,
            kSettingsSubKey,
            0,
            nullptr,
            REG_OPTION_NON_VOLATILE,
            KEY_WRITE,
            nullptr,
            &key,
            &disposition) != ERROR_SUCCESS)
    {
        return false;
    }

    WriteDwordValue(key, kLiveConversionValue, settings.liveConversionEnabled ? 1u : 0u);
    WriteDwordValue(key, kCandidatePageSizeValue, static_cast<DWORD>(ClampCandidatePageSize(settings.candidatePageSize)));
    WriteStringValue(key, kRomajiMapPathValue, settings.romajiMapPath);

    RegCloseKey(key);
    return true;
}

std::wstring GetRegistryPath()
{
    return kSettingsSubKey;
}

std::wstring GetInstallRegistryPath()
{
    return kInstallSubKey;
}
}
