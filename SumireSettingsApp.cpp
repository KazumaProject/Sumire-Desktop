#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>

#include <string>

#include "SumireSettingsStore.h"

namespace
{
constexpr wchar_t kWindowClassName[] = L"SumireSettingsWindow";
constexpr int kCheckLiveConversion = 101;
constexpr int kEditCandidatePageSize = 102;
constexpr int kEditRomajiMapPath = 103;
constexpr int kButtonBrowseRomajiMap = 104;
constexpr int kButtonSave = 105;
constexpr int kButtonClose = 106;

struct WindowState
{
    HWND liveConversion = nullptr;
    HWND candidatePageSize = nullptr;
    HWND romajiMapPath = nullptr;
    HWND status = nullptr;
};

HMENU ControlId(int id)
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

void SetStatusText(HWND hwnd, const wchar_t* text)
{
    WindowState* state = GetWindowState(hwnd);
    if (state != nullptr && state->status != nullptr)
    {
        SetWindowTextW(state->status, text);
    }
}

void LoadSettingsIntoWindow(HWND hwnd)
{
    WindowState* state = GetWindowState(hwnd);
    if (state == nullptr)
    {
        return;
    }

    const SumireSettingsStore::Settings settings = SumireSettingsStore::Load();
    SetCheckBoxState(state->liveConversion, settings.liveConversionEnabled);

    wchar_t pageSize[16] = {};
    wsprintfW(pageSize, L"%d", settings.candidatePageSize);
    SetWindowTextW(state->candidatePageSize, pageSize);
    SetWindowTextW(state->romajiMapPath, settings.romajiMapPath.c_str());
    SetStatusText(hwnd, L"Changes apply after the IME regains focus.");
}

bool SaveSettingsFromWindow(HWND hwnd)
{
    WindowState* state = GetWindowState(hwnd);
    if (state == nullptr)
    {
        return false;
    }

    wchar_t pageSizeBuffer[16] = {};
    GetWindowTextW(state->candidatePageSize, pageSizeBuffer, ARRAYSIZE(pageSizeBuffer));
    const int pageSize = _wtoi(pageSizeBuffer);
    if (pageSize <= 0)
    {
        MessageBoxW(hwnd, L"Candidate page size must be 1 or greater.", L"Sumire Settings", MB_ICONWARNING | MB_OK);
        return false;
    }

    wchar_t pathBuffer[MAX_PATH] = {};
    GetWindowTextW(state->romajiMapPath, pathBuffer, ARRAYSIZE(pathBuffer));

    SumireSettingsStore::Settings settings;
    settings.liveConversionEnabled = GetCheckBoxState(state->liveConversion);
    settings.candidatePageSize = pageSize;
    settings.romajiMapPath = pathBuffer;

    if (!SumireSettingsStore::Save(settings))
    {
        MessageBoxW(hwnd, L"Failed to save settings.", L"Sumire Settings", MB_ICONERROR | MB_OK);
        return false;
    }

    SetStatusText(hwnd, L"Saved. Changes apply after the IME regains focus.");
    return true;
}

void BrowseRomajiMapFile(HWND hwnd)
{
    WindowState* state = GetWindowState(hwnd);
    if (state == nullptr)
    {
        return;
    }

    wchar_t filePath[MAX_PATH] = {};
    OPENFILENAMEW openFileName = {};
    openFileName.lStructSize = sizeof(openFileName);
    openFileName.hwndOwner = hwnd;
    openFileName.lpstrFilter = L"TSV (*.tsv)\0*.tsv\0All files (*.*)\0*.*\0";
    openFileName.lpstrFile = filePath;
    openFileName.nMaxFile = ARRAYSIZE(filePath);
    openFileName.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&openFileName))
    {
        SetWindowTextW(state->romajiMapPath, filePath);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        {
            auto* state = new WindowState();
            SetWindowState(hwnd, state);

            CreateWindowW(L"STATIC", L"Live conversion", WS_CHILD | WS_VISIBLE, 16, 20, 128, 20, hwnd, nullptr, nullptr, nullptr);
            state->liveConversion = CreateWindowW(
                L"BUTTON",
                L"Enabled",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                160,
                18,
                120,
                24,
                hwnd,
                ControlId(kCheckLiveConversion),
                nullptr,
                nullptr);

            CreateWindowW(L"STATIC", L"Candidate page size", WS_CHILD | WS_VISIBLE, 16, 58, 128, 20, hwnd, nullptr, nullptr, nullptr);
            state->candidatePageSize = CreateWindowW(
                L"EDIT",
                L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                160,
                56,
                80,
                24,
                hwnd,
                ControlId(kEditCandidatePageSize),
                nullptr,
                nullptr);

            CreateWindowW(L"STATIC", L"Romaji map path", WS_CHILD | WS_VISIBLE, 16, 96, 128, 20, hwnd, nullptr, nullptr, nullptr);
            state->romajiMapPath = CreateWindowW(
                L"EDIT",
                L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                160,
                94,
                220,
                24,
                hwnd,
                ControlId(kEditRomajiMapPath),
                nullptr,
                nullptr);
            CreateWindowW(
                L"BUTTON",
                L"Browse...",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                388,
                94,
                76,
                24,
                hwnd,
                ControlId(kButtonBrowseRomajiMap),
                nullptr,
                nullptr);

            state->status = CreateWindowW(
                L"STATIC",
                L"",
                WS_CHILD | WS_VISIBLE,
                16,
                138,
                448,
                20,
                hwnd,
                nullptr,
                nullptr,
                nullptr);

            CreateWindowW(
                L"BUTTON",
                L"Save",
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                292,
                176,
                80,
                28,
                hwnd,
                ControlId(kButtonSave),
                nullptr,
                nullptr);
            CreateWindowW(
                L"BUTTON",
                L"Close",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                384,
                176,
                80,
                28,
                hwnd,
                ControlId(kButtonClose),
                nullptr,
                nullptr);

            LoadSettingsIntoWindow(hwnd);
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case kButtonBrowseRomajiMap:
            BrowseRomajiMapFile(hwnd);
            return 0;
        case kButtonSave:
            SaveSettingsFromWindow(hwnd);
            return 0;
        case kButtonClose:
            DestroyWindow(hwnd);
            return 0;
        default:
            break;
        }
        return 0;

    case WM_DESTROY:
        delete GetWindowState(hwnd);
        SetWindowState(hwnd, nullptr);
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int commandShow)
{
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
        L"Sumire Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        500,
        260,
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
