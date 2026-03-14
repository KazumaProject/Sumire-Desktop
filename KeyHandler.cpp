//////////////////////////////////////////////////////////////////////
//
//  THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
//  ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED
//  TO THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
//  PARTICULAR PURPOSE.
//
//  Copyright (C) 2003  Microsoft Corporation.  All rights reserved.
//
//  KeyHandler.cpp
//
//          the handler routines for key events
//
//////////////////////////////////////////////////////////////////////

#include "Globals.h"
#include "EditSession.h"
#include "TextService.h"
#include "CandidateList.h"

#include <cwctype>

//+---------------------------------------------------------------------------
//
// CKeyHandlerEditSession
//
//----------------------------------------------------------------------------

class CKeyHandlerEditSession : public CEditSessionBase
{
public:
    CKeyHandlerEditSession(CTextService* pTextService, ITfContext* pContext, WPARAM wParam, LPARAM lParam) : CEditSessionBase(pTextService, pContext)
    {
        _wParam = wParam;
        _lParam = lParam;
    }

    // ITfEditSession
    STDMETHODIMP DoEditSession(TfEditCookie ec);

private:
    WPARAM _wParam;
    LPARAM _lParam;
};

//+---------------------------------------------------------------------------
//
// DoEditSession
//
//----------------------------------------------------------------------------

STDAPI CKeyHandlerEditSession::DoEditSession(TfEditCookie ec)
{

    switch (_wParam)
    {
    case VK_LEFT:
    case VK_RIGHT:
        return _pTextService->_HandleArrowKey(ec, _pContext, _wParam);

    case VK_RETURN:
        return _pTextService->_HandleReturnKey(ec, _pContext);

    case VK_SPACE:
        return _pTextService->_HandleSpaceKey(ec, _pContext);

    case VK_BACK:
        return _pTextService->_HandleBackspaceKey(ec, _pContext);

    case VK_DELETE:
        return _pTextService->_HandleDeleteKey(ec, _pContext);

    case VK_ESCAPE:
        return _pTextService->_HandleEscapeKey(ec, _pContext);

    case VK_F12:
        DebugLog(L"[KeyHandler] F12 path entered\r\n");
        return _pTextService->_HandleModeToggleKey(ec, _pContext);

    default:
        if ((_wParam >= 'A' && _wParam <= 'Z') ||
            (_wParam >= '0' && _wParam <= '9'))
            return _pTextService->_HandleCharacterKey(ec, _pContext, _wParam, _lParam);
        return _pTextService->_HandleCharacterKey(ec, _pContext, _wParam, _lParam);
        break;
    }

    return S_OK;

}

static WCHAR NormalizeRawInputChar(WCHAR ch, InputMode mode)
{
    switch (mode)
    {
    case InputMode::Hiragana:
    case InputMode::HalfwidthKatakana:
    case InputMode::FullwidthKatakana:
        if (ch >= L'A' && ch <= L'Z')
        {
            return static_cast<WCHAR>(ch - L'A' + L'a');
        }
        return ch;
    case InputMode::DirectInput:
    case InputMode::FullwidthAlphanumeric:
    default:
        return ch;
    }
}

