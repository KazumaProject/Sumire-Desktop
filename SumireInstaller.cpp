#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <filesystem>
#include <initializer_list>
#include <string>

#include "SumireInstallUtil.h"

namespace
{
constexpr wchar_t kWindowClassName[] = L"SumireInstallerWizard";
constexpr UINT WM_SUMIRE_START_INSTALL = WM_APP + 0x210;

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
    IdShortcutCheck = 106,
    IdLaunchSettingsCheck = 107,
    IdBack = 108,
    IdNext = 109,
    IdCancel = 110,
    IdStatus = 111,
};

struct WizardState
{
    WizardPage page = WizardPage::Welcome;
    bool installSucceeded = false;
    bool createShortcuts = true;
    bool launchSettingsAfterFinish = false;
    bool installStarted = false;
    std::wstring installDirectory;
    std::wstring installedSettingsPath;

    HWND title = nullptr;
    HWND body = nullptr;
    HWND installPathLabel = nullptr;
    HWND installPathEdit = nullptr;
    HWND browseButton = nullptr;
    HWND shortcutCheck = nullptr;
    HWND launchSettingsCheck = nullptr;
    HWND backButton = nullptr;
    HWND nextButton = nullptr;
    HWND cancelButton = nullptr;
    HWND status = nullptr;
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
        *error = L"インストーラーの近くに必要なファイルが見つかりません。";
        return false;
    }

    if (!SumireInstallUtil::EnsureDirectory(installDirectory))
    {
        *error = L"インストール先フォルダーを作成できませんでした。";
        return false;
    }

    if (!SumireInstallUtil::CopyFileIntoDirectory(dllPath, installDirectory) ||
        !SumireInstallUtil::CopyFileIntoDirectory(settingsPath, installDirectory) ||
        !SumireInstallUtil::CopyFileIntoDirectory(uninstallerPath, installDirectory))
    {
        *error = L"主要ファイルのコピーに失敗しました。";
        return false;
    }

    if (!romajiMapPath.empty() &&
        std::filesystem::exists(romajiMapPath) &&
        !SumireInstallUtil::CopyFileIntoDirectory(romajiMapPath, installDirectory))
    {
        *error = L"romaji-hiragana.tsv のコピーに失敗しました。";
        return false;
    }

    if (!dictionariesPath.empty() &&
        std::filesystem::exists(dictionariesPath) &&
        !SumireInstallUtil::CopyDirectoryRecursive(dictionariesPath, installDirectory / L"dictionaries"))
    {
        *error = L"辞書ファイルのコピーに失敗しました。";
        return false;
    }

    return true;
}

