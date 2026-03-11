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

    default:
        if ((_wParam >= 'A' && _wParam <= 'Z') ||
            (_wParam >= '0' && _wParam <= '9'))
            return _pTextService->_HandleCharacterKey(ec, _pContext, _wParam);
        break;
    }

    return S_OK;

}

// 半角英数字 → 全角英数字 に変換するヘルパー
static WCHAR ToFullWidth(WCHAR ch)
{
    // 数字
    if (ch >= L'0' && ch <= L'9')
    {
        return (WCHAR)(0xFF10 + (ch - L'0'));   // '０'～'９'
    }

    // 大文字英字
    if (ch >= L'A' && ch <= L'Z')
    {
        return (WCHAR)(0xFF21 + (ch - L'A'));   // 'Ａ'～'Ｚ'
    }

    // 小文字英字（必要なら）
    if (ch >= L'a' && ch <= L'z')
    {
        return (WCHAR)(0xFF41 + (ch - L'a'));   // 'ａ'～'ｚ'
    }

    // それ以外はそのまま
    return ch;
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
    ITfRange* pRangeComposition = nullptr;
    TF_SELECTION tfSelection;
    ULONG cFetched = 0;
    BOOL fCovered = FALSE;

    // 1. Composition が無ければ開始する
    if (!_IsComposing())
    {
        _StartComposition(pContext);   // 戻り値は気にしない
        _composingText.Reset();        // 新しく始めたタイミングで ComposingText もリセット
    }

    // 2. 入力されたキーを文字に変換（元コードと同じく VK → WCHAR）
    WCHAR ch = (WCHAR)wParam;

    // ★ ここで半角 → 全角に変換（RawText は全角アルファベットとして保持）
    ch = ToFullWidth(ch);

    // 3. 現在の選択範囲（キャレット位置）を取得
    if (pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &tfSelection, &cFetched) != S_OK ||
        cFetched != 1)
    {
        return S_FALSE;
    }

    // 4. 今のキャレット位置が composition の中かどうか確認
    if (_pComposition != nullptr && _pComposition->GetRange(&pRangeComposition) == S_OK)
    {
        fCovered = IsRangeCovered(ec, tfSelection.range, pRangeComposition);
    }

    if (!fCovered)
    {
        if (pRangeComposition)
        {
            pRangeComposition->Release();
        }
        tfSelection.range->Release();
        return S_OK;
    }

    // 5. ComposingText の RawText に 1 文字追加
    //
    //    本当は「RawCursor の位置に挿入」が理想ですが、
    //    まずは「常に末尾に追加する」簡易版とします。
    //    （RawCursor は常に末尾＝length になります）
    //
    _composingText.InsertCharAtEnd(ch);

    // Raw のカーソル位置を末尾にそろえる
    LONG rawLen = (LONG)_composingText.GetRawText().size();
    _composingText.SetRaw(_composingText.GetRawText(), rawLen);

    // 6. RawText → SurfaceText へ変換
    //
    //   - RawText には「全角アルファベット」が詰まっている
    //   - _romajiConverter.ConvertFromRaw() の内部で
    //       全角 → 半角 → ローマ字かな変換
    //     を行い、ひらがな文字列を返す
    //
    const std::wstring& raw = _composingText.GetRawText();
    std::wstring surface = _romajiConverter.ConvertFromRaw(raw);

    LONG surfaceLen = (LONG)surface.size();
    _composingText.SetSurface(surface, surfaceLen);

    // ライブ変換はこのタイミングではリセット
    _composingText.ClearLiveConversionText();

    // 7. composition 全体を書き換える
    if (pRangeComposition)
    {
        const std::wstring& text = _composingText.GetCurrentText();

        // composition の範囲全体に SetText
        if (pRangeComposition->SetText(ec, 0, text.c_str(), (ULONG)text.size()) == S_OK)
        {
            // キャレットを末尾に移動（元コードと同じ挙動）
            tfSelection.range->Collapse(ec, TF_ANCHOR_END);
            pContext->SetSelection(ec, 1, &tfSelection);
        }

        pRangeComposition->Release();
    }

    //
    // 8. 表示属性を composition 全体に設定（元コードと同じ）
    //
    _SetCompositionDisplayAttributes(ec, pContext, _gaDisplayAttributeInput);

    tfSelection.range->Release();
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleReturnKey
//
//----------------------------------------------------------------------------

