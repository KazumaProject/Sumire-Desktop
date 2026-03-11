//////////////////////////////////////////////////////////////////////
//
//  KeyEditHandlers.cpp
//
//  Backspace/Delete handling to keep ComposingText and TSF composition range
//  in sync.
//
//////////////////////////////////////////////////////////////////////

#include "Globals.h"
#include "TextService.h"

#include <string>

//+---------------------------------------------------------------------------
//
// IsRangeCovered
//
// Returns TRUE if pRangeTest is entirely contained within pRangeCover.
//
//----------------------------------------------------------------------------

static BOOL IsRangeCovered(TfEditCookie ec, ITfRange* pRangeTest, ITfRange* pRangeCover)
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
// GetRangeText
//
// Read all text in range. (Uses MOVESTART, so clone before reading.)
//
//----------------------------------------------------------------------------

static std::wstring GetRangeText(TfEditCookie ec, ITfRange* pRange)
{
    if (!pRange)
    {
        return L"";
    }

    ITfRange* pClone = nullptr;
    if (pRange->Clone(&pClone) != S_OK || !pClone)
    {
        return L"";
    }

    std::wstring result;

    WCHAR buf[256];
    ULONG cchOut = 0;

    for (;;)
    {
        cchOut = 0;
        HRESULT hr = pClone->GetText(
            ec,
            TF_TF_MOVESTART,
            buf,
            (ULONG)(sizeof(buf) / sizeof(buf[0])),
            &cchOut);

        if (FAILED(hr) || cchOut == 0)
        {
            break;
        }

        result.append(buf, buf + cchOut);
    }

    pClone->Release();
    return result;
}

//+---------------------------------------------------------------------------
//
// GetSurfaceCursorFromSelection
//
// Compute surface cursor = length of (composition start .. caret).
//
// NOTE:
// ITfRange has no SetEndPoint(). Use ShiftEndToRange().
//
//----------------------------------------------------------------------------

static LONG GetSurfaceCursorFromSelection(TfEditCookie ec, ITfContext* pContext, ITfRange* pCompositionRange)
{
    if (!pContext || !pCompositionRange)
    {
        return 0;
    }

    TF_SELECTION tfSelection;
    ULONG cFetched = 0;

    if (pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &tfSelection, &cFetched) != S_OK || cFetched != 1)
    {
        return 0;
    }

    LONG surfaceCursor = 0;

    // composition の start..caret の range を作る
    ITfRange* pPrefix = nullptr;
    if (pCompositionRange->Clone(&pPrefix) == S_OK && pPrefix)
    {
        // start に潰す（start=end=composition start）
        pPrefix->Collapse(ec, TF_ANCHOR_START);

        // end を caret(start) に合わせる
        // ITfRange::ShiftEndToRange(ec, targetRange, targetAnchor)
        // -> pPrefix の end が tfSelection.range の start に移動する
        if (pPrefix->ShiftEndToRange(ec, tfSelection.range, TF_ANCHOR_START) == S_OK)
        {
            std::wstring prefixText = GetRangeText(ec, pPrefix);
            surfaceCursor = (LONG)prefixText.size();
        }

        pPrefix->Release();
    }

    tfSelection.range->Release();
    return surfaceCursor;
}

//+---------------------------------------------------------------------------
//
// SetCaretInComposition
//
// Move caret to composition start + surfaceCursor.
// (Uses ShiftStart on a cloned range.)
//
//----------------------------------------------------------------------------

static void SetCaretInComposition(TfEditCookie ec, ITfContext* pContext, ITfRange* pCompositionRange, LONG surfaceCursor)
{
    if (!pContext || !pCompositionRange)
    {
        return;
    }

    ITfRange* pCaret = nullptr;
    if (pCompositionRange->Clone(&pCaret) != S_OK || !pCaret)
    {
        return;
    }

    // start に collapse
    pCaret->Collapse(ec, TF_ANCHOR_START);

    // start を surfaceCursor だけ進める
    LONG moved = 0;
    pCaret->ShiftStart(ec, surfaceCursor, &moved, NULL);

    // insertion point にする
    pCaret->Collapse(ec, TF_ANCHOR_START);

    TF_SELECTION sel;
    sel.range = pCaret;
    sel.style.ase = TF_AE_NONE;
    sel.style.fInterimChar = FALSE;

    pContext->SetSelection(ec, 1, &sel);

    pCaret->Release();
}

//+---------------------------------------------------------------------------
//
// MapSurfaceCursorToRawCursor
//
// Approx map surface cursor -> raw cursor by converting prefixes.
//
//----------------------------------------------------------------------------

static LONG MapSurfaceCursorToRawCursor(const RomajiKanaConverter& converter,
    const std::wstring& rawText,
    LONG surfaceCursor)
{
    if (surfaceCursor <= 0)
    {
        return 0;
    }

    if (rawText.empty())
    {
        return 0;
    }

    LONG best = (LONG)rawText.size();

    // i 文字までの raw を変換して、その長さが surfaceCursor 以上になった最小 i を採用
    for (LONG i = 0; i <= (LONG)rawText.size(); i++)
    {
        std::wstring rawPrefix = rawText.substr(0, i);
        std::wstring surfPrefix = converter.ConvertFromRaw(rawPrefix);

        if ((LONG)surfPrefix.size() >= surfaceCursor)
        {
            best = i;
            break;
        }
    }

    if (best < 0) best = 0;
    if (best > (LONG)rawText.size()) best = (LONG)rawText.size();
    return best;
}

