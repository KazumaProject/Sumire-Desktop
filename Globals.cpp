//////////////////////////////////////////////////////////////////////
//
//  Globals.cpp
//
//////////////////////////////////////////////////////////////////////

#include "Globals.h"
#include <initguid.h>

HINSTANCE g_hInst;
LONG g_cRefDll = -1;
CRITICAL_SECTION g_cs;

/*
 * 一部 SDK では GUID_LBI_INPUTMODE がヘッダに宣言されていない場合があるので、
 * 「宣言だけ」自前で追加する（今回は実際には使わない）。
 */
#ifndef GUID_LBI_INPUTMODE
EXTERN_C const GUID GUID_LBI_INPUTMODE;
#endif

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
