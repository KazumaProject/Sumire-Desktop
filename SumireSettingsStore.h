#ifndef SUMIRE_SETTINGS_STORE_H
#define SUMIRE_SETTINGS_STORE_H

#include <windows.h>

#include <string>

namespace SumireSettingsStore
{
struct Settings
{
    bool liveConversionEnabled = true;
    int candidatePageSize = 9;
    std::wstring romajiMapPath;
};

Settings Load();
bool Save(const Settings& settings);
std::wstring GetRegistryPath();
std::wstring GetInstallRegistryPath();
}

#endif // SUMIRE_SETTINGS_STORE_H
