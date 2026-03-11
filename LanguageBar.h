#ifndef LANGUAGEBAR_H
#define LANGUAGEBAR_H

#include <windows.h>
#include <msctf.h>

class CTextService;

// Sumire 用 LangBar ボタン実装
class CLangBarItemButton :
    public ITfLangBarItemButton,
    public ITfSource
{
public:
    CLangBarItemButton(CTextService* pTextService, REFGUID guidItem);
    ~CLangBarItemButton();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj);
    STDMETHODIMP_(ULONG) AddRef(void);
    STDMETHODIMP_(ULONG) Release(void);

    // ITfLangBarItem
    STDMETHODIMP GetInfo(TF_LANGBARITEMINFO* pInfo);
    STDMETHODIMP GetStatus(DWORD* pdwStatus);
    STDMETHODIMP Show(BOOL fShow);
    STDMETHODIMP GetTooltipString(BSTR* pbstrToolTip);

    // ITfLangBarItemButton
    STDMETHODIMP OnClick(TfLBIClick click, POINT pt, const RECT* prcArea);
    STDMETHODIMP InitMenu(ITfMenu* pMenu);
    STDMETHODIMP OnMenuSelect(UINT wID);
    STDMETHODIMP GetIcon(HICON* phIcon);
    STDMETHODIMP GetText(BSTR* pbstrText);

    // ITfSource
    STDMETHODIMP AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie);
    STDMETHODIMP UnadviseSink(DWORD dwCookie);

    // TextService から状態変更時に呼ぶ
    HRESULT _Update();

private:
    LONG _cRef;
    CTextService* _pTextService;
    ITfLangBarItemSink* _pLangBarItemSink;
    TF_LANGBARITEMINFO _LangBarItemInfo;

    HRESULT _GetIconInternal(HICON* phIcon);
};

#endif // LANGUAGEBAR_H
