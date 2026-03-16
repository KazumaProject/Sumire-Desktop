#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <urlmon.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <vector>

#include "SumireInstallUtil.h"

namespace
{
constexpr wchar_t kWindowClassName[] = L"SumireInstallerWizard";
constexpr UINT WM_SUMIRE_START_INSTALL = WM_APP + 0x210;

constexpr wchar_t kDefaultZenzModelRepo[] = L"https://huggingface.co/Miwa-Keita/zenz-v3.1-small-gguf";
constexpr wchar_t kDefaultZenzModelRelativePath[] = L"models\\zenz-v3.1-small-gguf\\ggml-model-Q5_K_M.gguf";
constexpr wchar_t kSettingsShortcutFileName[] = L"Sumire Settings.lnk";
constexpr wchar_t kUninstallShortcutFileName[] = L"Uninstall Sumire IME.lnk";
constexpr char kEmbeddedPayloadMagic[] = "SUMIRE_PAYLOAD1";
constexpr std::uint32_t kEmbeddedPayloadVersion = 1;
constexpr std::size_t kCopyBufferSize = 64 * 1024;

enum class WizardPage
{
    Welcome,
    Options,
    Progress,
    Finish,
};

enum ControlId
{
    IdTitle = 101,
    IdBody = 102,
    IdInstallPathLabel = 103,
    IdInstallPathEdit = 104,
    IdBrowse = 105,
    IdStartMenuShortcutCheck = 106,
    IdDownloadModelCheck = 107,
    IdLaunchSettingsCheck = 108,
    IdBack = 109,
    IdNext = 110,
    IdCancel = 111,
    IdStatus = 112,
    IdDesktopSettingsShortcutCheck = 113,
};

struct WizardState
{
    WizardPage page = WizardPage::Welcome;
    bool installSucceeded = false;
    bool createStartMenuShortcuts = true;
    bool createDesktopSettingsShortcut = true;
    bool downloadModelDuringInstall = true;
    bool launchSettingsAfterFinish = false;
    bool installStarted = false;
    std::wstring installDirectory;
    std::wstring installedSettingsPath;

