//////////////////////////////////////////////////////////////////////
//
//  LanguageBar.cpp
//
//////////////////////////////////////////////////////////////////////

#include "Globals.h"
#include "TextService.h"
#include "Resource.h"
#include "LanguageBar.h"

#define TEXTSERVICE_LANGBARITEMSINK_COOKIE 0x0fab0fab

// メニュー ID
#define MENUITEM_INDEX_0         0   // ひらがな
#define MENUITEM_INDEX_1         1   // 英数
#define MENUITEM_INDEX_OPENCLOSE 2   // キーボード有効/無効

// メニュー表示文字列
static WCHAR c_szMenuItemDescription0[] = L"ひらがな (あ)";
static WCHAR c_szMenuItemDescription1[] = L"直接入力 (A)";
static WCHAR c_szMenuItemDescriptionOpenClose[] = L"キーボードの有効 / 無効";

// Globals.cpp 側で定義している GUID
extern const GUID c_guidLangBarItemButton;      // ブランド用
extern const GUID GUID_LBI_INPUTMODE;

//
// CLangBarItemButton
//

CLangBarItemButton::CLangBarItemButton(CTextService* pTextService, REFGUID guidItem)
{
    DllAddRef();
    _cRef = 1;

    _pTextService = pTextService;
    if (_pTextService)
    {
        _pTextService->AddRef();
    }

    _pLangBarItemSink = nullptr;

    ZeroMemory(&_LangBarItemInfo, sizeof(_LangBarItemInfo));
    _LangBarItemInfo.clsidService = c_clsidTextService;
    _LangBarItemInfo.guidItem = guidItem;

    // モードアイコン（GUID_LBI_INPUTMODE）のときはボタン、
    // それ以外（ブランドアイコン）はメニュー付きボタン。
    _LangBarItemInfo.dwStyle =
        TF_LBI_STYLE_SHOWNINTRAY |
        (IsEqualGUID(guidItem, GUID_LBI_INPUTMODE)
            ? TF_LBI_STYLE_BTN_BUTTON
            : TF_LBI_STYLE_BTN_MENU);

    _LangBarItemInfo.ulSort = 1;

    // ツールチップ・LangBar の説明
    wcsncpy_s(_LangBarItemInfo.szDescription,
        LANGBAR_ITEM_DESC,
        _TRUNCATE);

    DebugLogGuid(L"[LanguageBar] CLangBarItemButton::ctor guidItem", guidItem);
    DebugLog(L"[LanguageBar] CLangBarItemButton::ctor dwStyle=0x%08X\r\n", _LangBarItemInfo.dwStyle);
}

CLangBarItemButton::~CLangBarItemButton()
{
    if (_pLangBarItemSink)
    {
        _pLangBarItemSink->Release();
        _pLangBarItemSink = nullptr;
    }

    if (_pTextService)
    {
        _pTextService->Release();
        _pTextService = nullptr;
    }

    DllRelease();
}

//
// IUnknown
//

