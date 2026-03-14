//////////////////////////////////////////////////////////////////////
//
//  THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
//  ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED
//  TO THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
//  PARTICULAR PURPOSE.
//
//  Copyright (C) 2003  Microsoft Corporation.  All rights reserved.
//
//  CandidateWindow.cpp
//
//          CCandidateWindow class
//
//////////////////////////////////////////////////////////////////////

#include "Globals.h"
#include "TextService.h"
#include "CandidateWindow.h"
#include "EditSession.h"
#include "CandidateList.h"
#define CAND_WIDTH     240
#define CAND_HEIGHT    220

ATOM CCandidateWindow::_atomWndClass = 0;

const TCHAR c_szCandidateDescription[] = TEXT("Candidate Window");

/* 3e5fdd2d-bbf6-46a9-aded-b480fe18f8d0 */
const GUID c_guidCandUIElement = {
    0x3e5fdd2d,
    0xbbf6,
    0x46a9,
    {0xad, 0xed, 0xb4, 0x80, 0xfe, 0x18, 0xf8, 0xd0}
  };

namespace
{
UINT GetEffectiveCandidatePageSize(const CTextService* textService)
{
    if (textService == nullptr)
    {
        return 9;
    }

    const int pageSize = textService->GetCandidatePageSize();
    return static_cast<UINT>(pageSize > 0 ? pageSize : 9);
}
}

//+---------------------------------------------------------------------------
//
// CGetCompositionEditSession
//
//----------------------------------------------------------------------------

class CGetCompositionEditSession : public CEditSessionBase
{
public:
    CGetCompositionEditSession(CTextService *pTextService, ITfComposition *pComposition, ITfContext *pContext) : CEditSessionBase(pTextService, pContext)
    {
        _pComposition = pComposition;
        _pComposition->AddRef();
        _szText[0] = L'\0';
    }

    ~CGetCompositionEditSession()
    {
        _pComposition->Release();
    }


    // ITfEditSession
    STDMETHODIMP DoEditSession(TfEditCookie ec);

    WCHAR *GetText()
    {
        return _szText;
    }

private:
    ITfComposition *_pComposition;
    WCHAR _szText[256];
};

class CSelectCandidateEditSession : public CEditSessionBase
{
public:
    CSelectCandidateEditSession(CTextService* pTextService, ITfContext* pContext, UINT index)
        : CEditSessionBase(pTextService, pContext)
    {
        _index = index;
    }

    STDMETHODIMP DoEditSession(TfEditCookie ec)
    {
        _pTextService->_GetCompositionState().SetSelectedCandidateIndex(static_cast<int>(_index));
        _pTextService->SetCompositionPhase(_pTextService->_GetCompositionState().GetPhase());
        return _pTextService->_UpdateCompositionText(ec, _pContext);
    }

private:
    UINT _index;
};

class CCommitCandidateEditSession : public CEditSessionBase
{
public:
    CCommitCandidateEditSession(CTextService* pTextService, ITfContext* pContext)
        : CEditSessionBase(pTextService, pContext)
    {
    }

    STDMETHODIMP DoEditSession(TfEditCookie ec)
    {
        return _pTextService->_CommitCurrentCandidate(ec, _pContext);
    }
};

//+---------------------------------------------------------------------------
//
// DoEditSession
//
//----------------------------------------------------------------------------

STDAPI CGetCompositionEditSession::DoEditSession(TfEditCookie ec)
{
    ITfRange *pRange;
    ULONG cch = 0;

    if (_pComposition->GetRange(&pRange) != S_OK)
        return E_FAIL;

    pRange->GetText(ec, 0, _szText, ARRAYSIZE(_szText) - 1, &cch);
    _szText[cch] = L'\0';

    pRange->Release();
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ctor
//
//----------------------------------------------------------------------------

CCandidateWindow::CCandidateWindow(CTextService *pTextService)
{
    _hwnd = NULL;
    _pTextService = pTextService;
    _pTextService->AddRef();

    _bInShowMode = FALSE;

    int i;
    _uCandList = 0;
    for (i = 0; i < MAX_CAND_STR; i++)
        _arCandStr[i] = NULL;

    _uPageCnt = 0;
    for (i = 0; i < MAX_CAND_STR; i++)
        _arPageIndex[i] = 0;

    _uSelection = 0;
    _dwUpdatetFlags = 0;

    _cRef = 1;
}

//+---------------------------------------------------------------------------
//
// dtor
//
//----------------------------------------------------------------------------

CCandidateWindow::~CCandidateWindow()
{
    for (UINT i = 0; i < _uCandList; i++)
    {
        delete[] _arCandStr[i];
        _arCandStr[i] = NULL;
    }

    _pTextService->Release();
}

//+---------------------------------------------------------------------------
//
// QueryInterface
//
//----------------------------------------------------------------------------

STDAPI CCandidateWindow::QueryInterface(REFIID riid, void **ppvObj)
{
    if (ppvObj == NULL)
        return E_INVALIDARG;

    *ppvObj = NULL;

    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfUIElement) ||
        IsEqualIID(riid, IID_ITfCandidateListUIElement) ||
        IsEqualIID(riid, IID_ITfCandidateListUIElementBehavior))
    {
        *ppvObj = (ITfCandidateListUIElementBehavior *)this;
    }

    if (*ppvObj)
    {
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}


