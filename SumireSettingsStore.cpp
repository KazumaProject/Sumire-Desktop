#include "SumireSettingsStore.h"

#include <algorithm>

namespace
{
constexpr wchar_t kSettingsSubKey[] = L"Software\\Sumire\\Settings";
constexpr wchar_t kInstallSubKey[] = L"Software\\Sumire";
constexpr wchar_t kLiveConversionValue[] = L"LiveConversionEnabled";
constexpr wchar_t kLiveConversionSourceViewValue[] = L"LiveConversionSourceViewEnabled";
constexpr wchar_t kCandidatePageSizeValue[] = L"CandidatePageSize";
constexpr wchar_t kSettingsUiLanguageValue[] = L"SettingsUiLanguage";
constexpr wchar_t kRomajiMapPathValue[] = L"RomajiMapPath";
constexpr wchar_t kZenzEnabledValue[] = L"ZenzEnabled";
constexpr wchar_t kZenzServiceEnabledValue[] = L"ZenzServiceEnabled";
constexpr wchar_t kZenzModelPresetValue[] = L"ZenzModelPreset";
constexpr wchar_t kZenzModelPathValue[] = L"ZenzModelPath";
constexpr wchar_t kZenzModelRepoValue[] = L"ZenzModelRepo";
constexpr wchar_t kProfilesSubKey[] = L"UserDictionaryProfiles";
constexpr wchar_t kLegacyProfilesSubKey[] = L"NameDictionaryProfiles";
constexpr wchar_t kProfileNameValue[] = L"Name";
constexpr wchar_t kProfileSourcePathValue[] = L"SourcePath";
constexpr wchar_t kProfileBuiltPathValue[] = L"BuiltPath";
constexpr wchar_t kProfileEnabledValue[] = L"Enabled";
constexpr wchar_t kDefaultZenzModelPreset[] = L"small";
constexpr wchar_t kDefaultZenzModelRepoXsmall[] = L"https://huggingface.co/Miwa-Keita/zenz-v3.1-xsmall-gguf";
constexpr wchar_t kDefaultZenzModelRepoSmall[] = L"https://huggingface.co/Miwa-Keita/zenz-v3.1-small-gguf";
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

std::wstring NormalizeSettingsUiLanguage(const std::wstring& value)
{
    if (value == L"en")
    {
        return L"en";
    }

    return L"ja";
}

std::wstring NormalizeZenzModelPreset(const std::wstring& value)
{
    if (value == L"small")
    {
        return L"small";
    }

    if (value == L"custom")
    {
        return L"custom";
    }

    return kDefaultZenzModelPreset;
}

std::wstring GetDefaultZenzModelRepo(const std::wstring& preset)
{
    if (preset == L"xsmall")
    {
        return kDefaultZenzModelRepoXsmall;
    }

    if (preset == L"small")
    {
        return kDefaultZenzModelRepoSmall;
    }

    return kDefaultZenzModelRepoSmall;
}

void LoadProfilesFromSubKey(
    HKEY settingsKey,
    const wchar_t* profilesSubKeyName,
    std::vector<SumireSettingsStore::UserDictionaryProfile>* profiles)
{
    profiles->clear();

    HKEY profilesKey = nullptr;
    if (RegOpenKeyExW(settingsKey, profilesSubKeyName, 0, KEY_READ, &profilesKey) != ERROR_SUCCESS)
    {
        return;
    }

    for (DWORD index = 0;; ++index)
    {
        wchar_t profileSubKeyName[256] = {};
        DWORD profileSubKeyNameLength = ARRAYSIZE(profileSubKeyName);
        FILETIME lastWriteTime = {};
        const LONG enumResult = RegEnumKeyExW(
            profilesKey,
            index,
            profileSubKeyName,
            &profileSubKeyNameLength,
            nullptr,
            nullptr,
            nullptr,
            &lastWriteTime);
        if (enumResult == ERROR_NO_MORE_ITEMS)
        {
            break;
        }

        if (enumResult != ERROR_SUCCESS)
        {
            continue;
        }

        HKEY profileKey = nullptr;
        if (RegOpenKeyExW(profilesKey, profileSubKeyName, 0, KEY_READ, &profileKey) != ERROR_SUCCESS)
        {
            continue;
        }

        SumireSettingsStore::UserDictionaryProfile profile;
        profile.id = profileSubKeyName;
        profile.name = ReadStringValue(profileKey, kProfileNameValue);
        profile.sourcePath = ReadStringValue(profileKey, kProfileSourcePathValue);
        profile.builtPath = ReadStringValue(profileKey, kProfileBuiltPathValue);
        profile.enabled = ReadDwordValue(profileKey, kProfileEnabledValue, 1) != 0;
        if (!profile.id.empty())
        {
            profiles->push_back(std::move(profile));
        }

        RegCloseKey(profileKey);
    }

    RegCloseKey(profilesKey);
}

void LoadProfiles(HKEY settingsKey, std::vector<SumireSettingsStore::UserDictionaryProfile>* profiles)
{
    LoadProfilesFromSubKey(settingsKey, kProfilesSubKey, profiles);
    if (!profiles->empty())
    {
        return;
    }

    LoadProfilesFromSubKey(settingsKey, kLegacyProfilesSubKey, profiles);
}

void SaveProfiles(HKEY settingsKey, const std::vector<SumireSettingsStore::UserDictionaryProfile>& profiles)
{
    RegDeleteTreeW(settingsKey, kProfilesSubKey);
    RegDeleteTreeW(settingsKey, kLegacyProfilesSubKey);

    if (profiles.empty())
    {
        return;
    }

    HKEY profilesKey = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyExW(
            settingsKey,
            kProfilesSubKey,
            0,
            nullptr,
            REG_OPTION_NON_VOLATILE,
            KEY_WRITE,
            nullptr,
            &profilesKey,
            &disposition) != ERROR_SUCCESS)
    {
        return;
    }

