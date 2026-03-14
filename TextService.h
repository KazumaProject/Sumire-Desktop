//////////////////////////////////////////////////////////////////////
//
//  THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
//  ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED
//  TO THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
//  PARTICULAR PURPOSE.
//
//  TextService.h
//
//          CTextService declaration.
//
//////////////////////////////////////////////////////////////////////

#ifndef TEXTSERVICE_H
#define TEXTSERVICE_H

#include <msctf.h>
#include "CompositionState.h"
#include "ComposingText.h"
#include "InputModeState.h"
#include "InputScopeEvaluator.h"
#include "KanaKanjiConverter.h"
#include "RomajiKanaConverter.h"

class CLangBarItemButton;
class CCandidateList;

class CTextService : public ITfTextInputProcessorEx,
    public ITfThreadMgrEventSink,
    public ITfThreadFocusSink,
    public ITfCompartmentEventSink,
    public ITfTextEditSink,
    public ITfKeyEventSink,
    public ITfCompositionSink,
    public ITfDisplayAttributeProvider
{
public:
    CTextService();
    ~CTextService();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj);
    STDMETHODIMP_(ULONG) AddRef(void);
    STDMETHODIMP_(ULONG) Release(void);

    // ITfTextInputProcessor
    STDMETHODIMP Activate(ITfThreadMgr* pThreadMgr, TfClientId tfClientId);
    STDMETHODIMP Deactivate();

    // ITfTextInputProcessorEx
    STDMETHODIMP ActivateEx(ITfThreadMgr* pThreadMgr, TfClientId tfClientId, DWORD dwFlags);

    // ITfThreadMgrEventSink
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr* pDocMgr);
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr* pDocMgr);
    STDMETHODIMP OnSetFocus(ITfDocumentMgr* pDocMgrFocus, ITfDocumentMgr* pDocMgrPrevFocus);
    STDMETHODIMP OnPushContext(ITfContext* pContext);
    STDMETHODIMP OnPopContext(ITfContext* pContext);
    STDMETHODIMP OnSetThreadFocus();
    STDMETHODIMP OnKillThreadFocus();
    STDMETHODIMP OnChange(REFGUID rguid);

    // ITfTextEditSink
    STDMETHODIMP OnEndEdit(ITfContext* pContext, TfEditCookie ecReadOnly, ITfEditRecord* pEditRecord);

    // ITfKeyEventSink
    STDMETHODIMP OnSetFocus(BOOL fForeground);
    STDMETHODIMP OnTestKeyDown(ITfContext* pContext, WPARAM wParam, LPARAM lParam, BOOL* pfEaten);
    STDMETHODIMP OnKeyDown(ITfContext* pContext, WPARAM wParam, LPARAM lParam, BOOL* pfEaten);
    STDMETHODIMP OnTestKeyUp(ITfContext* pContext, WPARAM wParam, LPARAM lParam, BOOL* pfEaten);
    STDMETHODIMP OnKeyUp(ITfContext* pContext, WPARAM wParam, LPARAM lParam, BOOL* pfEaten);
    STDMETHODIMP OnPreservedKey(ITfContext* pContext, REFGUID rguid, BOOL* pfEaten);

    // ITfCompositionSink
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite, ITfComposition* pComposition);

    // ITfDisplayAttributeProvider
    STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum);
    STDMETHODIMP GetDisplayAttributeInfo(REFGUID guidInfo, ITfDisplayAttributeInfo** ppInfo);

    // CClassFactory factory callback
    static HRESULT CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObj);

    ITfThreadMgr* _GetThreadMgr() { return _pThreadMgr; }
    TfClientId    _GetClientId() { return _tfClientId; }
    ITfComposition* _GetComposition() { return _pComposition; }
    CCandidateList* _GetCandidateList() { return _pCandidateList; }
    CompositionState& _GetCompositionState() { return _compositionState; }
    const CompositionState& _GetCompositionState() const { return _compositionState; }

    // utility function for compartment
    BOOL    _IsKeyboardDisabled();
    BOOL    _IsKeyboardOpen();
    HRESULT _SetKeyboardOpen(BOOL fOpen);

    // ★ LanguageBar からも使うコンパートメント更新ヘルパー（public にする）★
    HRESULT _SetCompartment(REFGUID rguid, const VARIANT* pvar);

    // functions for the composition object.
    void _StartComposition(ITfContext* pContext);
    void _EndComposition(ITfContext* pContext);
    void _TerminateComposition(TfEditCookie ec, ITfContext* pContext);
    BOOL _IsComposing();
    void _SetComposition(ITfComposition* pComposition);
    HRESULT _UpdateCompositionText(TfEditCookie ec, ITfContext* pContext);
    HRESULT _ClearCompositionText(TfEditCookie ec, ITfContext* pContext);
    void _ResetCompositionState();

    // key event handlers.
    HRESULT _HandleCharacterKey(TfEditCookie ec, ITfContext* pContext, WPARAM wParam, LPARAM lParam);
    HRESULT _HandleShiftKey(TfEditCookie ec, ITfContext* pContext);
    HRESULT _HandleArrowKey(TfEditCookie ec, ITfContext* pContext, WPARAM wParam);
    HRESULT _HandleReturnKey(TfEditCookie ec, ITfContext* pContext);
    HRESULT _HandleSpaceKey(TfEditCookie ec, ITfContext* pContext);
    HRESULT _HandleBackspaceKey(TfEditCookie ec, ITfContext* pContext);
    HRESULT _HandleDeleteKey(TfEditCookie ec, ITfContext* pContext);
    HRESULT _HandleEscapeKey(TfEditCookie ec, ITfContext* pContext);
    HRESULT _HandleModeToggleKey(TfEditCookie ec, ITfContext* pContext);
    HRESULT _SelectNextCandidate(TfEditCookie ec, ITfContext* pContext);
    HRESULT _SelectPrevCandidate(TfEditCookie ec, ITfContext* pContext);
    HRESULT _SelectNextCandidatePage(TfEditCookie ec, ITfContext* pContext);
    HRESULT _SelectPrevCandidatePage(TfEditCookie ec, ITfContext* pContext);
    HRESULT _SelectFirstCandidate(TfEditCookie ec, ITfContext* pContext);
    HRESULT _SelectLastCandidate(TfEditCookie ec, ITfContext* pContext);
    HRESULT _CommitCurrentCandidate(TfEditCookie ec, ITfContext* pContext);
    HRESULT _CancelConversion(TfEditCookie ec, ITfContext* pContext);
    HRESULT _ShowCandidateList(TfEditCookie ec, ITfContext* pContext);
    void _CloseCandidateList();

    HRESULT _InvokeKeyHandler(ITfContext* pContext, WPARAM wParam, LPARAM lParam);

    void  _ClearCompositionDisplayAttributes(TfEditCookie ec, ITfContext* pContext);
    BOOL  _SetCompositionDisplayAttributes(TfEditCookie ec, ITfContext* pContext, TfGuidAtom gaDisplayAttribute);
    BOOL  _InitDisplayAttributeGuidAtom();

    // 入力モードの操作
    void SetUserInputMode(InputMode mode);
    InputMode GetUserInputMode() const;
    InputMode GetEffectiveInputMode() const;
    void SetLiveConversionEnabled(BOOL enabled);
    BOOL IsLiveConversionEnabled() const;
    BOOL HasInputScopeOverride() const;
    void SetInputScopeOverride(InputMode mode);
    void ClearInputScopeOverride();

    // 既存呼び出し用の互換アクセサ
    void SetInputMode(InputMode mode) { SetUserInputMode(mode); }
    void ToggleInputMode()
    {
        if (GetUserInputMode() == InputMode::Hiragana)
        {
            SetUserInputMode(InputMode::DirectInput);
        }
        else
        {
            SetUserInputMode(InputMode::Hiragana);
        }
    }

    InputMode GetInputMode() const { return GetEffectiveInputMode(); }

    void SetCompositionPhase(CompositionPhase phase);
    CompositionPhase GetCompositionPhase() const;

    // InputScope 評価用の内部ヘルパー。
    // TSF の read edit session から呼ばれるため public に公開する。
    void _ApplyInputScopeOverride(ITfContext* pContext, TfEditCookie ec);
    void _MarkInternalEdit();
    InputMode _GetCompositionInputMode() const;

