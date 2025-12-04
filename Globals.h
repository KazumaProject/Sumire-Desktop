//////////////////////////////////////////////////////////////////////
//
//  THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
//  ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED
//  TO THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
//  PARTICULAR PURPOSE.
//
//  Globals.h
//
//          Global variable declarations.
//
//////////////////////////////////////////////////////////////////////

#ifndef GLOBALS_H
#define GLOBALS_H

#include <windows.h>
#include <ole2.h>
#include <olectl.h>
#include <strsafe.h>
#include <assert.h>
#include <msctf.h>

void DllAddRef();
void DllRelease();

#define TEXTSERVICE_LANGID      MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT)

#define TEXTSERVICE_DESC        TEXT("Sumire IME")
#define TEXTSERVICE_MODEL       TEXT("Apartment")

// TextService の既定アイコン（レジストリ登録で使う）
#define TEXTSERVICE_ICON_INDEX  0

// LangBar の説明テキスト
#define LANGBAR_ITEM_DESC       L"Sumire Text Service Button"

extern HINSTANCE g_hInst;
extern LONG g_cRefDll;
extern CRITICAL_SECTION g_cs;

extern const CLSID c_clsidTextService;
extern const GUID  c_guidProfile;

// LangBar ボタン用 GUID
//   c_guidLangBarItemButton      : ブランドアイコン用（独自 GUID）
//   c_guidLangBarItemButtonMode  : IME モードアイコン用（独自 GUID）
extern const GUID c_guidLangBarItemButton;
extern const GUID c_guidLangBarItemButtonMode;

// Display Attribute GUID
extern const GUID c_guidDisplayAttributeInput;
extern const GUID c_guidDisplayAttributeConverted;

// 一部 SDK では GUID_LBI_INPUTMODE がヘッダに宣言されていない場合があるので、
// 「宣言だけ」自前で追加する。
// 実体は uuid.lib(msctf_g.obj) 側にあるので、ここでは定義しないことが重要。
#ifndef GUID_LBI_INPUTMODE
EXTERN_C const GUID GUID_LBI_INPUTMODE;
#endif

#endif // GLOBALS_H
