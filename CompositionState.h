#pragma once

#include <Windows.h>

#include <string>
#include <vector>

#include "ConversionTypes.h"
#include "ComposingText.h"
#include "InputModeState.h"

class KanaKanjiConverter;
class RomajiKanaConverter;

enum class CompositionPhase
{
    Idle = 0,
    Composing = 1,
    Converting = 2,
    CandidateSelecting = 3,
    RechunkSelecting = 4,
};

class CompositionState
{
public:
    struct Segment;

    struct CandidateItem
    {
        LONG start = 0;
        LONG end = 0;
        int startSegmentIndex = -1;
        int endSegmentIndex = -1;
        int sourceCandidateIndex = -1;
        std::wstring reading;
        std::wstring displayText;
    };

    struct RechunkOption
    {
        LONG readingStart = 0;
        LONG readingEnd = 0;
        int replaceSegmentStart = -1;
        int replaceSegmentEnd = -1;
        int sourceCandidateIndex = -1;
        std::wstring reading;
        std::wstring label;
        std::wstring surfaceText;
        std::vector<Segment> replacementSegments;
    };

    struct Segment
    {
        LONG start = 0;
        LONG end = 0;
        std::wstring rawText;
        std::vector<std::wstring> candidates;
        int selectedCandidateIndex = -1;
        std::wstring currentDisplayText;
        bool isCommitted = false;
    };

    struct DisplaySpan
    {
        LONG start = 0;
        LONG end = 0;
        bool focused = false;
    };

    struct ConversionSession
    {
        std::vector<Segment> segments;
        std::vector<ConversionCandidate> candidatePaths;
        std::vector<CandidateItem> candidateItems;
        std::vector<RechunkOption> rechunkOptions;
        std::vector<std::wstring> rechunkLabels;
        int selectedRechunkOptionIndex = -1;
        int focusedSegmentIndex = -1;
        LONG originalCaretPosition = 0;
    };

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
    const std::vector<LONG>& GetBoundaries() const;
    LONG GetRawCursor() const;
    LONG GetCaretPosition() const;
    LONG GetPreeditCursor() const;

    void SetPhase(CompositionPhase phase);
    CompositionPhase GetPhase() const;

    const std::vector<std::wstring>& GetCandidates() const;
    const std::vector<ConversionCandidate>& GetConversionCandidates() const;
    const std::vector<CandidateItem>& GetCandidateItems() const;
    const std::vector<RechunkOption>& GetRechunkOptions() const;
    bool StartConversion(const KanaKanjiConverter& kanaKanjiConverter, InputMode mode, const RomajiKanaConverter& converter);
    void EnterCandidateSelecting();
    bool BeginSegmentSelection();
    bool BeginRechunkSelection(const RomajiKanaConverter& converter);
    bool SelectNextCandidate();
    bool SelectPrevCandidate();
    bool SelectFirstCandidate();
    bool SelectLastCandidate();
    bool ApplySelectedRechunkOption();
    bool CancelRechunkSelection();
    bool MoveFocusLeft();
    bool MoveFocusRight();
    bool CommitFocusedSegment();
    bool HasFocusedSegment() const;
    bool HasUncommittedSegments() const;
    bool HasSelectedCandidate() const;
    const std::wstring& GetSelectedCandidate() const;
    std::vector<DisplaySpan> GetDisplaySpans() const;
    void CancelConversion(InputMode mode, const RomajiKanaConverter& converter);
    std::wstring GetHiraganaText(const RomajiKanaConverter& converter) const;
    std::wstring GetKatakanaText(const RomajiKanaConverter& converter) const;
    std::wstring GetHalfwidthRomanText() const;
    std::wstring GetFullwidthRomanText() const;

    int GetSelectedCandidateIndex() const;
    void SetSelectedCandidateIndex(int index);

private:
    void RebuildTexts(InputMode mode, const RomajiKanaConverter& converter);
    void RebuildBoundaries();
    void RebuildConversionDisplay();
    void ResetConversionSession();
    void SyncRawCursorFromCaret(InputMode mode, const RomajiKanaConverter& converter);
    void SyncCaretFromRawCursor(InputMode mode, const RomajiKanaConverter& converter);
    LONG GetReadingCursor(const RomajiKanaConverter& converter) const;
    int FindPrevUncommittedSegment(int startIndex) const;
    int FindFirstUncommittedSegment() const;
    int FindNextUncommittedSegment(int startIndex) const;
    Segment* GetFocusedSegment();
    const Segment* GetFocusedSegment() const;
    RechunkOption* GetSelectedRechunkOption();
    const RechunkOption* GetSelectedRechunkOption() const;
    void UpdateSegmentSelection(Segment& segment, int index);
    void UpdateRechunkSelection(int index);
    void UpdateBoundariesFromSegments();
    std::vector<RechunkOption> BuildRechunkOptionsForFocusedSegment(const RomajiKanaConverter& converter) const;
    Segment BuildSegmentFromBunsetsu(const BunsetsuConversion& bunsetsu, const RomajiKanaConverter& converter) const;
    void SyncLegacyBuffer();
    static std::wstring BuildPreeditText(const std::wstring& raw, InputMode mode, const RomajiKanaConverter& converter);
    static LONG RawCursorFromVisibleCursor(const std::wstring& raw, LONG visibleCursor, InputMode mode, const RomajiKanaConverter& converter);
    static std::vector<LONG> NormalizeBoundaries(const std::wstring& text, const std::vector<LONG>& boundaries);
    static std::vector<LONG> BuildDefaultBoundaries(const std::wstring& text);
    static bool IsParticleBoundaryCandidate(WCHAR ch);
    static std::wstring ToFullwidth(const std::wstring& src);
    static std::wstring HiraganaToFullwidthKatakana(const std::wstring& src);
    static std::wstring HiraganaToHalfwidthKatakana(const std::wstring& src);

    std::wstring _rawInput;
    std::wstring _reading;
    std::wstring _preedit;
    LONG _rawCursor;
    LONG _caretPosition;
    LONG _preeditCursor;
    std::vector<LONG> _boundaries;
    ConversionSession _conversionSession;
    CompositionPhase _phase;

    ComposingText _legacyBuffer;
};
