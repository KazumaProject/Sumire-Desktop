#ifndef SUMIRE_INSTALL_UTIL_H
#define SUMIRE_INSTALL_UTIL_H

#include <windows.h>

#include <filesystem>
#include <string>

namespace SumireInstallUtil
{
std::filesystem::path GetExecutablePath();
std::filesystem::path GetExecutableDirectory();
std::filesystem::path GetDefaultInstallDirectory();
std::filesystem::path GetInstallDirectoryFromRegistry();
std::filesystem::path GetStartMenuDirectory();
std::filesystem::path GetStartMenuShortcutPath(const wchar_t* fileName);

bool EnsureDirectory(const std::filesystem::path& directory);
bool CopyFileIntoDirectory(const std::filesystem::path& sourceFile, const std::filesystem::path& targetDirectory);
bool CopyDirectoryRecursive(const std::filesystem::path& sourceDirectory, const std::filesystem::path& targetDirectory);
bool DeleteDirectoryBestEffort(const std::filesystem::path& directory, bool* rebootRequired);

bool RegisterTextServiceDll(const std::filesystem::path& dllPath);
bool UnregisterTextServiceDll(const std::filesystem::path& dllPath);
bool ActivateTextServiceProfile();
bool DeactivateTextServiceProfile();

bool WriteInstallMetadata(const std::filesystem::path& installDirectory, const std::filesystem::path& uninstallExePath);
void RemoveInstallMetadata();

bool CreateShortcut(
    const std::filesystem::path& shortcutPath,
    const std::filesystem::path& targetPath,
    const std::wstring& description,
    const std::filesystem::path& iconPath);
void RemoveShortcut(const std::filesystem::path& shortcutPath);

int ShowErrorMessage(const std::wstring& message, const std::wstring& title = L"Sumire IME");
int ShowInfoMessage(const std::wstring& message, const std::wstring& title = L"Sumire IME");
}

#endif // SUMIRE_INSTALL_UTIL_H
