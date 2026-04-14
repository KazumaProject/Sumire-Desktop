#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "PersonNameLexicon.h"
#include "KeymapStore.h"
#include "SumireSettingsStore.h"

namespace
{
constexpr wchar_t kWindowClassName[] = L"SumireSettingsWindow";
constexpr UINT WM_SUMIRE_BUILD_FINISHED = WM_APP + 0x230;
constexpr wchar_t kZenzServiceProcessName[] = L"SumireZenzService.exe";
constexpr wchar_t kUninstallerProcessName[] = L"SumireUninstaller.exe";

enum ControlId
{
    IdCheckLiveConversion = 101,
    IdEditCandidatePageSize = 102,
    IdEditRomajiMapPath = 103,
    IdButtonBrowseRomajiMap = 104,
    IdCheckZenzEnabled = 105,
    IdComboZenzModelPreset = 106,
    IdCheckZenzServiceEnabled = 107,
    IdEditZenzModelPath = 108,
    IdButtonBrowseZenzModel = 109,
    IdEditZenzModelRepo = 110,
    IdButtonSave = 111,
    IdButtonClose = 112,
    IdListDictionaries = 113,
    IdEditDictionaryName = 114,
    IdEditDictionarySource = 115,
    IdButtonBrowseDictionarySource = 116,
    IdCheckDictionaryEnabled = 117,
    IdButtonAddOrUpdateDictionary = 118,
    IdButtonRemoveDictionary = 119,
    IdButtonBuildSelected = 120,
    IdButtonBuildAll = 121,
    IdComboSettingsLanguage = 122,
    IdCheckLiveConversionSourceView = 123,
    IdButtonUninstall = 124,
    IdButtonKeymap = 125,
};

struct BuildResult
{
    bool success = false;
    std::wstring message;
    std::vector<SumireSettingsStore::UserDictionaryProfile> profiles;
};

struct WindowState
{
    std::wstring uiLanguage = L"ja";
    HWND liveConversion = nullptr;
    HWND liveConversionSourceView = nullptr;
    HWND settingsLanguage = nullptr;
    HWND candidatePageSize = nullptr;
    HWND romajiMapPath = nullptr;
    HWND zenzEnabled = nullptr;
    HWND zenzServiceEnabled = nullptr;
    HWND zenzServiceStatus = nullptr;
    HWND zenzModelPreset = nullptr;
    HWND zenzModelPath = nullptr;
    HWND zenzModelRepo = nullptr;
    HWND zenzWarning = nullptr;
    HWND dictionaryList = nullptr;
    HWND dictionaryName = nullptr;
    HWND dictionarySource = nullptr;
    HWND dictionaryEnabled = nullptr;
    HWND status = nullptr;

    std::vector<SumireSettingsStore::UserDictionaryProfile> profiles;
    int selectedProfileIndex = -1;
    bool buildInProgress = false;
    std::thread buildWorker;
};

bool LaunchExecutable(const std::filesystem::path& executablePath, DWORD creationFlags);

HMENU ControlMenu(int id)
{
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

void SetWindowState(HWND hwnd, WindowState* state)
{
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
}

WindowState* GetWindowState(HWND hwnd)
{
    return reinterpret_cast<WindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void SetCheckBoxState(HWND control, bool checked)
{
    SendMessageW(control, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
}

bool GetCheckBoxState(HWND control)
{
    return SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

std::wstring GetWindowTextString(HWND control)
{
    const int length = GetWindowTextLengthW(control);
    if (length <= 0)
    {
        return L"";
    }

    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(control, &value[0], length + 1);
    value.resize(static_cast<size_t>(length));
    return value;
}

std::wstring Trim(const std::wstring& value)
{
    size_t start = 0;
    while (start < value.size() && iswspace(value[start]) != 0)
    {
        ++start;
    }

    size_t end = value.size();
    while (end > start && iswspace(value[end - 1]) != 0)
    {
        --end;
    }

    return value.substr(start, end - start);
}

bool IsEnglishUiLanguage(const std::wstring& language)
{
    return language == L"en";
}

const wchar_t* UiText(const std::wstring& language, const wchar_t* ja, const wchar_t* en)
{
    return IsEnglishUiLanguage(language) ? en : ja;
}

std::wstring GetCurrentUiLanguage(HWND hwnd)
{
    WindowState* state = GetWindowState(hwnd);
    if (state == nullptr)
    {
        return L"ja";
    }

    if (state->settingsLanguage == nullptr)
    {
        return state->uiLanguage;
    }

    const LRESULT index = SendMessageW(state->settingsLanguage, CB_GETCURSEL, 0, 0);
    if (index == 1)
    {
        return L"en";
    }

    return L"ja";
}

std::wstring LocalizedText(HWND hwnd, const wchar_t* ja, const wchar_t* en)
{
    return UiText(GetCurrentUiLanguage(hwnd), ja, en);
}

void SetStatusText(HWND hwnd, const std::wstring& text)
{
    WindowState* state = GetWindowState(hwnd);
    if (state != nullptr && state->status != nullptr)
    {
        SetWindowTextW(state->status, text.c_str());
    }
}

std::filesystem::path GetModuleDirectory()
{
    std::wstring path(MAX_PATH, L'\0');
    for (;;)
    {
        const DWORD length = GetModuleFileNameW(nullptr, &path[0], static_cast<DWORD>(path.size()));
        if (length == 0)
        {
            return std::filesystem::current_path();
        }

        if (length < path.size() - 1)
        {
            path.resize(length);
            return std::filesystem::path(path).parent_path();
        }

        path.resize(path.size() * 2);
    }
}

std::wstring ReadInstallDirFromRegistry()
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(
            HKEY_CURRENT_USER,
            SumireSettingsStore::GetInstallRegistryPath().c_str(),
            0,
            KEY_READ,
            &key) != ERROR_SUCCESS)
    {
        return L"";
    }

    DWORD type = 0;
    DWORD size = 0;
    std::wstring value;
    if (RegQueryValueExW(key, L"InstallDir", nullptr, &type, nullptr, &size) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ) &&
        size > 0)
    {
        value.assign(size / sizeof(wchar_t), L'\0');
        if (RegQueryValueExW(
                key,
                L"InstallDir",
                nullptr,
                &type,
                reinterpret_cast<LPBYTE>(&value[0]),
                &size) == ERROR_SUCCESS)
        {
            while (!value.empty() && value.back() == L'\0')
            {
                value.pop_back();
            }
        }
        else
        {
            value.clear();
        }
    }

    RegCloseKey(key);
    return value;
}

std::filesystem::path GetDictionaryBuildDirectory()
{
    const std::wstring installDir = ReadInstallDirFromRegistry();
    if (!installDir.empty())
    {
        return std::filesystem::path(installDir) / L"dictionaries" / L"user" / L"build";
    }

    return GetModuleDirectory() / L"dictionaries" / L"user" / L"build";
}

bool EqualsIgnoreCase(const std::wstring& lhs, const std::wstring& rhs)
{
    return CompareStringOrdinal(lhs.c_str(), -1, rhs.c_str(), -1, TRUE) == CSTR_EQUAL;
}

std::filesystem::path GetInstalledExecutablePath(const wchar_t* fileName)
{
    std::filesystem::path root = GetModuleDirectory();
    const std::wstring installDir = ReadInstallDirFromRegistry();
    if (!installDir.empty())
    {
        root = std::filesystem::path(installDir);
    }

    const std::filesystem::path candidate = root / fileName;
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec) && !ec)
    {
        return candidate;
    }

    return std::filesystem::path();
}

std::filesystem::path GetZenzServiceExecutablePath()
{
    return GetInstalledExecutablePath(kZenzServiceProcessName);
}

std::filesystem::path GetUninstallerExecutablePath()
{
    return GetInstalledExecutablePath(kUninstallerProcessName);
}

std::vector<DWORD> GetZenzServiceProcessIds()
{
    std::vector<DWORD> processIds;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return processIds;
    }

    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (EqualsIgnoreCase(entry.szExeFile, kZenzServiceProcessName))
            {
                processIds.push_back(entry.th32ProcessID);
            }
        }
        while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return processIds;
}

bool IsZenzServiceRunning()
{
    return !GetZenzServiceProcessIds().empty();
}

bool StopZenzServiceProcesses()
{
    bool success = true;
    for (DWORD processId : GetZenzServiceProcessIds())
    {
        HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, processId);
        if (process == nullptr)
        {
            success = false;
            continue;
        }

        if (!TerminateProcess(process, 0))
        {
            success = false;
            CloseHandle(process);
            continue;
        }

        WaitForSingleObject(process, 2000);
        CloseHandle(process);
    }

    return success;
}

bool StartZenzServiceProcess()
{
    if (IsZenzServiceRunning())
    {
        return true;
    }

    const std::filesystem::path servicePath = GetZenzServiceExecutablePath();
    if (servicePath.empty())
    {
        return false;
    }

    return LaunchExecutable(servicePath, CREATE_NO_WINDOW);
}

bool PersistZenzServiceEnabledSetting(bool enabled)
{
    SumireSettingsStore::Settings settings = SumireSettingsStore::Load();
    settings.zenzServiceEnabled = enabled;
    return SumireSettingsStore::Save(settings);
}

