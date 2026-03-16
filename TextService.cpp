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
#include "SumireSettingsStore.h"

extern const GUID GUID_LBI_INPUTMODE;

namespace
{
constexpr UINT WM_SUMIRE_LIVE_CONVERSION_READY = WM_APP + 0x120;
const wchar_t kLiveConversionWindowClassName[] = L"SumireLiveConversionWindow";
const wchar_t kLiveConversionSourceViewWindowClassName[] = L"SumireLiveConversionSourceViewWindow";
constexpr auto kLiveConversionDebounce = std::chrono::milliseconds(45);
constexpr LONG kMaxZenzLeftContextChars = 48;

std::wstring NormalizeLeftContextText(const std::wstring& text)
{
    std::wstring normalized;
    normalized.reserve(text.size());
    for (wchar_t ch : text)
    {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t')
        {
            normalized.push_back(L' ');
        }
        else if (ch >= 0x20)
        {
            normalized.push_back(ch);
        }
    }

    if (normalized.size() > static_cast<size_t>(kMaxZenzLeftContextChars))
    {
        normalized.erase(0, normalized.size() - static_cast<size_t>(kMaxZenzLeftContextChars));
    }
    return normalized;
}

void AppendSignaturePart(std::wstring* signature, const std::wstring& value)
{
    if (signature == nullptr)
    {
        return;
    }

    signature->append(value);
    signature->push_back(L'\x1f');
}

std::wstring BuildConverterSettingsSignature(const SumireSettingsStore::Settings& settings)
{
    std::wstring signature;
    AppendSignaturePart(&signature, settings.romajiMapPath);
    AppendSignaturePart(&signature, settings.zenzEnabled ? L"1" : L"0");
    AppendSignaturePart(&signature, settings.zenzServiceEnabled ? L"1" : L"0");
    AppendSignaturePart(&signature, settings.zenzModelPreset);
    AppendSignaturePart(&signature, settings.zenzModelPath);
    AppendSignaturePart(&signature, settings.zenzModelRepo);

    for (const SumireSettingsStore::UserDictionaryProfile& profile : settings.userDictionaryProfiles)
    {
        AppendSignaturePart(&signature, profile.id);
        AppendSignaturePart(&signature, profile.name);
        AppendSignaturePart(&signature, profile.sourcePath);
        AppendSignaturePart(&signature, profile.builtPath);
        AppendSignaturePart(&signature, profile.enabled ? L"1" : L"0");
    }

    return signature;
}

ConversionCandidate BuildLiveConversionZenzCandidate(const std::wstring& reading, const std::wstring& surface)
{
    ConversionCandidate candidate;
    candidate.reading = reading;
    candidate.surface = surface;

    BunsetsuConversion bunsetsu;
    bunsetsu.start = 0;
    bunsetsu.length = static_cast<int>(reading.size());
    bunsetsu.reading = reading;
    bunsetsu.surface = surface;
    bunsetsu.alternatives.push_back(surface);
    if (surface != reading)
    {
        bunsetsu.alternatives.push_back(reading);
    }
    candidate.bunsetsu.push_back(std::move(bunsetsu));
    return candidate;
}

LRESULT CALLBACK LiveConversionWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        const CREATESTRUCTW* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
        return TRUE;
    }

    CTextService* textService = reinterpret_cast<CTextService*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_SUMIRE_LIVE_CONVERSION_READY && textService != nullptr)
    {
        textService->_ApplyCompletedLiveConversionPreview();
        return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK LiveConversionSourceViewWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        const CREATESTRUCTW* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
        return TRUE;
    }

    CTextService* textService = reinterpret_cast<CTextService*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (textService == nullptr)
    {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    switch (message)
    {
    case WM_PAINT:
        {
            PAINTSTRUCT ps = {};
            HDC hdc = BeginPaint(hwnd, &ps);
            textService->_PaintLiveConversionSourceView(hdc, ps.rcPaint);
            EndPaint(hwnd, &ps);
        }
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}
}

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

class CLiveConversionRefreshEditSession : public CEditSessionBase
{
public:
    CLiveConversionRefreshEditSession(CTextService* pTextService, ITfContext* pContext)
        : CEditSessionBase(pTextService, pContext)
    {
    }

    STDMETHODIMP DoEditSession(TfEditCookie ec)
    {
        return _pTextService->_UpdateCompositionText(ec, _pContext);
    }
};