std::wstring BrowseForFolder(HWND owner, const std::wstring& currentPath)
{
    BROWSEINFOW browseInfo = {};
    browseInfo.hwndOwner = owner;
    browseInfo.lpszTitle = L"インストール先フォルダーを選択してください。";
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
    SetVisible(state->shortcutCheck, showOptions);
    SetVisible(state->launchSettingsCheck, showLaunchSettings);

    switch (state->page)
    {
    case WizardPage::Welcome:
        SetWindowTextW(state->title, L"Sumire IME セットアップへようこそ");
        SetWindowTextW(
            state->body,
            L"このウィザードは Sumire IME をインストールします。\r\n\r\n次へ進むと、インストール先やショートカット作成の有無を設定できます。");
        SetWindowTextW(state->nextButton, L"次へ >");
        EnableWindow(state->backButton, FALSE);
        EnableWindow(state->nextButton, TRUE);
        SetVisible(state->cancelButton, true);
        SetWindowTextW(state->status, L"");
        break;

    case WizardPage::Options:
        SetWindowTextW(state->title, L"インストール設定");
        SetWindowTextW(
            state->body,
            L"インストール先フォルダーを確認してください。必要に応じてスタートメニューのショートカット作成を無効にできます。");
        SetWindowTextW(state->nextButton, L"インストール");
        EnableWindow(state->backButton, TRUE);
        EnableWindow(state->nextButton, TRUE);
        SetVisible(state->cancelButton, true);
        SetWindowTextW(state->status, L"");
        break;

    case WizardPage::Progress:
        SetWindowTextW(state->title, L"インストール中");
        SetWindowTextW(state->body, L"必要なファイルをコピーし、IME を登録しています。しばらくお待ちください。");
        SetWindowTextW(state->nextButton, L"次へ >");
        EnableWindow(state->backButton, FALSE);
        EnableWindow(state->nextButton, FALSE);
        SetVisible(state->cancelButton, false);
        break;

    case WizardPage::Finish:
        if (state->installSucceeded)
        {
            SetWindowTextW(state->title, L"セットアップ完了");
            SetWindowTextW(
                state->body,
                L"Sumire IME のインストールが完了しました。\r\n必要に応じて Windows の入力方式一覧から有効化してください。");
            SetWindowTextW(state->status, L"完了を押すとセットアップを終了します。");
        }
        else
        {
            SetWindowTextW(state->title, L"セットアップ失敗");
            SetWindowTextW(
                state->body,
                L"インストール中にエラーが発生しました。内容を確認して、もう一度実行してください。");
        }
        SetWindowTextW(state->nextButton, L"完了");
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
    SetWindowTextW(state->status, L"インストールを開始しています...");
    UpdateWindow(hwnd);

    wchar_t installPath[MAX_PATH] = {};
    GetWindowTextW(state->installPathEdit, installPath, ARRAYSIZE(installPath));
    state->installDirectory = installPath;
    state->createShortcuts = IsChecked(state->shortcutCheck);

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

    std::wstring error;
    bool success = CopyPayload(sourceDirectory, installDirectory, &error);

    const std::filesystem::path installedDll = FindFirstExistingFile(installDirectory, {L"Sumite-Desktop.dll", L"TextService.dll"});
    const std::filesystem::path installedSettings = installDirectory / L"SumireSettings.exe";
    const std::filesystem::path installedUninstaller = installDirectory / L"SumireUninstaller.exe";

    if (success && (installedDll.empty() || !SumireInstallUtil::RegisterTextServiceDll(installedDll)))
    {
        success = false;
        error = L"IME DLL の登録に失敗しました。";
    }

    if (success && !SumireInstallUtil::ActivateTextServiceProfile())
    {
        success = false;
        error = L"IME プロファイルの有効化に失敗しました。";
    }

    if (success && !SumireInstallUtil::WriteInstallMetadata(installDirectory, installedUninstaller))
    {
        success = false;
        error = L"インストール情報の登録に失敗しました。";
    }

    if (success && state->createShortcuts)
    {
        SumireInstallUtil::CreateShortcut(
            SumireInstallUtil::GetStartMenuShortcutPath(L"Sumire 設定.lnk"),
            installedSettings,
            L"Sumire IME の設定を開く",
            installedSettings);
        SumireInstallUtil::CreateShortcut(
            SumireInstallUtil::GetStartMenuShortcutPath(L"Sumire アンインストール.lnk"),
            installedUninstaller,
            L"Sumire IME をアンインストールする",
            installedUninstaller);
    }

    state->installedSettingsPath = installedSettings.wstring();
    state->installSucceeded = success;
    state->installStarted = false;
    state->page = WizardPage::Finish;

    if (success)
    {
        SetChecked(state->launchSettingsCheck, false);
        SetWindowTextW(state->status, L"インストールが完了しました。");
    }
    else
    {
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
                460,
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
                460,
                72,
                hwnd,
                ControlMenu(IdBody),
                nullptr,
                nullptr);

            state->installPathLabel = CreateWindowW(
                L"STATIC",
                L"インストール先",
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
                360,
                24,
                hwnd,
                ControlMenu(IdInstallPathEdit),
                nullptr,
                nullptr);

            state->browseButton = CreateWindowW(
                L"BUTTON",
                L"参照...",
                WS_CHILD | BS_PUSHBUTTON,
                386,
                152,
                90,
                24,
                hwnd,
                ControlMenu(IdBrowse),
                nullptr,
                nullptr);

            state->shortcutCheck = CreateWindowW(
                L"BUTTON",
                L"スタートメニューにショートカットを作成する",
                WS_CHILD | BS_AUTOCHECKBOX,
                16,
                188,
                260,
                24,
                hwnd,
                ControlMenu(IdShortcutCheck),
                nullptr,
                nullptr);
            SetChecked(state->shortcutCheck, true);

            state->status = CreateWindowW(
                L"STATIC",
                L"",
                WS_CHILD | WS_VISIBLE,
                16,
                224,
                460,
                36,
                hwnd,
                ControlMenu(IdStatus),
                nullptr,
                nullptr);

            state->launchSettingsCheck = CreateWindowW(
                L"BUTTON",
                L"完了後に設定画面を開く",
                WS_CHILD | BS_AUTOCHECKBOX,
                16,
                224,
                200,
                24,
                hwnd,
                ControlMenu(IdLaunchSettingsCheck),
                nullptr,
                nullptr);

            state->backButton = CreateWindowW(
                L"BUTTON",
                L"< 戻る",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                214,
                270,
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
                304,
                270,
                80,
                28,
                hwnd,
                ControlMenu(IdNext),
                nullptr,
                nullptr);

            state->cancelButton = CreateWindowW(
                L"BUTTON",
                L"キャンセル",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                394,
                270,
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
                    if (state->installSucceeded && state->launchSettingsAfterFinish && !state->installedSettingsPath.empty())
                    {
                        ShellExecuteW(hwnd, L"open", state->installedSettingsPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
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
        L"Sumire IME セットアップ",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        500,
        350,
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
