//////////////////////////////////////////////////////////////////////
//
//  THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
//  ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED
//  TO THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
//  PARTICULAR PURPOSE.
//
//  TextService.cpp
//
//          IUnknown, ITfTextInputProcessor implementation.
//
//////////////////////////////////////////////////////////////////////

#include "Globals.h"
#include "TextService.h"
#include "CandidateList.h"

//+---------------------------------------------------------------------------
//
// CreateInstance
//
//----------------------------------------------------------------------------

/* static */
HRESULT CTextService::CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObj)
{
    CTextService* pCase;
    HRESULT hr;

    if (ppvObj == NULL)
        return E_INVALIDARG;

    *ppvObj = NULL;

    if (NULL != pUnkOuter)
        return CLASS_E_NOAGGREGATION;

    if ((pCase = new CTextService) == NULL)
        return E_OUTOFMEMORY;

    hr = pCase->QueryInterface(riid, ppvObj);

    pCase->Release(); // caller still holds ref if hr == S_OK

    return hr;
}

//+---------------------------------------------------------------------------
//
// ctor
//
//----------------------------------------------------------------------------

CTextService::CTextService()
{
    DllAddRef();

    //
    // Initialize the thread manager pointer.
    //
    _pThreadMgr = NULL;

    //
    // Initialize the numbers for ThreadMgrEventSink.
    //
    _dwThreadMgrEventSinkCookie = TF_INVALID_COOKIE;

    //
    // Initialize the numbers for TextEditSink.
    //
    _pTextEditSinkContext = NULL;
    _dwTextEditSinkCookie = TF_INVALID_COOKIE;

    //
    // Initialize the LanguageBar item pointers.
    //
    _pLangBarItemBrand = NULL;
    _pLangBarItemMode = NULL;

    //
    // Initialize the composition object pointer.
    //
    _pComposition = NULL;

    //
    // Initialize the candidate list object pointer.
    //
    _pCandidateList = NULL;

    //
    // Initialize display attribute guid atoms.
    //
    _gaDisplayAttributeInput = TF_INVALID_GUIDATOM;
    _gaDisplayAttributeConverted = TF_INVALID_GUIDATOM;

    //
    // Initialize composing text / romaji converter.
    //
    // （特に何もする必要がなければデフォルトコンストラクタに任せる）

    //
    // 入力モードを既定値（ひらがな）に初期化
    //
    _inputMode = INPUTMODE_HIRAGANA;

    _tfClientId = TF_CLIENTID_NULL;

    _cRef = 1;
}

//+---------------------------------------------------------------------------
//
// dtor
//
//----------------------------------------------------------------------------

CTextService::~CTextService()
{
    DllRelease();

    _pThreadMgr = NULL;
    _dwThreadMgrEventSinkCookie = TF_INVALID_COOKIE;
    _pTextEditSinkContext = NULL;
    _dwTextEditSinkCookie = TF_INVALID_COOKIE;

    // LangBar item ポインタ初期化
    _pLangBarItemBrand = NULL;
    _pLangBarItemMode = NULL;

    _pComposition = NULL;
    _pCandidateList = NULL;

    _gaDisplayAttributeInput = TF_INVALID_GUIDATOM;
    _gaDisplayAttributeConverted = TF_INVALID_GUIDATOM;

    _inputMode = INPUTMODE_HIRAGANA;
    _tfClientId = TF_CLIENTID_NULL;

    _cRef = 1;
}

//+---------------------------------------------------------------------------
//
// QueryInterface
//
//----------------------------------------------------------------------------