//+---------------------------------------------------------------------------
//
// _HandleBackspaceKey
//
//----------------------------------------------------------------------------

HRESULT CTextService::_HandleBackspaceKey(TfEditCookie ec, ITfContext* pContext)
{
    if (!pContext)
    {
        return E_INVALIDARG;
    }

    if (!_IsComposing())
    {
        return S_OK;
    }

    ITfRange* pRangeComposition = nullptr;
    if (_pComposition == nullptr || _pComposition->GetRange(&pRangeComposition) != S_OK || !pRangeComposition)
    {
        return S_OK;
    }

    // selection が composition 内か確認
    TF_SELECTION tfSelection;
    ULONG cFetched = 0;

    if (pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &tfSelection, &cFetched) != S_OK || cFetched != 1)
    {
        pRangeComposition->Release();
        return S_OK;
    }

    BOOL covered = IsRangeCovered(ec, tfSelection.range, pRangeComposition);
    tfSelection.range->Release();

    if (!covered)
    {
        pRangeComposition->Release();
        return S_OK;
    }

    // caret(surface) -> raw cursor にマップ
    LONG surfaceCursor = GetSurfaceCursorFromSelection(ec, pContext, pRangeComposition);
    LONG rawCursor = MapSurfaceCursorToRawCursor(_romajiConverter, _composingText.GetRawText(), surfaceCursor);
    _composingText.SetRawCursor(rawCursor);

    // Backspace: raw のカーソル前を 1 文字削除
    if (!_composingText.DeleteRawBeforeCursor())
    {
        pRangeComposition->Release();
        return S_OK;
    }

    // raw -> surface 再生成
    _composingText.UpdateSurfaceFromRaw(_romajiConverter);
    _composingText.ClearLiveConversionText();

    const std::wstring& newText = _composingText.GetCurrentText();

    if (newText.empty())
    {
        // 空なら composition 終了
        pRangeComposition->SetText(ec, 0, L"", 0);
        pRangeComposition->Release();

        _composingText.Reset();
        _TerminateComposition(ec, pContext);
        return S_OK;
    }

    // composition 範囲を更新
    pRangeComposition->SetText(ec, 0, newText.c_str(), (ULONG)newText.size());

    // 表示属性
    _SetCompositionDisplayAttributes(ec, pContext, _gaDisplayAttributeInput);

    // caret を再設定
    LONG newSurfaceCursor = _composingText.GetSurfaceCursor();
    if (newSurfaceCursor < 0) newSurfaceCursor = 0;
    if (newSurfaceCursor > (LONG)newText.size()) newSurfaceCursor = (LONG)newText.size();

    SetCaretInComposition(ec, pContext, pRangeComposition, newSurfaceCursor);

    pRangeComposition->Release();
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleDeleteKey
//
//----------------------------------------------------------------------------

HRESULT CTextService::_HandleDeleteKey(TfEditCookie ec, ITfContext* pContext)
{
    if (!pContext)
    {
        return E_INVALIDARG;
    }

    if (!_IsComposing())
    {
        return S_OK;
    }

    ITfRange* pRangeComposition = nullptr;
    if (_pComposition == nullptr || _pComposition->GetRange(&pRangeComposition) != S_OK || !pRangeComposition)
    {
        return S_OK;
    }

    // selection が composition 内か確認
    TF_SELECTION tfSelection;
    ULONG cFetched = 0;

    if (pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &tfSelection, &cFetched) != S_OK || cFetched != 1)
    {
        pRangeComposition->Release();
        return S_OK;
    }

    BOOL covered = IsRangeCovered(ec, tfSelection.range, pRangeComposition);
    tfSelection.range->Release();

    if (!covered)
    {
        pRangeComposition->Release();
        return S_OK;
    }

    // caret(surface) -> raw cursor にマップ
    LONG surfaceCursor = GetSurfaceCursorFromSelection(ec, pContext, pRangeComposition);
    LONG rawCursor = MapSurfaceCursorToRawCursor(_romajiConverter, _composingText.GetRawText(), surfaceCursor);
    _composingText.SetRawCursor(rawCursor);

    // Delete: raw のカーソル位置を 1 文字削除
    if (!_composingText.DeleteRawAtCursor())
    {
        pRangeComposition->Release();
        return S_OK;
    }

    // raw -> surface 再生成
    _composingText.UpdateSurfaceFromRaw(_romajiConverter);
    _composingText.ClearLiveConversionText();

    const std::wstring& newText = _composingText.GetCurrentText();

    if (newText.empty())
    {
        // 空なら composition 終了
        pRangeComposition->SetText(ec, 0, L"", 0);
        pRangeComposition->Release();

        _composingText.Reset();
        _TerminateComposition(ec, pContext);
        return S_OK;
    }

    // composition 範囲を更新
    pRangeComposition->SetText(ec, 0, newText.c_str(), (ULONG)newText.size());

    // 表示属性
    _SetCompositionDisplayAttributes(ec, pContext, _gaDisplayAttributeInput);

    // caret を再設定（Delete はカーソル位置維持）
    LONG newSurfaceCursor = _composingText.GetSurfaceCursor();
    if (newSurfaceCursor < 0) newSurfaceCursor = 0;
    if (newSurfaceCursor > (LONG)newText.size()) newSurfaceCursor = (LONG)newText.size();

    SetCaretInComposition(ec, pContext, pRangeComposition, newSurfaceCursor);

    pRangeComposition->Release();
    return S_OK;
}