bool LaunchExecutable(const std::filesystem::path& executablePath, DWORD creationFlags)
{
    if (executablePath.empty())
    {
        return false;
    }

    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo = {};
    std::wstring commandLine = L"\"";
    commandLine += executablePath.wstring();
    commandLine += L"\"";

    if (!CreateProcessW(
            nullptr,
            commandLine.data(),
            nullptr,
            nullptr,
            FALSE,
            creationFlags,
            nullptr,
            executablePath.parent_path().c_str(),
            &startupInfo,
            &processInfo))
    {
        return false;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return true;
}

void LaunchUninstaller(HWND hwnd)
{
    WindowState* state = GetWindowState(hwnd);
    if (state != nullptr && state->buildInProgress)
    {
        MessageBoxW(
            hwnd,
            UiText(
                GetCurrentUiLanguage(hwnd),
                L"辞書ビルド中はアンインストールできません。完了してから再実行してください。",
                L"Uninstall is unavailable while a dictionary build is running. Wait for it to finish first."),
            UiText(GetCurrentUiLanguage(hwnd), L"Sumire 設定", L"Sumire Settings"),
            MB_ICONINFORMATION | MB_OK);
        return;
    }

    if (MessageBoxW(
            hwnd,
            UiText(
                GetCurrentUiLanguage(hwnd),
                L"Sumire IME をアンインストールします。設定画面は閉じます。続行しますか？",
                L"Start uninstalling Sumire IME? Settings will close first."),
            UiText(GetCurrentUiLanguage(hwnd), L"Sumire 設定", L"Sumire Settings"),
            MB_ICONQUESTION | MB_OKCANCEL) != IDOK)
    {
        return;
    }

    const std::filesystem::path uninstallerPath = GetUninstallerExecutablePath();
    if (uninstallerPath.empty())
    {
        MessageBoxW(
            hwnd,
            UiText(
                GetCurrentUiLanguage(hwnd),
                L"アンインストーラーが見つかりませんでした。再インストール後に再度お試しください。",
                L"The uninstaller was not found. Reinstall Sumire and try again."),
            UiText(GetCurrentUiLanguage(hwnd), L"Sumire 設定", L"Sumire Settings"),
            MB_ICONERROR | MB_OK);
        return;
    }

    if (!LaunchExecutable(uninstallerPath, 0))
    {
        MessageBoxW(
            hwnd,
            UiText(
                GetCurrentUiLanguage(hwnd),
                L"アンインストーラーを起動できませんでした。",
                L"Failed to launch the uninstaller."),
            UiText(GetCurrentUiLanguage(hwnd), L"Sumire 設定", L"Sumire Settings"),
            MB_ICONERROR | MB_OK);
        return;
    }

    DestroyWindow(hwnd);
}

enum KeymapEditorControlId
{
    IdKeymapProfile = 301,
    IdKeymapList = 302,
    IdKeymapStatus = 303,
    IdKeymapKey = 304,
    IdKeymapCommand = 305,
    IdKeymapEnabled = 306,
    IdKeymapAdd = 307,
    IdKeymapUpdate = 308,
    IdKeymapRemove = 309,
    IdKeymapSave = 310,
    IdKeymapImportTsv = 311,
    IdKeymapExportTsv = 312,
    IdKeymapBackup = 313,
    IdKeymapOpenJson = 314,
    IdKeymapClose = 315,
};

struct KeymapEditorState
{
    SumireKeymap::Database database;
    HWND profileCombo = nullptr;
    HWND bindingList = nullptr;
    HWND statusEdit = nullptr;
    HWND keyEdit = nullptr;
    HWND commandEdit = nullptr;
    HWND enabledCheck = nullptr;
    HWND message = nullptr;
    int selectedProfileIndex = -1;
    int selectedBindingIndex = -1;
    bool closeRequested = false;
};

KeymapEditorState* GetKeymapEditorState(HWND hwnd)
{
    return reinterpret_cast<KeymapEditorState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

SumireKeymap::Profile* GetSelectedKeymapProfile(KeymapEditorState* state)
{
    if (state == nullptr ||
        state->selectedProfileIndex < 0 ||
        state->selectedProfileIndex >= static_cast<int>(state->database.profiles.size()))
    {
        return nullptr;
    }

    return &state->database.profiles[static_cast<size_t>(state->selectedProfileIndex)];
}

void SetKeymapEditorMessage(HWND hwnd, const std::wstring& text)
{
    KeymapEditorState* state = GetKeymapEditorState(hwnd);
    if (state != nullptr && state->message != nullptr)
    {
        SetWindowTextW(state->message, text.c_str());
    }
}

std::wstring BuildBindingListText(const SumireKeymap::Binding& binding)
{
    std::wstring text = binding.enabled ? L"[on] " : L"[off] ";
    text += binding.status;
    text += L"    ";
    text += binding.key;
    text += L"    ";
    text += binding.command;
    if (!SumireKeymap::IsSupportedCommand(binding.command))
    {
        text += L"    (unsupported)";
    }
    return text;
}

void PopulateKeymapBindingList(HWND hwnd)
{
    KeymapEditorState* state = GetKeymapEditorState(hwnd);
    if (state == nullptr)
    {
        return;
    }

    SendMessageW(state->bindingList, LB_RESETCONTENT, 0, 0);
    SumireKeymap::Profile* profile = GetSelectedKeymapProfile(state);
    if (profile == nullptr)
    {
        return;
    }

    for (const auto& binding : profile->bindings)
    {
        const std::wstring text = BuildBindingListText(binding);
        SendMessageW(state->bindingList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }
}

void PopulateKeymapProfiles(HWND hwnd)
{
    KeymapEditorState* state = GetKeymapEditorState(hwnd);
    if (state == nullptr)
    {
        return;
    }

    SendMessageW(state->profileCombo, CB_RESETCONTENT, 0, 0);
    state->selectedProfileIndex = -1;
    for (size_t i = 0; i < state->database.profiles.size(); ++i)
    {
        const auto& profile = state->database.profiles[i];
        std::wstring text = profile.name + L" (" + profile.id + L")";
        SendMessageW(state->profileCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        if (profile.id == state->database.activeProfileId)
        {
            state->selectedProfileIndex = static_cast<int>(i);
        }
    }

    if (state->selectedProfileIndex < 0 && !state->database.profiles.empty())
    {
        state->selectedProfileIndex = 0;
        state->database.activeProfileId = state->database.profiles.front().id;
    }

    if (state->selectedProfileIndex >= 0)
    {
        SendMessageW(state->profileCombo, CB_SETCURSEL, state->selectedProfileIndex, 0);
    }
    PopulateKeymapBindingList(hwnd);
}

void ClearKeymapBindingEditors(HWND hwnd)
{
    KeymapEditorState* state = GetKeymapEditorState(hwnd);
    if (state == nullptr)
    {
        return;
    }

    state->selectedBindingIndex = -1;
    SendMessageW(state->bindingList, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
    SetWindowTextW(state->statusEdit, L"Composition");
    SetWindowTextW(state->keyEdit, L"Ctrl a");
    SetWindowTextW(state->commandEdit, L"MoveCursorToBeginning");
    SetCheckBoxState(state->enabledCheck, true);
}

void LoadSelectedKeymapBinding(HWND hwnd)
{
    KeymapEditorState* state = GetKeymapEditorState(hwnd);
    SumireKeymap::Profile* profile = GetSelectedKeymapProfile(state);
    if (state == nullptr || profile == nullptr)
    {
        return;
    }

    const int index = static_cast<int>(SendMessageW(state->bindingList, LB_GETCURSEL, 0, 0));
    state->selectedBindingIndex = index;
    if (index < 0 || index >= static_cast<int>(profile->bindings.size()))
    {
        ClearKeymapBindingEditors(hwnd);
        return;
    }

    const auto& binding = profile->bindings[static_cast<size_t>(index)];
    SetWindowTextW(state->statusEdit, binding.status.c_str());
    SetWindowTextW(state->keyEdit, binding.key.c_str());
    SetWindowTextW(state->commandEdit, binding.command.c_str());
    SetCheckBoxState(state->enabledCheck, binding.enabled);
}

bool HasDuplicateKeymapBinding(const SumireKeymap::Profile& profile, const SumireKeymap::Binding& candidate, int exceptIndex)
{
    const std::wstring key = SumireKeymap::NormalizeKeyText(candidate.key);
    for (size_t i = 0; i < profile.bindings.size(); ++i)
    {
        if (static_cast<int>(i) == exceptIndex)
        {
            continue;
        }
        const auto& existing = profile.bindings[i];
        if (existing.status == candidate.status && SumireKeymap::NormalizeKeyText(existing.key) == key)
        {
            return true;
        }
    }
    return false;
}

bool SaveKeymapBindingFromEditors(HWND hwnd, bool append)
{
    KeymapEditorState* state = GetKeymapEditorState(hwnd);
    SumireKeymap::Profile* profile = GetSelectedKeymapProfile(state);
    if (state == nullptr || profile == nullptr)
    {
        return false;
    }

    SumireKeymap::Binding binding;
    binding.status = Trim(GetWindowTextString(state->statusEdit));
    binding.key = SumireKeymap::NormalizeKeyText(Trim(GetWindowTextString(state->keyEdit)));
    binding.command = Trim(GetWindowTextString(state->commandEdit));
    binding.enabled = GetCheckBoxState(state->enabledCheck);

    if (binding.status.empty() || binding.key.empty() || binding.command.empty())
    {
        MessageBoxW(hwnd, L"status, key, and command are required.", L"Keymap", MB_ICONWARNING | MB_OK);
        return false;
    }

    const int targetIndex = append ? -1 : state->selectedBindingIndex;
    if (HasDuplicateKeymapBinding(*profile, binding, targetIndex))
    {
        MessageBoxW(hwnd, L"The same status and key already exist in this profile.", L"Keymap", MB_ICONWARNING | MB_OK);
        return false;
    }

    if (!append && targetIndex >= 0 && targetIndex < static_cast<int>(profile->bindings.size()))
    {
        profile->bindings[static_cast<size_t>(targetIndex)] = binding;
    }
    else
    {
        profile->bindings.push_back(binding);
        state->selectedBindingIndex = static_cast<int>(profile->bindings.size() - 1);
    }
    profile->updatedAt = L"";

    PopulateKeymapBindingList(hwnd);
    SendMessageW(state->bindingList, LB_SETCURSEL, state->selectedBindingIndex, 0);
    LoadSelectedKeymapBinding(hwnd);
    SetKeymapEditorMessage(hwnd, L"Edited in memory. Choose Save to write keymaps.json.");
    return true;
}

void RemoveSelectedKeymapBinding(HWND hwnd)
{
    KeymapEditorState* state = GetKeymapEditorState(hwnd);
    SumireKeymap::Profile* profile = GetSelectedKeymapProfile(state);
    if (state == nullptr || profile == nullptr)
    {
        return;
    }

    if (state->selectedBindingIndex < 0 || state->selectedBindingIndex >= static_cast<int>(profile->bindings.size()))
    {
        return;
    }

    profile->bindings.erase(profile->bindings.begin() + state->selectedBindingIndex);
    ClearKeymapBindingEditors(hwnd);
    PopulateKeymapBindingList(hwnd);
    SetKeymapEditorMessage(hwnd, L"Removed in memory. Choose Save to write keymaps.json.");
}

std::wstring MakeProfileIdFromPath(const std::filesystem::path& path)
{
    std::wstring id = path.stem().wstring();
    for (wchar_t& ch : id)
    {
        if (!iswalnum(ch))
        {
            ch = L'-';
        }
        else
        {
            ch = static_cast<wchar_t>(towlower(ch));
        }
    }
    return id.empty() ? L"imported" : id;
}

void ImportKeymapTsv(HWND hwnd)
{
    KeymapEditorState* state = GetKeymapEditorState(hwnd);
    if (state == nullptr)
    {
        return;
    }

    std::wstring buffer(MAX_PATH, L'\0');
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd;
    dialog.lpstrFilter = L"TSV files (*.tsv)\0*.tsv\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&dialog))
    {
        return;
    }
    buffer.resize(wcslen(buffer.c_str()));

    const std::filesystem::path path(buffer);
    std::wstring id = MakeProfileIdFromPath(path);
    std::wstring uniqueId = id;
    int suffix = 2;
    bool duplicate = true;
    while (duplicate)
    {
        duplicate = false;
        for (const auto& profile : state->database.profiles)
        {
            if (profile.id == uniqueId)
            {
                duplicate = true;
                uniqueId = id + L"-" + std::to_wstring(suffix++);
                break;
            }
        }
    }

    std::wstring error;
    if (!SumireKeymap::ImportTsvFileAsProfile(path, &state->database, uniqueId, path.stem().wstring(), true, &error))
    {
        MessageBoxW(hwnd, error.c_str(), L"Keymap", MB_ICONERROR | MB_OK);
        return;
    }

    if (!SumireKeymap::SaveDatabase(state->database, &error))
    {
        MessageBoxW(hwnd, error.c_str(), L"Keymap", MB_ICONERROR | MB_OK);
        return;
    }

    PopulateKeymapProfiles(hwnd);
    ClearKeymapBindingEditors(hwnd);
    SetKeymapEditorMessage(hwnd, L"Imported TSV and saved keymaps.json.");
}

void ExportSelectedKeymapTsv(HWND hwnd)
{
    KeymapEditorState* state = GetKeymapEditorState(hwnd);
    SumireKeymap::Profile* profile = GetSelectedKeymapProfile(state);
    if (state == nullptr || profile == nullptr)
    {
        return;
    }

    std::wstring buffer(MAX_PATH, L'\0');
    const std::wstring defaultName = profile->id + L".tsv";
    wcsncpy_s(buffer.data(), buffer.size(), defaultName.c_str(), _TRUNCATE);
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd;
    dialog.lpstrFilter = L"TSV files (*.tsv)\0*.tsv\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrDefExt = L"tsv";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&dialog))
    {
        return;
    }
    buffer.resize(wcslen(buffer.c_str()));

    std::wstring error;
    if (!SumireKeymap::ExportProfileToTsv(state->database, profile->id, std::filesystem::path(buffer), &error))
    {
        MessageBoxW(hwnd, error.c_str(), L"Keymap", MB_ICONERROR | MB_OK);
        return;
    }
    SetKeymapEditorMessage(hwnd, L"Exported TSV backup.");
}

void BackupKeymapJson(HWND hwnd)
{
    std::filesystem::path backupPath;
    std::wstring error;
    if (!SumireKeymap::BackupDatabase(&backupPath, &error))
    {
        MessageBoxW(hwnd, error.c_str(), L"Keymap", MB_ICONERROR | MB_OK);
        return;
    }
    SetKeymapEditorMessage(hwnd, L"Backed up JSON: " + backupPath.wstring());
}

void OpenKeymapJsonInEditor(HWND hwnd)
{
    std::wstring error;
    if (!SumireKeymap::EnsureInitialized(&error))
    {
        MessageBoxW(hwnd, error.c_str(), L"Keymap", MB_ICONERROR | MB_OK);
        return;
    }

    const std::filesystem::path jsonPath = SumireKeymap::GetKeymapJsonPath();
    std::wstring commandLine = L"notepad.exe \"";
    commandLine += jsonPath.wstring();
    commandLine += L"\"";

    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo = {};
    if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startupInfo, &processInfo))
    {
        MessageBoxW(hwnd, L"Failed to launch notepad.", L"Keymap", MB_ICONERROR | MB_OK);
        return;
    }
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    SetKeymapEditorMessage(hwnd, L"Opened keymaps.json. Reopen this window after manual edits.");
}

bool SaveKeymapDatabaseFromEditor(HWND hwnd)
{
    KeymapEditorState* state = GetKeymapEditorState(hwnd);
    if (state == nullptr)
    {
        return false;
    }

    if (state->selectedProfileIndex >= 0 && state->selectedProfileIndex < static_cast<int>(state->database.profiles.size()))
    {
        state->database.activeProfileId = state->database.profiles[static_cast<size_t>(state->selectedProfileIndex)].id;
    }

    std::wstring error;
    if (!SumireKeymap::SaveDatabase(state->database, &error))
    {
        MessageBoxW(hwnd, error.c_str(), L"Keymap", MB_ICONERROR | MB_OK);
        return false;
    }

    SetKeymapEditorMessage(hwnd, L"Saved keymaps.json. Refocus the IME target app to reload it.");
    return true;
}

LRESULT CALLBACK KeymapEditorWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        {
            auto* state = new KeymapEditorState();
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

            std::wstring error;
            if (!SumireKeymap::LoadDatabase(&state->database, &error))
            {
                MessageBoxW(hwnd, error.c_str(), L"Keymap", MB_ICONERROR | MB_OK);
            }

            CreateWindowW(L"STATIC", L"Profile", WS_CHILD | WS_VISIBLE, 16, 16, 80, 20, hwnd, nullptr, nullptr, nullptr);
            state->profileCombo = CreateWindowW(WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWNLIST | WS_VSCROLL, 100, 14, 260, 160, hwnd, ControlMenu(IdKeymapProfile), nullptr, nullptr);
            CreateWindowW(L"BUTTON", L"Import TSV...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 380, 12, 110, 26, hwnd, ControlMenu(IdKeymapImportTsv), nullptr, nullptr);
            CreateWindowW(L"BUTTON", L"Export TSV...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 500, 12, 110, 26, hwnd, ControlMenu(IdKeymapExportTsv), nullptr, nullptr);
            CreateWindowW(L"BUTTON", L"Backup JSON", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 620, 12, 110, 26, hwnd, ControlMenu(IdKeymapBackup), nullptr, nullptr);

            state->bindingList = CreateWindowW(L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY, 16, 52, 714, 260, hwnd, ControlMenu(IdKeymapList), nullptr, nullptr);

            CreateWindowW(L"STATIC", L"status", WS_CHILD | WS_VISIBLE, 16, 330, 70, 20, hwnd, nullptr, nullptr, nullptr);
            state->statusEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 100, 326, 180, 24, hwnd, ControlMenu(IdKeymapStatus), nullptr, nullptr);
            CreateWindowW(L"STATIC", L"key", WS_CHILD | WS_VISIBLE, 300, 330, 70, 20, hwnd, nullptr, nullptr, nullptr);
            state->keyEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 360, 326, 180, 24, hwnd, ControlMenu(IdKeymapKey), nullptr, nullptr);
            state->enabledCheck = CreateWindowW(L"BUTTON", L"enabled", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 560, 326, 120, 24, hwnd, ControlMenu(IdKeymapEnabled), nullptr, nullptr);

            CreateWindowW(L"STATIC", L"command", WS_CHILD | WS_VISIBLE, 16, 362, 70, 20, hwnd, nullptr, nullptr, nullptr);
            state->commandEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 100, 358, 440, 24, hwnd, ControlMenu(IdKeymapCommand), nullptr, nullptr);
            CreateWindowW(L"BUTTON", L"Add", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 560, 356, 80, 26, hwnd, ControlMenu(IdKeymapAdd), nullptr, nullptr);
            CreateWindowW(L"BUTTON", L"Update", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 650, 356, 80, 26, hwnd, ControlMenu(IdKeymapUpdate), nullptr, nullptr);
            CreateWindowW(L"BUTTON", L"Remove", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 560, 388, 80, 26, hwnd, ControlMenu(IdKeymapRemove), nullptr, nullptr);
            CreateWindowW(L"BUTTON", L"Open JSON", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 650, 388, 80, 26, hwnd, ControlMenu(IdKeymapOpenJson), nullptr, nullptr);

            state->message = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 16, 424, 714, 44, hwnd, nullptr, nullptr, nullptr);
            CreateWindowW(L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 560, 480, 80, 28, hwnd, ControlMenu(IdKeymapSave), nullptr, nullptr);
            CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 650, 480, 80, 28, hwnd, ControlMenu(IdKeymapClose), nullptr, nullptr);

            PopulateKeymapProfiles(hwnd);
            ClearKeymapBindingEditors(hwnd);
            SetKeymapEditorMessage(hwnd, L"Unsupported commands are kept in JSON/TSV but are not executed yet.");
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IdKeymapProfile:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                KeymapEditorState* state = GetKeymapEditorState(hwnd);
                if (state != nullptr)
                {
                    state->selectedProfileIndex = static_cast<int>(SendMessageW(state->profileCombo, CB_GETCURSEL, 0, 0));
                    if (state->selectedProfileIndex >= 0 && state->selectedProfileIndex < static_cast<int>(state->database.profiles.size()))
                    {
                        state->database.activeProfileId = state->database.profiles[static_cast<size_t>(state->selectedProfileIndex)].id;
                    }
                    ClearKeymapBindingEditors(hwnd);
                    PopulateKeymapBindingList(hwnd);
                }
            }
            return 0;
        case IdKeymapList:
            if (HIWORD(wParam) == LBN_SELCHANGE)
            {
                LoadSelectedKeymapBinding(hwnd);
            }
            return 0;
        case IdKeymapAdd:
            SaveKeymapBindingFromEditors(hwnd, true);
            return 0;
        case IdKeymapUpdate:
            SaveKeymapBindingFromEditors(hwnd, false);
            return 0;
        case IdKeymapRemove:
            RemoveSelectedKeymapBinding(hwnd);
            return 0;
        case IdKeymapSave:
            SaveKeymapDatabaseFromEditor(hwnd);
            return 0;
        case IdKeymapImportTsv:
            ImportKeymapTsv(hwnd);
            return 0;
        case IdKeymapExportTsv:
            ExportSelectedKeymapTsv(hwnd);
            return 0;
        case IdKeymapBackup:
            BackupKeymapJson(hwnd);
            return 0;
        case IdKeymapOpenJson:
            OpenKeymapJsonInEditor(hwnd);
            return 0;
        case IdKeymapClose:
            DestroyWindow(hwnd);
            return 0;
        default:
            break;
        }
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        {
            KeymapEditorState* state = GetKeymapEditorState(hwnd);
            delete state;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void OpenKeymapEditor(HWND owner)
{
    constexpr wchar_t kKeymapEditorClassName[] = L"SumireKeymapEditorWindow";
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE));

    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = KeymapEditorWindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kKeymapEditorClassName;
    RegisterClassW(&windowClass);

    EnableWindow(owner, FALSE);
    HWND editor = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kKeymapEditorClassName,
        L"Sumire Keymap",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        764,
        560,
        owner,
        nullptr,
        instance,
        nullptr);

    if (editor == nullptr)
    {
        EnableWindow(owner, TRUE);
        return;
    }

    ShowWindow(editor, SW_SHOW);
    UpdateWindow(editor);

    MSG message = {};
    while (IsWindow(editor) && GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (!IsDialogMessageW(editor, &message))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
}

