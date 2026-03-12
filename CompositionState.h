#pragma once

#include <Windows.h>

#include <string>
#include <vector>

#include "ComposingText.h"
#include "InputModeState.h"

class RomajiKanaConverter;

enum class CompositionPhase
{
    Idle = 0,
    Composing = 1,
    Converting = 2,
    CandidateSelecting = 3,
};

class CompositionState
{
public:
    CompositionState();

    void Reset();
    void Begin();

    void InsertRawChar(WCHAR ch, InputMode mode, const RomajiKanaConverter& converter);
    bool Backspace(InputMode mode, const RomajiKanaConverter& converter);
    bool Delete(InputMode mode, const RomajiKanaConverter& converter);
    bool MoveLeft(InputMode mode, const RomajiKanaConverter& converter);
    bool MoveRight(InputMode mode, const RomajiKanaConverter& converter);

    bool Empty() const;
    bool HasCandidates() const;

    const std::wstring& GetRawInput() const;
    const std::wstring& GetReading() const;
    const std::wstring& GetPreedit() const;
    LONG GetRawCursor() const;
    LONG GetPreeditCursor() const;

    void SetPhase(CompositionPhase phase);
    CompositionPhase GetPhase() const;

    std::vector<std::wstring>& MutableCandidates();
    const std::vector<std::wstring>& GetCandidates() const;
    void StartConversion(const std::vector<std::wstring>& candidates);
    void EnterCandidateSelecting();
    bool SelectNextCandidate();
    bool SelectPrevCandidate();
    bool SelectFirstCandidate();
    bool SelectLastCandidate();
    bool HasSelectedCandidate() const;
    const std::wstring& GetSelectedCandidate() const;
    void CancelConversion(InputMode mode, const RomajiKanaConverter& converter);
    std::wstring GetHiraganaText(const RomajiKanaConverter& converter) const;
    std::wstring GetKatakanaText(const RomajiKanaConverter& converter) const;
    std::wstring GetHalfwidthRomanText() const;
    std::wstring GetFullwidthRomanText() const;

    int GetSelectedCandidateIndex() const;
    void SetSelectedCandidateIndex(int index);

private:
    void RebuildTexts(InputMode mode, const RomajiKanaConverter& converter);
    void SyncLegacyBuffer();
    static std::wstring BuildPreeditText(const std::wstring& raw, InputMode mode, const RomajiKanaConverter& converter);
    static LONG RawCursorFromVisibleCursor(const std::wstring& raw, LONG visibleCursor, InputMode mode, const RomajiKanaConverter& converter);
    static std::wstring ToFullwidth(const std::wstring& src);
    static std::wstring HiraganaToFullwidthKatakana(const std::wstring& src);
    static std::wstring HiraganaToHalfwidthKatakana(const std::wstring& src);

    std::wstring _rawInput;
    std::wstring _reading;
    std::wstring _preedit;
    LONG _rawCursor;
    LONG _preeditCursor;
    std::vector<std::wstring> _candidates;
    int _selectedCandidateIndex;
    CompositionPhase _phase;

    ComposingText _legacyBuffer;
};
