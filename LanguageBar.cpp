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

#define MENUITEM_INDEX_0         0   // ひらがな
#define MENUITEM_INDEX_1         1   // 英数
#define MENUITEM_INDEX_OPENCLOSE 2   // キーボード有効/無効

static WCHAR c_szMenuItemDescription0[] = L"ひらがな (あ)";
static WCHAR c_szMenuItemDescription1[] = L"英数 (A)";
static WCHAR c_szMenuItemDescriptionOpenClose[] = L"キーボードの有効 / 無効";

// Globals.cpp 側で定義している GUID
extern const GUID c_guidLangBarItemButton;  // ブランド用
// ※モード用は GUID_LBI_INPUTMODE を使う（独自 GUID ではない）

//
// CLangBarItemButton
//

CLangBarItemButton::CLangBarItemButton(CTextService* pTextService, REFGUID guidItem)
{
    DllAddRef();
    _cRef = 1;

    _pTextService = pTextService;
    _pTextService->AddRef();

    _pLangBarItemSink = nullptr;

    ZeroMemory(&_LangBarItemInfo, sizeof(_LangBarItemInfo));
    _LangBarItemInfo.clsidService = c_clsidTextService;
    _LangBarItemInfo.guidItem = guidItem;

    // モードアイコン用 GUID_LBI_INPUTMODE のときだけボタン、それ以外（ブランド）はメニュー
    _LangBarItemInfo.dwStyle =
        TF_LBI_STYLE_SHOWNINTRAY |
        (IsEqualGUID(guidItem, GUID_LBI_INPUTMODE)
            ? TF_LBI_STYLE_BTN_BUTTON
            : TF_LBI_STYLE_BTN_MENU);

    _LangBarItemInfo.ulSort = 0;
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

    // モードアイコン（GUID_LBI_INPUTMODE）のときだけ特別扱い
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
            HMENU hMenu = CreatePopupMenu();
            if (hMenu)
            {
                InputMode mode = _pTextService->GetInputMode();

                InsertMenuW(hMenu, -1, MF_BYPOSITION |
                    (mode == INPUTMODE_HIRAGANA ? MF_CHECKED : 0),
                    1, L"ひらがな");

                InsertMenuW(hMenu, -1, MF_BYPOSITION |
                    (mode == INPUTMODE_ALPHANUMERIC ? MF_CHECKED : 0),
                    2, L"ENG");

                UINT cmd = TrackPopupMenuEx(
                    hMenu,
                    TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN |
                    TPM_NONOTIFY | TPM_RIGHTBUTTON,
                    pt.x, pt.y,
                    GetFocus(),
                    nullptr);

                if (cmd == 1)
                    _pTextService->SetInputMode(INPUTMODE_HIRAGANA);
                else if (cmd == 2)
                    _pTextService->SetInputMode(INPUTMODE_ALPHANUMERIC);

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
        // ブランドアイコン側クリック（設定ダイアログなど）は必要ならここに書く
    }

    return S_OK;
}