//+---------------------------------------------------------------------------
//
// AddRef
//
//----------------------------------------------------------------------------

STDAPI_(ULONG) CCandidateWindow::AddRef()
{
    return ++_cRef;
}

//+---------------------------------------------------------------------------
//
// Release
//
//----------------------------------------------------------------------------

STDAPI_(ULONG) CCandidateWindow::Release()
{
    LONG cr = --_cRef;

    assert(_cRef >= 0);

    if (_cRef == 0)
    {
        delete this;
    }

    return cr;
}

//+---------------------------------------------------------------------------
//
// GetDescription
//
//----------------------------------------------------------------------------

STDAPI CCandidateWindow::GetDescription(BSTR *bstr)
{
   *bstr = SysAllocString(L"Sample Candidate Window");
   return S_OK;
}

//+---------------------------------------------------------------------------
//
// GetGUID
//
//----------------------------------------------------------------------------

STDAPI CCandidateWindow::GetGUID(GUID *pguid)
{
   *pguid = c_guidCandUIElement;
   return S_OK;
}

//+---------------------------------------------------------------------------
//
// Show
//
//----------------------------------------------------------------------------

STDAPI CCandidateWindow::Show(BOOL bShow)
{
   if (!_bInShowMode)
       return E_UNEXPECTED;

   if (bShow)
       ShowWindow(_hwnd, SW_SHOWNA);
   else
   {
       ShowWindow(_hwnd, SW_HIDE);
       _CallUpdateUIElement();
   }
   return S_OK;
}

//+---------------------------------------------------------------------------
//
// IsShown
//
//----------------------------------------------------------------------------

STDAPI CCandidateWindow::IsShown(BOOL *pbShow)
{
   *pbShow = IsWindowVisible(_hwnd);
   return S_OK;
}

//+---------------------------------------------------------------------------
//
// GetUpdatedFlags
//
//----------------------------------------------------------------------------

