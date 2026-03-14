#include <windows.h>

#include <filesystem>
#include <initializer_list>
#include <string>

#include "SumireInstallUtil.h"

namespace
{
std::filesystem::path FindExistingPathUpTree(
    const std::filesystem::path& baseDirectory,
    const std::initializer_list<std::filesystem::path>& relativePaths)
{
    std::filesystem::path current = baseDirectory;
    for (int depth = 0; depth < 6; ++depth)
    {
        for (const std::filesystem::path& relativePath : relativePaths)
        {
            const std::filesystem::path candidate = current / relativePath;
            if (std::filesystem::exists(candidate))
            {
                return candidate;
            }
        }

        if (!current.has_parent_path())
        {
            break;
        }

        const std::filesystem::path parent = current.parent_path();
        if (parent == current)
        {
            break;
        }

        current = parent;
    }

    return std::filesystem::path();
}

std::filesystem::path FindFirstExistingFile(
    const std::filesystem::path& directory,
    const std::initializer_list<const wchar_t*>& fileNames)
{
    for (const wchar_t* fileName : fileNames)
    {
        const std::filesystem::path path = directory / fileName;
        if (std::filesystem::exists(path))
        {
            return path;
        }
    }

    return std::filesystem::path();
}

bool CopyPayload(const std::filesystem::path& sourceDirectory, const std::filesystem::path& installDirectory, std::wstring* error)
{
    const std::filesystem::path dllPath = FindFirstExistingFile(sourceDirectory, {L"Sumite-Desktop.dll", L"TextService.dll"});
    const std::filesystem::path settingsPath = FindFirstExistingFile(sourceDirectory, {L"SumireSettings.exe"});
    const std::filesystem::path uninstallerPath = FindFirstExistingFile(sourceDirectory, {L"SumireUninstaller.exe"});
    const std::filesystem::path romajiMapPath = FindExistingPathUpTree(
        sourceDirectory,
        {std::filesystem::path(L"romaji-hiragana.tsv"),
         std::filesystem::path(L"dictionaries") / L"romaji-hiragana.tsv"});
    const std::filesystem::path dictionariesPath = FindExistingPathUpTree(
        sourceDirectory,
        {std::filesystem::path(L"dictionaries")});

    if (dllPath.empty() || settingsPath.empty() || uninstallerPath.empty())
    {
        *error = L"Required files were not found next to the installer.";
        return false;
    }

    if (!SumireInstallUtil::EnsureDirectory(installDirectory))
    {
        *error = L"Failed to create the install directory.";
        return false;
    }

    if (!SumireInstallUtil::CopyFileIntoDirectory(dllPath, installDirectory) ||
        !SumireInstallUtil::CopyFileIntoDirectory(settingsPath, installDirectory) ||
        !SumireInstallUtil::CopyFileIntoDirectory(uninstallerPath, installDirectory))
    {
        *error = L"Failed to copy the main application files.";
        return false;
    }

    if (std::filesystem::exists(romajiMapPath) &&
        !SumireInstallUtil::CopyFileIntoDirectory(romajiMapPath, installDirectory))
    {
        *error = L"Failed to copy romaji-hiragana.tsv.";
        return false;
    }

    if (!dictionariesPath.empty() &&
        std::filesystem::exists(dictionariesPath) &&
        !SumireInstallUtil::CopyDirectoryRecursive(dictionariesPath, installDirectory / L"dictionaries"))
    {
        *error = L"Failed to copy dictionary files.";
        return false;
    }

    return true;
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    const std::filesystem::path sourceDirectory = SumireInstallUtil::GetExecutableDirectory();
    const std::filesystem::path installDirectory = SumireInstallUtil::GetDefaultInstallDirectory();

    std::wstring error;
    if (!CopyPayload(sourceDirectory, installDirectory, &error))
    {
        return SumireInstallUtil::ShowErrorMessage(error);
    }

    const std::filesystem::path installedDll = FindFirstExistingFile(installDirectory, {L"Sumite-Desktop.dll", L"TextService.dll"});
    const std::filesystem::path installedSettings = installDirectory / L"SumireSettings.exe";
    const std::filesystem::path installedUninstaller = installDirectory / L"SumireUninstaller.exe";

    if (installedDll.empty() || !SumireInstallUtil::RegisterTextServiceDll(installedDll))
    {
        return SumireInstallUtil::ShowErrorMessage(L"Failed to register the IME DLL.");
    }

    if (!SumireInstallUtil::WriteInstallMetadata(installDirectory, installedUninstaller))
    {
        return SumireInstallUtil::ShowErrorMessage(L"Failed to write installation metadata.");
    }

    SumireInstallUtil::CreateShortcut(
        SumireInstallUtil::GetStartMenuShortcutPath(L"Sumire Settings.lnk"),
        installedSettings,
        L"Open Sumire IME settings",
        installedSettings);
    SumireInstallUtil::CreateShortcut(
        SumireInstallUtil::GetStartMenuShortcutPath(L"Uninstall Sumire IME.lnk"),
        installedUninstaller,
        L"Uninstall Sumire IME",
        installedUninstaller);

    return SumireInstallUtil::ShowInfoMessage(
        L"Sumire IME was installed.\nEnable it from the Windows input method list if needed.");
}
