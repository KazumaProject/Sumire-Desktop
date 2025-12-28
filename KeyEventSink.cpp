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

//+---------------------------------------------------------------------------
//
// _IsKeyEaten
//
//----------------------------------------------------------------------------

BOOL CTextService::_IsKeyEaten(ITfContext* pContext, WPARAM wParam)
{
    // 1. キーボードが無効なら何も食べない
    if (_IsKeyboardDisabled())
        return FALSE;

    // 2. IME OFF なら食べない
    if (!_IsKeyboardOpen())
        return FALSE;

    //
    // 3. 候補ウィンドウ表示中は、キー処理を CandidateList 側に任せる
    //    （元サンプルと同じ挙動）
    //
    if (_pCandidateList &&
        _pCandidateList->_IsContextCandidateWindow(pContext))
    {
        return FALSE;
    }

    //
    // 4. 入力モード共通で扱いたいキー
    //    - F12: IME 内部の「あ／ENG」モード切替用
    //      → KeyHandler（CKeyHandlerEditSession）に処理させるため IME が食う
    //
    if (wParam == VK_F12)
    {
        return TRUE;
    }

    //
    // 5. ENG モード (半角英数モード) のとき:
    //    - 通常のキー入力はアプリケーションにそのまま流す
    //    - つまりローマ字かな変換は走らない
    //
    if (_inputMode == INPUTMODE_ALPHANUMERIC)
    {
        // 上で F12 だけは TRUE で返しているので、ここでは全て FALSE
        return FALSE;
    }

    //
    // 6. ここからは「ひらがなモード」のキー判定
    //    - 元のサンプルのロジックをベースに、ローマ字入力用として A〜Z, 0〜9 を食う
    //
    switch (wParam)
    {
    case VK_LEFT:
    case VK_RIGHT:
    case VK_RETURN:
    case VK_SPACE:
    case VK_BACK:
    case VK_DELETE:
        // composition 中だけ IME 側で処理
        if (_IsComposing())
            return TRUE;
        return FALSE;
    }

    // A〜Z はローマ字かな入力として IME が食う
    if (wParam >= 'A' && wParam <= 'Z')
        return TRUE;

    // 0〜9 もローマ字テーブル側で使うので IME が食う
    if (wParam >= '0' && wParam <= '9')
        return TRUE;

    return FALSE;
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

    *pfEaten = _IsKeyEaten(pContext, wParam);
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

    *pfEaten = _IsKeyEaten(pContext, wParam);

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
    HRESULT hr;

    if (_pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&pKeystrokeMgr) != S_OK)
        return FALSE;

    hr = pKeystrokeMgr->AdviseKeyEventSink(_tfClientId, (ITfKeyEventSink*)this, TRUE);

    pKeystrokeMgr->Release();

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
    HRESULT hr;

    if (_pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&pKeystrokeMgr) != S_OK)
        return FALSE;

    // register Alt+~ key
    hr = pKeystrokeMgr->PreserveKey(_tfClientId,
        GUID_PRESERVEDKEY_ONOFF,
        &c_pkeyOnOff0,
        c_szPKeyOnOff,
        lstrlen(c_szPKeyOnOff));

    // register KANJI key
    hr = pKeystrokeMgr->PreserveKey(_tfClientId,
        GUID_PRESERVEDKEY_ONOFF,
        &c_pkeyOnOff1,
        c_szPKeyOnOff,
        lstrlen(c_szPKeyOnOff));

    // register F6 key (サンプルのまま登録しておく。必要なら後で利用可能)
    hr = pKeystrokeMgr->PreserveKey(_tfClientId,
        GUID_PRESERVEDKEY_F6,
        &c_pkeyF6,
        c_szPKeyF6,
        lstrlen(c_szPKeyF6));

    pKeystrokeMgr->Release();

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