static bool TryTranslateVirtualKeyToChar(WPARAM wParam, LPARAM lParam, WCHAR* ch)
{
    if (ch == nullptr)
    {
        return false;
    }

    if ((wParam >= 'A' && wParam <= 'Z') || (wParam >= '0' && wParam <= '9'))
    {
        *ch = static_cast<WCHAR>(wParam);
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
        *ch = translated[0];
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


//+---------------------------------------------------------------------------
//
// IsRangeCovered
//
// Returns TRUE if pRangeTest is entirely contained within pRangeCover.
//
//----------------------------------------------------------------------------

BOOL IsRangeCovered(TfEditCookie ec, ITfRange* pRangeTest, ITfRange* pRangeCover)
{
    LONG lResult;

    if (pRangeCover->CompareStart(ec, pRangeTest, TF_ANCHOR_START, &lResult) != S_OK ||
        lResult > 0)
    {
        return FALSE;
    }

    if (pRangeCover->CompareEnd(ec, pRangeTest, TF_ANCHOR_END, &lResult) != S_OK ||
        lResult < 0)
    {
        return FALSE;
    }

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// _HandleCharacterKey
//
// If the keystroke happens within a composition, eat the key and return S_OK.
//
//----------------------------------------------------------------------------

HRESULT CTextService::_HandleCharacterKey(TfEditCookie ec, ITfContext* pContext, WPARAM wParam, LPARAM lParam)
{
    if (!_IsComposing())
    {
        _StartComposition(pContext);
    }

    if (!_IsComposing())
    {
        return E_FAIL;
    }

    InputMode mode = GetEffectiveInputMode();
    WCHAR rawChar = 0;
    if (!TryTranslateVirtualKeyToChar(wParam, lParam, &rawChar))
    {
        return S_FALSE;
    }

    WCHAR ch = NormalizeRawInputChar(rawChar, mode);

    _compositionState.Begin();
    _compositionState.InsertRawChar(ch, mode, _romajiConverter);
    _compositionPhase = _compositionState.GetPhase();
    return _UpdateCompositionText(ec, pContext);
}

//+---------------------------------------------------------------------------
//
// _HandleReturnKey
//
//----------------------------------------------------------------------------

HRESULT CTextService::_HandleReturnKey(TfEditCookie ec, ITfContext* pContext)
{
    CompositionPhase phase = _compositionState.GetPhase();
    if (phase == CompositionPhase::Idle)
    {
        return S_OK;
    }

    if (phase == CompositionPhase::CandidateSelecting ||
        phase == CompositionPhase::Converting ||
        phase == CompositionPhase::RechunkSelecting)
    {
        return _CommitCurrentCandidate(ec, pContext);
    }

    _TerminateComposition(ec, pContext);
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleSpaceKey
//
//----------------------------------------------------------------------------

HRESULT CTextService::_HandleSpaceKey(TfEditCookie ec, ITfContext* pContext)
{
    CompositionPhase phase = _compositionState.GetPhase();
    if (phase == CompositionPhase::Idle)
    {
        return S_OK;
    }

    if (phase == CompositionPhase::CandidateSelecting)
    {
        if (!_compositionState.BeginRechunkSelection(_romajiConverter))
        {
            return S_OK;
        }

        _compositionPhase = _compositionState.GetPhase();
        HRESULT hr = _UpdateCompositionText(ec, pContext);
        if (FAILED(hr))
        {
            return hr;
        }

        return _ShowCandidateList(ec, pContext);
    }

    if (phase == CompositionPhase::RechunkSelecting)
    {
        return _SelectNextCandidate(ec, pContext);
    }

    if (phase == CompositionPhase::Converting)
    {
        if (!_compositionState.BeginSegmentSelection())
        {
            return S_OK;
        }

        _compositionPhase = _compositionState.GetPhase();
        HRESULT hr = _UpdateCompositionText(ec, pContext);
        if (FAILED(hr))
        {
            return hr;
        }

        return _ShowCandidateList(ec, pContext);
    }

    if (!_compositionState.StartConversion(_kanaKanjiConverter, GetEffectiveInputMode(), _romajiConverter))
    {
        return S_OK;
    }

    _compositionPhase = _compositionState.GetPhase();
    return _UpdateCompositionText(ec, pContext);
}

//+---------------------------------------------------------------------------
//
// _HandleArrowKey
//
// Update the selection within a composition.
//
//----------------------------------------------------------------------------

HRESULT CTextService::_HandleArrowKey(TfEditCookie ec, ITfContext* pContext, WPARAM wParam)
{
    if (!_IsComposing() || _pComposition == NULL)
    {
        return S_FALSE;
    }

    CompositionPhase phase = _compositionState.GetPhase();
    if (phase == CompositionPhase::CandidateSelecting ||
        phase == CompositionPhase::RechunkSelecting)
    {
        bool moved = (wParam == VK_LEFT)
            ? _compositionState.MoveFocusLeft()
            : _compositionState.MoveFocusRight();
        _compositionPhase = _compositionState.GetPhase();
        if (!moved)
        {
            return S_OK;
        }
        return _UpdateCompositionText(ec, pContext);
    }

    if (phase == CompositionPhase::Converting)
    {
        return S_OK;
    }

    InputMode mode = GetEffectiveInputMode();

    if (wParam == VK_LEFT)
    {
        _compositionState.MoveLeft(mode, _romajiConverter);
    }
    else
    {
        _compositionState.MoveRight(mode, _romajiConverter);
    }

    return _UpdateCompositionText(ec, pContext);
}

//+---------------------------------------------------------------------------
//
// _InvokeKeyHandler
//
// This text service is interested in handling keystrokes to demonstrate the
// use the compositions. Some apps will cancel compositions if they receive
// keystrokes while a compositions is ongoing.
//
//----------------------------------------------------------------------------

HRESULT CTextService::_InvokeKeyHandler(ITfContext* pContext, WPARAM wParam, LPARAM lParam)
{
    CKeyHandlerEditSession* pEditSession;
    HRESULT hr = E_FAIL;

    // we'll insert a char ourselves in place of this keystroke
    if ((pEditSession = new CKeyHandlerEditSession(this, pContext, wParam, lParam)) == NULL)
        goto Exit;

    // we need a lock to do our work
    // nb: this method is one of the few places where it is legal to use
    // the TF_ES_SYNC flag
    hr = pContext->RequestEditSession(_tfClientId, pEditSession, TF_ES_SYNC | TF_ES_READWRITE, &hr);

    pEditSession->Release();

Exit:
    return hr;
}

HRESULT CTextService::_HandleBackspaceKey(TfEditCookie ec, ITfContext* pContext)
{
    if (!_IsComposing() || _pComposition == nullptr)
    {
        return S_FALSE;
    }

    if (_compositionState.GetPhase() == CompositionPhase::Converting ||
        _compositionState.GetPhase() == CompositionPhase::CandidateSelecting ||
        _compositionState.GetPhase() == CompositionPhase::RechunkSelecting)
    {
        return _CancelConversion(ec, pContext);
    }

    InputMode mode = GetEffectiveInputMode();
    if (!_compositionState.Backspace(mode, _romajiConverter))
    {
        return S_OK;
    }

    if (_compositionState.Empty())
    {
        _ClearCompositionText(ec, pContext);
        _TerminateComposition(ec, pContext);
        return S_OK;
    }

    _compositionPhase = _compositionState.GetPhase();
    return _UpdateCompositionText(ec, pContext);
}

HRESULT CTextService::_HandleDeleteKey(TfEditCookie ec, ITfContext* pContext)
{
    if (!_IsComposing() || _pComposition == nullptr)
    {
        return S_FALSE;
    }

    if (_compositionState.GetPhase() == CompositionPhase::Converting ||
        _compositionState.GetPhase() == CompositionPhase::CandidateSelecting ||
        _compositionState.GetPhase() == CompositionPhase::RechunkSelecting)
    {
        return _CancelConversion(ec, pContext);
    }

    InputMode mode = GetEffectiveInputMode();
    if (!_compositionState.Delete(mode, _romajiConverter))
    {
        return S_OK;
    }

    if (_compositionState.Empty())
    {
        _ClearCompositionText(ec, pContext);
        _TerminateComposition(ec, pContext);
        return S_OK;
    }

    _compositionPhase = _compositionState.GetPhase();
    return _UpdateCompositionText(ec, pContext);
}

HRESULT CTextService::_HandleEscapeKey(TfEditCookie ec, ITfContext* pContext)
{
    if (_compositionState.GetPhase() == CompositionPhase::RechunkSelecting)
    {
        if (!_compositionState.CancelRechunkSelection())
        {
            return S_OK;
        }

        _compositionPhase = _compositionState.GetPhase();
        return _UpdateCompositionText(ec, pContext);
    }

    if (_compositionState.GetPhase() != CompositionPhase::Converting &&
        _compositionState.GetPhase() != CompositionPhase::CandidateSelecting)
    {
        return S_OK;
    }

    HRESULT hr = _CancelConversion(ec, pContext);
    _CloseCandidateList();
    return hr;
}

HRESULT CTextService::_HandleModeToggleKey(TfEditCookie ec, ITfContext* pContext)
{
    UNREFERENCED_PARAMETER(ec);
    UNREFERENCED_PARAMETER(pContext);

    DebugLog(L"[KeyHandler] F12 toggle executed before user=%d effective=%d\r\n",
        static_cast<int>(GetUserInputMode()),
        static_cast<int>(GetEffectiveInputMode()));
    ToggleInputMode();
    _SetKeyboardOpen(TRUE);
    _UpdateLanguageBar();
    DebugLog(L"[KeyHandler] F12 toggle executed after user=%d effective=%d\r\n",
        static_cast<int>(GetUserInputMode()),
        static_cast<int>(GetEffectiveInputMode()));
    return S_OK;
}