    for (const auto& profile : profiles)
    {
        if (profile.id.empty())
        {
            continue;
        }

        HKEY profileKey = nullptr;
        if (RegCreateKeyExW(
                profilesKey,
                profile.id.c_str(),
                0,
                nullptr,
                REG_OPTION_NON_VOLATILE,
                KEY_WRITE,
                nullptr,
                &profileKey,
                &disposition) != ERROR_SUCCESS)
        {
            continue;
        }

        WriteStringValue(profileKey, kProfileNameValue, profile.name);
        WriteStringValue(profileKey, kProfileSourcePathValue, profile.sourcePath);
        WriteStringValue(profileKey, kProfileBuiltPathValue, profile.builtPath);
        WriteDwordValue(profileKey, kProfileEnabledValue, profile.enabled ? 1u : 0u);
        RegCloseKey(profileKey);
    }

    RegCloseKey(profilesKey);
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
    settings.liveConversionSourceViewEnabled = ReadDwordValue(key, kLiveConversionSourceViewValue, 1) != 0;
    settings.candidatePageSize = ClampCandidatePageSize(
        static_cast<int>(ReadDwordValue(key, kCandidatePageSizeValue, kDefaultCandidatePageSize)));
    settings.settingsUiLanguage = NormalizeSettingsUiLanguage(ReadStringValue(key, kSettingsUiLanguageValue));
    settings.romajiMapPath = ReadStringValue(key, kRomajiMapPathValue);
    settings.zenzEnabled = ReadDwordValue(key, kZenzEnabledValue, 1) != 0;
    settings.zenzServiceEnabled = ReadDwordValue(key, kZenzServiceEnabledValue, 1) != 0;
    settings.zenzModelPreset = NormalizeZenzModelPreset(ReadStringValue(key, kZenzModelPresetValue));
    settings.zenzModelPath = ReadStringValue(key, kZenzModelPathValue);
    settings.zenzModelRepo = ReadStringValue(key, kZenzModelRepoValue);
    if (settings.zenzModelRepo.empty())
    {
        settings.zenzModelRepo = GetDefaultZenzModelRepo(settings.zenzModelPreset);
    }
    LoadProfiles(key, &settings.userDictionaryProfiles);

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
    WriteDwordValue(key, kLiveConversionSourceViewValue, settings.liveConversionSourceViewEnabled ? 1u : 0u);
    WriteDwordValue(key, kCandidatePageSizeValue, static_cast<DWORD>(ClampCandidatePageSize(settings.candidatePageSize)));
    WriteStringValue(key, kSettingsUiLanguageValue, NormalizeSettingsUiLanguage(settings.settingsUiLanguage));
    WriteStringValue(key, kRomajiMapPathValue, settings.romajiMapPath);
    WriteDwordValue(key, kZenzEnabledValue, settings.zenzEnabled ? 1u : 0u);
    WriteDwordValue(key, kZenzServiceEnabledValue, settings.zenzServiceEnabled ? 1u : 0u);
    WriteStringValue(key, kZenzModelPresetValue, NormalizeZenzModelPreset(settings.zenzModelPreset));
    WriteStringValue(key, kZenzModelPathValue, settings.zenzModelPath);
    WriteStringValue(
        key,
        kZenzModelRepoValue,
        settings.zenzModelRepo.empty()
            ? GetDefaultZenzModelRepo(NormalizeZenzModelPreset(settings.zenzModelPreset))
            : settings.zenzModelRepo);
    SaveProfiles(key, settings.userDictionaryProfiles);

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