STDAPI CLangBarItemButton::QueryInterface(REFIID riid, void** ppvObj)
{
    if (!ppvObj)
        return E_INVALIDARG;

    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfLangBarItem) ||
        IsEqualIID(riid, IID_ITfLangBarItemButton))
    {
        *ppvObj = static_cast<ITfLangBarItemButton*>(this);
    }
    else if (IsEqualIID(riid, IID_ITfSource))
    {
        *ppvObj = static_cast<ITfSource*>(this);
    }

    if (*ppvObj)
    {
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

STDAPI_(ULONG) CLangBarItemButton::AddRef()
{
    return ++_cRef;
}

STDAPI_(ULONG) CLangBarItemButton::Release()
{
    LONG cr = --_cRef;
    assert(_cRef >= 0);

    if (_cRef == 0)
    {
        delete this;
    }

    return cr;
}

//
// ITfLangBarItem
//

STDAPI CLangBarItemButton::GetInfo(TF_LANGBARITEMINFO* pInfo)
{
    if (!pInfo) return E_INVALIDARG;
    *pInfo = _LangBarItemInfo;
    DebugLogGuid(L"[LanguageBar] GetInfo guidItem", _LangBarItemInfo.guidItem);
    DebugLogGuid(L"[LanguageBar] GetInfo clsidService", _LangBarItemInfo.clsidService);
    DebugLog(L"[LanguageBar] GetInfo dwStyle=0x%08X\r\n", _LangBarItemInfo.dwStyle);
    return S_OK;
}

STDAPI CLangBarItemButton::GetStatus(DWORD* pdwStatus)
{
    if (!pdwStatus) return E_INVALIDARG;

    if (_pTextService && _pTextService->_IsKeyboardDisabled())
    {
        *pdwStatus = TF_LBI_STATUS_DISABLED;
    }
    else
    {
        *pdwStatus = 0;
    }

    DebugLog(L"[LanguageBar] GetStatus guid=%s status=0x%08X\r\n",
        IsEqualGUID(_LangBarItemInfo.guidItem, GUID_LBI_INPUTMODE) ? L"mode" : L"brand",
        *pdwStatus);
    return S_OK;
}

STDAPI CLangBarItemButton::Show(BOOL fShow)
{
    DebugLog(L"[LanguageBar] Show guid=%s fShow=%s\r\n",
        IsEqualGUID(_LangBarItemInfo.guidItem, GUID_LBI_INPUTMODE) ? L"mode" : L"brand",
        fShow ? L"TRUE" : L"FALSE");
    UNREFERENCED_PARAMETER(fShow);

    if (_pLangBarItemSink == nullptr)
    {
        return E_FAIL;
    }

    return _pLangBarItemSink->OnUpdate(TF_LBI_STATUS);
}

STDAPI CLangBarItemButton::GetTooltipString(BSTR* pbstrToolTip)
{
    if (!pbstrToolTip) return E_INVALIDARG;

    *pbstrToolTip = SysAllocString(LANGBAR_ITEM_DESC);
    return (*pbstrToolTip == nullptr) ? E_OUTOFMEMORY : S_OK;
}

//
// ITfLangBarItemButton
//

STDMETHODIMP CLangBarItemButton::OnClick(TfLBIClick click, POINT pt, const RECT* prcArea)
{
    UNREFERENCED_PARAMETER(prcArea);

    if (_pTextService == nullptr)
        return E_UNEXPECTED;

    // モードアイコン（GUID_LBI_INPUTMODE）かどうか
    if (IsEqualGUID(_LangBarItemInfo.guidItem, GUID_LBI_INPUTMODE))
    {
        switch (click)
        {
        case TF_LBI_CLK_LEFT:
            // 左クリックでモードトグル
            _pTextService->ToggleInputMode();
            _pTextService->_SetKeyboardOpen(TRUE);
            _Update();
            break;

        case TF_LBI_CLK_RIGHT:
        {
            // 右クリックで簡易メニュー（ひらがな / ENG）
            HMENU hMenu = CreatePopupMenu();
            if (hMenu)
            {
                InputMode mode = _pTextService->GetUserInputMode();

                InsertMenuW(hMenu, -1, MF_BYPOSITION |
                    (mode == InputMode::Hiragana ? MF_CHECKED : 0),
                    1, L"ひらがな");

                InsertMenuW(hMenu, -1, MF_BYPOSITION |
                    (mode == InputMode::DirectInput ? MF_CHECKED : 0),
                    2, L"直接入力");

                UINT cmd = TrackPopupMenuEx(
                    hMenu,
                    TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN |
                    TPM_NONOTIFY | TPM_RIGHTBUTTON,
                    pt.x, pt.y,
                    GetFocus(),
                    nullptr);

                if (cmd == 1)
                {
                    _pTextService->SetUserInputMode(InputMode::Hiragana);
                    _pTextService->_SetKeyboardOpen(TRUE);
                }
                else if (cmd == 2)
                {
                    _pTextService->SetUserInputMode(InputMode::DirectInput);
                    _pTextService->_SetKeyboardOpen(TRUE);
                }

                DestroyMenu(hMenu);
                _Update();
            }
            break;
        }

        default:
            break;
        }
    }
    else
    {
        // ブランドアイコン側のクリック（必要なら設定ダイアログなど）
        // 今は何もしない
    }

    return S_OK;
}

STDAPI CLangBarItemButton::InitMenu(ITfMenu* pMenu)
{
    if (!pMenu)
        return E_INVALIDARG;

    if (_pTextService == nullptr)
        return E_UNEXPECTED;

    // モードアイコン以外（ブランドアイコン）はここではメニューなし
    if (!IsEqualGUID(_LangBarItemInfo.guidItem, GUID_LBI_INPUTMODE))
    {
        return S_OK;
    }

    InputMode mode = _pTextService->GetUserInputMode();

    // ひらがな
    DWORD dwFlagsHiragana = 0;
    if (mode == InputMode::Hiragana)
        dwFlagsHiragana |= TF_LBMENUF_CHECKED;

    pMenu->AddMenuItem(
        MENUITEM_INDEX_0,
        dwFlagsHiragana,
        nullptr,
        nullptr,
        c_szMenuItemDescription0,
        (ULONG)wcslen(c_szMenuItemDescription0),
        nullptr);

    // 英数
    DWORD dwFlagsAlnum = 0;
    if (mode == InputMode::DirectInput)
        dwFlagsAlnum |= TF_LBMENUF_CHECKED;

    pMenu->AddMenuItem(
        MENUITEM_INDEX_1,
        dwFlagsAlnum,
        nullptr,
        nullptr,
        c_szMenuItemDescription1,
        (ULONG)wcslen(c_szMenuItemDescription1),
        nullptr);

    // キーボード有効/無効
    DWORD dwFlagsOpenClose = 0;
    if (_pTextService->_IsKeyboardDisabled())
        dwFlagsOpenClose |= TF_LBMENUF_GRAYED;
    else if (_pTextService->_IsKeyboardOpen())
        dwFlagsOpenClose |= TF_LBMENUF_CHECKED;

    pMenu->AddMenuItem(
        MENUITEM_INDEX_OPENCLOSE,
        dwFlagsOpenClose,
        nullptr,
        nullptr,
        c_szMenuItemDescriptionOpenClose,
        (ULONG)wcslen(c_szMenuItemDescriptionOpenClose),
        nullptr);

    return S_OK;
}

STDAPI CLangBarItemButton::OnMenuSelect(UINT wID)
{
    if (_pTextService == nullptr)
        return E_UNEXPECTED;

    if (!IsEqualGUID(_LangBarItemInfo.guidItem, GUID_LBI_INPUTMODE))
    {
        // ブランドアイコンのメニューを実装したければここで処理
        return S_OK;
    }

    switch (wID)
    {
    case MENUITEM_INDEX_0: // ひらがな
        _pTextService->SetUserInputMode(InputMode::Hiragana);
        _pTextService->_SetKeyboardOpen(TRUE);
        break;

    case MENUITEM_INDEX_1: // 直接入力
        _pTextService->SetUserInputMode(InputMode::DirectInput);
        _pTextService->_SetKeyboardOpen(TRUE);
        break;

    case MENUITEM_INDEX_OPENCLOSE:
    {
        BOOL fOpen = _pTextService->_IsKeyboardOpen();
        _pTextService->_SetKeyboardOpen(fOpen ? FALSE : TRUE);
        break;
    }

    default:
        break;
    }

    if (_pLangBarItemSink)
    {
        _pLangBarItemSink->OnUpdate(TF_LBI_ICON | TF_LBI_TEXT | TF_LBI_STATUS);
    }

    return S_OK;
}

STDMETHODIMP CLangBarItemButton::GetIcon(HICON* phIcon)
{
    if (!phIcon) return E_INVALIDARG;
    *phIcon = nullptr;
    HRESULT hr = _GetIconInternal(phIcon);
    DebugLog(L"[LanguageBar] GetIcon guid=%s hr=0x%08X icon=0x%p\r\n",
        IsEqualGUID(_LangBarItemInfo.guidItem, GUID_LBI_INPUTMODE) ? L"mode" : L"brand",
        hr,
        *phIcon);
    return hr;
}

HRESULT CLangBarItemButton::_GetIconInternal(HICON* phIcon)
{
    WORD idIcon = IDI_TEXTSERVICE;
    int size = 16;

    if (IsEqualGUID(_LangBarItemInfo.guidItem, GUID_LBI_INPUTMODE))
    {
        // モードアイコン
        if (_pTextService)
        {
            InputMode mode = _pTextService->GetEffectiveInputMode();
            if (mode == InputMode::Hiragana ||
                mode == InputMode::HalfwidthKatakana ||
                mode == InputMode::FullwidthKatakana)
            {
                idIcon = IDI_MODE_HIRAGANA;
            }
            else
            {
                idIcon = IDI_MODE_ALPHANUMERIC;
            }
        }
        else
        {
            idIcon = IDI_MODE_HIRAGANA;
        }
    }
    else
    {
        // ブランドアイコン
        idIcon = IDI_TEXTSERVICE;
    }

    HDC hdc = GetDC(nullptr);
    if (hdc != nullptr)
    {
        size = MulDiv(16, GetDeviceCaps(hdc, LOGPIXELSY), USER_DEFAULT_SCREEN_DPI);
        ReleaseDC(nullptr, hdc);
    }

    *phIcon = (HICON)LoadImageW(
        g_hInst,
        MAKEINTRESOURCEW(idIcon),
        IMAGE_ICON,
        size, size,
        LR_SHARED);

    return (*phIcon != nullptr) ? S_OK : E_FAIL;
}

STDAPI CLangBarItemButton::GetText(BSTR* pbstrText)
{
    if (!pbstrText)
        return E_INVALIDARG;

    const WCHAR* pszText = nullptr;

    if (_pTextService != nullptr &&
        IsEqualGUID(_LangBarItemInfo.guidItem, GUID_LBI_INPUTMODE))
    {
        // モードアイコンのテキスト
        InputMode mode = _pTextService->GetEffectiveInputMode();
        switch (mode)
        {
        case InputMode::Hiragana:
            pszText = L"あ";
            break;
        case InputMode::DirectInput:
            pszText = L"A";
            break;
        case InputMode::FullwidthAlphanumeric:
            pszText = L"Ａ";
            break;
        case InputMode::HalfwidthKatakana:
            pszText = L"_ｶ";
            break;
        case InputMode::FullwidthKatakana:
            pszText = L"カ";
            break;
        default:
            pszText = L"A";
            break;
        }
    }
    else
    {
        // ブランドアイコン or TextService 未接続
        pszText = LANGBAR_ITEM_DESC;
    }

    *pbstrText = SysAllocString(pszText);
    DebugLog(L"[LanguageBar] GetText guid=%s text=%s\r\n",
        IsEqualGUID(_LangBarItemInfo.guidItem, GUID_LBI_INPUTMODE) ? L"mode" : L"brand",
        pszText);
    return (*pbstrText == nullptr) ? E_OUTOFMEMORY : S_OK;
}

//
// ITfSource
//

STDAPI CLangBarItemButton::AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie)
{
    if (!IsEqualIID(IID_ITfLangBarItemSink, riid))
        return CONNECT_E_CANNOTCONNECT;

    if (_pLangBarItemSink != nullptr)
        return CONNECT_E_ADVISELIMIT;

    if (punk->QueryInterface(IID_ITfLangBarItemSink, (void**)&_pLangBarItemSink) != S_OK)
    {
        _pLangBarItemSink = nullptr;
        return E_NOINTERFACE;
    }

    *pdwCookie = TEXTSERVICE_LANGBARITEMSINK_COOKIE;
    return S_OK;
}

STDAPI CLangBarItemButton::UnadviseSink(DWORD dwCookie)
{
    if (dwCookie != TEXTSERVICE_LANGBARITEMSINK_COOKIE)
        return CONNECT_E_NOCONNECTION;

    if (_pLangBarItemSink == nullptr)
        return CONNECT_E_NOCONNECTION;

    _pLangBarItemSink->Release();
    _pLangBarItemSink = nullptr;

    return S_OK;
}

HRESULT CLangBarItemButton::_Update()
{
    if (_pTextService == nullptr)
        return E_FAIL;

    DebugLog(L"[CLangBarItemButton::_Update] guid=%s user=%d effective=%d keyboardOpen=%s\r\n",
        IsEqualGUID(_LangBarItemInfo.guidItem, GUID_LBI_INPUTMODE) ? L"mode" : L"brand",
        static_cast<int>(_pTextService->GetUserInputMode()),
        static_cast<int>(_pTextService->GetEffectiveInputMode()),
        _pTextService->_IsKeyboardOpen() ? L"TRUE" : L"FALSE");

    if (_pLangBarItemSink == nullptr)
        return E_FAIL;

    return _pLangBarItemSink->OnUpdate(TF_LBI_ICON | TF_LBI_STATUS);
}



