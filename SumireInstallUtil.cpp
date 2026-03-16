#include "SumireInstallUtil.h"

#include <shlobj.h>
#include <shobjidl.h>
#include <strsafe.h>
#include <tlhelp32.h>

#include "Globals.h"
#include "SumireSettingsStore.h"

namespace
{
constexpr wchar_t kInstallDirValue[] = L"InstallDir";
constexpr wchar_t kUninstallKeyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\SumireIME";
constexpr wchar_t kAppFolderName[] = L"Sumire IME";
constexpr wchar_t kPublisher[] = L"Sumire";

using DllRegisterProc = HRESULT(STDAPICALLTYPE*)();

std::wstring ToWideString(const std::filesystem::path& path)
{
    return path.wstring();
}

bool WriteStringValue(HKEY key, const wchar_t* valueName, const std::wstring& value)
{
    return RegSetValueExW(
               key,
               valueName,
               0,
               REG_SZ,
               reinterpret_cast<const BYTE*>(value.c_str()),
               static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

bool WriteDwordValue(HKEY key, const wchar_t* valueName, DWORD value)
{
    return RegSetValueExW(
               key,
               valueName,
               0,
               REG_DWORD,
               reinterpret_cast<const BYTE*>(&value),
               sizeof(value)) == ERROR_SUCCESS;
}

std::filesystem::path GetKnownFolder(REFKNOWNFOLDERID folderId)
{
    PWSTR buffer = nullptr;
    if (SHGetKnownFolderPath(folderId, KF_FLAG_CREATE, nullptr, &buffer) != S_OK)
    {
        return std::filesystem::path();
    }

    std::filesystem::path path(buffer);
    CoTaskMemFree(buffer);
    return path;
}

bool InvokeDllRegistration(const std::filesystem::path& dllPath, const char* exportName)
{
    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = SUCCEEDED(initHr);
    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE)
    {
        return false;
    }

    HMODULE module = LoadLibraryW(dllPath.c_str());
    if (module == nullptr)
    {
        if (shouldUninitialize)
        {
            CoUninitialize();
        }
        return false;
    }

    const auto proc = reinterpret_cast<DllRegisterProc>(GetProcAddress(module, exportName));
    if (proc == nullptr)
    {
        FreeLibrary(module);
        if (shouldUninitialize)
        {
            CoUninitialize();
        }
        return false;
    }

    const HRESULT hr = proc();
    FreeLibrary(module);
    if (shouldUninitialize)
    {
        CoUninitialize();
    }
    return SUCCEEDED(hr);
}

void ScheduleDeleteOnReboot(const std::filesystem::path& path)
{
    MoveFileExW(path.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
}

bool AreEquivalentPaths(const std::filesystem::path& lhs, const std::filesystem::path& rhs)
{
    std::error_code ec;
    return std::filesystem::exists(lhs) &&
        std::filesystem::exists(rhs) &&
        std::filesystem::equivalent(lhs, rhs, ec) &&
        !ec;
}

bool TryReplaceFile(const std::filesystem::path& sourceFile, const std::filesystem::path& targetFile)
{
    if (AreEquivalentPaths(sourceFile, targetFile))
    {
        return true;
    }

    if (CopyFileW(sourceFile.c_str(), targetFile.c_str(), FALSE))
    {
        return true;
    }

    DWORD attributes = GetFileAttributesW(targetFile.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY) != 0)
    {
        SetFileAttributesW(targetFile.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
        if (CopyFileW(sourceFile.c_str(), targetFile.c_str(), FALSE))
        {
            return true;
        }
    }

    const std::filesystem::path backupFile = targetFile.wstring() + L".old";
    if (MoveFileExW(targetFile.c_str(), backupFile.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        if (CopyFileW(sourceFile.c_str(), targetFile.c_str(), FALSE))
        {
            ScheduleDeleteOnReboot(backupFile);
            return true;
        }

        MoveFileExW(backupFile.c_str(), targetFile.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }

    return false;
}

bool EqualsIgnoreCase(const wchar_t* lhs, const wchar_t* rhs)
{
    return CompareStringOrdinal(lhs, -1, rhs, -1, TRUE) == CSTR_EQUAL;
}
}

namespace SumireInstallUtil
{
std::filesystem::path GetExecutablePath()
{
    std::wstring path(MAX_PATH, L'\0');
    for (;;)
    {
        const DWORD size = GetModuleFileNameW(nullptr, &path[0], static_cast<DWORD>(path.size()));
        if (size == 0)
        {
            return std::filesystem::path();
        }

        if (size < path.size() - 1)
        {
            path.resize(size);
            return std::filesystem::path(path);
        }

        path.resize(path.size() * 2);
    }
}

std::filesystem::path GetExecutableDirectory()
{
    return GetExecutablePath().parent_path();
}

std::filesystem::path GetDefaultInstallDirectory()
{
    return GetKnownFolder(FOLDERID_LocalAppData) / L"SumireIME";
}

std::filesystem::path GetInstallDirectoryFromRegistry()
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, SumireSettingsStore::GetInstallRegistryPath().c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
    {
        return std::filesystem::path();
    }

    DWORD type = 0;
    DWORD size = 0;
    std::filesystem::path installDir;
    if (RegQueryValueExW(key, kInstallDirValue, nullptr, &type, nullptr, &size) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ) &&
        size > 0)
    {
        std::wstring value(size / sizeof(wchar_t), L'\0');
        if (RegQueryValueExW(
                key,
                kInstallDirValue,
                nullptr,
                &type,
                reinterpret_cast<LPBYTE>(&value[0]),
                &size) == ERROR_SUCCESS)
        {
            while (!value.empty() && value.back() == L'\0')
            {
                value.pop_back();
            }
            installDir = std::filesystem::path(value);
        }
    }

    RegCloseKey(key);
    return installDir;
}

std::filesystem::path GetDesktopDirectory()
{
    return GetKnownFolder(FOLDERID_Desktop);
}

std::filesystem::path GetDesktopShortcutPath(const wchar_t* fileName)
{
    return GetDesktopDirectory() / fileName;
}

std::filesystem::path GetStartMenuDirectory()
{
    return GetKnownFolder(FOLDERID_Programs) / kAppFolderName;
}

std::filesystem::path GetStartMenuShortcutPath(const wchar_t* fileName)
{
    return GetStartMenuDirectory() / fileName;
}

bool EnsureDirectory(const std::filesystem::path& directory)
{
    if (directory.empty())
    {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    return !ec && std::filesystem::exists(directory);
}

bool CopyFileIntoDirectory(const std::filesystem::path& sourceFile, const std::filesystem::path& targetDirectory)
{
    if (sourceFile.empty() || !std::filesystem::exists(sourceFile))
    {
        return false;
    }

    if (!EnsureDirectory(targetDirectory))
    {
        return false;
    }

    const std::filesystem::path targetFile = targetDirectory / sourceFile.filename();
    return TryReplaceFile(sourceFile, targetFile);
}

bool CopyDirectoryRecursive(const std::filesystem::path& sourceDirectory, const std::filesystem::path& targetDirectory)
{
    if (!std::filesystem::exists(sourceDirectory))
    {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(targetDirectory, ec);
    if (ec)
    {
        return false;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(sourceDirectory, ec))
    {
        if (ec)
        {
            return false;
        }

        const auto relative = std::filesystem::relative(entry.path(), sourceDirectory, ec);
        if (ec)
        {
            return false;
        }

        const auto destination = targetDirectory / relative;
        if (entry.is_directory())
        {
            std::filesystem::create_directories(destination, ec);
        }
        else
        {
            std::filesystem::create_directories(destination.parent_path(), ec);
            if (!ec)
            {
                std::filesystem::copy_file(
                    entry.path(),
                    destination,
                    std::filesystem::copy_options::overwrite_existing,
                    ec);
            }
        }

        if (ec)
        {
            return false;
        }
    }

    return true;
}

bool DeleteDirectoryBestEffort(const std::filesystem::path& directory, bool* rebootRequired)
{
    if (rebootRequired != nullptr)
    {
        *rebootRequired = false;
    }

    if (directory.empty() || !std::filesystem::exists(directory))
    {
        return true;
    }

    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             directory,
             std::filesystem::directory_options::skip_permission_denied,
             ec))
    {
        if (ec)
        {
            break;
        }

        if (entry.is_directory())
        {
            continue;
        }

        std::filesystem::remove(entry.path(), ec);
        if (ec)
        {
            ScheduleDeleteOnReboot(entry.path());
            if (rebootRequired != nullptr)
            {
                *rebootRequired = true;
            }
            ec.clear();
        }
    }

    std::filesystem::remove_all(directory, ec);
    if (ec)
    {
        if (rebootRequired != nullptr)
        {
            *rebootRequired = true;
        }
        ec.clear();
    }

    return true;
}

bool RegisterTextServiceDll(const std::filesystem::path& dllPath)
{
    return InvokeDllRegistration(dllPath, "DllRegisterServer");
}

bool UnregisterTextServiceDll(const std::filesystem::path& dllPath)
{
    return InvokeDllRegistration(dllPath, "DllUnregisterServer");
}

bool ActivateTextServiceProfile()
{
    HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = SUCCEEDED(initHr);
    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE)
    {
        return false;
    }

    ITfInputProcessorProfileMgr* profileMgr = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_TF_InputProcessorProfiles,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfileMgr,
        reinterpret_cast<void**>(&profileMgr));
    if (FAILED(hr) || profileMgr == nullptr)
    {
        if (shouldUninitialize)
        {
            CoUninitialize();
        }
        return false;
    }

    ITfInputProcessorProfiles* profiles = nullptr;
    hr = CoCreateInstance(
        CLSID_TF_InputProcessorProfiles,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfiles,
        reinterpret_cast<void**>(&profiles));

    bool success = true;
    if (SUCCEEDED(hr) && profiles != nullptr)
    {
        profiles->EnableLanguageProfile(c_clsidTextService, TEXTSERVICE_LANGID, c_guidProfile, TRUE);
        profiles->EnableLanguageProfileByDefault(c_clsidTextService, TEXTSERVICE_LANGID, c_guidProfile, TRUE);
        profiles->Release();
    }

    hr = profileMgr->ActivateProfile(
        TF_PROFILETYPE_INPUTPROCESSOR,
        TEXTSERVICE_LANGID,
        c_clsidTextService,
        c_guidProfile,
        nullptr,
        TF_IPPMF_ENABLEPROFILE | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE);
    success = SUCCEEDED(hr);
    profileMgr->Release();

    if (shouldUninitialize)
    {
        CoUninitialize();
    }

    return success;
}

bool DeactivateTextServiceProfile()
{
    HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = SUCCEEDED(initHr);
    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE)
    {
        return false;
    }

