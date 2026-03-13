//////////////////////////////////////////////////////////////////////
//
//  Globals.cpp
//
//////////////////////////////////////////////////////////////////////

#include "Globals.h"
HINSTANCE g_hInst;
LONG g_cRefDll = -1;
CRITICAL_SECTION g_cs;

void DebugLog(const WCHAR* format, ...)
{
    WCHAR buffer[512] = {};
    va_list args;
    va_start(args, format);
    StringCchVPrintfW(buffer, ARRAYSIZE(buffer), format, args);
    va_end(args);
    OutputDebugStringW(buffer);
}

void DebugLogHr(const WCHAR* scope, HRESULT hr)
{
    DebugLog(L"%s hr=0x%08X\r\n", scope, hr);
}

void DebugLogBool(const WCHAR* scope, BOOL value)
{
    DebugLog(L"%s=%s\r\n", scope, value ? L"TRUE" : L"FALSE");
}

void DebugLogGuid(const WCHAR* scope, REFGUID guid)
{
    OLECHAR guidText[64] = {};
    if (StringFromGUID2(guid, guidText, ARRAYSIZE(guidText)) > 0)
    {
        DebugLog(L"%s guid=%s\r\n", scope, guidText);
    }
    else
    {
        DebugLog(L"%s guid=<format-failed>\r\n", scope);
    }
}

/* e7ea138e-69f8-11d7-a6ea-00065b84435c */
const CLSID c_clsidTextService = {
    0xe7ea138e,
    0x69f8,
    0x11d7,
    {0xa6, 0xea, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}
};

/* e7ea138f-69f8-11d7-a6ea-00065b84435c */
const GUID c_guidProfile = {
    0xe7ea138f,
    0x69f8,
    0x11d7,
    {0xa6, 0xea, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}
};

// System language bar input mode item GUID.
const GUID GUID_LBI_INPUTMODE = {
    0x2c77a81e,
    0x41cc,
    0x4178,
    {0xa3, 0xa7, 0x5f, 0x8a, 0x98, 0x75, 0x68, 0xe6}
};

/*
 * LangBar ボタン用 GUID
 *
 *  - c_guidLangBarItemButton
 *      Sumire の「ブランドアイコン」用 GUID
 *
 *  - c_guidLangBarItemButtonMode
 *      Sumire の「モードアイコン」用 GUID
 *      （GUID_LBI_INPUTMODE は使わず、完全に独立させる）
 */

// ブランドアイコン用 GUID
const GUID c_guidLangBarItemButton = {
    0x0f7b7c10,
    0x9f8a,
    0x4e65,
    {0x9a, 0x3f, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc}
};

// モードアイコン用 GUID
const GUID c_guidLangBarItemButtonMode = {
    0x5d8a7733,
    0x11b8,
    0x44d8,
    {0x93, 0xd0, 0x1e, 0x89, 0x46, 0xb0, 0x75, 0x4a}
};

//
// Display Attribute GUID（元のまま）
//
const GUID c_guidDisplayAttributeInput = {
    0x4e1aa3fe,
    0x6c7f,
    0x11d7,
    {0xa6, 0xec, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}
};

const GUID c_guidDisplayAttributeConverted = {
    0x4e1aa3ff,
    0x6c7f,
    0x11d7,
    {0xa6, 0xec, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}
};

