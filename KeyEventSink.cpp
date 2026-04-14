//////////////////////////////////////////////////////////////////////
//
//  THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
//  ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED
//  TO THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
//  PARTICULAR PURPOSE.
//
//  Copyright (C) 2003  Microsoft Corporation.  All rights reserved.
//
//  KeyEventSink.cpp
//
//          ITfKeyEventSink implementation.
//
//////////////////////////////////////////////////////////////////////

#include "Globals.h"
#include "TextService.h"
#include "CandidateList.h"

#include <cwctype>

//
// GUID for the preserved keys.
//
/* 6a0bde41-6adf-11d7-a6ea-00065b84435c */
static const GUID GUID_PRESERVEDKEY_ONOFF = {
    0x6a0bde41,
    0x6adf,
    0x11d7,
    {0xa6, 0xea, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}
};

/* 6a0bde42-6adf-11d7-a6ea-00065b84435c */
static const GUID GUID_PRESERVEDKEY_F6 = {
    0x6a0bde42,
    0x6adf,
    0x11d7,
    {0xa6, 0xea, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}
};

//
// the preserved keys declaration
//
// VK_KANJI is the virtual key for Kanji key, which is available in 106
// Japanese keyboard.
//
static const TF_PRESERVEDKEY c_pkeyOnOff0 = { 0xC0, TF_MOD_ALT };
static const TF_PRESERVEDKEY c_pkeyOnOff1 = { VK_KANJI, TF_MOD_IGNORE_ALL_MODIFIER };
static const TF_PRESERVEDKEY c_pkeyF6 = { VK_F6, TF_MOD_ON_KEYUP };

//
// the description for the preserved keys
//
static const WCHAR c_szPKeyOnOff[] = L"OnOff";
static const WCHAR c_szPKeyF6[] = L"Function 6";

namespace
{
bool CanTranslateToPrintableChar(WPARAM wParam, LPARAM lParam)
{
    if ((wParam >= 'A' && wParam <= 'Z') || (wParam >= '0' && wParam <= '9'))
    {
        return true;
    }

    BYTE keyboardState[256] = {};
    if (!GetKeyboardState(keyboardState))
    {
        return false;
    }

    WCHAR translated[4] = {};
    const UINT scanCode = (static_cast<UINT>(lParam) >> 16) & 0xFF;
    const HKL keyboardLayout = GetKeyboardLayout(0);
    const int translatedCount = ToUnicodeEx(
        static_cast<UINT>(wParam),
        scanCode,
        keyboardState,
        translated,
        ARRAYSIZE(translated),
        0,
        keyboardLayout);

    if (translatedCount > 0 && iswprint(translated[0]))
    {
        return true;
    }

    if (translatedCount < 0)
    {
        BYTE emptyState[256] = {};
        ToUnicodeEx(
            static_cast<UINT>(wParam),
            scanCode,
            emptyState,
            translated,
            ARRAYSIZE(translated),
            0,
            keyboardLayout);
    }

    return false;
}
}

//+---------------------------------------------------------------------------
//
// _IsKeyEaten
//
//----------------------------------------------------------------------------

BOOL CTextService::_IsKeyEaten(ITfContext* pContext, WPARAM wParam, LPARAM lParam)
{
    // 1. キーボードが無効なら何も食べない
    if (_IsKeyboardDisabled())
    {
        if (wParam == VK_F12)
        {
            DebugLog(L"[_IsKeyEaten] wParam=VK_F12 result=FALSE reason=keyboard_disabled\r\n");
        }
        return FALSE;
    }

    //
    // 2. 候補ウィンドウ表示中は、キー処理を CandidateList 側に任せる
    //    （元サンプルと同じ挙動）
    //
    if (_pCandidateList &&
        _pCandidateList->_IsContextCandidateWindow(pContext))
    {
        return FALSE;
    }

    std::wstring command;
    if (_TryGetKeymapCommand(wParam, lParam, &command))
    {
        return TRUE;
    }

    // F12 は現在の内部モード切替の後方互換として残す。
    if (wParam == VK_F12)
    {
        DebugLog(L"[_IsKeyEaten] wParam=VK_F12 result=TRUE fallback\r\n");
        return TRUE;
    }

    if (!_IsKeyboardOpen())
    {
        return FALSE;
    }

    if (wParam == VK_SHIFT && GetEffectiveInputMode() == InputMode::Hiragana)
    {
        return TRUE;
    }

    return FALSE;
}

