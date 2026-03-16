#include <windows.h>

#include <filesystem>

#include "SumireInstallUtil.h"
#include "SumireSettingsStore.h"

namespace
{
std::filesystem::path FindInstalledDll(const std::filesystem::path& installDirectory)
{
    const std::filesystem::path primary = installDirectory / L"Sumite-Desktop.dll";
    if (std::filesystem::exists(primary))
    {
        return primary;
    }

    const std::filesystem::path fallback = installDirectory / L"TextService.dll";
    if (std::filesystem::exists(fallback))
    {
        return fallback;
    }

    return std::filesystem::path();
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    const int settingsChoice = MessageBoxW(
        nullptr,
        L"Remove user settings as well?\nYes: remove settings\nNo: keep settings\nCancel: abort",
        L"Sumire IME",
        MB_ICONQUESTION | MB_YESNOCANCEL);
    if (settingsChoice == IDCANCEL)
    {
        return 0;
    }

    std::filesystem::path installDirectory = SumireInstallUtil::GetInstallDirectoryFromRegistry();
    if (installDirectory.empty())
    {
        installDirectory = SumireInstallUtil::GetExecutableDirectory();
    }

    const std::filesystem::path installedDll = FindInstalledDll(installDirectory);
    SumireInstallUtil::DeactivateTextServiceProfile();
    if (!installedDll.empty())
    {
        SumireInstallUtil::UnregisterTextServiceDll(installedDll);
    }

    SumireInstallUtil::RemoveShortcut(SumireInstallUtil::GetStartMenuShortcutPath(L"Sumire Settings.lnk"));
    SumireInstallUtil::RemoveShortcut(SumireInstallUtil::GetStartMenuShortcutPath(L"Uninstall Sumire IME.lnk"));
    SumireInstallUtil::RemoveShortcut(SumireInstallUtil::GetDesktopShortcutPath(L"Sumire Settings.lnk"));
    SumireInstallUtil::RemoveInstallMetadata();

    if (settingsChoice == IDYES)
    {
        RegDeleteTreeW(HKEY_CURRENT_USER, SumireSettingsStore::GetRegistryPath().c_str());
    }

    bool rebootRequired = false;
    SumireInstallUtil::DeleteDirectoryBestEffort(installDirectory, &rebootRequired);

    if (rebootRequired)
    {
        return SumireInstallUtil::ShowInfoMessage(
            L"Uninstall completed, but some files are still in use and will be removed after sign-out or reboot.");
    }

    return SumireInstallUtil::ShowInfoMessage(L"Sumire IME was uninstalled.");
}
