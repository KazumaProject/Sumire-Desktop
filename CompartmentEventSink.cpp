#include "Globals.h"
#include "TextService.h"

STDAPI CTextService::OnChange(REFGUID rguid)
{
    if (IsEqualGUID(rguid, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE))
    {
        DebugLog(L"[CompartmentEventSink] OnChange GUID_COMPARTMENT_KEYBOARD_OPENCLOSE\r\n");
        _KeyboardOpenCloseChanged();
    }
    else if (IsEqualGUID(rguid, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION))
    {
        DebugLog(L"[CompartmentEventSink] OnChange GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION\r\n");
        _KeyboardInputConversionChanged();
    }

    return S_OK;
}

BOOL CTextService::_InitCompartmentEventSink()
{
    ITfCompartmentMgr* pCompartmentMgr = NULL;
    BOOL fOpenClose = FALSE;
    BOOL fConversion = FALSE;

    if (_pThreadMgr == NULL)
    {
        return FALSE;
    }

    if (_pThreadMgr->QueryInterface(IID_ITfCompartmentMgr, (void**)&pCompartmentMgr) != S_OK)
    {
        return FALSE;
    }

    ITfCompartment* pCompartment = NULL;
    if (pCompartmentMgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &pCompartment) == S_OK)
    {
        ITfSource* pSource = NULL;
        if (pCompartment->QueryInterface(IID_ITfSource, (void**)&pSource) == S_OK)
        {
            if (pSource->AdviseSink(IID_ITfCompartmentEventSink, (ITfCompartmentEventSink*)this, &_dwCompartmentEventSinkOpenCloseCookie) == S_OK)
            {
                fOpenClose = TRUE;
            }
            else
            {
                _dwCompartmentEventSinkOpenCloseCookie = TF_INVALID_COOKIE;
            }
            pSource->Release();
        }
        pCompartment->Release();
    }

    pCompartment = NULL;
    if (pCompartmentMgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, &pCompartment) == S_OK)
    {
        ITfSource* pSource = NULL;
        if (pCompartment->QueryInterface(IID_ITfSource, (void**)&pSource) == S_OK)
        {
            if (pSource->AdviseSink(IID_ITfCompartmentEventSink, (ITfCompartmentEventSink*)this, &_dwCompartmentEventSinkInputmodeConversionCookie) == S_OK)
            {
                fConversion = TRUE;
            }
            else
            {
                _dwCompartmentEventSinkInputmodeConversionCookie = TF_INVALID_COOKIE;
            }
            pSource->Release();
        }
        pCompartment->Release();
    }

    pCompartmentMgr->Release();
    return fOpenClose && fConversion;
}

void CTextService::_UninitCompartmentEventSink()
{
    ITfCompartmentMgr* pCompartmentMgr = NULL;

    if (_pThreadMgr == NULL)
    {
        return;
    }

    if (_pThreadMgr->QueryInterface(IID_ITfCompartmentMgr, (void**)&pCompartmentMgr) != S_OK)
    {
        return;
    }

    ITfCompartment* pCompartment = NULL;
    if (_dwCompartmentEventSinkOpenCloseCookie != TF_INVALID_COOKIE &&
        pCompartmentMgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &pCompartment) == S_OK)
    {
        ITfSource* pSource = NULL;
        if (pCompartment->QueryInterface(IID_ITfSource, (void**)&pSource) == S_OK)
        {
            pSource->UnadviseSink(_dwCompartmentEventSinkOpenCloseCookie);
            pSource->Release();
        }
        pCompartment->Release();
    }

    pCompartment = NULL;
    if (_dwCompartmentEventSinkInputmodeConversionCookie != TF_INVALID_COOKIE &&
        pCompartmentMgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, &pCompartment) == S_OK)
    {
        ITfSource* pSource = NULL;
        if (pCompartment->QueryInterface(IID_ITfSource, (void**)&pSource) == S_OK)
        {
            pSource->UnadviseSink(_dwCompartmentEventSinkInputmodeConversionCookie);
            pSource->Release();
        }
        pCompartment->Release();
    }

    pCompartmentMgr->Release();
    _dwCompartmentEventSinkOpenCloseCookie = TF_INVALID_COOKIE;
    _dwCompartmentEventSinkInputmodeConversionCookie = TF_INVALID_COOKIE;
}

void CTextService::_KeyboardOpenCloseChanged()
{
    DebugLog(L"[CompartmentEventSink] _KeyboardOpenCloseChanged open=%s\r\n",
        _IsKeyboardOpen() ? L"TRUE" : L"FALSE");
    _UpdateLanguageBar();
}

void CTextService::_KeyboardInputConversionChanged()
{
    DebugLog(L"[CompartmentEventSink] _KeyboardInputConversionChanged\r\n");
    _UpdateLanguageBar();
}
