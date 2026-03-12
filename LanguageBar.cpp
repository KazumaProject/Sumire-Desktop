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

    // モードアイコン（c_guidLangBarItemButtonMode）のときはボタン、
    // それ以外（ブランドアイコン）はメニュー付きボタン。
    _LangBarItemInfo.dwStyle =
        TF_LBI_STYLE_SHOWNINTRAY |
        (IsEqualGUID(guidItem, GUID_LBI_INPUTMODE)
            ? TF_LBI_STYLE_BTN_BUTTON
            : TF_LBI_STYLE_BTN_MENU);

    _LangBarItemInfo.ulSort = 0;

    // ツールチップ・LangBar の説明
    wcsncpy_s(_LangBarItemInfo.szDescription,
        LANGBAR_ITEM_DESC,
        _TRUNCATE);
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
    return S_OK;
}

STDAPI CLangBarItemButton::GetStatus(DWORD* pdwStatus)
{
    if (!pdwStatus) return E_INVALIDARG;
    *pdwStatus = 0;
    return S_OK;
}

STDAPI CLangBarItemButton::Show(BOOL fShow)
{
    UNREFERENCED_PARAMETER(fShow);
    return S_OK;
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

    // モードアイコン（c_guidLangBarItemButtonMode）かどうか
    if (IsEqualGUID(_LangBarItemInfo.guidItem, GUID_LBI_INPUTMODE))
    {
        switch (click)
        {
        case TF_LBI_CLK_LEFT:
            // 左クリックでモードトグル & キーボード ON
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
                }
                else if (cmd == 2)
                {
                    _pTextService->SetUserInputMode(InputMode::DirectInput);
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
        break;

    case MENUITEM_INDEX_1: // 直接入力
        _pTextService->SetUserInputMode(InputMode::DirectInput);
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

    return _GetIconInternal(phIcon);
}

HRESULT CLangBarItemButton::_GetIconInternal(HICON* phIcon)
{
    WORD idIcon = IDI_TEXTSERVICE;

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

    *phIcon = (HICON)LoadImageW(
        g_hInst,
        MAKEINTRESOURCEW(idIcon),
        IMAGE_ICON,
        0, 0,
        LR_DEFAULTSIZE | LR_SHARED);

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

    VARIANT var;
    VariantInit(&var);

    // 文節モード（必要なければ削除可）
    V_VT(&var) = VT_I4;
    V_I4(&var) = TF_SENTENCEMODE_PHRASEPREDICT;
    _pTextService->_SetCompartment(
        GUID_COMPARTMENT_KEYBOARD_INPUTMODE_SENTENCE, &var);

    // 変換モード（ひらがな / ENG）
    if (!_pTextService->_IsKeyboardDisabled() && _pTextService->_IsKeyboardOpen())
    {
        InputMode mode = _pTextService->GetEffectiveInputMode();

        switch (mode)
        {
        case InputMode::Hiragana:
            V_I4(&var) = TF_CONVERSIONMODE_NATIVE |
                TF_CONVERSIONMODE_FULLSHAPE |
                TF_CONVERSIONMODE_ROMAN;
            break;
        case InputMode::FullwidthAlphanumeric:
            V_I4(&var) = TF_CONVERSIONMODE_ALPHANUMERIC |
                TF_CONVERSIONMODE_FULLSHAPE;
            break;
        case InputMode::HalfwidthKatakana:
            V_I4(&var) = TF_CONVERSIONMODE_NATIVE |
                TF_CONVERSIONMODE_KATAKANA;
            break;
        case InputMode::FullwidthKatakana:
            V_I4(&var) = TF_CONVERSIONMODE_NATIVE |
                TF_CONVERSIONMODE_KATAKANA |
                TF_CONVERSIONMODE_FULLSHAPE;
            break;
        case InputMode::DirectInput:
        default:
            V_I4(&var) = TF_CONVERSIONMODE_ALPHANUMERIC;
            break;
        }

        _pTextService->_SetCompartment(
            GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, &var);
    }

    if (_pLangBarItemSink == nullptr)
        return E_FAIL;

    return _pLangBarItemSink->OnUpdate(TF_LBI_ICON | TF_LBI_TEXT | TF_LBI_STATUS);
}