std::wstring CTextService::_GetKeymapStatus() const
{
    if (GetEffectiveInputMode() == InputMode::DirectInput)
    {
        return L"DirectInput";
    }

    switch (_compositionState.GetPhase())
    {
    case CompositionPhase::Idle:
        return L"Precomposition";
    case CompositionPhase::Composing:
        return L"Composition";
    case CompositionPhase::Converting:
    case CompositionPhase::CandidateSelecting:
    case CompositionPhase::RechunkSelecting:
        return L"Conversion";
    default:
        return L"Precomposition";
    }
}

bool CTextService::_TryGetKeymapCommand(WPARAM wParam, LPARAM lParam, std::wstring* command) const
{
    bool printableAscii = false;
    const std::wstring key = SumireKeymap::NormalizeKeyStroke(wParam, lParam, &printableAscii);
    if (key.empty())
    {
        return false;
    }

    std::wstring foundCommand;
    if (!SumireKeymap::FindCommand(_keymap, _GetKeymapStatus(), key, printableAscii, &foundCommand))
    {
        return false;
    }

    if (!SumireKeymap::IsSupportedCommand(foundCommand))
    {
        return false;
    }

    if (command != nullptr)
    {
        *command = foundCommand;
    }
    return true;
}

//+---------------------------------------------------------------------------
//
// OnSetFocus
//
// Called by the system whenever this service gets the keystroke device focus.
//----------------------------------------------------------------------------