class CLiveConversionSourceViewEditSession : public CEditSessionBase
{
public:
    CLiveConversionSourceViewEditSession(
        CTextService* pTextService,
        ITfContext* pContext,
        ITfContextView* pContextView,
        ITfRange* pRangeComposition)
        : CEditSessionBase(pTextService, pContext)
        , _pContextView(pContextView)
        , _pRangeComposition(pRangeComposition)
    {
        if (_pContextView != NULL)
        {
            _pContextView->AddRef();
        }
        if (_pRangeComposition != NULL)
        {
            _pRangeComposition->AddRef();
        }
    }

    ~CLiveConversionSourceViewEditSession()
    {
        if (_pRangeComposition != NULL)
        {
            _pRangeComposition->Release();
        }
        if (_pContextView != NULL)
        {
            _pContextView->Release();
        }
    }

    STDMETHODIMP DoEditSession(TfEditCookie ec)
    {
        if (_pContextView == NULL || _pRangeComposition == NULL)
        {
            return S_OK;
        }

        RECT rc = {};
        BOOL clipped = FALSE;
        if (SUCCEEDED(_pContextView->GetTextExt(ec, _pRangeComposition, &rc, &clipped)))
        {
            _pTextService->_UpdateLiveConversionSourceViewFromRect(rc);
        }

        return S_OK;
    }

private:
    ITfContextView* _pContextView;
    ITfRange* _pRangeComposition;
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
    _liveConversionEnabled = TRUE;
    _liveConversionSourceViewEnabled = TRUE;
    _candidatePageSize = 9;
    _pendingAlphabeticShift = FALSE;
    _liveConversionWindow = NULL;
    _liveConversionSourceViewWindow = NULL;
    _liveConversionWorkerRunning = false;
    _liveConversionHasPendingRequest = false;
    _liveConversionLatestRequestedVersion = 0;
    _liveConversionCompletedVersion = 0;
    _liveConversionCompletedIsFinal = false;
    _liveConversionSourceViewBusy = false;
    _pendingInternalEdits = 0;
    _liveConversionLatestRequestedAt = std::chrono::steady_clock::time_point();

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
    _liveConversionSourceViewEnabled = TRUE;
    _liveConversionWindow = NULL;
    _liveConversionSourceViewWindow = NULL;
    _liveConversionWorkerRunning = false;
    _liveConversionHasPendingRequest = false;
    _liveConversionLatestRequestedVersion = 0;
    _liveConversionCompletedVersion = 0;
    _liveConversionCompletedIsFinal = false;
    _liveConversionSourceViewBusy = false;
    _pendingInternalEdits = 0;
    _liveConversionLatestRequestedAt = std::chrono::steady_clock::time_point();
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
    _ReloadSettings();

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

    if (!_InitLiveConversionAsync())
        goto ExitError;