std::wstring GetDefaultZenzModelRepoForPreset(const std::wstring& preset)
{
    if (preset == L"xsmall")
    {
        return L"https://huggingface.co/Miwa-Keita/zenz-v3.1-xsmall-gguf";
    }

    if (preset == L"small")
    {
        return L"https://huggingface.co/Miwa-Keita/zenz-v3.1-small-gguf";
    }

    return L"https://huggingface.co/Miwa-Keita/zenz-v3.1-small-gguf";
}

std::wstring GetDefaultZenzModelPathForPreset(const std::wstring& preset)
{
    std::filesystem::path root = GetModuleDirectory();
    const std::wstring installDir = ReadInstallDirFromRegistry();
    if (!installDir.empty())
    {
        root = std::filesystem::path(installDir);
    }

    if (preset == L"xsmall")
    {
        return (root / L"models" / L"zenz-v3.1-xsmall-gguf" / L"ggml-model-Q5_K_M.gguf").wstring();
    }

    if (preset == L"small")
    {
        return (root / L"models" / L"zenz-v3.1-small-gguf" / L"ggml-model-Q5_K_M.gguf").wstring();
    }

    return (root / L"models" / L"zenz-v3.1-small-gguf" / L"ggml-model-Q5_K_M.gguf").wstring();
}