HRESULT CTextService::_HandleReturnKey(TfEditCookie ec, ITfContext* pContext)
{
    // just terminate the composition
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
    //
    // set the display attribute to the composition range.
    //
    // The real text service may have linguistic logic here and set 
    // the specific range to apply the display attribute rather than 
    // applying the display attribute to the entire composition range.
    //
    _SetCompositionDisplayAttributes(ec, pContext, _gaDisplayAttributeConverted);

    // 
    // create an instance of the candidate list class.
    // 
    if (_pCandidateList == NULL)
        _pCandidateList = new CCandidateList(this);

    // 
    // we don't cache the document manager object. So get it from pContext.
    // 
    ITfDocumentMgr* pDocumentMgr;
    if (pContext->GetDocumentMgr(&pDocumentMgr) == S_OK)
    {
        // 
        // get the composition range.
        // 
        ITfRange* pRange;
        if (_pComposition->GetRange(&pRange) == S_OK)
        {
            _pCandidateList->_StartCandidateList(_tfClientId, pDocumentMgr, pContext, ec, pRange);
            pRange->Release();
        }
        pDocumentMgr->Release();
    }
    return S_OK;
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
    ITfRange* pRangeComposition;
    LONG cch;
    BOOL fEqual;
    TF_SELECTION tfSelection;
    ULONG cFetched;

    // get the selection
    if (pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &tfSelection, &cFetched) != S_OK ||
        cFetched != 1)
    {
        // no selection?
        return S_OK; // eat the keystroke
    }

    // get the composition range
    if (_pComposition->GetRange(&pRangeComposition) != S_OK)
        goto Exit;

    // adjust the selection, we won't do anything fancy
    if (wParam == VK_LEFT)
    {
        if (tfSelection.range->IsEqualStart(ec, pRangeComposition, TF_ANCHOR_START, &fEqual) == S_OK &&
            !fEqual)
        {
            tfSelection.range->ShiftStart(ec, -1, &cch, NULL);
        }
        tfSelection.range->Collapse(ec, TF_ANCHOR_START);
    }
    else
    {
        // VK_RIGHT
        if (tfSelection.range->IsEqualEnd(ec, pRangeComposition, TF_ANCHOR_END, &fEqual) == S_OK &&
            !fEqual)
        {
            tfSelection.range->ShiftEnd(ec, +1, &cch, NULL);
        }
        tfSelection.range->Collapse(ec, TF_ANCHOR_END);
    }

    pContext->SetSelection(ec, 1, &tfSelection);

    pRangeComposition->Release();

Exit:
    tfSelection.range->Release();
    return S_OK; // eat the keystroke
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
    // composition 中だけ IME が処理する。非 composition ならアプリ側に任せる。
    if (!_IsComposing() || _pComposition == nullptr)
    {
        return S_FALSE; // = IME が食べない想定（呼び側の pfEaten 判定に合わせてください）
    }

    ITfRange* pRangeComposition = nullptr;
    TF_SELECTION tfSelection;
    ULONG cFetched = 0;
    BOOL fCovered = FALSE;

    // 現在の選択（キャレット）取得
    if (pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &tfSelection, &cFetched) != S_OK ||
        cFetched != 1)
    {
        return S_OK; // eat
    }

    // composition range 取得
    if (_pComposition->GetRange(&pRangeComposition) == S_OK)
    {
        fCovered = IsRangeCovered(ec, tfSelection.range, pRangeComposition);
    }

    if (!fCovered)
    {
        if (pRangeComposition) pRangeComposition->Release();
        tfSelection.range->Release();
        return S_OK; // eat（ここはポリシー次第だが、元コードに合わせて eat で OK）
    }

    // RawText の末尾 1 文字削除
    bool removed = _composingText.RemoveLastRawChar();

    // 何も消せない（空）なら composition を終了してクリーンにする
    if (!removed)
    {
        if (pRangeComposition) pRangeComposition->Release();
        tfSelection.range->Release();

        _TerminateComposition(ec, pContext);
        _composingText.Reset();
        return S_OK;
    }

    // Raw -> Surface 更新（カーソルも計算）
    _composingText.UpdateSurfaceFromRaw(_romajiConverter);

    // ライブ変換はこのタイミングではクリア
    _composingText.ClearLiveConversionText();

    // composition range 全体を書き換え
    if (pRangeComposition)
    {
        const std::wstring& text = _composingText.GetCurrentText();

        if (pRangeComposition->SetText(ec, 0, text.c_str(), (ULONG)text.size()) == S_OK)
        {
            // キャレット末尾へ（要望が末尾削除なので末尾に寄せる）
            tfSelection.range->Collapse(ec, TF_ANCHOR_END);
            pContext->SetSelection(ec, 1, &tfSelection);
        }

        // 表示属性（入力中）
        _SetCompositionDisplayAttributes(ec, pContext, _gaDisplayAttributeInput);

        pRangeComposition->Release();
    }

    tfSelection.range->Release();
    return S_OK; // eat
}

HRESULT CTextService::_HandleDeleteKey(TfEditCookie ec, ITfContext* pContext)
{
    // 要望が「末尾削除」なので Backspace と同じ挙動にします
    return _HandleBackspaceKey(ec, pContext);
}