    if (!_InitLiveConversionSourceView())
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
    _UninitLiveConversionAsync();
    _UninitLiveConversionSourceView();

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

void CTextService::SetLiveConversionEnabled(BOOL enabled)
{
    _liveConversionEnabled = enabled;
}

BOOL CTextService::IsLiveConversionEnabled() const
{
    return _liveConversionEnabled;
}

int CTextService::GetCandidatePageSize() const
{
    return _candidatePageSize;
}

InputMode CTextService::_GetCompositionInputMode() const
{
    if (_compositionState.IsAlphabeticPreeditActive())
    {
        return InputMode::DirectInput;
    }

    return GetEffectiveInputMode();
}

BOOL CTextService::_InitLiveConversionAsync()
{
    if (_liveConversionWindow != NULL)
    {
        return TRUE;
    }

    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = LiveConversionWindowProc;
    windowClass.hInstance = g_hInst;
    windowClass.lpszClassName = kLiveConversionWindowClassName;
    RegisterClassW(&windowClass);

    _liveConversionWindow = CreateWindowExW(
        0,
        kLiveConversionWindowClassName,
        L"",
        0,
        0, 0, 0, 0,
        HWND_MESSAGE,
        NULL,
        g_hInst,
        this);
    if (_liveConversionWindow == NULL)
    {
        return FALSE;
    }

    _liveConversionWorkerRunning = true;
    _liveConversionWorker = std::thread([this]()
    {
        for (;;)
        {
            std::wstring reading;
            std::wstring leftContext;
            std::uint64_t version = 0;
            std::chrono::steady_clock::time_point requestedAt;
            {
                std::unique_lock<std::mutex> lock(_liveConversionMutex);
                _liveConversionCv.wait(lock, [this]()
                {
                    return !_liveConversionWorkerRunning || _liveConversionHasPendingRequest;
                });

                if (!_liveConversionWorkerRunning)
                {
                    break;
                }

                version = _liveConversionLatestRequestedVersion;
                requestedAt = _liveConversionLatestRequestedAt;
                _liveConversionCv.wait_until(lock, requestedAt + kLiveConversionDebounce, [this, version]()
                {
                    return !_liveConversionWorkerRunning ||
                        _liveConversionLatestRequestedVersion != version;
                });

                if (!_liveConversionWorkerRunning)
                {
                    break;
                }

                if (_liveConversionLatestRequestedVersion != version)
                {
                    continue;
                }

                reading = _liveConversionPendingReading;
                leftContext = _liveConversionPendingLeftContext;
                _liveConversionHasPendingRequest = false;
            }

            KanaKanjiConverter::ConvertOptions convertOptions;
            convertOptions.useZenz = _kanaKanjiConverter.IsZenzEnabled();
            convertOptions.zenzOnly = convertOptions.useZenz;
            convertOptions.leftContext = leftContext;
            ConversionResult result = _kanaKanjiConverter.Convert(reading, [this, version]()
            {
                std::lock_guard<std::mutex> lock(_liveConversionMutex);
                return !_liveConversionWorkerRunning ||
                    version != _liveConversionLatestRequestedVersion;
            }, convertOptions);
            if (convertOptions.zenzOnly && result.candidates.empty())
            {
                convertOptions.useZenz = false;
                convertOptions.zenzOnly = false;
                result = _kanaKanjiConverter.Convert(reading, [this, version]()
                {
                    std::lock_guard<std::mutex> lock(_liveConversionMutex);
                    return !_liveConversionWorkerRunning ||
                        version != _liveConversionLatestRequestedVersion;
                }, convertOptions);
            }

            bool shouldNotify = false;
            {
                std::lock_guard<std::mutex> lock(_liveConversionMutex);
                if (!_liveConversionWorkerRunning)
                {
                    break;
                }

                if (version != _liveConversionLatestRequestedVersion ||
                    reading != _liveConversionLatestRequestedReading ||
                    leftContext != _liveConversionLatestRequestedLeftContext)
                {
                    continue;
                }

                _liveConversionCompletedReading = reading;
                _liveConversionCompletedLeftContext = leftContext;
                _liveConversionCompletedCandidates = std::move(result.candidates);
                _liveConversionCompletedVersion = version;
                _liveConversionCompletedIsFinal = true;
                shouldNotify = true;
            }

            if (shouldNotify && _liveConversionWindow != NULL)
            {
                PostMessageW(_liveConversionWindow, WM_SUMIRE_LIVE_CONVERSION_READY, 0, 0);
            }
        }
    });

    return TRUE;
}

void CTextService::_UninitLiveConversionAsync()
{
    {
        std::lock_guard<std::mutex> lock(_liveConversionMutex);
        _liveConversionWorkerRunning = false;
        _liveConversionHasPendingRequest = false;
        _liveConversionPendingReading.clear();
        _liveConversionPendingLeftContext.clear();
        _liveConversionLatestRequestedReading.clear();
        _liveConversionLatestRequestedLeftContext.clear();
        _liveConversionCompletedReading.clear();
        _liveConversionCompletedLeftContext.clear();
        _liveConversionCompletedCandidates.clear();
        _liveConversionCompletedIsFinal = false;
    }
    _liveConversionCv.notify_all();

    if (_liveConversionWorker.joinable())
    {
        _liveConversionWorker.join();
    }

    if (_liveConversionWindow != NULL)
    {
        DestroyWindow(_liveConversionWindow);
        _liveConversionWindow = NULL;
    }
}

void CTextService::_QueueLiveConversionRequest(const std::wstring& reading, const std::wstring& leftContext)
{
    std::lock_guard<std::mutex> lock(_liveConversionMutex);
    if (!_liveConversionWorkerRunning)
    {
        return;
    }

    if (reading == _liveConversionLatestRequestedReading &&
        leftContext == _liveConversionLatestRequestedLeftContext &&
        (_liveConversionHasPendingRequest ||
            (_liveConversionCompletedReading == reading &&
                _liveConversionCompletedLeftContext == leftContext)))
    {
        return;
    }

    ++_liveConversionLatestRequestedVersion;
    _liveConversionLatestRequestedReading = reading;
    _liveConversionLatestRequestedLeftContext = leftContext;
    _liveConversionPendingReading = reading;
    _liveConversionPendingLeftContext = leftContext;
    _liveConversionLatestRequestedAt = std::chrono::steady_clock::now();
    _liveConversionHasPendingRequest = true;
    _liveConversionCv.notify_one();
}

void CTextService::_CancelLiveConversionRequests()
{
    std::lock_guard<std::mutex> lock(_liveConversionMutex);
    ++_liveConversionLatestRequestedVersion;
    _liveConversionLatestRequestedReading.clear();
    _liveConversionLatestRequestedLeftContext.clear();
    _liveConversionPendingReading.clear();
    _liveConversionPendingLeftContext.clear();
    _liveConversionHasPendingRequest = false;
    _liveConversionLatestRequestedAt = std::chrono::steady_clock::time_point();
    _liveConversionCompletedReading.clear();
    _liveConversionCompletedLeftContext.clear();
    _liveConversionCompletedCandidates.clear();
    _liveConversionCompletedVersion = 0;
    _liveConversionCompletedIsFinal = false;
}

bool CTextService::_CanUseLiveConversionPreview() const
{
    return _liveConversionEnabled != FALSE &&
        _compositionState.GetPhase() == CompositionPhase::Composing &&
        !_compositionState.Empty() &&
        !_compositionState.IsAlphabeticPreeditActive() &&
        GetEffectiveInputMode() == InputMode::Hiragana;
}

BOOL CTextService::_InitLiveConversionSourceView()
{
    if (_liveConversionSourceViewWindow != NULL)
    {
        return TRUE;
    }

    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = LiveConversionSourceViewWindowProc;
    windowClass.hInstance = g_hInst;
    windowClass.hCursor = LoadCursorW(NULL, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kLiveConversionSourceViewWindowClassName;
    RegisterClassW(&windowClass);

    _liveConversionSourceViewWindow = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kLiveConversionSourceViewWindowClassName,
        L"",
        WS_POPUP | WS_DISABLED,
        0, 0, 0, 0,
        NULL,
        NULL,
        g_hInst,
        this);
    return _liveConversionSourceViewWindow != NULL;
}

void CTextService::_UninitLiveConversionSourceView()
{
    _HideLiveConversionSourceView();
    if (_liveConversionSourceViewWindow != NULL)
    {
        DestroyWindow(_liveConversionSourceViewWindow);
        _liveConversionSourceViewWindow = NULL;
    }
}

void CTextService::_HideLiveConversionSourceView()
{
    _liveConversionSourceViewText.clear();
    _liveConversionSourceViewBusy = false;
    if (_liveConversionSourceViewWindow != NULL)
    {
        ShowWindow(_liveConversionSourceViewWindow, SW_HIDE);
    }
}

bool CTextService::_IsLiveConversionBusyForCurrentReading(const std::wstring& reading)
{
    std::lock_guard<std::mutex> lock(_liveConversionMutex);
    return !reading.empty() &&
        _liveConversionLatestRequestedReading == reading &&
        (!_liveConversionCompletedIsFinal ||
            _liveConversionCompletedVersion != _liveConversionLatestRequestedVersion);
}

std::wstring CTextService::_GetLeftContextText(TfEditCookie ec, ITfRange* pRangeComposition) const
{
    if (pRangeComposition == NULL)
    {
        return std::wstring();
    }

    ITfRange* pLeftRange = NULL;
    if (pRangeComposition->Clone(&pLeftRange) != S_OK || pLeftRange == NULL)
    {
        return std::wstring();
    }

    std::wstring leftContext;
    pLeftRange->Collapse(ec, TF_ANCHOR_START);

    LONG shifted = 0;
    if (pLeftRange->ShiftStart(ec, -kMaxZenzLeftContextChars, &shifted, NULL) == S_OK && shifted != 0)
    {
        WCHAR buffer[kMaxZenzLeftContextChars + 1] = {};
        ULONG fetched = 0;
        if (pLeftRange->GetText(ec, 0, buffer, kMaxZenzLeftContextChars, &fetched) == S_OK && fetched > 0)
        {
            leftContext.assign(buffer, buffer + fetched);
        }
    }

    pLeftRange->Release();
    return NormalizeLeftContextText(leftContext);
}

void CTextService::_RequestLiveConversionSourceViewUpdate(ITfContext* pContext, ITfRange* pRangeComposition)
{
    if (_liveConversionSourceViewEnabled == FALSE ||
        !_CanUseLiveConversionPreview() ||
        pContext == NULL ||
        pRangeComposition == NULL)
    {
        _HideLiveConversionSourceView();
        return;
    }

    const std::wstring reading = _compositionState.GetReading();
    if (reading.empty() || !_InitLiveConversionSourceView())
    {
        _HideLiveConversionSourceView();
        return;
    }

    ITfContextView* contextView = NULL;
    if (FAILED(pContext->GetActiveView(&contextView)) || contextView == NULL)
    {
        _HideLiveConversionSourceView();
        return;
    }

    CLiveConversionSourceViewEditSession* editSession =
        new CLiveConversionSourceViewEditSession(this, pContext, contextView, pRangeComposition);
    contextView->Release();
    if (editSession == NULL)
    {
        return;
    }

    HRESULT hr = E_FAIL;
    pContext->RequestEditSession(_tfClientId, editSession, TF_ES_ASYNCDONTCARE | TF_ES_READ, &hr);
    editSession->Release();
    if (FAILED(hr))
    {
        _HideLiveConversionSourceView();
    }
}

void CTextService::_UpdateLiveConversionSourceViewFromRect(const RECT& rc)
{
    if (_liveConversionSourceViewEnabled == FALSE || !_CanUseLiveConversionPreview())
    {
        _HideLiveConversionSourceView();
        return;
    }

    const std::wstring reading = _compositionState.GetReading();
    if (reading.empty() || !_InitLiveConversionSourceView())
    {
        _HideLiveConversionSourceView();
        return;
    }

    _liveConversionSourceViewText = reading;
    _liveConversionSourceViewBusy = _IsLiveConversionBusyForCurrentReading(reading);

    const int iconWidth = _liveConversionSourceViewBusy ? 14 : 0;
    const int paddingX = 8;
    const int paddingY = 5;

    HDC screenDc = GetDC(NULL);
    SIZE textSize = {};
    HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HFONT oldFont = static_cast<HFONT>(SelectObject(screenDc, font));
    GetTextExtentPoint32W(
        screenDc,
        _liveConversionSourceViewText.c_str(),
        static_cast<int>(_liveConversionSourceViewText.size()),
        &textSize);
    SelectObject(screenDc, oldFont);
    ReleaseDC(NULL, screenDc);

    const int width = textSize.cx + paddingX * 2 + iconWidth + (_liveConversionSourceViewBusy ? 6 : 0);
    const int height = (std::max)(24, static_cast<int>(textSize.cy) + paddingY * 2);
    const int x = rc.right + 8;
    const int y = rc.bottom + 4;

    MoveWindow(_liveConversionSourceViewWindow, x, y, width, height, TRUE);
    ShowWindow(_liveConversionSourceViewWindow, SW_SHOWNOACTIVATE);
    InvalidateRect(_liveConversionSourceViewWindow, NULL, TRUE);
}

void CTextService::_PaintLiveConversionSourceView(HDC hdc, const RECT& bounds) const
{
    HBRUSH background = CreateSolidBrush(RGB(255, 252, 245));
    FillRect(hdc, &bounds, background);
    DeleteObject(background);

    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(198, 186, 164));
    HGDIOBJ oldPen = SelectObject(hdc, borderPen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(hdc, bounds.left, bounds.top, bounds.right, bounds.bottom);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(borderPen);

    int textLeft = bounds.left + 8;
    if (_liveConversionSourceViewBusy)
    {
        HBRUSH busyBrush = CreateSolidBrush(RGB(0, 120, 215));
        const int centerY = (bounds.top + bounds.bottom) / 2;
        const RECT iconRect = { bounds.left + 8, centerY - 4, bounds.left + 16, centerY + 4 };
        FillRect(hdc, &iconRect, busyBrush);
        DeleteObject(busyBrush);
        textLeft = bounds.left + 22;
    }

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(64, 54, 40));
    HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HGDIOBJ oldFont = SelectObject(hdc, font);
    RECT textRect = bounds;
    textRect.left = textLeft;
    DrawTextW(
        hdc,
        _liveConversionSourceViewText.c_str(),
        static_cast<int>(_liveConversionSourceViewText.size()),
        &textRect,
        DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
    SelectObject(hdc, oldFont);
}

void CTextService::_ReloadSettings()
{
    const SumireSettingsStore::Settings settings = SumireSettingsStore::Load();
    _liveConversionEnabled = settings.liveConversionEnabled ? TRUE : FALSE;
    _liveConversionSourceViewEnabled = settings.liveConversionSourceViewEnabled ? TRUE : FALSE;
    if (_liveConversionSourceViewEnabled == FALSE)
    {
        _HideLiveConversionSourceView();
    }
    _candidatePageSize = settings.candidatePageSize;
    const std::wstring converterSettingsSignature = BuildConverterSettingsSignature(settings);
    if (_converterSettingsSignature != converterSettingsSignature)
    {
        _kanaKanjiConverter = KanaKanjiConverter();
        _kanaKanjiConverter.WarmUpZenzAsync();
        _converterSettingsSignature = converterSettingsSignature;
    }
    _romajiConverter.ReloadFromSettings();
}

void CTextService::_ApplyCompletedLiveConversionPreview()
{
    if (_pTextEditSinkContext == NULL || !_IsComposing())
    {
        return;
    }

    CLiveConversionRefreshEditSession* editSession =
        new CLiveConversionRefreshEditSession(this, _pTextEditSinkContext);
    if (editSession == NULL)
    {
        return;
    }

    HRESULT hr = E_FAIL;
    _pTextEditSinkContext->RequestEditSession(
        _tfClientId,
        editSession,
        TF_ES_ASYNCDONTCARE | TF_ES_READWRITE,
        &hr);
    editSession->Release();
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

    if (_CanUseLiveConversionPreview())
    {
        const std::wstring reading = _compositionState.GetReading();
        const std::wstring leftContext = _GetLeftContextText(ec, pRangeComposition);
        _QueueLiveConversionRequest(reading, leftContext);

        std::wstring completedReading;
        std::wstring completedLeftContext;
        std::vector<ConversionCandidate> completedCandidates;
        {
            std::lock_guard<std::mutex> lock(_liveConversionMutex);
            if (_liveConversionCompletedVersion == _liveConversionLatestRequestedVersion &&
                !reading.empty() &&
                _liveConversionCompletedReading == reading &&
                _liveConversionCompletedLeftContext == leftContext)
            {
                completedReading = _liveConversionCompletedReading;
                completedLeftContext = _liveConversionCompletedLeftContext;
                completedCandidates = _liveConversionCompletedCandidates;
            }
        }

        if (!completedReading.empty() && completedLeftContext == leftContext)
        {
            _compositionState.ApplyLiveConversionPreview(completedReading, completedCandidates);
        }
        else
        {
            _compositionState.ClearLiveConversionPreviewState();
        }
    }
    else
    {
        _CancelLiveConversionRequests();
        _compositionState.ClearLiveConversionPreviewState();
        _HideLiveConversionSourceView();
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

        _RequestLiveConversionSourceViewUpdate(pContext, pRangeComposition);
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
    _HideLiveConversionSourceView();
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
    _CancelLiveConversionRequests();
    _HideLiveConversionSourceView();
    _compositionState.Reset();
    _composingText.Reset();
    _compositionPhase = CompositionPhase::Idle;
    _pendingAlphabeticShift = FALSE;
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

HRESULT CTextService::_SelectNextCandidatePage(TfEditCookie ec, ITfContext* pContext)
{
    if (!_compositionState.SelectCandidatePage(1, _candidatePageSize))
    {
        return S_FALSE;
    }

    _compositionPhase = _compositionState.GetPhase();
    return _UpdateCompositionText(ec, pContext);
}

HRESULT CTextService::_SelectPrevCandidatePage(TfEditCookie ec, ITfContext* pContext)
{
    if (!_compositionState.SelectCandidatePage(-1, _candidatePageSize))
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

    if (_compositionState.GetPhase() == CompositionPhase::RechunkSelecting)
    {
        if (!_compositionState.ApplySelectedRechunkOption())
        {
            return S_FALSE;
        }

        _compositionPhase = _compositionState.GetPhase();
        return _UpdateCompositionText(ec, pContext);
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
    if (_compositionState.GetPhase() == CompositionPhase::RechunkSelecting)
    {
        if (!_compositionState.CancelRechunkSelection())
        {
            return S_FALSE;
        }

        _compositionPhase = _compositionState.GetPhase();
        return _UpdateCompositionText(ec, pContext);
    }

    _compositionState.CancelConversion(_GetCompositionInputMode(), _romajiConverter);
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

