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
#include "EditSession.h"
#include "LanguageBar.h"

extern const GUID GUID_LBI_INPUTMODE;

class CInputScopeUpdateEditSession : public CEditSessionBase
{
public:
    CInputScopeUpdateEditSession(CTextService* pTextService, ITfContext* pContext)
        : CEditSessionBase(pTextService, pContext)
    {
    }

    STDMETHODIMP DoEditSession(TfEditCookie ec)
    {
        _pTextService->_ApplyInputScopeOverride(_pContext, ec);
        return S_OK;
    }
};

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
    _dwThreadFocusSinkCookie = TF_INVALID_COOKIE;
    _dwCompartmentEventSinkOpenCloseCookie = TF_INVALID_COOKIE;
    _dwCompartmentEventSinkInputmodeConversionCookie = TF_INVALID_COOKIE;

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
    _gaDisplayAttributeFocusedConverted = TF_INVALID_GUIDATOM;

    //
    // Initialize composing text / romaji converter.
    //
    // （特に何もする必要がなければデフォルトコンストラクタに任せる）

    //
    // composition セッション段階を既定値に初期化
    //
    _compositionPhase = CompositionPhase::Idle;
    _pendingInternalEdits = 0;

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
    _dwThreadFocusSinkCookie = TF_INVALID_COOKIE;
    _dwCompartmentEventSinkOpenCloseCookie = TF_INVALID_COOKIE;
    _dwCompartmentEventSinkInputmodeConversionCookie = TF_INVALID_COOKIE;
    _pTextEditSinkContext = NULL;
    _dwTextEditSinkCookie = TF_INVALID_COOKIE;

    // LangBar item ポインタ初期化
    _pLangBarItemBrand = NULL;
    _pLangBarItemMode = NULL;

    _pComposition = NULL;
    _pCandidateList = NULL;

    _gaDisplayAttributeInput = TF_INVALID_GUIDATOM;
    _gaDisplayAttributeConverted = TF_INVALID_GUIDATOM;
    _gaDisplayAttributeFocusedConverted = TF_INVALID_GUIDATOM;

    _compositionPhase = CompositionPhase::Idle;
    _pendingInternalEdits = 0;
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
    else if (IsEqualIID(riid, IID_ITfThreadFocusSink))
    {
        *ppvObj = (ITfThreadFocusSink*)this;
    }
    else if (IsEqualIID(riid, IID_ITfCompartmentEventSink))
    {
        *ppvObj = (ITfCompartmentEventSink*)this;
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

    DebugLog(L"[ActivateEx] start pThreadMgr=0x%p tfClientId=0x%08X\r\n", pThreadMgr, tfClientId);

    _pThreadMgr = pThreadMgr;
    _pThreadMgr->AddRef();
    _tfClientId = tfClientId;

    //
    // Initialize ThreadMgrEventSink.
    //
    BOOL fThreadMgrEventSink = _InitThreadMgrEventSink();
    DebugLogBool(L"[ActivateEx] _InitThreadMgrEventSink", fThreadMgrEventSink);
    if (!fThreadMgrEventSink)
        goto ExitError;

    BOOL fThreadFocusSink = _InitThreadFocusSink();
    DebugLogBool(L"[ActivateEx] _InitThreadFocusSink", fThreadFocusSink);
    if (!fThreadFocusSink)
        goto ExitError;

    BOOL fCompartmentEventSink = _InitCompartmentEventSink();
    DebugLogBool(L"[ActivateEx] _InitCompartmentEventSink", fCompartmentEventSink);
    if (!fCompartmentEventSink)
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
    BOOL fLanguageBar = _InitLanguageBar();
    DebugLogBool(L"[ActivateEx] _InitLanguageBar", fLanguageBar);
    if (!fLanguageBar)
        goto ExitError;

    //
    // Initialize KeyEventSink
    //
    BOOL fKeyEventSink = _InitKeyEventSink();
    DebugLogBool(L"[ActivateEx] _InitKeyEventSink", fKeyEventSink);
    if (!fKeyEventSink)
        goto ExitError;

    //
    // Initialize PreservedKeys
    //
    BOOL fPreservedKey = _InitPreservedKey();
    DebugLogBool(L"[ActivateEx] _InitPreservedKey", fPreservedKey);
    if (!fPreservedKey)
        goto ExitError;

    //
    // Initialize display guid atom
    //
    if (!_InitDisplayAttributeGuidAtom())
        goto ExitError;

    DebugLog(L"[ActivateEx] success\r\n");
    return S_OK;

ExitError:
    DebugLog(L"[ActivateEx] ExitError\r\n");
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
    _UninitCompartmentEventSink();
    _UninitThreadFocusSink();
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

void CTextService::SetUserInputMode(InputMode mode)
{
    InputMode oldMode = _inputModeState.GetUserInputMode();
    if (oldMode == mode)
    {
        DebugLog(L"[SetUserInputMode] old=%d new=%d no-op\r\n",
            static_cast<int>(oldMode), static_cast<int>(mode));
        return;
    }

    _inputModeState.SetUserInputMode(mode);

    DebugLog(L"[SetUserInputMode] old=%d new=%d effective=%d\r\n",
        static_cast<int>(oldMode),
        static_cast<int>(mode),
        static_cast<int>(_inputModeState.GetEffectiveInputMode()));
    _UpdateLanguageBar();
}

InputMode CTextService::GetUserInputMode() const
{
    return _inputModeState.GetUserInputMode();
}

InputMode CTextService::GetEffectiveInputMode() const
{
    return _inputModeState.GetEffectiveInputMode();
}

BOOL CTextService::HasInputScopeOverride() const
{
    return _inputModeState.HasInputScopeOverride() ? TRUE : FALSE;
}

void CTextService::SetInputScopeOverride(InputMode mode)
{
    _inputModeState.SetInputScopeOverride(mode);
    _UpdateLanguageBar();
}

void CTextService::ClearInputScopeOverride()
{
    if (_inputModeState.HasInputScopeOverride())
    {
        _inputModeState.ClearInputScopeOverride();
        _UpdateLanguageBar();
    }
}

void CTextService::SetCompositionPhase(CompositionPhase phase)
{
    _compositionPhase = phase;
}

CompositionPhase CTextService::GetCompositionPhase() const
{
    return _compositionPhase;
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

//+---------------------------------------------------------------------------
//
//  _InitLanguageBar
//
//----------------------------------------------------------------------------
BOOL CTextService::_InitLanguageBar()
{
    ITfLangBarItemMgr* pLangBarItemMgr;
    BOOL fBrandAdded = FALSE;
    HRESULT hr = S_OK;

    DebugLog(L"[LanguageBar] _InitLanguageBar start\r\n");

    if (_pThreadMgr == NULL)
    {
        DebugLog(L"[LanguageBar] _InitLanguageBar threadMgr=null\r\n");
        return FALSE;
    }

    hr = _pThreadMgr->QueryInterface(IID_ITfLangBarItemMgr,
        (void**)&pLangBarItemMgr);
    DebugLogHr(L"[LanguageBar] QueryInterface(ITfLangBarItemMgr)", hr);
    if (hr != S_OK)
    {
        return FALSE;
    }

    // ブランドアイコン（独自 GUID）
    _pLangBarItemBrand = new CLangBarItemButton(this, c_guidLangBarItemButton);
    DebugLog(L"[LanguageBar] brand item %s\r\n", _pLangBarItemBrand != NULL ? L"created" : L"null");
    if (_pLangBarItemBrand != NULL)
    {
        hr = pLangBarItemMgr->AddItem(_pLangBarItemBrand);
        DebugLogHr(L"[LanguageBar] brand AddItem", hr);
        if (hr != S_OK)
        {
            _pLangBarItemBrand->Release();
            _pLangBarItemBrand = NULL;
        }
        else
        {
            fBrandAdded = TRUE;
        }
    }

    // モードアイコンは OS 標準の入力モード GUID で登録する
    _pLangBarItemMode = new CLangBarItemButton(this, GUID_LBI_INPUTMODE);
    DebugLog(L"[LanguageBar] mode item %s\r\n", _pLangBarItemMode != NULL ? L"created" : L"null");
    DebugLogGuid(L"[LanguageBar] mode item registration", GUID_LBI_INPUTMODE);
    if (_pLangBarItemMode != NULL)
    {
        hr = pLangBarItemMgr->AddItem(_pLangBarItemMode);
        DebugLogHr(L"[LanguageBar] mode AddItem", hr);
        if (hr != S_OK)
        {
            _pLangBarItemMode->Release();
            _pLangBarItemMode = NULL;
        }
    }

    pLangBarItemMgr->Release();

    // 初期状態はひらがな / キーボード ON に寄せる。
    SetUserInputMode(InputMode::Hiragana);
    _SetKeyboardOpen(TRUE);

    // 初期状態（ひらがな / キーボード ON）を TSF に通知
    _UpdateLanguageBar();

    DebugLogBool(L"[LanguageBar] _InitLanguageBar brandAdded", fBrandAdded);
    DebugLogBool(L"[LanguageBar] _InitLanguageBar return", TRUE);
    return TRUE;
}


//+---------------------------------------------------------------------------
//
//  _UninitLanguageBar
//
//----------------------------------------------------------------------------

void CTextService::_UninitLanguageBar()
{
    ITfLangBarItemMgr* pLangBarItemMgr;

    if (_pThreadMgr == NULL)
        return;

    if (_pThreadMgr->QueryInterface(IID_ITfLangBarItemMgr,
        (void**)&pLangBarItemMgr) != S_OK)
    {
        return;
    }

    if (_pLangBarItemBrand != NULL)
    {
        pLangBarItemMgr->RemoveItem(_pLangBarItemBrand);
        _pLangBarItemBrand->Release();
        _pLangBarItemBrand = NULL;
    }

    if (_pLangBarItemMode != NULL)
    {
        pLangBarItemMgr->RemoveItem(_pLangBarItemMode);
        _pLangBarItemMode->Release();
        _pLangBarItemMode = NULL;
    }

    pLangBarItemMgr->Release();
}

//+---------------------------------------------------------------------------
//
//  _UpdateLanguageBar
//
//----------------------------------------------------------------------------

void CTextService::_UpdateLanguageBar()
{
    _UpdateLanguageBarCompartments();

    DebugLog(L"[_UpdateLanguageBar] called brand=%s mode=%s\r\n",
        _pLangBarItemBrand ? L"present" : L"null",
        _pLangBarItemMode ? L"present" : L"null");
    if (_pLangBarItemBrand)
    {
        _pLangBarItemBrand->_Update();
    }

    if (_pLangBarItemMode)
    {
        _pLangBarItemMode->_Update();
    }
}

void CTextService::_UpdateLanguageBarCompartments()
{
    VARIANT var;
    VariantInit(&var);

    V_VT(&var) = VT_I4;
    V_I4(&var) = TF_SENTENCEMODE_PHRASEPREDICT;
    _SetCompartment(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_SENTENCE, &var);

    if (_IsKeyboardDisabled() || !_IsKeyboardOpen())
    {
        return;
    }

    InputMode mode = GetEffectiveInputMode();
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

    _SetCompartment(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, &var);
}


void CTextService::_UpdateInputScopeForDocumentMgr(ITfDocumentMgr* pDocMgr)
{
    if (pDocMgr == NULL)
    {
        ClearInputScopeOverride();
        return;
    }

    ITfContext* pContext = NULL;
    if (pDocMgr->GetTop(&pContext) != S_OK || pContext == NULL)
    {
        ClearInputScopeOverride();
        return;
    }

    CInputScopeUpdateEditSession* pEditSession = new CInputScopeUpdateEditSession(this, pContext);
    if (pEditSession != NULL)
    {
        HRESULT hr = E_FAIL;
        pContext->RequestEditSession(_tfClientId, pEditSession, TF_ES_SYNC | TF_ES_READ, &hr);
        pEditSession->Release();

        if (FAILED(hr))
        {
            ClearInputScopeOverride();
        }
    }
    else
    {
        ClearInputScopeOverride();
    }

    pContext->Release();
}

void CTextService::_ApplyInputScopeOverride(ITfContext* pContext, TfEditCookie ec)
{
    InputMode overrideMode;
    if (_inputScopeEvaluator.Evaluate(pContext, ec, &overrideMode))
    {
        SetInputScopeOverride(overrideMode);
    }
    else
    {
        ClearInputScopeOverride();
    }
}

void CTextService::_MarkInternalEdit()
{
    ++_pendingInternalEdits;
}

namespace
{
BOOL ApplyDisplayAttributeToRange(
    ITfContext* pContext,
    TfEditCookie ec,
    ITfRange* pRangeComposition,
    LONG start,
    LONG end,
    TfGuidAtom gaDisplayAttribute)
{
    if (pContext == NULL || pRangeComposition == NULL || end <= start)
    {
        return FALSE;
    }

    ITfRange* pAttributeRange = NULL;
    if (pRangeComposition->Clone(&pAttributeRange) != S_OK || pAttributeRange == NULL)
    {
        return FALSE;
    }

    pAttributeRange->Collapse(ec, TF_ANCHOR_START);

    LONG moved = 0;
    if (start > 0)
    {
        pAttributeRange->ShiftStart(ec, start, &moved, NULL);
    }

    if (end > start)
    {
        pAttributeRange->ShiftEnd(ec, end - start, &moved, NULL);
    }

    ITfProperty* pDisplayAttributeProperty = NULL;
    HRESULT hr = E_FAIL;
    if (pContext->GetProperty(GUID_PROP_ATTRIBUTE, &pDisplayAttributeProperty) == S_OK)
    {
        VARIANT var = {};
        var.vt = VT_I4;
        var.lVal = gaDisplayAttribute;
        hr = pDisplayAttributeProperty->SetValue(ec, pAttributeRange, &var);
        pDisplayAttributeProperty->Release();
    }

    pAttributeRange->Release();
    return hr == S_OK;
}
}

HRESULT CTextService::_UpdateCompositionText(TfEditCookie ec, ITfContext* pContext)
{
    if (_pComposition == NULL || pContext == NULL)
    {
        return E_FAIL;
    }

    ITfRange* pRangeComposition = NULL;
    if (_pComposition->GetRange(&pRangeComposition) != S_OK || pRangeComposition == NULL)
    {
        return E_FAIL;
    }

    const std::wstring& preedit = _compositionState.GetPreedit();
    _MarkInternalEdit();
    HRESULT hr = pRangeComposition->SetText(ec, 0, preedit.c_str(), static_cast<ULONG>(preedit.size()));
    if (SUCCEEDED(hr))
    {
        TF_SELECTION tfSelection;
        ITfRange* pSelectionRange = NULL;
        if (pRangeComposition->Clone(&pSelectionRange) == S_OK && pSelectionRange != NULL)
        {
            LONG cch = 0;
            pSelectionRange->Collapse(ec, TF_ANCHOR_START);
            if (_compositionState.GetPreeditCursor() > 0)
            {
                pSelectionRange->ShiftEnd(ec, _compositionState.GetPreeditCursor(), &cch, NULL);
                pSelectionRange->Collapse(ec, TF_ANCHOR_END);
            }

            tfSelection.range = pSelectionRange;
            tfSelection.style.ase = TF_AE_NONE;
            tfSelection.style.fInterimChar = FALSE;
            pContext->SetSelection(ec, 1, &tfSelection);
            pSelectionRange->Release();
        }

        if (_compositionState.GetPhase() == CompositionPhase::Converting ||
            _compositionState.GetPhase() == CompositionPhase::CandidateSelecting)
        {
            _ClearCompositionDisplayAttributes(ec, pContext);

            std::vector<CompositionState::DisplaySpan> spans = _compositionState.GetDisplaySpans();
            if (spans.empty())
            {
                _SetCompositionDisplayAttributes(ec, pContext, _gaDisplayAttributeConverted);
            }
            else
            {
                for (const CompositionState::DisplaySpan& span : spans)
                {
                    ApplyDisplayAttributeToRange(
                        pContext,
                        ec,
                        pRangeComposition,
                        span.start,
                        span.end,
                        span.focused ? _gaDisplayAttributeFocusedConverted : _gaDisplayAttributeConverted);
                }
            }
        }
        else
        {
            _SetCompositionDisplayAttributes(ec, pContext, _gaDisplayAttributeInput);
        }
    }

    pRangeComposition->Release();
    return hr;
}

HRESULT CTextService::_ClearCompositionText(TfEditCookie ec, ITfContext* pContext)
{
    if (_pComposition == NULL || pContext == NULL)
    {
        return E_FAIL;
    }

    ITfRange* pRangeComposition = NULL;
    if (_pComposition->GetRange(&pRangeComposition) != S_OK || pRangeComposition == NULL)
    {
        return E_FAIL;
    }

    _MarkInternalEdit();
    HRESULT hr = pRangeComposition->SetText(ec, 0, L"", 0);
    if (SUCCEEDED(hr))
    {
        TF_SELECTION tfSelection = {};
        ITfRange* pSelectionRange = NULL;
        if (pRangeComposition->Clone(&pSelectionRange) == S_OK && pSelectionRange != NULL)
        {
            pSelectionRange->Collapse(ec, TF_ANCHOR_START);
            tfSelection.range = pSelectionRange;
            tfSelection.style.ase = TF_AE_NONE;
            tfSelection.style.fInterimChar = FALSE;
            pContext->SetSelection(ec, 1, &tfSelection);
            pSelectionRange->Release();
        }
    }

    pRangeComposition->Release();
    return hr;
}

void CTextService::_ResetCompositionState()
{
    _compositionState.Reset();
    _composingText.Reset();
    _compositionPhase = CompositionPhase::Idle;
}

HRESULT CTextService::_SelectNextCandidate(TfEditCookie ec, ITfContext* pContext)
{
    if (!_compositionState.SelectNextCandidate())
    {
        return S_FALSE;
    }

    _compositionPhase = _compositionState.GetPhase();
    return _UpdateCompositionText(ec, pContext);
}

HRESULT CTextService::_SelectPrevCandidate(TfEditCookie ec, ITfContext* pContext)
{
    if (!_compositionState.SelectPrevCandidate())
    {
        return S_FALSE;
    }

    _compositionPhase = _compositionState.GetPhase();
    return _UpdateCompositionText(ec, pContext);
}

HRESULT CTextService::_SelectFirstCandidate(TfEditCookie ec, ITfContext* pContext)
{
    if (!_compositionState.SelectFirstCandidate())
    {
        return S_FALSE;
    }

    _compositionPhase = _compositionState.GetPhase();
    return _UpdateCompositionText(ec, pContext);
}

HRESULT CTextService::_SelectLastCandidate(TfEditCookie ec, ITfContext* pContext)
{
    if (!_compositionState.SelectLastCandidate())
    {
        return S_FALSE;
    }

    _compositionPhase = _compositionState.GetPhase();
    return _UpdateCompositionText(ec, pContext);
}

HRESULT CTextService::_CommitCurrentCandidate(TfEditCookie ec, ITfContext* pContext)
{
    if (!_compositionState.HasSelectedCandidate())
    {
        _TerminateComposition(ec, pContext);
        return S_OK;
    }

    if (_compositionState.GetPhase() == CompositionPhase::CandidateSelecting)
    {
        if (!_compositionState.CommitFocusedSegment())
        {
            return S_FALSE;
        }

        _compositionPhase = _compositionState.GetPhase();
        HRESULT hr = _UpdateCompositionText(ec, pContext);
        if (FAILED(hr))
        {
            return hr;
        }

        if (_compositionState.HasUncommittedSegments())
        {
            return S_OK;
        }

        _CloseCandidateList();
        _TerminateComposition(ec, pContext);
        return S_OK;
    }

    HRESULT hr = _UpdateCompositionText(ec, pContext);
    if (FAILED(hr))
    {
        return hr;
    }

    _CloseCandidateList();
    _TerminateComposition(ec, pContext);
    return S_OK;
}

HRESULT CTextService::_CancelConversion(TfEditCookie ec, ITfContext* pContext)
{
    _compositionState.CancelConversion(GetEffectiveInputMode(), _romajiConverter);
    _compositionPhase = _compositionState.GetPhase();

    if (_compositionPhase == CompositionPhase::Idle)
    {
        _TerminateComposition(ec, pContext);
        return S_OK;
    }

    return _UpdateCompositionText(ec, pContext);
}

HRESULT CTextService::_ShowCandidateList(TfEditCookie ec, ITfContext* pContext)
{
    if (_pCandidateList == NULL)
    {
        _pCandidateList = new CCandidateList(this);
        if (_pCandidateList == NULL)
        {
            return E_OUTOFMEMORY;
        }
    }

    ITfDocumentMgr* pDocumentMgr = NULL;
    if (pContext->GetDocumentMgr(&pDocumentMgr) != S_OK || pDocumentMgr == NULL)
    {
        return E_FAIL;
    }

    ITfRange* pRange = NULL;
    HRESULT hr = E_FAIL;
    if (_pComposition->GetRange(&pRange) == S_OK)
    {
        hr = _pCandidateList->_StartCandidateList(_tfClientId, pDocumentMgr, pContext, ec, pRange);
        pRange->Release();
    }

    pDocumentMgr->Release();
    return hr;
}

void CTextService::_CloseCandidateList()
{
    if (_pCandidateList != NULL)
    {
        _pCandidateList->_EndCandidateList();
    }
}