private:
    // initialize and uninitialize ThreadMgrEventSink.
    BOOL _InitThreadMgrEventSink();
    void _UninitThreadMgrEventSink();
    BOOL _InitThreadFocusSink();
    void _UninitThreadFocusSink();
    BOOL _InitCompartmentEventSink();
    void _UninitCompartmentEventSink();

    // initialize TextEditSink.
    BOOL _InitTextEditSink(ITfDocumentMgr* pDocMgr);

    // initialize and uninitialize LanguageBar Item.
    BOOL _InitLanguageBar();
    void _UninitLanguageBar();
    void _UpdateLanguageBar();
    void _UpdateLanguageBarCompartments();

    // initialize and uninitialize KeyEventSink.
    BOOL _InitKeyEventSink();
    void _UninitKeyEventSink();

    // initialize and uninitialize PreservedKey.
    BOOL _InitPreservedKey();
    void _UninitPreservedKey();

    // utility function for KeyEventSink
    BOOL _IsKeyEaten(ITfContext* pContext, WPARAM wParam, LPARAM lParam);
    void _UpdateInputScopeForDocumentMgr(ITfDocumentMgr* pDocMgr);
    void _KeyboardOpenCloseChanged();
    void _KeyboardInputConversionChanged();

    //
    // state
    //
    ITfThreadMgr* _pThreadMgr;
    TfClientId    _tfClientId;

    // The cookie of ThreadMgrEventSink
    DWORD _dwThreadMgrEventSinkCookie;
    DWORD _dwThreadFocusSinkCookie;
    DWORD _dwCompartmentEventSinkOpenCloseCookie;
    DWORD _dwCompartmentEventSinkInputmodeConversionCookie;

    //
    // private variables for TextEditSink
    //
    ITfContext* _pTextEditSinkContext;
    DWORD       _dwTextEditSinkCookie;

    // LangBar items
    CLangBarItemButton* _pLangBarItemBrand; // ブランドアイコン
    CLangBarItemButton* _pLangBarItemMode;  // IME モードアイコン

    // the current composition object.
    ITfComposition* _pComposition;

    // guidatom for the display attibute.
    TfGuidAtom _gaDisplayAttributeInput;
    TfGuidAtom _gaDisplayAttributeConverted;
    TfGuidAtom _gaDisplayAttributeFocusedConverted;

    // the candidate list object.
    CCandidateList* _pCandidateList;

    CompositionState    _compositionState;
    ComposingText       _composingText;
    InputModeState      _inputModeState;
    InputScopeEvaluator _inputScopeEvaluator;
    KanaKanjiConverter  _kanaKanjiConverter;
    RomajiKanaConverter _romajiConverter;
    BOOL _liveConversionEnabled;
    BOOL _pendingAlphabeticShift;

    // 現在の composition セッション段階
    CompositionPhase _compositionPhase;
    LONG _pendingInternalEdits;

    LONG _cRef;     // COM ref count
};

#endif // TEXTSERVICE_H

