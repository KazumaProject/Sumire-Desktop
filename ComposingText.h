// ComposingText.h
#pragma once

#include <Windows.h> 
#include <string>
#include "RomajiKanaConverter.h"

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

    // ★ここが追加★
    // RawText（全角ローマ字）から SurfaceText（かな）を生成し、
    // Surface 側のカーソル位置も Raw のカーソル位置に合わせておおよそ計算する。
    void UpdateSurfaceFromRaw(const RomajiKanaConverter& converter)
    {
        // 1. Raw 全体をかなに変換
        _surfaceText = converter.ConvertFromRaw(_rawText);

        // 2. Raw 上のカーソル位置に対応する Surface のカーソル位置を計算
        if (_rawCursor <= 0)
        {
            _surfaceCursor = 0;
        }
        else if (_rawCursor >= (LONG)_rawText.size())
        {
            _surfaceCursor = static_cast<LONG>(_surfaceText.size());
        }
        else
        {
            // 先頭から Raw のカーソル位置までを変換した結果の長さを Surface のカーソルとみなす
            std::wstring rawPrefix = _rawText.substr(0, _rawCursor);
            std::wstring surfacePrefix = converter.ConvertFromRaw(rawPrefix);
            _surfaceCursor = static_cast<LONG>(surfacePrefix.size());
        }
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

    bool RemoveLastRawChar()
    {
        if (_rawText.empty())
        {
            _rawCursor = 0;
            return false;
        }

        _rawText.pop_back();

        // カーソルは末尾に合わせる
        _rawCursor = static_cast<LONG>(_rawText.size());
        return true;
    }

    // surfaceCursor を外部からセットできるように
    void SetSurfaceCursor(LONG pos)
    {
        if (pos < 0) pos = 0;
        if (pos > (LONG)_surfaceText.size()) pos = (LONG)_surfaceText.size();
        _surfaceCursor = pos;
    }

    // Backspace: surface の「カーソル直前 1 文字」を削除
    bool RemoveSurfaceCharBeforeCursor(const RomajiKanaConverter& converter)
    {
        if (_surfaceCursor <= 0)
        {
            _surfaceCursor = 0;
            _rawCursor = 0;
            return false;
        }

        // 削除対象: [surfaceCursor-1 .. surfaceCursor)
        LONG s0 = _surfaceCursor - 1;
        LONG s1 = _surfaceCursor;

        LONG r0 = _RawPosFromSurfacePos(converter, s0);
        LONG r1 = _RawPosFromSurfacePos(converter, s1);

        if (r1 <= r0)
        {
            // 変換の性質上、対応 raw 範囲が 0 になる場合がある
            // （例: 未確定ローマ字が surface に出ない等）
            // fallback: raw の直前 1 文字削除
            if (_rawCursor > 0 && !_rawText.empty())
            {
                LONG idx = _rawCursor - 1;
                _rawText.erase(_rawText.begin() + idx);
                _rawCursor = idx;
                _surfaceCursor = s0;
                return true;
            }
            return false;
        }

        _rawText.erase(_rawText.begin() + r0, _rawText.begin() + r1);

        // カーソルは削除後の surface 位置へ
        _surfaceCursor = s0;

        // rawCursor は「削除後の surfaceCursor に対応する rawPos」に再設定
        _rawCursor = _RawPosFromSurfacePos(converter, _surfaceCursor);
        return true;
    }

    // Delete: surface の「カーソル位置 1 文字」を削除
    bool RemoveSurfaceCharAtCursor(const RomajiKanaConverter& converter)
    {
        // 末尾なら何も消せない
        std::wstring fullSurface = converter.ConvertFromRaw(_rawText);
        LONG fullSurfaceLen = (LONG)fullSurface.size();
        if (_surfaceCursor < 0) _surfaceCursor = 0;
        if (_surfaceCursor >= fullSurfaceLen)
        {
            return false;
        }

        // 削除対象: [surfaceCursor .. surfaceCursor+1)
        LONG s0 = _surfaceCursor;
        LONG s1 = _surfaceCursor + 1;

        LONG r0 = _RawPosFromSurfacePos(converter, s0);
        LONG r1 = _RawPosFromSurfacePos(converter, s1);

        if (r1 <= r0)
        {
            // fallback: raw のカーソル位置を 1 文字削除
            if (_rawCursor >= 0 && _rawCursor < (LONG)_rawText.size())
            {
                _rawText.erase(_rawText.begin() + _rawCursor);
                // surfaceCursor はそのまま
                return true;
            }
            return false;
        }

        _rawText.erase(_rawText.begin() + r0, _rawText.begin() + r1);

        // surfaceCursor は同じ位置に留める（Delete の挙動）
        _rawCursor = _RawPosFromSurfacePos(converter, _surfaceCursor);
        return true;
    }

private:
    std::wstring _rawText;             // RawText: アルファベット（全角含む）
    std::wstring _surfaceText;         // SurfaceText: ひらがな or そのまま
    std::wstring _liveConversionText;  // LiveConversionText: ライブ変換の第1候補

    LONG _rawCursor;                   // RawText 上のカーソル位置
    LONG _surfaceCursor;               // SurfaceText 上のカーソル位置
    BOOL _liveConversionEnabled;       // ライブ変換フラグ

    LONG _RawPosFromSurfacePos(const RomajiKanaConverter& converter, LONG surfacePos) const
    {
        if (surfacePos <= 0) return 0;

        // 現在の raw から生成される surface の長さを上限にする
        // ただし surfacePos がそれ以上なら raw 末尾
        std::wstring fullSurface = converter.ConvertFromRaw(_rawText);
        LONG fullSurfaceLen = (LONG)fullSurface.size();
        if (surfacePos >= fullSurfaceLen) return (LONG)_rawText.size();

        LONG bestRaw = 0;
        LONG rawLen = (LONG)_rawText.size();

        // rawPos を増やしながら prefix を変換し、surface長が surfacePos を超えない最大 rawPos を探す
        for (LONG rawPos = 0; rawPos <= rawLen; rawPos++)
        {
            std::wstring rawPrefix = _rawText.substr(0, rawPos);
            std::wstring surfacePrefix = converter.ConvertFromRaw(rawPrefix);
            LONG sLen = (LONG)surfacePrefix.size();

            if (sLen <= surfacePos)
            {
                bestRaw = rawPos;
            }
            else
            {
                break;
            }
        }

        return bestRaw;
    }
};