    ITfInputProcessorProfileMgr* profileMgr = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_TF_InputProcessorProfiles,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfileMgr,
        reinterpret_cast<void**>(&profileMgr));
    if (FAILED(hr) || profileMgr == nullptr)
    {
        if (shouldUninitialize)
        {
            CoUninitialize();
        }
        return false;
    }

    hr = profileMgr->DeactivateProfile(
        TF_PROFILETYPE_INPUTPROCESSOR,
        TEXTSERVICE_LANGID,
        c_clsidTextService,
        c_guidProfile,
        nullptr,
        TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE);
    const bool success = SUCCEEDED(hr);
    profileMgr->Release();

    if (shouldUninitialize)
    {
        CoUninitialize();
    }

    return success;
}

bool StopProcessesByName(const wchar_t* processName)
{
    if (processName == nullptr || *processName == L'\0')
    {
        return false;
    }

    bool found = false;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (!EqualsIgnoreCase(entry.szExeFile, processName))
            {
                continue;
            }

            found = true;
            HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, entry.th32ProcessID);
            if (process == nullptr)
            {
                continue;
            }

            if (TerminateProcess(process, 0))
            {
                WaitForSingleObject(process, 3000);
            }

            CloseHandle(process);
        }
        while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return found;
}

bool WriteInstallMetadata(const std::filesystem::path& installDirectory, const std::filesystem::path& uninstallExePath)
{
    HKEY installKey = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER,
            SumireSettingsStore::GetInstallRegistryPath().c_str(),
            0,
            nullptr,
            REG_OPTION_NON_VOLATILE,
            KEY_WRITE,
            nullptr,
            &installKey,
            &disposition) != ERROR_SUCCESS)
    {
        return false;
    }

    bool success = WriteStringValue(installKey, kInstallDirValue, ToWideString(installDirectory));
    RegCloseKey(installKey);

    HKEY uninstallKey = nullptr;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER,
            kUninstallKeyPath,
            0,
            nullptr,
            REG_OPTION_NON_VOLATILE,
            KEY_WRITE,
            nullptr,
            &uninstallKey,
            &disposition) != ERROR_SUCCESS)
    {
        return false;
    }

    success = success &&
        WriteStringValue(uninstallKey, L"DisplayName", L"Sumire IME") &&
        WriteStringValue(uninstallKey, L"Publisher", kPublisher) &&
        WriteStringValue(uninstallKey, L"InstallLocation", ToWideString(installDirectory)) &&
        WriteStringValue(uninstallKey, L"UninstallString", ToWideString(uninstallExePath)) &&
        WriteDwordValue(uninstallKey, L"NoModify", 1) &&
        WriteDwordValue(uninstallKey, L"NoRepair", 1);

    RegCloseKey(uninstallKey);
    return success;
}