STDAPI CLangBarItemButton::InitMenu(ITfMenu* pMenu)
{
    if (!pMenu)
        return E_INVALIDARG;

    if (_pTextService == nullptr)
        return E_UNEXPECTED;

    // メニューを持つのはモードアイコンだけ
    if (!IsEqualGUID(_LangBarItemInfo.guidItem, GUID_LBI_INPUTMODE))
    {
        return S_OK;
    }

    InputMode mode = _pTextService->GetInputMode();

    // ひらがな
    DWORD dwFlagsHiragana = 0;
    if (mode == INPUTMODE_HIRAGANA)
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
    if (mode == INPUTMODE_ALPHANUMERIC)
        dwFlagsAlnum |= TF_LBMENUF_CHECKED;

    pMenu->AddMenuItem(
        MENUITEM_INDEX_1,
        dwFlagsAlnum,
        nullptr,
        nullptr,
        c_szMenuItemDescription1,
        (ULONG)wcslen(c_szMenuItemDescription1),
        nullptr);

    // キーボード有効／無効
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

    // メニューを持つのはモードアイコンだけ
    if (!IsEqualGUID(_LangBarItemInfo.guidItem, GUID_LBI_INPUTMODE))
    {
        return S_OK;
    }

    switch (wID)
    {
    case MENUITEM_INDEX_0: // ひらがな
        _pTextService->SetInputMode(INPUTMODE_HIRAGANA);
        break;

    case MENUITEM_INDEX_1: // 英数
        _pTextService->SetInputMode(INPUTMODE_ALPHANUMERIC);
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
        if (_pTextService->GetInputMode() == INPUTMODE_HIRAGANA)
            idIcon = IDI_MODE_HIRAGANA;
        else
            idIcon = IDI_MODE_ALPHANUMERIC;
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
        // モードアイコン側のテキスト
        InputMode mode = _pTextService->GetInputMode();
        pszText = (mode == INPUTMODE_HIRAGANA) ? L"あ" : L"A";
    }
    else
    {
        // ブランドアイコン側 / _pTextService == nullptr
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

    // ★ ここが tsf-tutcode と同じ「キーボード入力モードのコンパートメント更新」部分 ★
    VARIANT var;
    VariantInit(&var);

    // 文節モード
    V_VT(&var) = VT_I4;
    V_I4(&var) = TF_SENTENCEMODE_PHRASEPREDICT;
    _pTextService->_SetCompartment(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_SENTENCE, &var);

    // 変換モード
    if (!_pTextService->_IsKeyboardDisabled() && _pTextService->_IsKeyboardOpen())
    {
        InputMode mode = _pTextService->GetInputMode();

        switch (mode)
        {
        case INPUTMODE_HIRAGANA:
            V_I4(&var) = TF_CONVERSIONMODE_NATIVE |
                TF_CONVERSIONMODE_FULLSHAPE |
                TF_CONVERSIONMODE_ROMAN;
            _pTextService->_SetCompartment(
                GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, &var);
            break;

        case INPUTMODE_ALPHANUMERIC:
        default:
            V_I4(&var) = TF_CONVERSIONMODE_ALPHANUMERIC;
            _pTextService->_SetCompartment(
                GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, &var);
            break;
        }
    }

    if (_pLangBarItemSink == nullptr)
        return E_FAIL;

    return _pLangBarItemSink->OnUpdate(TF_LBI_ICON | TF_LBI_STATUS);
}

//
// CTextService::_InitLanguageBar / _UninitLanguageBar / _UpdateLanguageBar
//

BOOL CTextService::_InitLanguageBar()
{
    ITfLangBarItemMgr* pLangBarItemMgr;
    BOOL fRet = FALSE;

    if (_pThreadMgr->QueryInterface(IID_ITfLangBarItemMgr, (void**)&pLangBarItemMgr) != S_OK)
        return FALSE;

    // ブランドアイコン（独自 GUID）
    _pLangBarItemBrand = new CLangBarItemButton(this, c_guidLangBarItemButton);
    if (_pLangBarItemBrand != nullptr)
    {
        if (pLangBarItemMgr->AddItem(_pLangBarItemBrand) != S_OK)
        {
            _pLangBarItemBrand->Release();
            _pLangBarItemBrand = nullptr;
        }
        else
        {
            fRet = TRUE;
        }
    }

    // モードアイコン（★ GUID_LBI_INPUTMODE を使用 ★）
    _pLangBarItemMode = new CLangBarItemButton(this, GUID_LBI_INPUTMODE);
    if (_pLangBarItemMode != nullptr)
    {
        if (pLangBarItemMgr->AddItem(_pLangBarItemMode) != S_OK)
        {
            _pLangBarItemMode->Release();
            _pLangBarItemMode = nullptr;
        }
        else
        {
            fRet = TRUE;
        }
    }

    pLangBarItemMgr->Release();
    return fRet;
}

void CTextService::_UninitLanguageBar()
{
    ITfLangBarItemMgr* pLangBarItemMgr;

    if (_pThreadMgr == nullptr)
        return;

    if (_pThreadMgr->QueryInterface(IID_ITfLangBarItemMgr, (void**)&pLangBarItemMgr) != S_OK)
        return;

    if (_pLangBarItemBrand != nullptr)
    {
        pLangBarItemMgr->RemoveItem(_pLangBarItemBrand);
        _pLangBarItemBrand->Release();
        _pLangBarItemBrand = nullptr;
    }

    if (_pLangBarItemMode != nullptr)
    {
        pLangBarItemMgr->RemoveItem(_pLangBarItemMode);
        _pLangBarItemMode->Release();
        _pLangBarItemMode = nullptr;
    }

    pLangBarItemMgr->Release();
}

void CTextService::_UpdateLanguageBar()
{
    if (_pLangBarItemBrand)
        _pLangBarItemBrand->_Update();

    if (_pLangBarItemMode)
        _pLangBarItemMode->_Update();
}