std::wstring GetComboBoxSelectedText(HWND control)
{
    const LRESULT index = SendMessageW(control, CB_GETCURSEL, 0, 0);
    if (index == CB_ERR)
    {
        return L"";
    }

    const LRESULT length = SendMessageW(control, CB_GETLBTEXTLEN, static_cast<WPARAM>(index), 0);
    if (length <= 0)
    {
        return L"";
    }

    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    SendMessageW(control, CB_GETLBTEXT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(value.data()));
    value.resize(static_cast<size_t>(length));
    return value;
}

std::wstring NormalizeZenzPresetLabel(const std::wstring& label)
{
    if (label == L"xsmall")
    {
        return L"xsmall";
    }
    if (label == L"small")
    {
        return L"small";
    }
    if (label == L"custom")
    {
        return L"custom";
    }
    return L"small";
}

std::wstring GetSelectedZenzModelPreset(HWND hwnd)
{
    WindowState* state = GetWindowState(hwnd);
    if (state == nullptr || state->zenzModelPreset == nullptr)
    {
        return L"small";
    }

    return NormalizeZenzPresetLabel(GetComboBoxSelectedText(state->zenzModelPreset));
}

void SetSelectedZenzModelPreset(HWND hwnd, const std::wstring& preset)
{
    WindowState* state = GetWindowState(hwnd);
    if (state == nullptr || state->zenzModelPreset == nullptr)
    {
        return;
    }

    int index = 0;
    if (preset == L"xsmall")
    {
        index = 1;
    }
    else if (preset == L"custom")
    {
        index = 2;
    }

    SendMessageW(state->zenzModelPreset, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
}

std::wstring ResolveZenzModelPath(const std::wstring& preset, const std::wstring& configuredPath)
{
    const std::wstring trimmedPath = Trim(configuredPath);
    if (!trimmedPath.empty())
    {
        return trimmedPath;
    }

    if (preset == L"custom")
    {
        return L"";
    }

    return GetDefaultZenzModelPathForPreset(preset);
}

void UpdateZenzModelWarning(HWND hwnd)
{
    WindowState* state = GetWindowState(hwnd);
    if (state == nullptr || state->zenzWarning == nullptr)
    {
        return;
    }

    if (!GetCheckBoxState(state->zenzEnabled))
    {
        SetWindowTextW(state->zenzWarning, L"");
        return;
    }

    const std::wstring preset = GetSelectedZenzModelPreset(hwnd);
    const std::wstring modelPath = ResolveZenzModelPath(preset, GetWindowTextString(state->zenzModelPath));
    if (modelPath.empty())
    {
        SetWindowTextW(state->zenzWarning, L"Warning: choose a GGUF model file before enabling zenz.");
        return;
    }

    std::error_code ec;
    if (std::filesystem::exists(std::filesystem::path(modelPath), ec) && !ec)
    {
        SetWindowTextW(state->zenzWarning, L"");
        return;
    }

    SetWindowTextW(
        state->zenzWarning,
        L"Warning: GGUF model not found. Download it during install or place it at the path above.");
}

void UpdateZenzServiceStatus(HWND hwnd)
{
    WindowState* state = GetWindowState(hwnd);
    if (state == nullptr || state->zenzServiceStatus == nullptr || state->zenzServiceEnabled == nullptr)
    {
        return;
    }

    const bool serviceEnabled = GetCheckBoxState(state->zenzServiceEnabled);
    const bool running = IsZenzServiceRunning();
    std::wstring text;
    if (!serviceEnabled)
    {
        text = running ? L"Service disabled (still stopping...)" : L"Service disabled";
    }
    else if (GetZenzServiceExecutablePath().empty())
    {
        text = L"Service executable not found";
    }
    else if (running)
    {
        text = L"Service running";
    }
    else
    {
        text = L"Service stopped (starts on demand)";
    }

    SetWindowTextW(state->zenzServiceStatus, text.c_str());
}

void HandleZenzServiceToggle(HWND hwnd)
{
    WindowState* state = GetWindowState(hwnd);
    if (state == nullptr || state->zenzServiceEnabled == nullptr)
    {
        return;
    }

    const bool enabled = GetCheckBoxState(state->zenzServiceEnabled);
    const bool actionSucceeded = enabled ? StartZenzServiceProcess() : StopZenzServiceProcesses();
    if (!PersistZenzServiceEnabledSetting(enabled))
    {
        MessageBoxW(
            hwnd,
            UiText(GetCurrentUiLanguage(hwnd), L"SumireZenzService の設定保存に失敗しました。", L"Failed to save SumireZenzService setting."),
            UiText(GetCurrentUiLanguage(hwnd), L"Sumire 設定", L"Sumire Settings"),
            MB_ICONERROR | MB_OK);
    }

    UpdateZenzServiceStatus(hwnd);
    if (!enabled)
    {
        SetStatusText(
            hwnd,
            actionSucceeded
                ? UiText(GetCurrentUiLanguage(hwnd), L"SumireZenzService を無効にしました。", L"SumireZenzService disabled.")
                : UiText(GetCurrentUiLanguage(hwnd), L"SumireZenzService を無効にしましたが、一部プロセスを停止できませんでした。", L"SumireZenzService was disabled, but some processes could not be stopped."));
        return;
    }

    SetStatusText(
        hwnd,
        actionSucceeded
            ? UiText(GetCurrentUiLanguage(hwnd), L"SumireZenzService を有効にしました。", L"SumireZenzService enabled.")
            : UiText(GetCurrentUiLanguage(hwnd), L"SumireZenzService を有効にしましたが起動していません。インストール済みの実行ファイルを確認してください。", L"SumireZenzService enabled, but it is not running. Check the installed service executable."));
}

void ApplyZenzPreset(HWND hwnd, const std::wstring& preset, bool overwriteModelPath)
{
    WindowState* state = GetWindowState(hwnd);
    if (state == nullptr)
    {
        return;
    }

    const std::wstring normalized = NormalizeZenzPresetLabel(preset);
    SetSelectedZenzModelPreset(hwnd, normalized);
    if (normalized == L"custom")
    {
        UpdateZenzModelWarning(hwnd);
        return;
    }

    SetWindowTextW(state->zenzModelRepo, GetDefaultZenzModelRepoForPreset(normalized).c_str());
    if (overwriteModelPath)
    {
        SetWindowTextW(state->zenzModelPath, GetDefaultZenzModelPathForPreset(normalized).c_str());
    }

    UpdateZenzModelWarning(hwnd);
}

std::wstring MakeProfileId(const std::wstring& name)
{
    std::wstring id;
    for (wchar_t ch : name)
    {
        if ((ch >= L'0' && ch <= L'9') ||
            (ch >= L'a' && ch <= L'z') ||
            (ch >= L'A' && ch <= L'Z'))
        {
            id.push_back(static_cast<wchar_t>(towlower(ch)));
        }
        else if (ch == L' ' || ch == L'-' || ch == L'_')
        {
            if (id.empty() || id.back() != L'_')
            {
                id.push_back(L'_');
            }
        }
    }

    if (id.empty())
    {
        id = L"dict";
    }

    id += L"_";
    id += std::to_wstring(static_cast<unsigned long long>(GetTickCount64()));
    return id;
}

std::filesystem::path GetBuildOutputPath(const SumireSettingsStore::UserDictionaryProfile& profile)
{
    if (!profile.builtPath.empty())
    {
        return std::filesystem::path(profile.builtPath);
    }

    return GetDictionaryBuildDirectory() / (profile.id + L".bin");
}

std::wstring BuildProfileLabel(const SumireSettingsStore::UserDictionaryProfile& profile)
{
    std::wstring label = profile.enabled ? L"[on] " : L"[off] ";
    label += profile.name.empty() ? L"(unnamed)" : profile.name;

    std::error_code ec;
    if (!profile.builtPath.empty() && std::filesystem::exists(std::filesystem::path(profile.builtPath), ec) && !ec)
    {
        label += L" [built]";
    }
    else
    {
        label += L" [not built]";
    }

    return label;
}

void PopulateDictionaryList(HWND hwnd)
{
    WindowState* state = GetWindowState(hwnd);
    if (state == nullptr || state->dictionaryList == nullptr)
    {
        return;
    }

    SendMessageW(state->dictionaryList, LB_RESETCONTENT, 0, 0);
    for (const auto& profile : state->profiles)
    {
        const std::wstring label = BuildProfileLabel(profile);
        SendMessageW(state->dictionaryList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }

    if (state->selectedProfileIndex >= 0 &&
        state->selectedProfileIndex < static_cast<int>(state->profiles.size()))
    {
        SendMessageW(state->dictionaryList, LB_SETCURSEL, state->selectedProfileIndex, 0);
    }
}

void LoadSelectedProfileIntoEditors(HWND hwnd)
{
    WindowState* state = GetWindowState(hwnd);
    if (state == nullptr)
    {
        return;
    }

    if (state->selectedProfileIndex < 0 ||
        state->selectedProfileIndex >= static_cast<int>(state->profiles.size()))
    {
        SetWindowTextW(state->dictionaryName, L"");
        SetWindowTextW(state->dictionarySource, L"");
        SetCheckBoxState(state->dictionaryEnabled, true);
        return;
    }

    const auto& profile = state->profiles[static_cast<size_t>(state->selectedProfileIndex)];
    SetWindowTextW(state->dictionaryName, profile.name.c_str());
    SetWindowTextW(state->dictionarySource, profile.sourcePath.c_str());
    SetCheckBoxState(state->dictionaryEnabled, profile.enabled);
}

void UpdateButtons(HWND hwnd)
{
    WindowState* state = GetWindowState(hwnd);
    if (state == nullptr)
    {
        return;
    }

    const bool hasSelection =
        state->selectedProfileIndex >= 0 &&
        state->selectedProfileIndex < static_cast<int>(state->profiles.size());
    const bool canBuildAll = !state->profiles.empty() && !state->buildInProgress;

    EnableWindow(GetDlgItem(hwnd, IdButtonRemoveDictionary), hasSelection && !state->buildInProgress);
    EnableWindow(GetDlgItem(hwnd, IdButtonBuildSelected), hasSelection && !state->buildInProgress);
    EnableWindow(GetDlgItem(hwnd, IdButtonBuildAll), canBuildAll);
    EnableWindow(GetDlgItem(hwnd, IdButtonUninstall), !state->buildInProgress);
    EnableWindow(GetDlgItem(hwnd, IdButtonClose), !state->buildInProgress);
}

bool SaveSettingsToStore(HWND hwnd, bool showStatus)
{
    WindowState* state = GetWindowState(hwnd);
    if (state == nullptr)
    {
        return false;
    }

    const std::wstring previousLanguage = state->uiLanguage;

    const int pageSize = _wtoi(GetWindowTextString(state->candidatePageSize).c_str());
    if (pageSize <= 0)
    {
        MessageBoxW(
            hwnd,
            UiText(GetCurrentUiLanguage(hwnd), L"候補数は 1 以上を指定してください。", L"Candidate page size must be 1 or greater."),
            UiText(GetCurrentUiLanguage(hwnd), L"Sumire 設定", L"Sumire Settings"),
            MB_ICONWARNING | MB_OK);
        return false;
    }

    SumireSettingsStore::Settings settings;
    settings.liveConversionEnabled = GetCheckBoxState(state->liveConversion);
    settings.liveConversionSourceViewEnabled = GetCheckBoxState(state->liveConversionSourceView);
    settings.candidatePageSize = pageSize;
    settings.settingsUiLanguage = GetCurrentUiLanguage(hwnd);
    settings.romajiMapPath = Trim(GetWindowTextString(state->romajiMapPath));
    settings.zenzEnabled = GetCheckBoxState(state->zenzEnabled);
    settings.zenzServiceEnabled = GetCheckBoxState(state->zenzServiceEnabled);
    settings.zenzModelPreset = GetSelectedZenzModelPreset(hwnd);
    settings.zenzModelPath = Trim(GetWindowTextString(state->zenzModelPath));
    settings.zenzModelRepo = Trim(GetWindowTextString(state->zenzModelRepo));
    settings.userDictionaryProfiles = state->profiles;

    if (!SumireSettingsStore::Save(settings))
    {
        MessageBoxW(
            hwnd,
            UiText(GetCurrentUiLanguage(hwnd), L"設定の保存に失敗しました。", L"Failed to save settings."),
            UiText(GetCurrentUiLanguage(hwnd), L"Sumire 設定", L"Sumire Settings"),
            MB_ICONERROR | MB_OK);
        return false;
    }

    state->uiLanguage = settings.settingsUiLanguage;

    if (showStatus)
    {
        if (previousLanguage != settings.settingsUiLanguage)
        {
            SetStatusText(
                hwnd,
                UiText(
                    settings.settingsUiLanguage,
                    L"設定を保存しました。表示言語を反映するには設定画面を開き直してください。",
                    L"Settings saved. Reopen Settings to apply the new language."));
        }
        else
        {
            SetStatusText(
                hwnd,
                UiText(
                    settings.settingsUiLanguage,
                    L"設定を保存しました。辞書の再読込には IME プロセスの再起動が必要です。",
                    L"Settings saved. Restart the IME process to reload dictionaries."));
        }
    }

    UpdateZenzModelWarning(hwnd);
    UpdateZenzServiceStatus(hwnd);
    return true;
}

void LoadSettingsIntoWindow(HWND hwnd)
{
    WindowState* state = GetWindowState(hwnd);
    if (state == nullptr)
    {
        return;
    }

    const SumireSettingsStore::Settings settings = SumireSettingsStore::Load();
    state->uiLanguage = settings.settingsUiLanguage;
    SetCheckBoxState(state->liveConversion, settings.liveConversionEnabled);
    SetCheckBoxState(state->liveConversionSourceView, settings.liveConversionSourceViewEnabled);
    SendMessageW(state->settingsLanguage, CB_SETCURSEL, settings.settingsUiLanguage == L"en" ? 1 : 0, 0);
    SetWindowTextW(state->candidatePageSize, std::to_wstring(settings.candidatePageSize).c_str());
    SetWindowTextW(state->romajiMapPath, settings.romajiMapPath.c_str());
    SetCheckBoxState(state->zenzEnabled, settings.zenzEnabled);
    SetCheckBoxState(state->zenzServiceEnabled, settings.zenzServiceEnabled);
    ApplyZenzPreset(hwnd, settings.zenzModelPreset, settings.zenzModelPath.empty());
    if (!settings.zenzModelPath.empty())
    {
        SetWindowTextW(state->zenzModelPath, settings.zenzModelPath.c_str());
    }
    SetWindowTextW(state->zenzModelRepo, settings.zenzModelRepo.c_str());
    state->profiles = settings.userDictionaryProfiles;
    state->selectedProfileIndex = state->profiles.empty() ? -1 : 0;

    PopulateDictionaryList(hwnd);
    LoadSelectedProfileIntoEditors(hwnd);
    UpdateButtons(hwnd);
    UpdateZenzModelWarning(hwnd);
    UpdateZenzServiceStatus(hwnd);
    SetStatusText(
        hwnd,
        UiText(
            state->uiLanguage,
            L"この画面から TXT をビルドします。IME 本体はビルド済み辞書のみを読み込み、zenz は llama.cpp で GGUF を読み込みます。",
            L"Build TXT files from this window. The IME loads built binaries only, and zenz loads GGUF through llama.cpp."));
}

void BrowseRomajiMapFile(HWND hwnd)
{
    WindowState* state = GetWindowState(hwnd);
    if (state == nullptr)
    {
        return;
    }

    std::wstring buffer(MAX_PATH, L'\0');
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd;
    dialog.lpstrFilter = L"TSV files (*.tsv)\0*.tsv\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = &buffer[0];
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&dialog))
    {
        SetWindowTextW(state->romajiMapPath, buffer.c_str());
    }
}

