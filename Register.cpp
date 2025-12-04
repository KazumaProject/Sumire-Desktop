//////////////////////////////////////////////////////////////////////
//
//  THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
//  ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED
//  TO THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
//  PARTICULAR PURPOSE.
//
//  Copyright (C) 2003  Microsoft Corporation.  All rights reserved.
//
//  Register.cpp
//
//          Server registration code.
//
//////////////////////////////////////////////////////////////////////

#include <windows.h>
#include <ole2.h>
#include "msctf.h"
#include "Globals.h"

#define CLSID_STRLEN 38  // strlen("{xxxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxx}")

static const TCHAR c_szInfoKeyPrefix[] = TEXT("CLSID\\");
static const TCHAR c_szInProcSvr32[] = TEXT("InProcServer32");
static const TCHAR c_szModelName[] = TEXT("ThreadingModel");

//+---------------------------------------------------------------------------
//
//  RegisterProfiles
//
//----------------------------------------------------------------------------

BOOL RegisterProfiles()
{
    ITfInputProcessorProfiles* pInputProcessProfiles;
    TCHAR achIconFile[MAX_PATH];
    int cchIconFile;
    HRESULT hr;

    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfiles, (void**)&pInputProcessProfiles);

    if (hr != S_OK)
        return FALSE;

    hr = pInputProcessProfiles->Register(c_clsidTextService);
    if (hr != S_OK)
        goto Exit;

    cchIconFile = GetModuleFileName(g_hInst, achIconFile, ARRAYSIZE(achIconFile));

    hr = pInputProcessProfiles->AddLanguageProfile(
        c_clsidTextService,
        TEXTSERVICE_LANGID,
        c_guidProfile,
        TEXTSERVICE_DESC,
        (ULONG)wcslen(TEXTSERVICE_DESC),
        achIconFile,
        cchIconFile,
        TEXTSERVICE_ICON_INDEX);

Exit:
    pInputProcessProfiles->Release();
    return (hr == S_OK);
}

//+---------------------------------------------------------------------------
//
//  UnregisterProfiles
//
//----------------------------------------------------------------------------

void UnregisterProfiles()
{
    ITfInputProcessorProfiles* pInputProcessProfiles;
    HRESULT hr;

    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfiles, (void**)&pInputProcessProfiles);

    if (hr != S_OK)
        return;

    pInputProcessProfiles->Unregister(c_clsidTextService);
    pInputProcessProfiles->Release();
}

//+---------------------------------------------------------------------------
//
//  RegisterCategories
//
//----------------------------------------------------------------------------

BOOL RegisterCategories()
{
    ITfCategoryMgr* pCategoryMgr;
    HRESULT hr;

    hr = CoCreateInstance(CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER,
        IID_ITfCategoryMgr, (void**)&pCategoryMgr);

    if (hr != S_OK)
        return FALSE;

    BOOL fRet = TRUE;

    // ★ここがポイント★
    // Win8 以降で IME アイコンや切り替えを正常に動かすために、
    // CATEGORY_OF_TIP / INPUTMODECOMPARTMENT / IMMERSIVESUPPORT / SYSTRAYSUPPORT などを
    // まとめて登録する。
    const GUID rguidCategories[] =
    {
        // TIP の種別
        GUID_TFCAT_CATEGORY_OF_TIP,
        GUID_TFCAT_TIP_KEYBOARD,

        // TIP の機能カテゴリ
        GUID_TFCAT_TIPCAP_SECUREMODE,
        GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
        GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT,
        GUID_TFCAT_TIPCAP_COMLESS,
        GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
        GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,

        // 表示属性プロバイダ
        GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER
    };

    for (int i = 0; i < ARRAYSIZE(rguidCategories); ++i)
    {
        hr = pCategoryMgr->RegisterCategory(
            c_clsidTextService,
            rguidCategories[i],
            c_clsidTextService);

        if (hr != S_OK)
        {
            // ひとつでも失敗したらフラグを落とすが、
            // 他も試すためにループは回し切る。
            fRet = FALSE;
        }
    }

    pCategoryMgr->Release();
    return fRet;
}

//+---------------------------------------------------------------------------
//
//  UnregisterCategories
//
//----------------------------------------------------------------------------

void UnregisterCategories()
{
    ITfCategoryMgr* pCategoryMgr;
    HRESULT hr;

    hr = CoCreateInstance(CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER,
        IID_ITfCategoryMgr, (void**)&pCategoryMgr);

    if (hr != S_OK)
        return;

    const GUID rguidCategories[] =
    {
        GUID_TFCAT_CATEGORY_OF_TIP,
        GUID_TFCAT_TIP_KEYBOARD,
        GUID_TFCAT_TIPCAP_SECUREMODE,
        GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
        GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT,
        GUID_TFCAT_TIPCAP_COMLESS,
        GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
        GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,
        GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER
    };

    for (int i = 0; i < ARRAYSIZE(rguidCategories); ++i)
    {
        pCategoryMgr->UnregisterCategory(
            c_clsidTextService,
            rguidCategories[i],
            c_clsidTextService);
    }

    pCategoryMgr->Release();
}

