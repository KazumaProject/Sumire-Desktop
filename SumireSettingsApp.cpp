#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "PersonNameLexicon.h"
#include "SumireSettingsStore.h"

namespace
{
constexpr wchar_t kWindowClassName[] = L"SumireSettingsWindow";
constexpr UINT WM_SUMIRE_BUILD_FINISHED = WM_APP + 0x230;
constexpr wchar_t kZenzServiceProcessName[] = L"SumireZenzService.exe";

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
};

struct BuildResult
{
    bool success = false;
    std::wstring message;
    std::vector<SumireSettingsStore::PersonNameDictionaryProfile> profiles;
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

    std::vector<SumireSettingsStore::PersonNameDictionaryProfile> profiles;
    int selectedProfileIndex = -1;
    bool buildInProgress = false;
    std::thread buildWorker;
};

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
        return std::filesystem::path(installDir) / L"dictionaries" / L"names" / L"build";
    }

    return GetModuleDirectory() / L"dictionaries" / L"names" / L"build";
}

bool EqualsIgnoreCase(const std::wstring& lhs, const std::wstring& rhs)
{
    return CompareStringOrdinal(lhs.c_str(), -1, rhs.c_str(), -1, TRUE) == CSTR_EQUAL;
}

std::filesystem::path GetZenzServiceExecutablePath()
{
    std::filesystem::path root = GetModuleDirectory();
    const std::wstring installDir = ReadInstallDirFromRegistry();
    if (!installDir.empty())
    {
        root = std::filesystem::path(installDir);
    }

    const std::filesystem::path candidate = root / kZenzServiceProcessName;
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec) && !ec)
    {
        return candidate;
    }

    return std::filesystem::path();
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

    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo = {};
    std::wstring commandLine = L"\"";
    commandLine += servicePath.wstring();
    commandLine += L"\"";

    if (!CreateProcessW(
            nullptr,
            commandLine.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            servicePath.parent_path().c_str(),
            &startupInfo,
            &processInfo))
    {
        return false;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return true;
}

bool PersistZenzServiceEnabledSetting(bool enabled)
{
    SumireSettingsStore::Settings settings = SumireSettingsStore::Load();
    settings.zenzServiceEnabled = enabled;
    return SumireSettingsStore::Save(settings);
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
        L"Warning: GGUF model not found. Install it to the path above or choose another file.");
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

std::filesystem::path GetBuildOutputPath(const SumireSettingsStore::PersonNameDictionaryProfile& profile)
{
    if (!profile.builtPath.empty())
    {
        return std::filesystem::path(profile.builtPath);
    }

    return GetDictionaryBuildDirectory() / (profile.id + L".bin");
}

std::wstring BuildProfileLabel(const SumireSettingsStore::PersonNameDictionaryProfile& profile)
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
    settings.personNameDictionaryProfiles = state->profiles;

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
    state->profiles = settings.personNameDictionaryProfiles;
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

bool CollectProfileEditors(HWND hwnd, SumireSettingsStore::PersonNameDictionaryProfile* profile, bool preserveIdentity)
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

    SumireSettingsStore::PersonNameDictionaryProfile profile;
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

    std::vector<SumireSettingsStore::PersonNameDictionaryProfile> targets;
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

        std::wstring message;
        for (auto& profile : result->profiles)
        {
            std::wstring errorMessage;
            if (!PersonNameLexicon::BuildBinaryFromText(profile.sourcePath, profile.builtPath, &errorMessage))
            {
                result->success = false;
                if (!message.empty())
                {
                    message += L"\r\n";
                }

                message += profile.name;
                message += L": ";
                message += errorMessage.empty()
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
                UiText(state->uiLanguage, L"人名辞書ビルド", L"Person name dictionary builds"),
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