void BrowseZenzModelFile(HWND hwnd)
{
    WindowState* state = GetWindowState(hwnd);
    if (state == nullptr)
    {
        return;
    }

    std::wstring buffer(MAX_PATH, L'\0');
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd;
    dialog.lpstrFilter = L"Model files (*.gguf)\0*.gguf\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = &buffer[0];
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&dialog))
    {
        SetWindowTextW(state->zenzModelPath, buffer.c_str());
        UpdateZenzModelWarning(hwnd);
    }
}

void BrowseDictionarySourceFile(HWND hwnd)
{
    WindowState* state = GetWindowState(hwnd);
    if (state == nullptr)
    {
        return;
    }

    std::wstring buffer(MAX_PATH, L'\0');
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd;
    dialog.lpstrFilter = L"Dictionary text (*.txt)\0*.txt\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = &buffer[0];
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&dialog))
    {
        SetWindowTextW(state->dictionarySource, buffer.c_str());
    }
}

bool CollectProfileEditors(HWND hwnd, SumireSettingsStore::UserDictionaryProfile* profile, bool preserveIdentity)
{
    WindowState* state = GetWindowState(hwnd);
    if (state == nullptr || profile == nullptr)
    {
        return false;
    }

    const std::wstring name = Trim(GetWindowTextString(state->dictionaryName));
    const std::wstring sourcePath = Trim(GetWindowTextString(state->dictionarySource));
    if (name.empty())
    {
        MessageBoxW(
            hwnd,
            UiText(GetCurrentUiLanguage(hwnd), L"辞書名を入力してください。", L"Dictionary name is required."),
            UiText(GetCurrentUiLanguage(hwnd), L"Sumire 設定", L"Sumire Settings"),
            MB_ICONWARNING | MB_OK);
        return false;
    }

    if (sourcePath.empty())
    {
        MessageBoxW(
            hwnd,
            UiText(GetCurrentUiLanguage(hwnd), L"元 TXT のパスを入力してください。", L"Source TXT path is required."),
            UiText(GetCurrentUiLanguage(hwnd), L"Sumire 設定", L"Sumire Settings"),
            MB_ICONWARNING | MB_OK);
        return false;
    }

    if (!preserveIdentity || profile->id.empty())
    {
        profile->id = MakeProfileId(name);
        profile->builtPath.clear();
    }

    if (profile->sourcePath != sourcePath)
    {
        profile->builtPath.clear();
    }

    profile->name = name;
    profile->sourcePath = sourcePath;
    profile->enabled = GetCheckBoxState(state->dictionaryEnabled);
    return true;
}

