#ifndef SUMIRE_SETTINGS_STORE_H
#define SUMIRE_SETTINGS_STORE_H

#include <windows.h>

#include <string>
#include <vector>

namespace SumireSettingsStore
{
struct UserDictionaryProfile
{
    std::wstring id;
    std::wstring name;
    std::wstring sourcePath;
    std::wstring builtPath;
    bool enabled = true;
};

struct Settings
{
    bool liveConversionEnabled = true;
    bool liveConversionSourceViewEnabled = true;
    int candidatePageSize = 9;
    std::wstring settingsUiLanguage = L"ja";
    std::wstring romajiMapPath;
    bool zenzEnabled = true;
    bool zenzServiceEnabled = true;
    std::wstring zenzModelPreset = L"small";
    std::wstring zenzModelPath;
    std::wstring zenzModelRepo = L"https://huggingface.co/Miwa-Keita/zenz-v3.1-small-gguf";
    std::vector<UserDictionaryProfile> userDictionaryProfiles;
};

Settings Load();
bool Save(const Settings& settings);
std::wstring GetRegistryPath();
std::wstring GetInstallRegistryPath();
}

#endif // SUMIRE_SETTINGS_STORE_H
