#pragma once

#include <Windows.h> 
#include <string>

class ComposingText
{
public:
    ComposingText()
    {
        Reset();
    }

    void Reset()
    {
        _rawText.clear();
        _surfaceText.clear();
        _liveConversionText.clear();
        _rawCursor = 0;
        _surfaceCursor = 0;
        _liveConversionEnabled = FALSE;
    }

    // --- RawText ---

    const std::wstring& GetRawText() const { return _rawText; }
    LONG GetRawCursor() const { return _rawCursor; }

    void SetRaw(const std::wstring& text, LONG cursor)
    {
        _rawText = text;
        if (cursor < 0) cursor = 0;
        if (cursor > (LONG)_rawText.size()) cursor = (LONG)_rawText.size();
        _rawCursor = cursor;
    }

    void InsertCharAtStart(WCHAR ch)
    {
        InsertCharAt(0, ch);
    }

    void InsertCharAtEnd(WCHAR ch)
    {
        InsertCharAt((LONG)_rawText.size(), ch);
    }

    void InsertCharAt(LONG pos, WCHAR ch)
    {
        if (pos < 0) pos = 0;
        if (pos > (LONG)_rawText.size()) pos = (LONG)_rawText.size();

        _rawText.insert(_rawText.begin() + pos, ch);

        // 挿入位置以降にカーソルがあれば 1 つ後ろにずらす
        if (_rawCursor >= pos)
        {
            _rawCursor++;
        }
    }

    void SetRawCursor(LONG pos)
    {
        if (pos < 0) pos = 0;
        if (pos > (LONG)_rawText.size()) pos = (LONG)_rawText.size();
        _rawCursor = pos;
    }

    // --- SurfaceText ---

    const std::wstring& GetSurfaceText() const { return _surfaceText; }
    LONG GetSurfaceCursor() const { return _surfaceCursor; }

    void SetSurface(const std::wstring& text, LONG cursor)
    {
        _surfaceText = text;
        if (cursor < 0) cursor = 0;
        if (cursor > (LONG)_surfaceText.size()) cursor = (LONG)_surfaceText.size();
        _surfaceCursor = cursor;
    }

    // --- LiveConversionText ---

    void EnableLiveConversion(BOOL enable)
    {
        _liveConversionEnabled = enable;
        if (!enable)
        {
            _liveConversionText.clear();
        }
    }

    BOOL IsLiveConversionEnabled() const { return _liveConversionEnabled; }

    void SetLiveConversionText(const std::wstring& text)
    {
        if (_liveConversionEnabled)
        {
            _liveConversionText = text;
        }
    }

    void ClearLiveConversionText()
    {
        _liveConversionText.clear();
    }

    const std::wstring& GetLiveConversionText() const
    {
        return _liveConversionText;
    }

    // 画面に出すテキスト（LiveConversion があればそちらを優先）
    const std::wstring& GetCurrentText() const
    {
        return _liveConversionText.empty() ? _surfaceText : _liveConversionText;
    }

private:
    std::wstring _rawText;             // RawText: アルファベット
    std::wstring _surfaceText;         // SurfaceText: ひらがな or そのまま
    std::wstring _liveConversionText;  // LiveConversionText: ライブ変換の第1候補

    LONG _rawCursor;                   // RawText 上のカーソル位置
    LONG _surfaceCursor;               // SurfaceText 上のカーソル位置
    BOOL _liveConversionEnabled;       // ライブ変換フラグ
};