void AddOrUpdateDictionaryProfile(HWND hwnd)
{
    WindowState* state = GetWindowState(hwnd);
    if (state == nullptr || state->buildInProgress)
    {
        return;
    }

    const bool hasSelection =
        state->selectedProfileIndex >= 0 &&
        state->selectedProfileIndex < static_cast<int>(state->profiles.size());

    SumireSettingsStore::UserDictionaryProfile profile;
    if (hasSelection)
    {
        profile = state->profiles[static_cast<size_t>(state->selectedProfileIndex)];
    }

    if (!CollectProfileEditors(hwnd, &profile, hasSelection))
    {
        return;
    }

    if (hasSelection)
    {
        state->profiles[static_cast<size_t>(state->selectedProfileIndex)] = profile;
    }
    else
    {
        state->profiles.push_back(profile);
        state->selectedProfileIndex = static_cast<int>(state->profiles.size()) - 1;
    }

    PopulateDictionaryList(hwnd);
    LoadSelectedProfileIntoEditors(hwnd);
    UpdateButtons(hwnd);
    SaveSettingsToStore(hwnd, false);
    SetStatusText(hwnd, UiText(GetCurrentUiLanguage(hwnd), L"辞書プロファイルを更新しました。", L"Dictionary profile updated."));
}

void RemoveSelectedDictionaryProfile(HWND hwnd)
{
    WindowState* state = GetWindowState(hwnd);
    if (state == nullptr || state->buildInProgress)
    {
        return;
    }

    if (state->selectedProfileIndex < 0 ||
        state->selectedProfileIndex >= static_cast<int>(state->profiles.size()))
    {
        return;
    }

    const auto profile = state->profiles[static_cast<size_t>(state->selectedProfileIndex)];
    if (!profile.builtPath.empty())
    {
        std::error_code ec;
        std::filesystem::remove(std::filesystem::path(profile.builtPath), ec);
    }

    state->profiles.erase(state->profiles.begin() + state->selectedProfileIndex);
    if (state->selectedProfileIndex >= static_cast<int>(state->profiles.size()))
    {
        state->selectedProfileIndex = static_cast<int>(state->profiles.size()) - 1;
    }

    PopulateDictionaryList(hwnd);
    LoadSelectedProfileIntoEditors(hwnd);
    UpdateButtons(hwnd);
    SaveSettingsToStore(hwnd, false);
    SetStatusText(hwnd, UiText(GetCurrentUiLanguage(hwnd), L"辞書プロファイルを削除しました。", L"Dictionary profile removed."));
}

