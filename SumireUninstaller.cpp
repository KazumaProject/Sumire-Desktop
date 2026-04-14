#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>

#include "SumireInstallUtil.h"
#include "SumireSettingsStore.h"

namespace
{
struct UninstallOptions
{
    bool silent = false;
    bool removeSettings = false;
};

bool IsSwitch(const std::wstring& argument, std::initializer_list<const wchar_t*> names)
{
    for (const wchar_t* name : names)
    {
        if (CompareStringOrdinal(argument.c_str(), -1, name, -1, TRUE) == CSTR_EQUAL)
        {
            return true;
        }
    }

    return false;
}

UninstallOptions ParseUninstallOptions()
{
    UninstallOptions options;

    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments == nullptr)
    {
        return options;
    }

    bool removeSettingsSpecified = false;
    for (int index = 1; index < argumentCount; ++index)
    {
        const std::wstring argument = arguments[index] != nullptr ? arguments[index] : L"";
        if (IsSwitch(argument, {L"/quiet", L"-quiet", L"--quiet",
                               L"/silent", L"-silent", L"--silent",
                               L"/verysilent", L"-verysilent", L"--verysilent",
                               L"/S", L"-S"}))
        {
            options.silent = true;
        }
        else if (IsSwitch(argument, {L"/remove-settings", L"-remove-settings", L"--remove-settings"}))
        {
            options.removeSettings = true;
            removeSettingsSpecified = true;
        }
        else if (IsSwitch(argument, {L"/keep-settings", L"-keep-settings", L"--keep-settings"}))
        {
            options.removeSettings = false;
            removeSettingsSpecified = true;
        }
    }

    LocalFree(arguments);

    if (options.silent && !removeSettingsSpecified)
    {
        options.removeSettings = true;
    }

    return options;
}

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

std::wstring QuoteCommandLineArgument(const std::wstring& value)
{
    std::wstring quoted;
    quoted.reserve(value.size() + 2);
    quoted.push_back(L'"');
    for (wchar_t ch : value)
    {
        if (ch == L'"')
        {
            quoted.push_back(L'\\');
        }
        quoted.push_back(ch);
    }
    quoted.push_back(L'"');
    return quoted;
}

std::filesystem::path GetTempDirectory()
{
    std::wstring path(MAX_PATH, L'\0');
    const DWORD length = GetTempPathW(static_cast<DWORD>(path.size()), &path[0]);
    if (length == 0)
    {
        return std::filesystem::path();
    }

    if (length >= path.size())
    {
        path.assign(length + 1, L'\0');
        if (GetTempPathW(static_cast<DWORD>(path.size()), &path[0]) == 0)
        {
            return std::filesystem::path();
        }
    }

    path.resize(wcslen(path.c_str()));
    return std::filesystem::path(path);
}

bool LaunchPostUninstallCleanup(const std::filesystem::path& installDirectory)
{
    if (installDirectory.empty())
    {
        return false;
    }

    const std::filesystem::path tempDirectory = GetTempDirectory();
    if (tempDirectory.empty())
    {
        return false;
    }

    const DWORD processId = GetCurrentProcessId();
    const std::filesystem::path scriptPath =
        tempDirectory / (L"sumire-cleanup-" + std::to_wstring(processId) + L".cmd");

    std::wofstream script(scriptPath);
    if (!script)
    {
        return false;
    }

    const std::wstring installPath = installDirectory.wstring();
    script
        << L"@echo off\n"
        << L"set \"target=" << installPath << L"\"\n"
        << L"for /L %%i in (1,1,30) do (\n"
        << L"  del /f /q \"%target%\\SumireUninstaller.exe\" >nul 2>nul\n"
        << L"  rmdir \"%target%\" >nul 2>nul\n"
        << L"  if not exist \"%target%\" goto done\n"
        << L"  timeout /t 1 /nobreak >nul 2>nul\n"
        << L")\n"
        << L":done\n"
        << L"del /f /q \"%~f0\" >nul 2>nul\n";
    script.close();

    wchar_t comspec[MAX_PATH] = {};
    DWORD comspecLength = GetEnvironmentVariableW(L"ComSpec", comspec, ARRAYSIZE(comspec));
    std::wstring commandProcessor = (comspecLength > 0 && comspecLength < ARRAYSIZE(comspec))
        ? std::wstring(comspec)
        : std::wstring(L"C:\\Windows\\System32\\cmd.exe");

    std::wstring commandLine =
        QuoteCommandLineArgument(commandProcessor) + L" /c " + QuoteCommandLineArgument(scriptPath.wstring());

    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo = {};
    if (!CreateProcessW(
            nullptr,
            &commandLine[0],
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startupInfo,
            &processInfo))
    {
        DeleteFileW(scriptPath.c_str());
        return false;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return true;
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    UninstallOptions options = ParseUninstallOptions();
    if (!options.silent)
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

        options.removeSettings = settingsChoice == IDYES;
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

    if (options.removeSettings)
    {
        RegDeleteTreeW(HKEY_CURRENT_USER, SumireSettingsStore::GetRegistryPath().c_str());
    }

    bool rebootRequired = false;
    SumireInstallUtil::DeleteDirectoryBestEffort(installDirectory, &rebootRequired);
    bool cleanupLaunched = false;
    std::error_code existsError;
    if (std::filesystem::exists(installDirectory, existsError) && !existsError)
    {
        cleanupLaunched = LaunchPostUninstallCleanup(installDirectory);
        if (!cleanupLaunched)
        {
            rebootRequired = true;
        }
    }

    if (rebootRequired)
    {
        if (!options.silent)
        {
            SumireInstallUtil::ShowInfoMessage(
                L"Uninstall completed, but some files are still in use and will be removed after sign-out or reboot.");
        }

        return ERROR_SUCCESS_REBOOT_REQUIRED;
    }

    if (!options.silent)
    {
        SumireInstallUtil::ShowInfoMessage(L"Sumire IME was uninstalled.");
    }

    return 0;
}