STDAPI CCandidateWindow::GetUpdatedFlags(DWORD *pdwFlags)
{
    *pdwFlags = _dwUpdatetFlags;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// GetDocumentMgr
//
//----------------------------------------------------------------------------

STDAPI CCandidateWindow::GetDocumentMgr(ITfDocumentMgr **ppDocumentMgr)
{
    *ppDocumentMgr = NULL;

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// GetCount
//
//----------------------------------------------------------------------------

STDAPI CCandidateWindow::GetCount(UINT *puCount)
{
    *puCount = _uCandList;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// GetGUID
//
//----------------------------------------------------------------------------

STDAPI CCandidateWindow::GetSelection(UINT *puIndex)
{
    *puIndex = _uSelection;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// GetString
//
//----------------------------------------------------------------------------

STDAPI CCandidateWindow::GetString(UINT uIndex, BSTR *pstr)
{
    *pstr = SysAllocString(_arCandStr[uIndex]);
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// GetPageIndex
//
//----------------------------------------------------------------------------

STDAPI CCandidateWindow::GetPageIndex(UINT *pIndex, UINT uSize, UINT *puPageCnt)
{
    UINT i;
    HRESULT hr = S_OK;

    if (uSize >= _uPageCnt)
        uSize = _uPageCnt;
    else
        hr = S_FALSE;

    for (i = 0; i < uSize; i++)
    {
        *pIndex = _arPageIndex[i];
        pIndex++;
    }

    *puPageCnt = _uPageCnt;
    return hr;
}

//+---------------------------------------------------------------------------
//
// SetPageIndex
//
//----------------------------------------------------------------------------

STDAPI CCandidateWindow::SetPageIndex(UINT *pIndex, UINT uPageCnt)
{
    UINT i;
    for (i = 0; i < uPageCnt; i++)
    {
        _arPageIndex[i] = *pIndex;
        pIndex++;
    }
    _uPageCnt = uPageCnt;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// GetCurrentPage
//
//----------------------------------------------------------------------------

STDAPI CCandidateWindow::GetCurrentPage(UINT *puPage)
{
    UINT i;
    if (!puPage)
        return E_INVALIDARG;

    *puPage = 0;

    if (_uPageCnt == 0)
        return E_UNEXPECTED;

    if (_uPageCnt == 1)
    {
        *puPage = 0;
        return S_OK;
    }

    for (i = 1; i < _uPageCnt; i++)
    {
        if (_arPageIndex[i] > _uSelection)
            break;
    }
  
    *puPage = i - 1;
    return S_OK;
}


//+---------------------------------------------------------------------------
//
// SetSelection
//
//----------------------------------------------------------------------------

STDAPI CCandidateWindow::SetSelection(UINT nIndex)
{
    if (nIndex >= _uCandList)
        return E_INVALIDARG;

    UINT uOldPage;
    UINT uNewPage;

    GetCurrentPage(&uOldPage);
    _uSelection = nIndex;
    GetCurrentPage(&uNewPage);

    _dwUpdatetFlags = TF_CLUIE_SELECTION;
    if (uNewPage != uOldPage)
        _dwUpdatetFlags |= TF_CLUIE_CURRENTPAGE;

    ITfRange* pRange = NULL;
    ITfContext* pContext = NULL;
    if (_pTextService->_GetComposition() != NULL &&
        _pTextService->_GetComposition()->GetRange(&pRange) == S_OK &&
        pRange != NULL &&
        pRange->GetContext(&pContext) == S_OK &&
        pContext != NULL)
    {
        CSelectCandidateEditSession* pEditSession = new CSelectCandidateEditSession(_pTextService, pContext, nIndex);
        if (pEditSession != NULL)
        {
            HRESULT hr = E_FAIL;
            pContext->RequestEditSession(_pTextService->_GetClientId(), pEditSession, TF_ES_SYNC | TF_ES_READWRITE, &hr);
            pEditSession->Release();
        }
    }

    if (pContext != NULL)
    {
        pContext->Release();
    }
    if (pRange != NULL)
    {
        pRange->Release();
    }

    _CallUpdateUIElement();
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// Finalize
//
//----------------------------------------------------------------------------

STDAPI CCandidateWindow::Finalize()
{
    ITfRange* pRange = NULL;
    ITfContext* pContext = NULL;
    if (_pTextService->_GetComposition() != NULL &&
        _pTextService->_GetComposition()->GetRange(&pRange) == S_OK &&
        pRange != NULL &&
        pRange->GetContext(&pContext) == S_OK &&
        pContext != NULL)
    {
        CCommitCandidateEditSession* pEditSession = new CCommitCandidateEditSession(_pTextService, pContext);
        if (pEditSession != NULL)
        {
            HRESULT hr = E_FAIL;
            pContext->RequestEditSession(_pTextService->_GetClientId(), pEditSession, TF_ES_SYNC | TF_ES_READWRITE, &hr);
            pEditSession->Release();
        }
    }

    if (pContext != NULL)
    {
        pContext->Release();
    }
    if (pRange != NULL)
    {
        pRange->Release();
    }

    if (_pTextService->_GetCandidateList())
    {
        if (_pTextService->_GetCompositionState().GetPhase() == CompositionPhase::CandidateSelecting)
        {
            _RefreshFromState();
        }
        else
        {
            _pTextService->_GetCandidateList()->_EndCandidateList();
        }
    }
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// Abort
//
//----------------------------------------------------------------------------

STDAPI CCandidateWindow::Abort()
{
    if (_pTextService->_GetCandidateList())
        _pTextService->_GetCandidateList()->_EndCandidateList();
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _InitWindowClass
//
//----------------------------------------------------------------------------

/* static */
BOOL CCandidateWindow::_InitWindowClass()
{
    WNDCLASS wc;

    wc.style = 0;
    wc.lpfnWndProc = CCandidateWindow::_WindowProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = g_hInst;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(LTGRAY_BRUSH);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = TEXT("TextServiceCandidateWindow");

    _atomWndClass = RegisterClass(&wc);

    return (_atomWndClass != 0);
}


//+---------------------------------------------------------------------------
//
// _UninitClass
//
//----------------------------------------------------------------------------

/* static */
void CCandidateWindow::_UninitWindowClass()
{
    if (_atomWndClass != 0)
    {
        UnregisterClass((LPCTSTR)_atomWndClass, g_hInst);
    }
}


//+---------------------------------------------------------------------------
//
// _Create
//
//----------------------------------------------------------------------------

BOOL CCandidateWindow::_Create()
{
    _hwnd = CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                           (LPCTSTR)_atomWndClass,
                           TEXT("TextService Candidate Window"),
                           WS_BORDER | WS_DISABLED | WS_POPUP,
                           0, 0,
                           CAND_WIDTH, CAND_HEIGHT,
                           NULL,
                           NULL,
                           g_hInst,
                           this);

    return (_hwnd != NULL);
}

//+---------------------------------------------------------------------------
//
// _Destroy
//
//----------------------------------------------------------------------------

void CCandidateWindow::_Destroy()
{
    if (_hwnd != NULL)
    {
        DestroyWindow(_hwnd);
        _hwnd = NULL;
    }
}

//+---------------------------------------------------------------------------
//
// _Move
//
//----------------------------------------------------------------------------

void CCandidateWindow::_Move(int x, int y)
{
    if (_hwnd != NULL)
    {
        RECT rc;
        GetWindowRect(_hwnd, &rc);
        MoveWindow(_hwnd, x, y, rc.right - rc.left, rc.bottom - rc.top, TRUE);
    }
}

//+---------------------------------------------------------------------------
//
// _GetCompositionText
//
//----------------------------------------------------------------------------

void CCandidateWindow::_GetCompositionText()
{
    ITfContext *pContext = NULL;
    ITfRange *pRange = NULL;
    CGetCompositionEditSession *pGetCompositionEditSession;
    HRESULT hr;

    if (_pTextService->_GetComposition() == NULL) {
        return;
    }

    if (_pTextService->_GetComposition()->GetRange(&pRange) != S_OK) {
        goto Exit;
    }

    if (pRange->GetContext(&pContext) != S_OK) {
        goto Exit;
    }

    if (pGetCompositionEditSession = new CGetCompositionEditSession(_pTextService, _pTextService->_GetComposition(), pContext)) {
        pContext->RequestEditSession(_pTextService->_GetClientId(), 
                                     pGetCompositionEditSession, 
                                     TF_ES_READ | TF_ES_SYNC, 
                                     &hr);

        StringCchCopy(_szText, ARRAYSIZE(_szText), pGetCompositionEditSession->GetText());
        pGetCompositionEditSession->Release();
    }

Exit:
    if (pRange)
        pRange->Release();
    if (pContext)
        pContext->Release();
}


//+---------------------------------------------------------------------------
//
// _InitList
//
//----------------------------------------------------------------------------

void CCandidateWindow::_InitList()
{
    UINT i;
    const UINT pageSize = _GetPageSize();

    for (i = 0; i < _uCandList; i++)
    {
        if (_arCandStr[i])
        {
            delete[] _arCandStr[i];
            _arCandStr[i] = NULL;
        }
    }

    const std::vector<std::wstring>& candidates = _pTextService->_GetCompositionState().GetCandidates();
    _uCandList = static_cast<UINT>(candidates.size());
    if (_uCandList > MAX_CAND_STR)
    {
        _uCandList = MAX_CAND_STR;
    }

    for (i = 0; i < _uCandList; i++)
    {
        size_t len = candidates[i].size() + 1;
        _arCandStr[i] = new WCHAR[len];
        StringCchCopy(_arCandStr[i], len, candidates[i].c_str());
    }
    for (; i < MAX_CAND_STR; i++)
    {
        _arCandStr[i] = NULL;
    }

    _uPageCnt = (_uCandList == 0) ? 0 : ((_uCandList - 1) / pageSize) + 1;
    for (i = 0; i < _uPageCnt; i++)
    {
        _arPageIndex[i] = i * pageSize;
    }
    for (; i < MAX_CAND_STR; i++)
    {
        _arPageIndex[i] = 0;
    }

    int selected = _pTextService->_GetCompositionState().GetSelectedCandidateIndex();
    _uSelection = (selected >= 0 && selected < static_cast<int>(_uCandList)) ? static_cast<UINT>(selected) : 0;

    _dwUpdatetFlags = TF_CLUIE_DOCUMENTMGR |
        TF_CLUIE_COUNT |
        TF_CLUIE_SELECTION |
        TF_CLUIE_STRING |
        TF_CLUIE_PAGEINDEX |
        TF_CLUIE_CURRENTPAGE;
}

UINT CCandidateWindow::_GetPageSize() const
{
    return GetEffectiveCandidatePageSize(_pTextService);
}

//+---------------------------------------------------------------------------
//
// _Begin
//
//----------------------------------------------------------------------------

void CCandidateWindow::_Begin()
{
    ITfUIElementMgr *pUIElementMgr;
    BOOL bShow = TRUE;

    _InitList();

    if (!_bInShowMode)
    {
        if (SUCCEEDED(_pTextService->_GetThreadMgr()->QueryInterface(IID_ITfUIElementMgr,
                                                         (void **)&pUIElementMgr)))
        {
            pUIElementMgr->BeginUIElement(this, &bShow, &_dwUIElementId);
            if (!bShow)
                pUIElementMgr->UpdateUIElement(_dwUIElementId);
            pUIElementMgr->Release();
        }
    }


    if (bShow)
        ShowWindow(_hwnd, SW_SHOWNA);

    _bInShowMode = TRUE;
}

void CCandidateWindow::_RefreshFromState()
{
    _InitList();
    _CallUpdateUIElement();
    if (_hwnd != NULL)
    {
        InvalidateRect(_hwnd, NULL, TRUE);
    }
}

//+---------------------------------------------------------------------------
//
// _End
//
//----------------------------------------------------------------------------

void CCandidateWindow::_End()
{
    if (_bInShowMode)
    {
        ITfUIElementMgr *pUIElementMgr;
        if (SUCCEEDED(_pTextService->_GetThreadMgr()->QueryInterface(IID_ITfUIElementMgr, 
                                                         (void **)&pUIElementMgr)))
        {
            pUIElementMgr->EndUIElement(_dwUIElementId);
            pUIElementMgr->Release();
        }
    }
    ShowWindow(_hwnd, SW_HIDE);
    _bInShowMode = FALSE;
}

//+---------------------------------------------------------------------------
//
// _CallUpdateUIElement
//
//----------------------------------------------------------------------------

void CCandidateWindow::_CallUpdateUIElement()
{
    //
    // we don't have to call UpdateUIElement when we show our own UI.
    //
    if (_bInShowMode && !IsWindowVisible(_hwnd))
    {
        ITfUIElementMgr *pUIElementMgr;
        if (SUCCEEDED(_pTextService->_GetThreadMgr()->QueryInterface(IID_ITfUIElementMgr, 
                                                         (void **)&pUIElementMgr)))
        {
            pUIElementMgr->UpdateUIElement(_dwUIElementId);
            pUIElementMgr->Release();
        }
    }
}

//+---------------------------------------------------------------------------
//
// _Next
//
//----------------------------------------------------------------------------

void CCandidateWindow::_Next()
{
    UINT uOldPage;
    UINT uNewPage;

    GetCurrentPage(&uOldPage);
    _uSelection++;
    if (_uSelection >= _uCandList)
        _uSelection = 0;
    GetCurrentPage(&uNewPage);

    _dwUpdatetFlags = TF_CLUIE_SELECTION;
    if (uNewPage != uOldPage)
        _dwUpdatetFlags |= TF_CLUIE_CURRENTPAGE;

    _CallUpdateUIElement();
}

//+---------------------------------------------------------------------------
//
// _Prev
//
//----------------------------------------------------------------------------

void CCandidateWindow::_Prev()
{
    UINT uOldPage;
    UINT uNewPage;

    GetCurrentPage(&uOldPage);
    if (_uSelection > 0)
        _uSelection--;
    else
        _uSelection = _uCandList - 1;

    GetCurrentPage(&uNewPage);

    _dwUpdatetFlags = TF_CLUIE_SELECTION;
    if (uNewPage != uOldPage)
        _dwUpdatetFlags |= TF_CLUIE_CURRENTPAGE;

    _CallUpdateUIElement();
}

//+---------------------------------------------------------------------------
//
// _NextPage
//
//----------------------------------------------------------------------------

void CCandidateWindow::_NextPage()
{
    UINT uOldPage;
    UINT uNewPage;

    GetCurrentPage(&uOldPage);
    uNewPage = uOldPage + 1;
    if (uNewPage >= _uPageCnt)
        uNewPage = 0;

    _uSelection = _arPageIndex[uNewPage];

    _dwUpdatetFlags = TF_CLUIE_SELECTION;
    if (uNewPage != uOldPage)
        _dwUpdatetFlags |= TF_CLUIE_CURRENTPAGE;

    _CallUpdateUIElement();
}

//+---------------------------------------------------------------------------
//
// _PrevPage
//
//----------------------------------------------------------------------------

void CCandidateWindow::_PrevPage()
{
    UINT uOldPage;
    UINT uNewPage;

    GetCurrentPage(&uOldPage);
    if (uOldPage > 0)
        uNewPage = uOldPage - 1;
    else
        uNewPage = _uPageCnt - 1;

    _uSelection = _arPageIndex[uNewPage];

    _dwUpdatetFlags = TF_CLUIE_SELECTION;
    if (uNewPage != uOldPage)
        _dwUpdatetFlags |= TF_CLUIE_CURRENTPAGE;

    _CallUpdateUIElement();
}
//+---------------------------------------------------------------------------
//
// _OnKeyDown
//
//----------------------------------------------------------------------------

HRESULT CCandidateWindow::_OnKeyDown(UINT uVKey)
{
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _OnKeyUp
//
//----------------------------------------------------------------------------

HRESULT CCandidateWindow::_OnKeyUp(UINT uVKey)
{
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _WindowProc
//
// Cand window proc.
//----------------------------------------------------------------------------

/* static */
LRESULT CALLBACK CCandidateWindow::_WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    HDC hdc;
    PAINTSTRUCT ps;

    switch (uMsg)
    {
        case WM_CREATE:
            _SetThis(hwnd, lParam);
            return 0;

        case WM_PAINT:
            hdc = BeginPaint(hwnd, &ps);
            SetBkMode(hdc, TRANSPARENT);
            {
                CCandidateWindow* pThis = _GetThis(hwnd);
                if (pThis == NULL || pThis->_uCandList == 0)
                {
                    TextOut(hdc, 8, 8, c_szCandidateDescription, lstrlen(c_szCandidateDescription));
                }
                else
                {
                    RECT rcClient;
                    GetClientRect(hwnd, &rcClient);
                    FillRect(hdc, &rcClient, (HBRUSH)GetStockObject(WHITE_BRUSH));

                    const int itemHeight = 20;
                    UINT currentPage = 0;
                    const UINT pageSize = pThis->_GetPageSize();
                    pThis->GetCurrentPage(&currentPage);
                    const UINT pageStart = pThis->_arPageIndex[currentPage];
                    const UINT pageEnd = min(pageStart + pageSize, pThis->_uCandList);

                    for (UINT i = pageStart; i < pageEnd; i++)
                    {
                        const UINT row = i - pageStart;
                        RECT rcItem = {
                            4,
                            4 + static_cast<int>(row) * itemHeight,
                            rcClient.right - 4,
                            4 + static_cast<int>(row + 1) * itemHeight };
                        if (i == pThis->_uSelection)
                        {
                            HBRUSH hBrush = CreateSolidBrush(RGB(220, 235, 255));
                            FillRect(hdc, &rcItem, hBrush);
                            DeleteObject(hBrush);
                        }

                        WCHAR szLine[256];
                        StringCchPrintf(szLine, ARRAYSIZE(szLine), L"%d. %s", i + 1, pThis->_arCandStr[i] ? pThis->_arCandStr[i] : L"");
                        TextOut(hdc, rcItem.left + 4, rcItem.top + 2, szLine, lstrlen(szLine));
                    }

                    WCHAR szPage[64];
                    StringCchPrintf(
                        szPage,
                        ARRAYSIZE(szPage),
                        L"Page %u/%u",
                        currentPage + 1,
                        max(1u, pThis->_uPageCnt));
                    TextOut(hdc, 8, rcClient.bottom - itemHeight, szPage, lstrlen(szPage));
                }
            }
            EndPaint(hwnd, &ps);
            return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