STDAPI CTextService::OnSetFocus(BOOL fForeground)
{
    UNREFERENCED_PARAMETER(fForeground);
    _ReloadSettings();
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// OnTestKeyDown
//
// Called by the system to query this service wants a potential keystroke.
//----------------------------------------------------------------------------

STDAPI CTextService::OnTestKeyDown(ITfContext* pContext, WPARAM wParam, LPARAM lParam, BOOL* pfEaten)
{
    UNREFERENCED_PARAMETER(lParam);

    if (pfEaten == NULL)
        return E_INVALIDARG;

    *pfEaten = _IsKeyEaten(pContext, wParam, lParam);
    if (wParam == VK_F12)
    {
        DebugLog(L"[OnTestKeyDown] wParam=VK_F12 pfEaten=%s\r\n", *pfEaten ? L"TRUE" : L"FALSE");
    }
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// OnKeyDown
//
// Called by the system to offer this service a keystroke.  If *pfEaten == TRUE
// on exit, the application will not handle the keystroke.
//----------------------------------------------------------------------------

STDAPI CTextService::OnKeyDown(ITfContext* pContext, WPARAM wParam, LPARAM lParam, BOOL* pfEaten)
{
    UNREFERENCED_PARAMETER(lParam);

    if (pfEaten == NULL)
        return E_INVALIDARG;

    *pfEaten = _IsKeyEaten(pContext, wParam, lParam);
    if (wParam == VK_F12)
    {
        DebugLog(L"[OnKeyDown] wParam=VK_F12 pfEaten=%s\r\n", *pfEaten ? L"TRUE" : L"FALSE");
    }

    if (*pfEaten)
    {
        // IME 側で処理するキーは EditSession で処理
        _InvokeKeyHandler(pContext, wParam, lParam);
    }
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// OnTestKeyUp
//
// Called by the system to query this service wants a potential keystroke.
//----------------------------------------------------------------------------

STDAPI CTextService::OnTestKeyUp(ITfContext* pContext, WPARAM wParam, LPARAM lParam, BOOL* pfEaten)
{
    UNREFERENCED_PARAMETER(pContext);
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(lParam);

    if (pfEaten == NULL)
        return E_INVALIDARG;

    // KeyUp は特に処理しないので常に FALSE
    *pfEaten = FALSE;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// OnKeyUp
//
// Called by the system to offer this service a keystroke.  If *pfEaten == TRUE
// on exit, the application will not handle the keystroke.
//----------------------------------------------------------------------------

STDAPI CTextService::OnKeyUp(ITfContext* pContext, WPARAM wParam, LPARAM lParam, BOOL* pfEaten)
{
    UNREFERENCED_PARAMETER(pContext);
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(lParam);

    if (pfEaten == NULL)
        return E_INVALIDARG;

    *pfEaten = FALSE;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// OnPreservedKey
//
// Called when a hotkey (registered by us, or by the system) is typed.
//----------------------------------------------------------------------------

STDAPI CTextService::OnPreservedKey(ITfContext* pContext, REFGUID rguid, BOOL* pfEaten)
{
    if (pfEaten == NULL)
        return E_INVALIDARG;

    DebugLog(L"[KeyEventSink] OnPreservedKey entered\r\n");
    DebugLogGuid(L"[KeyEventSink] OnPreservedKey rguid", rguid);

    if (IsEqualGUID(rguid, GUID_PRESERVEDKEY_ONOFF))
    {
        BOOL fOpen = _IsKeyboardOpen();
        _SetKeyboardOpen(fOpen ? FALSE : TRUE);
        *pfEaten = TRUE;
    }
    else
    {
        // GUID_PRESERVEDKEY_F6 は今のところ何もしない（サンプルのまま）
        *pfEaten = FALSE;
    }

    UNREFERENCED_PARAMETER(pContext);
    _UpdateLanguageBar();
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _InitKeyEventSink
//
// Advise a keystroke sink.
//----------------------------------------------------------------------------

BOOL CTextService::_InitKeyEventSink()
{
    ITfKeystrokeMgr* pKeystrokeMgr;
    HRESULT hr = E_FAIL;

    hr = _pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&pKeystrokeMgr);
    DebugLogHr(L"[KeyEventSink] QueryInterface(ITfKeystrokeMgr)", hr);
    if (hr != S_OK)
    {
        DebugLogBool(L"[KeyEventSink] _InitKeyEventSink", FALSE);
        return FALSE;
    }

    hr = pKeystrokeMgr->AdviseKeyEventSink(_tfClientId, (ITfKeyEventSink*)this, TRUE);
    DebugLogHr(L"[KeyEventSink] AdviseKeyEventSink", hr);

    pKeystrokeMgr->Release();

    DebugLogBool(L"[KeyEventSink] _InitKeyEventSink", (hr == S_OK));
    return (hr == S_OK);
}

//+---------------------------------------------------------------------------
//
// _UninitKeyEventSink
//
// Unadvise a keystroke sink.  Assumes we have advised one already.
//----------------------------------------------------------------------------

void CTextService::_UninitKeyEventSink()
{
    ITfKeystrokeMgr* pKeystrokeMgr;

    if (_pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&pKeystrokeMgr) != S_OK)
        return;

    pKeystrokeMgr->UnadviseKeyEventSink(_tfClientId);

    pKeystrokeMgr->Release();
}

//+---------------------------------------------------------------------------
//
// _InitPreservedKey
//
// Register a hot key.
//----------------------------------------------------------------------------

BOOL CTextService::_InitPreservedKey()
{
    ITfKeystrokeMgr* pKeystrokeMgr;
    HRESULT hr = E_FAIL;

    hr = _pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&pKeystrokeMgr);
    DebugLogHr(L"[KeyEventSink] QueryInterface(ITfKeystrokeMgr) for preserved", hr);
    if (hr != S_OK)
    {
        DebugLogBool(L"[KeyEventSink] _InitPreservedKey", FALSE);
        return FALSE;
    }

    // register Alt+~ key
    hr = pKeystrokeMgr->PreserveKey(_tfClientId,
        GUID_PRESERVEDKEY_ONOFF,
        &c_pkeyOnOff0,
        c_szPKeyOnOff,
        lstrlen(c_szPKeyOnOff));
    DebugLogHr(L"[KeyEventSink] PreserveKey Alt+~", hr);

    // register KANJI key
    hr = pKeystrokeMgr->PreserveKey(_tfClientId,
        GUID_PRESERVEDKEY_ONOFF,
        &c_pkeyOnOff1,
        c_szPKeyOnOff,
        lstrlen(c_szPKeyOnOff));
    DebugLogHr(L"[KeyEventSink] PreserveKey KANJI", hr);

    // register F6 key (サンプルのまま登録しておく。必要なら後で利用可能)
    hr = pKeystrokeMgr->PreserveKey(_tfClientId,
        GUID_PRESERVEDKEY_F6,
        &c_pkeyF6,
        c_szPKeyF6,
        lstrlen(c_szPKeyF6));
    DebugLogHr(L"[KeyEventSink] PreserveKey F6", hr);

    pKeystrokeMgr->Release();

    DebugLogBool(L"[KeyEventSink] _InitPreservedKey", (hr == S_OK));
    return (hr == S_OK);
}

//+---------------------------------------------------------------------------
//
// _UninitPreservedKey
//
// Uninit a hot key.
//----------------------------------------------------------------------------

void CTextService::_UninitPreservedKey()
{
    ITfKeystrokeMgr* pKeystrokeMgr;

    if (_pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&pKeystrokeMgr) != S_OK)
        return;

    pKeystrokeMgr->UnpreserveKey(GUID_PRESERVEDKEY_ONOFF, &c_pkeyOnOff0);
    pKeystrokeMgr->UnpreserveKey(GUID_PRESERVEDKEY_ONOFF, &c_pkeyOnOff1);
    pKeystrokeMgr->UnpreserveKey(GUID_PRESERVEDKEY_F6, &c_pkeyF6);

    pKeystrokeMgr->Release();
}