//+---------------------------------------------------------------------------
//
// CLSIDToString
//
//----------------------------------------------------------------------------

BOOL CLSIDToString(REFGUID refGUID, TCHAR* pchA)
{
    static const BYTE GuidMap[] = {
        3, 2, 1, 0, '-', 5, 4, '-', 7, 6, '-',
        8, 9, '-', 10, 11, 12, 13, 14, 15
    };

    static const TCHAR szDigits[] = TEXT("0123456789ABCDEF");

    int i;
    TCHAR* p = pchA;

    const BYTE* pBytes = (const BYTE*)&refGUID;

    *p++ = '{';
    for (i = 0; i < (int)sizeof(GuidMap); i++)
    {
        if (GuidMap[i] == TEXT('-'))
        {
            *p++ = TEXT('-');
        }
        else
        {
            *p++ = szDigits[(pBytes[GuidMap[i]] & 0xF0) >> 4];
            *p++ = szDigits[(pBytes[GuidMap[i]] & 0x0F)];
        }
    }

    *p++ = TEXT('}');
    *p = TEXT('\0');

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// RecurseDeleteKey
//
//----------------------------------------------------------------------------

LONG RecurseDeleteKey(HKEY hParentKey, LPCTSTR lpszKey)
{
    HKEY hKey;
    LONG lRes;
    FILETIME time;
    TCHAR szBuffer[256];
    DWORD dwSize = ARRAYSIZE(szBuffer);

    if (RegOpenKey(hParentKey, lpszKey, &hKey) != ERROR_SUCCESS)
        return ERROR_SUCCESS; // not exist

    lRes = ERROR_SUCCESS;
    while (RegEnumKeyEx(hKey, 0, szBuffer, &dwSize, NULL, NULL, NULL, &time) == ERROR_SUCCESS)
    {
        szBuffer[ARRAYSIZE(szBuffer) - 1] = '\0';
        lRes = RecurseDeleteKey(hKey, szBuffer);
        if (lRes != ERROR_SUCCESS)
            break;
        dwSize = ARRAYSIZE(szBuffer);
    }
    RegCloseKey(hKey);

    return (lRes == ERROR_SUCCESS) ? RegDeleteKey(hParentKey, lpszKey) : lRes;
}

//+---------------------------------------------------------------------------
//
//  RegisterServer
//
//----------------------------------------------------------------------------

BOOL RegisterServer()
{
    DWORD dw;
    HKEY hKey;
    HKEY hSubKey;
    BOOL fRet;
    TCHAR achIMEKey[ARRAYSIZE(c_szInfoKeyPrefix) + CLSID_STRLEN];
    TCHAR achFileName[MAX_PATH];

    if (!CLSIDToString(c_clsidTextService, achIMEKey + ARRAYSIZE(c_szInfoKeyPrefix) - 1))
        return FALSE;

    memcpy(achIMEKey, c_szInfoKeyPrefix, sizeof(c_szInfoKeyPrefix) - sizeof(TCHAR));

    fRet = (RegCreateKeyEx(HKEY_CLASSES_ROOT, achIMEKey, 0, NULL,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL,
        &hKey, &dw) == ERROR_SUCCESS);

    if (fRet)
    {
        fRet = (RegSetValueEx(hKey, NULL, 0, REG_SZ,
            (BYTE*)TEXTSERVICE_DESC,
            (lstrlen(TEXTSERVICE_DESC) + 1) * sizeof(TCHAR)) == ERROR_SUCCESS);

        if (fRet &&
            RegCreateKeyEx(hKey, c_szInProcSvr32, 0, NULL,
                REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL,
                &hSubKey, &dw) == ERROR_SUCCESS)
        {
            dw = GetModuleFileName(g_hInst, achFileName, ARRAYSIZE(achFileName));

            fRet &= (RegSetValueEx(hSubKey, NULL, 0, REG_SZ,
                (BYTE*)achFileName,
                (lstrlen(achFileName) + 1) * sizeof(TCHAR)) == ERROR_SUCCESS);

            fRet &= (RegSetValueEx(hSubKey, c_szModelName, 0, REG_SZ,
                (BYTE*)TEXTSERVICE_MODEL,
                (lstrlen(TEXTSERVICE_MODEL) + 1) * sizeof(TCHAR)) == ERROR_SUCCESS);

            RegCloseKey(hSubKey);
        }

        RegCloseKey(hKey);
    }

    return fRet;
}

//+---------------------------------------------------------------------------
//
//  UnregisterServer
//
//----------------------------------------------------------------------------

void UnregisterServer()
{
    TCHAR achIMEKey[ARRAYSIZE(c_szInfoKeyPrefix) + CLSID_STRLEN];

    if (!CLSIDToString(c_clsidTextService, achIMEKey + ARRAYSIZE(c_szInfoKeyPrefix) - 1))
        return;

    memcpy(achIMEKey, c_szInfoKeyPrefix, sizeof(c_szInfoKeyPrefix) - sizeof(TCHAR));

    RecurseDeleteKey(HKEY_CLASSES_ROOT, achIMEKey);
}
