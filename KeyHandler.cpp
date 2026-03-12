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

//+---------------------------------------------------------------------------
//
// CKeyHandlerEditSession
//
//----------------------------------------------------------------------------

class CKeyHandlerEditSession : public CEditSessionBase
{
public:
    CKeyHandlerEditSession(CTextService* pTextService, ITfContext* pContext, WPARAM wParam) : CEditSessionBase(pTextService, pContext)
    {
        _wParam = wParam;
    }

    // ITfEditSession
    STDMETHODIMP DoEditSession(TfEditCookie ec);

private:
    WPARAM _wParam;
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

    default:
        if ((_wParam >= 'A' && _wParam <= 'Z') ||
            (_wParam >= '0' && _wParam <= '9'))
            return _pTextService->_HandleCharacterKey(ec, _pContext, _wParam);
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

HRESULT CTextService::_HandleCharacterKey(TfEditCookie ec, ITfContext* pContext, WPARAM wParam)
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
    WCHAR ch = NormalizeRawInputChar((WCHAR)wParam, mode);

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

    if (phase == CompositionPhase::Converting || phase == CompositionPhase::CandidateSelecting)
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
        return _SelectNextCandidate(ec, pContext);
    }

    if (phase == CompositionPhase::Converting)
    {
        _compositionState.EnterCandidateSelecting();
        _compositionPhase = _compositionState.GetPhase();
        HRESULT hr = _UpdateCompositionText(ec, pContext);
        if (FAILED(hr))
        {
            return hr;
        }
        return _ShowCandidateList(ec, pContext);
    }

    std::vector<std::wstring> candidates = _kanaKanjiConverter.GenerateCandidates(
        _compositionState.GetReading(),
        _compositionState.GetKatakanaText(_romajiConverter),
        _compositionState.GetHalfwidthRomanText(),
        _compositionState.GetFullwidthRomanText());
    if (candidates.empty())
    {
        return S_OK;
    }

    _compositionState.StartConversion(candidates);
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
    if (phase == CompositionPhase::CandidateSelecting)
    {
        if (wParam == VK_LEFT)
        {
            return _SelectFirstCandidate(ec, pContext);
        }

        return _SelectLastCandidate(ec, pContext);
    }

    if (phase == CompositionPhase::Converting)
    {
        _CancelConversion(ec, pContext);
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
    if ((pEditSession = new CKeyHandlerEditSession(this, pContext, wParam)) == NULL)
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
        _compositionState.GetPhase() == CompositionPhase::CandidateSelecting)
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
        _compositionState.GetPhase() == CompositionPhase::CandidateSelecting)
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
        _TerminateComposition(ec, pContext);
        return S_OK;
    }

    _compositionPhase = _compositionState.GetPhase();
    return _UpdateCompositionText(ec, pContext);
}

HRESULT CTextService::_HandleEscapeKey(TfEditCookie ec, ITfContext* pContext)
{
    if (_compositionState.GetPhase() != CompositionPhase::Converting &&
        _compositionState.GetPhase() != CompositionPhase::CandidateSelecting)
    {
        return S_OK;
    }

    HRESULT hr = _CancelConversion(ec, pContext);
    _CloseCandidateList();
    return hr;
}


