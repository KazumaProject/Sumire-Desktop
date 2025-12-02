//////////////////////////////////////////////////////////////////////
//
//  THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
//  ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED
//  TO THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
//  PARTICULAR PURPOSE.
//
//  Resource.h
//
//          Resource declarations.
//
//////////////////////////////////////////////////////////////////////

#ifndef RESOURCE_H
#define RESOURCE_H

//
// String resources
//
#define IDS_TEXTSERVICE            1
#define IDS_DISPLAY_NAME           2
#define IDS_TRUE                   3
#define IDS_FALSE                  4
#define IDS_LANGBAR_ITEM_DESC      5

//
// LangBar の説明テキスト用マクロ
// LanguageBar.cpp / TextService.cpp で使っている LANGBAR_ITEM_DESC
//
#define LANGBAR_ITEM_DESC L"Sumire Text Service"   // 好きな説明文に変えてOK

//
// Icon resources
//
#define IDI_TEXTSERVICE           101   // 既存のテキストサービス共通アイコン
#define IDI_MODE_HIRAGANA         201   // 「あ」モード用アイコン
#define IDI_MODE_ALPHANUMERIC     202   // 「A」モード用アイコン

#endif // RESOURCE_H