    HWND title = nullptr;
    HWND body = nullptr;
    HWND installPathLabel = nullptr;
    HWND installPathEdit = nullptr;
    HWND browseButton = nullptr;
    HWND startMenuShortcutCheck = nullptr;
    HWND desktopSettingsShortcutCheck = nullptr;
    HWND downloadModelCheck = nullptr;
    HWND launchSettingsCheck = nullptr;
    HWND backButton = nullptr;
    HWND nextButton = nullptr;
    HWND cancelButton = nullptr;
    HWND status = nullptr;
};

struct EmbeddedPayloadEntry
{
    std::filesystem::path relativePath;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};

HMENU ControlMenu(int id)
{
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

void SetWizardState(HWND hwnd, WizardState* state)
{
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
}

WizardState* GetWizardState(HWND hwnd)
{
    return reinterpret_cast<WizardState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void SetChecked(HWND hwnd, bool checked)
{
    SendMessageW(hwnd, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
}

bool IsChecked(HWND hwnd)
{
    return SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

bool ReadExact(std::ifstream& input, void* buffer, std::size_t size)
{
    return static_cast<bool>(
        input.read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(size)));
}

bool Utf8ToWide(const std::string& value, std::wstring* result)
{
    if (result == nullptr)
    {
        return false;
    }

    if (value.empty())
    {
        result->clear();
        return true;
    }

    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (required <= 0)
    {
        return false;
    }

    result->assign(static_cast<std::size_t>(required), L'\0');
    const int converted = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        &(*result)[0],
        required);
    return converted == required;
}

bool IsSafeRelativePayloadPath(const std::filesystem::path& path)
{
    if (path.empty() || path.is_absolute())
    {
        return false;
    }

    const std::filesystem::path normalized = path.lexically_normal();
    if (normalized.empty() || normalized.is_absolute())
    {
        return false;
    }

    for (const auto& part : normalized)
    {
        if (part == L"..")
        {
            return false;
        }
    }

    return true;
}

bool LoadEmbeddedPayloadManifest(
    const std::filesystem::path& installerPath,
    std::vector<EmbeddedPayloadEntry>* entries,
    bool* payloadFound,
    std::wstring* error)
{
    if (entries == nullptr || payloadFound == nullptr)
    {
        return false;
    }

    entries->clear();
    *payloadFound = false;

    std::ifstream input(installerPath, std::ios::binary);
    if (!input)
    {
        if (error != nullptr)
        {
            *error = L"Failed to open the installer executable.";
        }
        return false;
    }

    input.seekg(0, std::ios::end);
    const std::streamoff fileSizeOffset = input.tellg();
    if (fileSizeOffset < 0)
    {
        if (error != nullptr)
        {
            *error = L"Failed to determine the installer size.";
        }
        return false;
    }

    const std::uint64_t fileSize = static_cast<std::uint64_t>(fileSizeOffset);
    const std::size_t magicSize = sizeof(kEmbeddedPayloadMagic) - 1;
    const std::uint64_t footerSize = sizeof(std::uint64_t) + magicSize;
    if (fileSize < footerSize)
    {
        return true;
    }

    input.seekg(static_cast<std::streamoff>(fileSize - footerSize), std::ios::beg);
    std::uint64_t tableSize = 0;
    std::vector<char> magic(magicSize, '\0');
    if (!ReadExact(input, &tableSize, sizeof(tableSize)) || !ReadExact(input, magic.data(), magic.size()))
    {
        if (error != nullptr)
        {
            *error = L"Failed to read the embedded installer payload.";
        }
        return false;
    }

    if (std::memcmp(magic.data(), kEmbeddedPayloadMagic, magicSize) != 0)
    {
        return true;
    }

    *payloadFound = true;
    if (tableSize > fileSize - footerSize)
    {
        if (error != nullptr)
        {
            *error = L"The embedded installer payload is corrupted.";
        }
        return false;
    }

    const std::uint64_t tableStart = fileSize - footerSize - tableSize;
    input.seekg(static_cast<std::streamoff>(tableStart), std::ios::beg);

    std::uint32_t version = 0;
    std::uint32_t entryCount = 0;
    if (!ReadExact(input, &version, sizeof(version)) || !ReadExact(input, &entryCount, sizeof(entryCount)))
    {
        if (error != nullptr)
        {
            *error = L"Failed to parse the embedded installer payload.";
        }
        return false;
    }

    if (version != kEmbeddedPayloadVersion)
    {
        if (error != nullptr)
        {
            *error = L"The embedded installer payload version is not supported.";
        }
        return false;
    }

    entries->reserve(entryCount);
    for (std::uint32_t index = 0; index < entryCount; ++index)
    {
        std::uint32_t pathSize = 0;
        if (!ReadExact(input, &pathSize, sizeof(pathSize)) || pathSize == 0 || pathSize > 32768)
        {
            if (error != nullptr)
            {
                *error = L"The embedded installer payload contains an invalid file path.";
            }
            return false;
        }

        std::string relativePathUtf8(pathSize, '\0');
        EmbeddedPayloadEntry entry;
        if (!ReadExact(input, &relativePathUtf8[0], relativePathUtf8.size()) ||
            !ReadExact(input, &entry.offset, sizeof(entry.offset)) ||
            !ReadExact(input, &entry.size, sizeof(entry.size)))
        {
            if (error != nullptr)
            {
                *error = L"Failed to read the embedded installer payload entries.";
            }
            return false;
        }

        std::wstring relativePathWide;
        if (!Utf8ToWide(relativePathUtf8, &relativePathWide))
        {
            if (error != nullptr)
            {
                *error = L"The embedded installer payload contains an invalid UTF-8 file path.";
            }
            return false;
        }

        entry.relativePath = std::filesystem::path(relativePathWide);
        if (!IsSafeRelativePayloadPath(entry.relativePath) ||
            entry.offset > tableStart ||
            entry.size > tableStart ||
            entry.offset > tableStart - entry.size)
        {
            if (error != nullptr)
            {
                *error = L"The embedded installer payload contains an invalid file range.";
            }
            return false;
        }

        entries->push_back(std::move(entry));
    }

    return true;
}

bool CopyEmbeddedPayload(
    const std::filesystem::path& installerPath,
    const std::filesystem::path& installDirectory,
    std::wstring* error,
    bool* payloadFound)
{
    std::vector<EmbeddedPayloadEntry> entries;
    if (!LoadEmbeddedPayloadManifest(installerPath, &entries, payloadFound, error))
    {
        return false;
    }

    if (payloadFound == nullptr || !*payloadFound)
    {
        return false;
    }

    if (!SumireInstallUtil::EnsureDirectory(installDirectory))
    {
        if (error != nullptr)
        {
            *error = L"Failed to create the install directory.";
        }
        return false;
    }

    std::ifstream input(installerPath, std::ios::binary);
    if (!input)
    {
        if (error != nullptr)
        {
            *error = L"Failed to reopen the installer executable.";
        }
        return false;
    }

    std::vector<char> buffer(kCopyBufferSize, '\0');
    for (const EmbeddedPayloadEntry& entry : entries)
    {
        const std::filesystem::path targetPath = installDirectory / entry.relativePath;
        if (!SumireInstallUtil::EnsureDirectory(targetPath.parent_path()))
        {
            if (error != nullptr)
            {
                *error = L"Failed to create an embedded payload subdirectory.";
            }
            return false;
        }

        std::ofstream output(targetPath, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            if (error != nullptr)
            {
                *error = L"Failed to create a file from the embedded payload.";
            }
            return false;
        }

        input.seekg(static_cast<std::streamoff>(entry.offset), std::ios::beg);
        std::uint64_t remaining = entry.size;
        while (remaining > 0)
        {
            const std::size_t chunkSize = remaining > buffer.size()
                ? buffer.size()
                : static_cast<std::size_t>(remaining);
            if (!ReadExact(input, buffer.data(), chunkSize))
            {
                if (error != nullptr)
                {
                    *error = L"Failed while extracting the embedded payload.";
                }
                return false;
            }

            output.write(buffer.data(), static_cast<std::streamsize>(chunkSize));
            if (!output)
            {
                if (error != nullptr)
                {
                    *error = L"Failed while writing an extracted payload file.";
                }
                return false;
            }

            remaining -= chunkSize;
        }
    }

    return true;
}

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

bool CopyPayloadFromDirectory(
    const std::filesystem::path& sourceDirectory,
    const std::filesystem::path& installDirectory,
    std::wstring* error)
{
    const std::filesystem::path dllPath = FindFirstExistingFile(sourceDirectory, {L"Sumite-Desktop.dll", L"TextService.dll"});
    const std::filesystem::path settingsPath = FindFirstExistingFile(sourceDirectory, {L"SumireSettings.exe"});
    const std::filesystem::path uninstallerPath = FindFirstExistingFile(sourceDirectory, {L"SumireUninstaller.exe"});
    const std::filesystem::path zenzServicePath = FindFirstExistingFile(sourceDirectory, {L"SumireZenzService.exe"});
    const std::filesystem::path romajiMapPath = FindExistingPathUpTree(
        sourceDirectory,
        {std::filesystem::path(L"romaji-hiragana.tsv"),
         std::filesystem::path(L"dictionaries") / L"romaji-hiragana.tsv"});
    const std::filesystem::path dictionariesPath = FindExistingPathUpTree(
        sourceDirectory,
        {std::filesystem::path(L"dictionaries")});
    const std::filesystem::path modelsPath = FindExistingPathUpTree(
        sourceDirectory,
        {std::filesystem::path(L"models")});

    if (dllPath.empty() || settingsPath.empty() || uninstallerPath.empty())
    {
        if (error != nullptr)
        {
            *error = L"Required installer payload files were not found next to the installer.";
        }
        return false;
    }

    if (!SumireInstallUtil::EnsureDirectory(installDirectory))
    {
        if (error != nullptr)
        {
            *error = L"Failed to create the install directory.";
        }
        return false;
    }

    if (!SumireInstallUtil::CopyFileIntoDirectory(dllPath, installDirectory) ||
        !SumireInstallUtil::CopyFileIntoDirectory(settingsPath, installDirectory) ||
        !SumireInstallUtil::CopyFileIntoDirectory(uninstallerPath, installDirectory))
    {
        if (error != nullptr)
        {
            *error = L"Failed to copy the main install files.";
        }
        return false;
    }

    if (!zenzServicePath.empty() &&
        !SumireInstallUtil::CopyFileIntoDirectory(zenzServicePath, installDirectory))
    {
        if (error != nullptr)
        {
            *error = L"Failed to copy SumireZenzService.exe.";
        }
        return false;
    }

    if (!romajiMapPath.empty() &&
        std::filesystem::exists(romajiMapPath) &&
        !SumireInstallUtil::CopyFileIntoDirectory(romajiMapPath, installDirectory))
    {
        if (error != nullptr)
        {
            *error = L"Failed to copy romaji-hiragana.tsv.";
        }
        return false;
    }

    if (!dictionariesPath.empty() &&
        std::filesystem::exists(dictionariesPath) &&
        !SumireInstallUtil::CopyDirectoryRecursive(dictionariesPath, installDirectory / L"dictionaries"))
    {
        if (error != nullptr)
        {
            *error = L"Failed to copy the dictionaries directory.";
        }
        return false;
    }

    if (!modelsPath.empty() &&
        std::filesystem::exists(modelsPath) &&
        !SumireInstallUtil::CopyDirectoryRecursive(modelsPath, installDirectory / L"models"))
    {
        if (error != nullptr)
        {
            *error = L"Failed to copy the models directory.";
        }
        return false;
    }

    return true;
}

std::wstring BuildDefaultZenzModelDownloadUrl()
{
    return std::wstring(kDefaultZenzModelRepo) + L"/resolve/main/ggml-model-Q5_K_M.gguf?download=true";
}

bool DownloadDefaultZenzModel(const std::filesystem::path& installDirectory, std::wstring* error)
{
    const std::filesystem::path modelPath = installDirectory / kDefaultZenzModelRelativePath;
    std::error_code ec;
    if (std::filesystem::exists(modelPath, ec) && !ec)
    {
        return true;
    }

    if (!SumireInstallUtil::EnsureDirectory(modelPath.parent_path()))
    {
        if (error != nullptr)
        {
            *error = L"Failed to create the model directory.";
        }
        return false;
    }

    const std::filesystem::path temporaryPath = modelPath.wstring() + L".download";
    std::filesystem::remove(temporaryPath, ec);
    ec.clear();

    const std::wstring downloadUrl = BuildDefaultZenzModelDownloadUrl();
    const HRESULT hr = URLDownloadToFileW(nullptr, downloadUrl.c_str(), temporaryPath.c_str(), 0, nullptr);
    if (FAILED(hr))
    {
        std::filesystem::remove(temporaryPath, ec);
        if (error != nullptr)
        {
            *error = L"Failed to download the default zenz model.";
        }
        return false;
    }

    if (!MoveFileExW(temporaryPath.c_str(), modelPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        std::filesystem::remove(temporaryPath, ec);
        if (error != nullptr)
        {
            *error = L"Failed to place the downloaded zenz model.";
        }
        return false;
    }

    return true;
}

std::wstring BrowseForFolder(HWND owner, const std::wstring& currentPath)
{
    BROWSEINFOW browseInfo = {};
    browseInfo.hwndOwner = owner;
    browseInfo.lpszTitle = L"Choose the Sumire IME install folder.";
    browseInfo.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE itemIdList = SHBrowseForFolderW(&browseInfo);
    if (itemIdList == nullptr)
    {
        return currentPath;
    }

    wchar_t path[MAX_PATH] = {};
    if (!SHGetPathFromIDListW(itemIdList, path))
    {
        CoTaskMemFree(itemIdList);
        return currentPath;
    }

    CoTaskMemFree(itemIdList);
    return path;
}

void SetVisible(HWND hwnd, bool visible)
{
    ShowWindow(hwnd, visible ? SW_SHOW : SW_HIDE);
}

void UpdateWizardPage(HWND hwnd)
{
    WizardState* state = GetWizardState(hwnd);
    if (state == nullptr)
    {
        return;
    }

    const bool showOptions = state->page == WizardPage::Options;
    const bool showLaunchSettings = state->page == WizardPage::Finish && state->installSucceeded;

    SetVisible(state->installPathLabel, showOptions);
    SetVisible(state->installPathEdit, showOptions);
    SetVisible(state->browseButton, showOptions);
    SetVisible(state->startMenuShortcutCheck, showOptions);
    SetVisible(state->desktopSettingsShortcutCheck, showOptions);
    SetVisible(state->downloadModelCheck, showOptions);
    SetVisible(state->launchSettingsCheck, showLaunchSettings);

    switch (state->page)
    {
    case WizardPage::Welcome:
        SetWindowTextW(state->title, L"Welcome to the Sumire IME Setup Wizard");
        SetWindowTextW(
            state->body,
            L"This wizard installs Sumire IME.\r\n\r\n"
            L"Choose Next to configure the install directory and shortcut options.");
        SetWindowTextW(state->nextButton, L"Next >");
        EnableWindow(state->backButton, FALSE);
        EnableWindow(state->nextButton, TRUE);
        SetVisible(state->cancelButton, true);
        SetWindowTextW(state->status, L"");
        break;

    case WizardPage::Options:
        SetWindowTextW(state->title, L"Install options");
        SetWindowTextW(
            state->body,
            L"Choose where to install Sumire IME and whether to create shortcuts.\r\n"
            L"You can also download the default zenz model during installation.");
        SetWindowTextW(state->nextButton, L"Install");
        EnableWindow(state->backButton, TRUE);
        EnableWindow(state->nextButton, TRUE);
        SetVisible(state->cancelButton, true);
        SetWindowTextW(state->status, L"");
        break;

    case WizardPage::Progress:
        SetWindowTextW(state->title, L"Installing");
        SetWindowTextW(
            state->body,
            L"Installing Sumire IME. Please wait until setup finishes.");
        SetWindowTextW(state->nextButton, L"Next >");
        EnableWindow(state->backButton, FALSE);
        EnableWindow(state->nextButton, FALSE);
        SetVisible(state->cancelButton, false);
        break;

    case WizardPage::Finish:
        if (state->installSucceeded)
        {
            SetWindowTextW(state->title, L"Setup complete");
            SetWindowTextW(
                state->body,
                L"Sumire IME was installed successfully.\r\n"
                L"If needed, sign out and back in so Windows reloads the IME.");
            SetWindowTextW(state->status, L"Setup completed successfully.");
        }
        else
        {
            SetWindowTextW(state->title, L"Setup failed");
            SetWindowTextW(
                state->body,
                L"Setup could not finish.\r\n"
                L"Review the error below and try again.");
        }

        SetWindowTextW(state->nextButton, L"Finish");
        EnableWindow(state->backButton, FALSE);
        EnableWindow(state->nextButton, TRUE);
        SetVisible(state->cancelButton, false);
        break;
    }
}

void RunInstall(HWND hwnd)
{
    WizardState* state = GetWizardState(hwnd);
    if (state == nullptr || state->installStarted)
    {
        return;
    }

    state->installStarted = true;
    SetWindowTextW(state->status, L"Starting setup...");
    UpdateWindow(hwnd);

    wchar_t installPath[MAX_PATH] = {};
    GetWindowTextW(state->installPathEdit, installPath, ARRAYSIZE(installPath));
    state->installDirectory = installPath;
    state->createStartMenuShortcuts = IsChecked(state->startMenuShortcutCheck);
    state->createDesktopSettingsShortcut = IsChecked(state->desktopSettingsShortcutCheck);
    state->downloadModelDuringInstall = IsChecked(state->downloadModelCheck);

    const std::filesystem::path installerPath = SumireInstallUtil::GetExecutablePath();
    const std::filesystem::path sourceDirectory = SumireInstallUtil::GetExecutableDirectory();
    const std::filesystem::path installDirectory = std::filesystem::path(state->installDirectory);

    const std::filesystem::path existingInstalledDll = FindFirstExistingFile(
        installDirectory,
        {L"Sumite-Desktop.dll", L"TextService.dll"});
    if (!existingInstalledDll.empty())
    {
        SumireInstallUtil::DeactivateTextServiceProfile();
        SumireInstallUtil::UnregisterTextServiceDll(existingInstalledDll);
    }

    SumireInstallUtil::StopProcessesByName(L"SumireZenzService.exe");
    SumireInstallUtil::StopProcessesByName(L"ctfmon.exe");

    std::wstring error;
    bool hasEmbeddedPayload = false;
    bool success = CopyEmbeddedPayload(installerPath, installDirectory, &error, &hasEmbeddedPayload);
    if (!hasEmbeddedPayload)
    {
        success = CopyPayloadFromDirectory(sourceDirectory, installDirectory, &error);
    }

    if (success && state->downloadModelDuringInstall)
    {
        SetWindowTextW(state->status, L"Downloading the default zenz model...");
        UpdateWindow(hwnd);
        success = DownloadDefaultZenzModel(installDirectory, &error);
    }

    const std::filesystem::path installedDll = FindFirstExistingFile(
        installDirectory,
        {L"Sumite-Desktop.dll", L"TextService.dll"});
    const std::filesystem::path installedSettings = installDirectory / L"SumireSettings.exe";
    const std::filesystem::path installedUninstaller = installDirectory / L"SumireUninstaller.exe";

    if (success && (installedDll.empty() || !SumireInstallUtil::RegisterTextServiceDll(installedDll)))
    {
        success = false;
        error = L"Failed to register the IME DLL.";
    }

    if (success && !SumireInstallUtil::ActivateTextServiceProfile())
    {
        success = false;
        error = L"Failed to activate the IME profile.";
    }

    if (success && !SumireInstallUtil::WriteInstallMetadata(installDirectory, installedUninstaller))
    {
        success = false;
        error = L"Failed to write install metadata.";
    }

    if (success)
    {
        const std::filesystem::path startMenuSettingsShortcut =
            SumireInstallUtil::GetStartMenuShortcutPath(kSettingsShortcutFileName);
        const std::filesystem::path startMenuUninstallShortcut =
            SumireInstallUtil::GetStartMenuShortcutPath(kUninstallShortcutFileName);
        const std::filesystem::path desktopSettingsShortcut =
            SumireInstallUtil::GetDesktopShortcutPath(kSettingsShortcutFileName);

        if (state->createStartMenuShortcuts)
        {
            SumireInstallUtil::CreateShortcut(
                startMenuSettingsShortcut,
                installedSettings,
                L"Open Sumire IME settings.",
                installedSettings);
            SumireInstallUtil::CreateShortcut(
                startMenuUninstallShortcut,
                installedUninstaller,
                L"Uninstall Sumire IME.",
                installedUninstaller);
        }
        else
        {
            SumireInstallUtil::RemoveShortcut(startMenuSettingsShortcut);
            SumireInstallUtil::RemoveShortcut(startMenuUninstallShortcut);
        }

        if (state->createDesktopSettingsShortcut)
        {
            SumireInstallUtil::CreateShortcut(
                desktopSettingsShortcut,
                installedSettings,
                L"Open Sumire IME settings.",
                installedSettings);
        }
        else
        {
            SumireInstallUtil::RemoveShortcut(desktopSettingsShortcut);
        }
    }

    state->installedSettingsPath = installedSettings.wstring();
    state->installSucceeded = success;
    state->installStarted = false;
    state->page = WizardPage::Finish;

    if (success)
    {
        SetChecked(state->launchSettingsCheck, false);
        SetWindowTextW(state->status, L"Setup completed successfully.");
    }
    else
    {
        if (error.empty())
        {
            error = L"Install failed. The IME DLL may still be in use. Sign out of Windows and try again.";
        }

        SetWindowTextW(state->status, error.c_str());
    }

    UpdateWizardPage(hwnd);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        {
            auto* state = new WizardState();
            state->installDirectory = SumireInstallUtil::GetDefaultInstallDirectory().wstring();
            SetWizardState(hwnd, state);

            state->title = CreateWindowW(
                L"STATIC",
                L"",
                WS_CHILD | WS_VISIBLE,
                16,
                12,
                480,
                24,
                hwnd,
                ControlMenu(IdTitle),
                nullptr,
                nullptr);
            SendMessageW(state->title, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

            state->body = CreateWindowW(
                L"STATIC",
                L"",
                WS_CHILD | WS_VISIBLE,
                16,
                46,
                480,
                72,
                hwnd,
                ControlMenu(IdBody),
                nullptr,
                nullptr);

            state->installPathLabel = CreateWindowW(
                L"STATIC",
                L"Install folder",
                WS_CHILD,
                16,
                128,
                120,
                20,
                hwnd,
                ControlMenu(IdInstallPathLabel),
                nullptr,
                nullptr);

            state->installPathEdit = CreateWindowW(
                L"EDIT",
                state->installDirectory.c_str(),
                WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                16,
                152,
                380,
                24,
                hwnd,
                ControlMenu(IdInstallPathEdit),
                nullptr,
                nullptr);

            state->browseButton = CreateWindowW(
                L"BUTTON",
                L"Browse...",
                WS_CHILD | BS_PUSHBUTTON,
                406,
                152,
                90,
                24,
                hwnd,
                ControlMenu(IdBrowse),
                nullptr,
                nullptr);

            state->startMenuShortcutCheck = CreateWindowW(
                L"BUTTON",
                L"Create Start Menu shortcuts",
                WS_CHILD | BS_AUTOCHECKBOX,
                16,
                188,
                280,
                24,
                hwnd,
                ControlMenu(IdStartMenuShortcutCheck),
                nullptr,
                nullptr);
            SetChecked(state->startMenuShortcutCheck, true);

            state->desktopSettingsShortcutCheck = CreateWindowW(
                L"BUTTON",
                L"Create a desktop shortcut for Sumire Settings",
                WS_CHILD | BS_AUTOCHECKBOX,
                16,
                216,
                340,
                24,
                hwnd,
                ControlMenu(IdDesktopSettingsShortcutCheck),
                nullptr,
                nullptr);
            SetChecked(state->desktopSettingsShortcutCheck, true);

            state->downloadModelCheck = CreateWindowW(
                L"BUTTON",
                L"Download the default zenz model during install",
                WS_CHILD | BS_AUTOCHECKBOX,
                16,
                244,
                360,
                24,
                hwnd,
                ControlMenu(IdDownloadModelCheck),
                nullptr,
                nullptr);
            SetChecked(state->downloadModelCheck, true);

            state->status = CreateWindowW(
                L"STATIC",
                L"",
                WS_CHILD | WS_VISIBLE,
                16,
                280,
                480,
                40,
                hwnd,
                ControlMenu(IdStatus),
                nullptr,
                nullptr);

            state->launchSettingsCheck = CreateWindowW(
                L"BUTTON",
                L"Open Sumire Settings after setup closes",
                WS_CHILD | BS_AUTOCHECKBOX,
                16,
                280,
                300,
                24,
                hwnd,
                ControlMenu(IdLaunchSettingsCheck),
                nullptr,
                nullptr);

            state->backButton = CreateWindowW(
                L"BUTTON",
                L"< Back",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                236,
                336,
                80,
                28,
                hwnd,
                ControlMenu(IdBack),
                nullptr,
                nullptr);

            state->nextButton = CreateWindowW(
                L"BUTTON",
                L"",
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                326,
                336,
                80,
                28,
                hwnd,
                ControlMenu(IdNext),
                nullptr,
                nullptr);

            state->cancelButton = CreateWindowW(
                L"BUTTON",
                L"Cancel",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                416,
                336,
                80,
                28,
                hwnd,
                ControlMenu(IdCancel),
                nullptr,
                nullptr);

            UpdateWizardPage(hwnd);
        }
        return 0;

    case WM_COMMAND:
        {
            WizardState* state = GetWizardState(hwnd);
            if (state == nullptr)
            {
                return 0;
            }

            switch (LOWORD(wParam))
            {
            case IdBrowse:
                {
                    wchar_t currentPath[MAX_PATH] = {};
                    GetWindowTextW(state->installPathEdit, currentPath, ARRAYSIZE(currentPath));
                    const std::wstring selected = BrowseForFolder(hwnd, currentPath);
                    SetWindowTextW(state->installPathEdit, selected.c_str());
                }
                return 0;

            case IdBack:
                if (state->page == WizardPage::Options)
                {
                    state->page = WizardPage::Welcome;
                    UpdateWizardPage(hwnd);
                }
                return 0;

            case IdNext:
                switch (state->page)
                {
                case WizardPage::Welcome:
                    state->page = WizardPage::Options;
                    UpdateWizardPage(hwnd);
                    return 0;

                case WizardPage::Options:
                    state->page = WizardPage::Progress;
                    UpdateWizardPage(hwnd);
                    PostMessageW(hwnd, WM_SUMIRE_START_INSTALL, 0, 0);
                    return 0;

                case WizardPage::Finish:
                    state->launchSettingsAfterFinish = IsChecked(state->launchSettingsCheck);
                    if (state->installSucceeded &&
                        state->launchSettingsAfterFinish &&
                        !state->installedSettingsPath.empty())
                    {
                        ShellExecuteW(
                            hwnd,
                            L"open",
                            state->installedSettingsPath.c_str(),
                            nullptr,
                            nullptr,
                            SW_SHOWNORMAL);
                    }

                    DestroyWindow(hwnd);
                    return 0;

                default:
                    return 0;
                }

            case IdCancel:
                DestroyWindow(hwnd);
                return 0;

            default:
                return 0;
            }
        }

    case WM_SUMIRE_START_INSTALL:
        RunInstall(hwnd);
        return 0;

    case WM_DESTROY:
        delete GetWizardState(hwnd);
        SetWizardState(hwnd, nullptr);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int commandShow)
{
    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kWindowClassName;
    if (!RegisterClassW(&windowClass))
    {
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        0,
        kWindowClassName,
        L"Sumire IME Setup",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        530,
        420,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (hwnd == nullptr)
    {
        return 1;
    }

    ShowWindow(hwnd, commandShow);
    UpdateWindow(hwnd);

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}