STDAPI CTextService::QueryInterface(REFIID riid, void** ppvObj)
{
    if (ppvObj == NULL)
        return E_INVALIDARG;

    *ppvObj = NULL;

    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfTextInputProcessor) ||
        IsEqualIID(riid, IID_ITfTextInputProcessorEx))
    {
        *ppvObj = (ITfTextInputProcessorEx*)this;
    }
    else if (IsEqualIID(riid, IID_ITfThreadMgrEventSink))
    {
        *ppvObj = (ITfThreadMgrEventSink*)this;
    }
    else if (IsEqualIID(riid, IID_ITfTextEditSink))
    {
        *ppvObj = (ITfTextEditSink*)this;
    }
    else if (IsEqualIID(riid, IID_ITfKeyEventSink))
    {
        *ppvObj = (ITfKeyEventSink*)this;
    }
    else if (IsEqualIID(riid, IID_ITfCompositionSink))
    {
        *ppvObj = (ITfCompositionSink*)this;
    }
    else if (IsEqualIID(riid, IID_ITfDisplayAttributeProvider))
    {
        *ppvObj = (ITfDisplayAttributeProvider*)this;
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

STDAPI_(ULONG) CTextService::AddRef()
{
    return ++_cRef;
}

//+---------------------------------------------------------------------------
//
// Release
//
//----------------------------------------------------------------------------

STDAPI_(ULONG) CTextService::Release()
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
// Activate
//
//----------------------------------------------------------------------------

STDAPI CTextService::Activate(ITfThreadMgr* pThreadMgr, TfClientId tfClientId)
{
    return ActivateEx(pThreadMgr, tfClientId, 0);
}

//+---------------------------------------------------------------------------
//
// ActivateEx
//
//----------------------------------------------------------------------------

STDAPI CTextService::ActivateEx(ITfThreadMgr* pThreadMgr, TfClientId tfClientId, DWORD dwFlags)
{
    UNREFERENCED_PARAMETER(dwFlags);

    _pThreadMgr = pThreadMgr;
    _pThreadMgr->AddRef();
    _tfClientId = tfClientId;

    //
    // Initialize ThreadMgrEventSink.
    //
    if (!_InitThreadMgrEventSink())
        goto ExitError;

    //
    //  If there is the focus document manager already,
    //  we advise the TextEditSink.
    //
    ITfDocumentMgr* pDocMgrFocus;
    if ((_pThreadMgr->GetFocus(&pDocMgrFocus) == S_OK) &&
        (pDocMgrFocus != NULL))
    {
        _InitTextEditSink(pDocMgrFocus);
        pDocMgrFocus->Release();
    }

    //
    // Initialize Language Bar.
    //
    if (!_InitLanguageBar())
        goto ExitError;

    //
    // Initialize KeyEventSink
    //
    if (!_InitKeyEventSink())
        goto ExitError;

    //
    // Initialize PreservedKeys
    //
    if (!_InitPreservedKey())
        goto ExitError;

    //
    // Initialize display guid atom
    //
    if (!_InitDisplayAttributeGuidAtom())
        goto ExitError;

    return S_OK;

ExitError:
    Deactivate(); // cleanup any half-finished init
    return E_FAIL;
}

//+---------------------------------------------------------------------------
//
// Deactivate
//
//----------------------------------------------------------------------------

STDAPI CTextService::Deactivate()
{
    // delete the candidate list object if it exists.
    if (_pCandidateList != NULL)
    {
        delete _pCandidateList;
        _pCandidateList = NULL;
    }

    //
    // Unadvise TextEditSink if it is advised.
    //
    _InitTextEditSink(NULL);

    //
    // Uninitialize ThreadMgrEventSink.
    //
    _UninitThreadMgrEventSink();

    //
    // Uninitialize Language Bar.
    //
    _UninitLanguageBar();

    //
    // Uninitialize KeyEventSink
    //
    _UninitKeyEventSink();

    //
    // Uninitialize PreservedKeys
    //
    _UninitPreservedKey();

    // we MUST release all refs to _pThreadMgr in Deactivate
    if (_pThreadMgr != NULL)
    {
        _pThreadMgr->Release();
        _pThreadMgr = NULL;
    }

    _tfClientId = TF_CLIENTID_NULL;

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// SetInputMode
//
//----------------------------------------------------------------------------

void CTextService::SetInputMode(InputMode mode)
{
    if (_inputMode == mode)
    {
        return;
    }

    _inputMode = mode;

    //
    // 必要に応じてここでコンポジション再描画や
    // 読みロジックのリセットを行うこともできる。
    // 現時点ではモードフラグの更新のみ。
    //
}

//+---------------------------------------------------------------------------
//
// CTextService::_SetCompartment
//
//   GUID_COMPARTMENT_* に値を書き込むヘルパー。
//   LanguageBar から「ひらがな / 英数」「キーボード ON/OFF」の状態を
//   OS 側に通知するために使う。
//
//----------------------------------------------------------------------------

HRESULT CTextService::_SetCompartment(REFGUID rguid, const VARIANT* pvar)
{
    if (_pThreadMgr == NULL)
    {
        return E_FAIL;
    }

    ITfCompartmentMgr* pCompMgr = NULL;
    ITfCompartment* pComp = NULL;

    HRESULT hr = _pThreadMgr->QueryInterface(
        IID_ITfCompartmentMgr,
        (void**)&pCompMgr);
    if (FAILED(hr) || !pCompMgr)
    {
        goto Exit;
    }

    hr = pCompMgr->GetCompartment(rguid, &pComp);
    if (FAILED(hr) || !pComp)
    {
        goto Exit;
    }

    // _tfClientId は Activate / ActivateEx で保存したクライアント ID
    hr = pComp->SetValue(_tfClientId, pvar);

Exit:
    if (pComp)
    {
        pComp->Release();
    }
    if (pCompMgr)
    {
        pCompMgr->Release();
    }

    return hr;
}