void StartDictionaryBuild(HWND hwnd, bool buildAll)
{
    WindowState* state = GetWindowState(hwnd);
    if (state == nullptr || state->buildInProgress)
    {
        return;
    }

    std::vector<SumireSettingsStore::UserDictionaryProfile> targets;
    if (buildAll)
    {
        targets = state->profiles;
    }
    else if (state->selectedProfileIndex >= 0 &&
             state->selectedProfileIndex < static_cast<int>(state->profiles.size()))
    {
        targets.push_back(state->profiles[static_cast<size_t>(state->selectedProfileIndex)]);
    }

    if (targets.empty())
    {
        SetStatusText(hwnd, UiText(GetCurrentUiLanguage(hwnd), L"辞書プロファイルが選択されていません。", L"No dictionary profile is selected."));
        return;
    }

    for (auto& profile : targets)
    {
        if (profile.id.empty() || profile.name.empty() || profile.sourcePath.empty())
        {
            SetStatusText(hwnd, UiText(GetCurrentUiLanguage(hwnd), L"選択中のプロファイルが未完成です。先に追加 / 更新を実行してください。", L"Selected profile is incomplete. Use Add/Update first."));
            return;
        }

        profile.builtPath = GetBuildOutputPath(profile).wstring();
    }

    state->buildInProgress = true;
    UpdateButtons(hwnd);
    SetStatusText(
        hwnd,
        buildAll
            ? UiText(GetCurrentUiLanguage(hwnd), L"すべての辞書プロファイルをビルドしています...", L"Building all dictionary profiles...")
            : UiText(GetCurrentUiLanguage(hwnd), L"選択中の辞書プロファイルをビルドしています...", L"Building selected dictionary profile..."));

    if (state->buildWorker.joinable())
    {
        state->buildWorker.join();
    }

    const std::wstring uiLanguage = GetCurrentUiLanguage(hwnd);
    state->buildWorker = std::thread([hwnd, targets, uiLanguage]()
    {
        auto result = std::make_unique<BuildResult>();
        result->success = true;
        result->profiles = targets;

        std::vector<std::future<std::wstring>> buildFutures;
        buildFutures.reserve(result->profiles.size());
        for (const auto& profile : result->profiles)
        {
            buildFutures.push_back(std::async(std::launch::async, [profile]()
            {
                std::wstring errorMessage;
                if (!UserDictionaryLexicon::BuildBinaryFromText(profile.sourcePath, profile.builtPath, &errorMessage))
                {
                    return errorMessage.empty() ? std::wstring(L"build failed") : errorMessage;
                }

                return std::wstring();
            }));
        }

        std::wstring message;
        for (size_t index = 0; index < result->profiles.size(); ++index)
        {
            const std::wstring errorMessage = buildFutures[index].get();
            if (!errorMessage.empty())
            {
                const auto& profile = result->profiles[index];
                result->success = false;
                if (!message.empty())
                {
                    message += L"\r\n";
                }

                message += profile.name;
                message += L": ";
                message += errorMessage == L"build failed"
                    ? UiText(uiLanguage, L"ビルド失敗", L"build failed")
                    : errorMessage;
            }
        }

        if (message.empty())
        {
            message = result->success
                ? UiText(uiLanguage, L"辞書ビルドが完了しました。ビルド済み辞書を再読込するには IME プロセスを再起動してください。", L"Dictionary build completed. Restart the IME process to reload built binaries.")
                : UiText(uiLanguage, L"辞書ビルドに失敗しました。", L"Dictionary build failed.");
        }
        result->message = std::move(message);

        PostMessageW(hwnd, WM_SUMIRE_BUILD_FINISHED, 0, reinterpret_cast<LPARAM>(result.release()));
    });
}

