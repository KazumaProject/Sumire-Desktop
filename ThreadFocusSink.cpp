#include "Globals.h"
#include "TextService.h"

STDAPI CTextService::OnSetThreadFocus()
{
    DebugLog(L"[ThreadFocusSink] OnSetThreadFocus\r\n");
    _ReloadSettings();
    _UpdateLanguageBar();
    return S_OK;
}

STDAPI CTextService::OnKillThreadFocus()
{
    DebugLog(L"[ThreadFocusSink] OnKillThreadFocus\r\n");
    return S_OK;
}

BOOL CTextService::_InitThreadFocusSink()
{
    ITfSource* pSource = NULL;
    BOOL fRet = FALSE;

    if (_pThreadMgr == NULL)
    {
        return FALSE;
    }

    if (_pThreadMgr->QueryInterface(IID_ITfSource, (void**)&pSource) != S_OK)
    {
        return FALSE;
    }

    if (pSource->AdviseSink(IID_ITfThreadFocusSink, (ITfThreadFocusSink*)this, &_dwThreadFocusSinkCookie) == S_OK)
    {
        fRet = TRUE;
    }
    else
    {
        _dwThreadFocusSinkCookie = TF_INVALID_COOKIE;
    }

    pSource->Release();
    return fRet;
}

void CTextService::_UninitThreadFocusSink()
{
    ITfSource* pSource = NULL;

    if (_dwThreadFocusSinkCookie == TF_INVALID_COOKIE || _pThreadMgr == NULL)
    {
        return;
    }

    if (_pThreadMgr->QueryInterface(IID_ITfSource, (void**)&pSource) == S_OK)
    {
        pSource->UnadviseSink(_dwThreadFocusSinkCookie);
        pSource->Release();
    }

    _dwThreadFocusSinkCookie = TF_INVALID_COOKIE;
}