void RemoveInstallMetadata()
{
    RegDeleteTreeW(HKEY_CURRENT_USER, kUninstallKeyPath);
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, SumireSettingsStore::GetInstallRegistryPath().c_str(), 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS)
    {
        RegDeleteValueW(key, kInstallDirValue);
        RegCloseKey(key);
    }
}

bool CreateShortcut(
    const std::filesystem::path& shortcutPath,
    const std::filesystem::path& targetPath,
    const std::wstring& description,
    const std::filesystem::path& iconPath)
{
    if (!EnsureDirectory(shortcutPath.parent_path()))
    {
        return false;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
    {
        return false;
    }

    IShellLinkW* shellLink = nullptr;
    bool success = false;
    hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&shellLink));
    if (SUCCEEDED(hr) && shellLink != nullptr)
    {
        shellLink->SetPath(targetPath.c_str());
        shellLink->SetDescription(description.c_str());
        if (!iconPath.empty())
        {
            shellLink->SetIconLocation(iconPath.c_str(), 0);
        }

        IPersistFile* persistFile = nullptr;
        hr = shellLink->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persistFile));
        if (SUCCEEDED(hr) && persistFile != nullptr)
        {
            hr = persistFile->Save(shortcutPath.c_str(), TRUE);
            success = SUCCEEDED(hr);
            persistFile->Release();
        }

        shellLink->Release();
    }

    if (shouldUninitialize)
    {
        CoUninitialize();
    }

    return success;
}

void RemoveShortcut(const std::filesystem::path& shortcutPath)
{
    std::error_code ec;
    std::filesystem::remove(shortcutPath, ec);
}

int ShowErrorMessage(const std::wstring& message, const std::wstring& title)
{
    return MessageBoxW(nullptr, message.c_str(), title.c_str(), MB_ICONERROR | MB_OK);
}

int ShowInfoMessage(const std::wstring& message, const std::wstring& title)
{
    return MessageBoxW(nullptr, message.c_str(), title.c_str(), MB_ICONINFORMATION | MB_OK);
}
}