void FinishDictionaryBuild(HWND hwnd, BuildResult* result)
{
    std::unique_ptr<BuildResult> ownedResult(result);
    WindowState* state = GetWindowState(hwnd);
    if (state == nullptr)
    {
        return;
    }

    if (state->buildWorker.joinable())
    {
        state->buildWorker.join();
    }

    state->buildInProgress = false;

    if (ownedResult != nullptr)
    {
        for (const auto& updatedProfile : ownedResult->profiles)
        {
            for (auto& profile : state->profiles)
            {
                if (profile.id == updatedProfile.id)
                {
                    profile.builtPath = updatedProfile.builtPath;
                    break;
                }
            }
        }

        SaveSettingsToStore(hwnd, false);
        SetStatusText(hwnd, ownedResult->message);
    }

    PopulateDictionaryList(hwnd);
    LoadSelectedProfileIntoEditors(hwnd);
    UpdateButtons(hwnd);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        {
            auto* state = new WindowState();
            state->uiLanguage = SumireSettingsStore::Load().settingsUiLanguage;
            SetWindowState(hwnd, state);

            CreateWindowW(
                L"STATIC",
                UiText(state->uiLanguage, L"全般", L"General"),
                WS_CHILD | WS_VISIBLE,
                16,
                16,
                120,
                20,
                hwnd,
                nullptr,
                nullptr,
                nullptr);
            state->liveConversion = CreateWindowW(
                L"BUTTON",
                UiText(state->uiLanguage, L"ライブ変換プレビューを有効にする", L"Enable live conversion preview"),
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                16,
                44,
                240,
                22,
                hwnd,
                ControlMenu(IdCheckLiveConversion),
                nullptr,
                nullptr);
            state->liveConversionSourceView = CreateWindowW(
                L"BUTTON",
                UiText(state->uiLanguage, L"ひらがな補助ビューを表示", L"Show hiragana helper view"),
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                280,
                44,
                220,
                22,
                hwnd,
                ControlMenu(IdCheckLiveConversionSourceView),
                nullptr,
                nullptr);

            CreateWindowW(
                L"STATIC",
                UiText(state->uiLanguage, L"候補数", L"Candidate page size"),
                WS_CHILD | WS_VISIBLE,
                16,
                76,
                140,
                20,
                hwnd,
                nullptr,
                nullptr,
                nullptr);
            state->candidatePageSize = CreateWindowW(
                L"EDIT",
                L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                160,
                74,
                80,
                24,
                hwnd,
                ControlMenu(IdEditCandidatePageSize),
                nullptr,
                nullptr);
            CreateWindowW(
                L"STATIC",
                UiText(state->uiLanguage, L"表示言語", L"UI language"),
                WS_CHILD | WS_VISIBLE,
                280,
                76,
                72,
                20,
                hwnd,
                nullptr,
                nullptr,
                nullptr);
            state->settingsLanguage = CreateWindowW(
                WC_COMBOBOXW,
                L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWNLIST | WS_VSCROLL,
                360,
                74,
                140,
                120,
                hwnd,
                ControlMenu(IdComboSettingsLanguage),
                nullptr,
                nullptr);
            SendMessageW(state->settingsLanguage, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"日本語"));
            SendMessageW(state->settingsLanguage, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"English"));

            CreateWindowW(
                L"STATIC",
                UiText(state->uiLanguage, L"ローマ字マップ", L"Romaji map"),
                WS_CHILD | WS_VISIBLE,
                16,
                108,
                140,
                20,
                hwnd,
                nullptr,
                nullptr,
                nullptr);
            state->romajiMapPath = CreateWindowW(
                L"EDIT",
                L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                160,
                106,
                360,
                24,
                hwnd,
                ControlMenu(IdEditRomajiMapPath),
                nullptr,
                nullptr);
            CreateWindowW(
                L"BUTTON",
                UiText(state->uiLanguage, L"参照...", L"Browse..."),
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                530,
                106,
                90,
                24,
                hwnd,
                ControlMenu(IdButtonBrowseRomajiMap),
                nullptr,
                nullptr);

            CreateWindowW(
                L"STATIC",
                L"Zenz",
                WS_CHILD | WS_VISIBLE,
                16,
                150,
                120,
                20,
                hwnd,
                nullptr,
                nullptr,
                nullptr);
            state->zenzEnabled = CreateWindowW(
                L"BUTTON",
                UiText(state->uiLanguage, L"zenz 融合を有効にする", L"Enable zenz fusion"),
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                16,
                178,
                240,
                22,
                hwnd,
                ControlMenu(IdCheckZenzEnabled),
                nullptr,
                nullptr);
            state->zenzServiceEnabled = CreateWindowW(
                L"BUTTON",
                UiText(state->uiLanguage, L"サービスプロセスを有効にする", L"Enable service process"),
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                280,
                178,
                190,
                22,
                hwnd,
                ControlMenu(IdCheckZenzServiceEnabled),
                nullptr,
                nullptr);
            state->zenzServiceStatus = CreateWindowW(
                L"STATIC",
                L"",
                WS_CHILD | WS_VISIBLE,
                472,
                180,
                148,
                20,
                hwnd,
                nullptr,
                nullptr,
                nullptr);

            CreateWindowW(
                L"STATIC",
                UiText(state->uiLanguage, L"モデル preset", L"Model preset"),
                WS_CHILD | WS_VISIBLE,
                16,
                210,
                140,
                20,
                hwnd,
                nullptr,
                nullptr,
                nullptr);
            state->zenzModelPreset = CreateWindowW(
                WC_COMBOBOXW,
                L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWNLIST | WS_VSCROLL,
                160,
                208,
                220,
                120,
                hwnd,
                ControlMenu(IdComboZenzModelPreset),
                nullptr,
                nullptr);
            SendMessageW(state->zenzModelPreset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"small"));
            SendMessageW(state->zenzModelPreset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"xsmall"));
            SendMessageW(state->zenzModelPreset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"custom"));

            CreateWindowW(
                L"STATIC",
                UiText(state->uiLanguage, L"モデルファイル", L"Model file"),
                WS_CHILD | WS_VISIBLE,
                16,
                242,
                140,
                20,
                hwnd,
                nullptr,
                nullptr,
                nullptr);
            state->zenzModelPath = CreateWindowW(
                L"EDIT",
                L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                160,
                240,
                360,
                24,
                hwnd,
                ControlMenu(IdEditZenzModelPath),
                nullptr,
                nullptr);
            CreateWindowW(
                L"BUTTON",
                UiText(state->uiLanguage, L"参照...", L"Browse..."),
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                530,
                240,
                90,
                24,
                hwnd,
                ControlMenu(IdButtonBrowseZenzModel),
                nullptr,
                nullptr);

            CreateWindowW(
                L"STATIC",
                UiText(state->uiLanguage, L"既定モデル repo", L"Default model repo"),
                WS_CHILD | WS_VISIBLE,
                16,
                274,
                140,
                20,
                hwnd,
                nullptr,
                nullptr,
                nullptr);
            state->zenzModelRepo = CreateWindowW(
                L"EDIT",
                L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                160,
                272,
                460,
                24,
                hwnd,
                ControlMenu(IdEditZenzModelRepo),
                nullptr,
                nullptr);
            state->zenzWarning = CreateWindowW(
                L"STATIC",
                L"",
                WS_CHILD | WS_VISIBLE,
                160,
                302,
                460,
                34,
                hwnd,
                nullptr,
                nullptr,
                nullptr);

            CreateWindowW(
                L"STATIC",
                UiText(state->uiLanguage, L"ユーザー辞書ビルド", L"User dictionary builds"),
                WS_CHILD | WS_VISIBLE,
                16,
                352,
                220,
                20,
                hwnd,
                nullptr,
                nullptr,
                nullptr);
            state->dictionaryList = CreateWindowW(
                L"LISTBOX",
                L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY | WS_VSCROLL,
                16,
                378,
                250,
                170,
                hwnd,
                ControlMenu(IdListDictionaries),
                nullptr,
                nullptr);

            CreateWindowW(
                L"STATIC",
                UiText(state->uiLanguage, L"名前", L"Name"),
                WS_CHILD | WS_VISIBLE,
                282,
                378,
                100,
                20,
                hwnd,
                nullptr,
                nullptr,
                nullptr);
            state->dictionaryName = CreateWindowW(
                L"EDIT",
                L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                282,
                400,
                338,
                24,
                hwnd,
                ControlMenu(IdEditDictionaryName),
                nullptr,
                nullptr);

            CreateWindowW(
                L"STATIC",
                UiText(state->uiLanguage, L"元 TXT", L"Source TXT"),
                WS_CHILD | WS_VISIBLE,
                282,
                432,
                100,
                20,
                hwnd,
                nullptr,
                nullptr,
                nullptr);
            state->dictionarySource = CreateWindowW(
                L"EDIT",
                L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                282,
                454,
                244,
                24,
                hwnd,
                ControlMenu(IdEditDictionarySource),
                nullptr,
                nullptr);
            CreateWindowW(
                L"BUTTON",
                UiText(state->uiLanguage, L"参照...", L"Browse..."),
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                530,
                454,
                90,
                24,
                hwnd,
                ControlMenu(IdButtonBrowseDictionarySource),
                nullptr,
                nullptr);

            state->dictionaryEnabled = CreateWindowW(
                L"BUTTON",
                UiText(state->uiLanguage, L"実行時に有効", L"Enabled at runtime"),
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                282,
                488,
                160,
                22,
                hwnd,
                ControlMenu(IdCheckDictionaryEnabled),
                nullptr,
                nullptr);

            CreateWindowW(
                L"BUTTON",
                UiText(state->uiLanguage, L"追加 / 更新", L"Add / Update"),
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                282,
                520,
                100,
                28,
                hwnd,
                ControlMenu(IdButtonAddOrUpdateDictionary),
                nullptr,
                nullptr);
            CreateWindowW(
                L"BUTTON",
                UiText(state->uiLanguage, L"削除", L"Remove"),
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                392,
                520,
                80,
                28,
                hwnd,
                ControlMenu(IdButtonRemoveDictionary),
                nullptr,
                nullptr);
            CreateWindowW(
                L"BUTTON",
                UiText(state->uiLanguage, L"選択をビルド", L"Build selected"),
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                482,
                520,
                138,
                28,
                hwnd,
                ControlMenu(IdButtonBuildSelected),
                nullptr,
                nullptr);
            CreateWindowW(
                L"BUTTON",
                UiText(state->uiLanguage, L"すべてビルド", L"Build all"),
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                482,
                554,
                138,
                28,
                hwnd,
                ControlMenu(IdButtonBuildAll),
                nullptr,
                nullptr);

            CreateWindowW(
                L"BUTTON",
                UiText(state->uiLanguage, L"アンインストール...", L"Uninstall..."),
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                16,
                644,
                120,
                28,
                hwnd,
                ControlMenu(IdButtonUninstall),
                nullptr,
                nullptr);
            CreateWindowW(
                L"BUTTON",
                L"Keymap...",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                150,
                644,
                120,
                28,
                hwnd,
                ControlMenu(IdButtonKeymap),
                nullptr,
                nullptr);

            state->status = CreateWindowW(
                L"STATIC",
                L"",
                WS_CHILD | WS_VISIBLE,
                16,
                594,
                604,
                42,
                hwnd,
                nullptr,
                nullptr,
                nullptr);

            CreateWindowW(
                L"BUTTON",
                UiText(state->uiLanguage, L"保存", L"Save"),
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                450,
                644,
                80,
                28,
                hwnd,
                ControlMenu(IdButtonSave),
                nullptr,
                nullptr);
            CreateWindowW(
                L"BUTTON",
                UiText(state->uiLanguage, L"閉じる", L"Close"),
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                540,
                644,
                80,
                28,
                hwnd,
                ControlMenu(IdButtonClose),
                nullptr,
                nullptr);

            LoadSettingsIntoWindow(hwnd);
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IdButtonBrowseRomajiMap:
            BrowseRomajiMapFile(hwnd);
            return 0;
        case IdButtonBrowseZenzModel:
            BrowseZenzModelFile(hwnd);
            return 0;
        case IdButtonBrowseDictionarySource:
            BrowseDictionarySourceFile(hwnd);
            return 0;
        case IdButtonAddOrUpdateDictionary:
            AddOrUpdateDictionaryProfile(hwnd);
            return 0;
        case IdButtonRemoveDictionary:
            RemoveSelectedDictionaryProfile(hwnd);
            return 0;
        case IdButtonBuildSelected:
            StartDictionaryBuild(hwnd, false);
            return 0;
        case IdButtonBuildAll:
            StartDictionaryBuild(hwnd, true);
            return 0;
        case IdButtonSave:
            SaveSettingsToStore(hwnd, true);
            return 0;
        case IdButtonUninstall:
            LaunchUninstaller(hwnd);
            return 0;
        case IdButtonKeymap:
            OpenKeymapEditor(hwnd);
            return 0;
        case IdButtonClose:
            if (!GetWindowState(hwnd)->buildInProgress)
            {
                DestroyWindow(hwnd);
            }
            return 0;
        case IdListDictionaries:
            if (HIWORD(wParam) == LBN_SELCHANGE)
            {
                WindowState* state = GetWindowState(hwnd);
                if (state != nullptr)
                {
                    state->selectedProfileIndex = static_cast<int>(
                        SendMessageW(state->dictionaryList, LB_GETCURSEL, 0, 0));
                    LoadSelectedProfileIntoEditors(hwnd);
                    UpdateButtons(hwnd);
                }
            }
            return 0;
        case IdComboZenzModelPreset:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                ApplyZenzPreset(hwnd, GetSelectedZenzModelPreset(hwnd), true);
            }
            return 0;
        case IdCheckZenzEnabled:
            if (HIWORD(wParam) == BN_CLICKED)
            {
                UpdateZenzModelWarning(hwnd);
            }
            return 0;
        case IdCheckZenzServiceEnabled:
            if (HIWORD(wParam) == BN_CLICKED)
            {
                HandleZenzServiceToggle(hwnd);
            }
            return 0;
        case IdEditZenzModelPath:
            if (HIWORD(wParam) == EN_CHANGE)
            {
                UpdateZenzModelWarning(hwnd);
            }
            return 0;
        case IdComboSettingsLanguage:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                SetStatusText(
                    hwnd,
                    UiText(
                        GetCurrentUiLanguage(hwnd),
                        L"表示言語の変更は保存後、設定画面を開き直すと全面反映されます。",
                        L"Save and reopen Settings to apply the language change to all labels."));
            }
            return 0;
        default:
            break;
        }
        return 0;

    case WM_SUMIRE_BUILD_FINISHED:
        FinishDictionaryBuild(hwnd, reinterpret_cast<BuildResult*>(lParam));
        return 0;

    case WM_ACTIVATE:
        UpdateZenzServiceStatus(hwnd);
        return 0;

    case WM_CLOSE:
        if (GetWindowState(hwnd) != nullptr && GetWindowState(hwnd)->buildInProgress)
        {
            MessageBoxW(
                hwnd,
                UiText(GetCurrentUiLanguage(hwnd), L"辞書ビルドがまだ実行中です。完了するまでお待ちください。", L"Dictionary build is still running. Please wait for it to finish."),
                UiText(GetCurrentUiLanguage(hwnd), L"Sumire 設定", L"Sumire Settings"),
                MB_ICONINFORMATION | MB_OK);
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        {
            WindowState* state = GetWindowState(hwnd);
            if (state != nullptr && state->buildWorker.joinable())
            {
                state->buildWorker.join();
            }

            delete state;
            SetWindowState(hwnd, nullptr);
            PostQuitMessage(0);
        }
        return 0;

    case WM_CTLCOLORSTATIC:
        {
            WindowState* state = GetWindowState(hwnd);
            if (state != nullptr && reinterpret_cast<HWND>(lParam) == state->zenzWarning)
            {
                HDC dc = reinterpret_cast<HDC>(wParam);
                SetTextColor(dc, RGB(180, 32, 32));
                SetBkMode(dc, TRANSPARENT);
                return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
            }
        }
        break;

    default:
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int commandShow)
{
    const std::wstring uiLanguage = SumireSettingsStore::Load().settingsUiLanguage;
    INITCOMMONCONTROLSEX commonControls = {};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&commonControls);

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
        UiText(uiLanguage, L"Sumire 設定", L"Sumire Settings"),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        652,
        724,
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
