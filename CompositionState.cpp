#include "CompositionState.h"

#include <algorithm>

#include "KanaKanjiConverter.h"
#include "RomajiKanaConverter.h"

namespace
{
const std::vector<std::wstring>& EmptyCandidates()
{
    static const std::vector<std::wstring> kEmptyCandidates;
    return kEmptyCandidates;
}

void AppendUniqueCandidate(std::vector<std::wstring>* candidates, const std::wstring& candidate)
{
    if (candidate.empty())
    {
        return;
    }

    for (const std::wstring& existing : *candidates)
    {
        if (existing == candidate)
        {
            return;
        }
    }

    candidates->push_back(candidate);
}

int FindCandidateIndex(const std::vector<std::wstring>& candidates, const std::wstring& value)
{
    for (size_t index = 0; index < candidates.size(); ++index)
    {
        if (candidates[index] == value)
        {
            return static_cast<int>(index);
        }
    }

    return -1;
}
}

CompositionState::CompositionState()
{
    Reset();
}

void CompositionState::Reset()
{
    _rawInput.clear();
    _reading.clear();
    _preedit.clear();
    _rawCursor = 0;
    _caretPosition = 0;
    _preeditCursor = 0;
    _boundaries.clear();
    ResetConversionSession();
    _phase = CompositionPhase::Idle;
    _legacyBuffer.Reset();
}

void CompositionState::Begin()
{
    if (_phase == CompositionPhase::Idle)
    {
        _phase = CompositionPhase::Composing;
    }
}

void CompositionState::InsertRawChar(WCHAR ch, InputMode mode, const RomajiKanaConverter& converter)
{
    SyncRawCursorFromCaret(mode, converter);

    if (_rawCursor < 0)
    {
        _rawCursor = 0;
    }
    if (_rawCursor > static_cast<LONG>(_rawInput.size()))
    {
        _rawCursor = static_cast<LONG>(_rawInput.size());
    }

    _rawInput.insert(_rawInput.begin() + _rawCursor, ch);
    ++_rawCursor;
    ResetConversionSession();
    _phase = CompositionPhase::Composing;
    RebuildTexts(mode, converter);
}

bool CompositionState::Backspace(InputMode mode, const RomajiKanaConverter& converter)
{
    SyncRawCursorFromCaret(mode, converter);

    if (_rawInput.empty() || _rawCursor <= 0)
    {
        return false;
    }

    LONG eraseStart = _rawCursor - 1;
    if (mode == InputMode::Hiragana ||
        mode == InputMode::HalfwidthKatakana ||
        mode == InputMode::FullwidthKatakana)
    {
        const LONG targetCaret = (_caretPosition > 0) ? (_caretPosition - 1) : 0;
        eraseStart = RawCursorFromVisibleCursor(_rawInput, targetCaret, mode, converter);
    }

    if (eraseStart < 0)
    {
        eraseStart = 0;
    }
    if (eraseStart >= _rawCursor)
    {
        eraseStart = _rawCursor - 1;
    }

    _rawInput.erase(_rawInput.begin() + eraseStart, _rawInput.begin() + _rawCursor);
    _rawCursor = eraseStart;
    ResetConversionSession();
    _phase = _rawInput.empty() ? CompositionPhase::Idle : CompositionPhase::Composing;
    RebuildTexts(mode, converter);

    return true;
}

bool CompositionState::Delete(InputMode mode, const RomajiKanaConverter& converter)
{
    SyncRawCursorFromCaret(mode, converter);

    if (_rawInput.empty() || _rawCursor >= static_cast<LONG>(_rawInput.size()))
    {
        return false;
    }

    LONG eraseEnd = _rawCursor + 1;
    if (mode == InputMode::Hiragana ||
        mode == InputMode::HalfwidthKatakana ||
        mode == InputMode::FullwidthKatakana)
    {
        const LONG currentCaret = std::max<LONG>(0, _caretPosition);
        const LONG nextCaret = std::min<LONG>(
            static_cast<LONG>(BuildPreeditText(_rawInput, mode, converter).size()),
            currentCaret + 1);
        eraseEnd = RawCursorFromVisibleCursor(_rawInput, nextCaret, mode, converter);
        if (eraseEnd <= _rawCursor)
        {
            eraseEnd = _rawCursor + 1;
        }
    }

    if (eraseEnd > static_cast<LONG>(_rawInput.size()))
    {
        eraseEnd = static_cast<LONG>(_rawInput.size());
    }

    _rawInput.erase(_rawInput.begin() + _rawCursor, _rawInput.begin() + eraseEnd);
    ResetConversionSession();
    _phase = _rawInput.empty() ? CompositionPhase::Idle : CompositionPhase::Composing;
    RebuildTexts(mode, converter);

    return true;
}

bool CompositionState::MoveLeft(InputMode mode, const RomajiKanaConverter& converter)
{
    if (_phase == CompositionPhase::Converting || _phase == CompositionPhase::CandidateSelecting)
    {
        return MoveFocusLeft();
    }

    if (_caretPosition <= 0)
    {
        _caretPosition = 0;
        SyncRawCursorFromCaret(mode, converter);
        RebuildTexts(mode, converter);
        return false;
    }

    --_caretPosition;
    SyncRawCursorFromCaret(mode, converter);
    RebuildTexts(mode, converter);
    return true;
}

bool CompositionState::MoveRight(InputMode mode, const RomajiKanaConverter& converter)
{
    if (_phase == CompositionPhase::Converting || _phase == CompositionPhase::CandidateSelecting)
    {
        return MoveFocusRight();
    }

    const LONG end = static_cast<LONG>(BuildPreeditText(_rawInput, mode, converter).size());
    if (_caretPosition >= end)
    {
        _caretPosition = end;
        SyncRawCursorFromCaret(mode, converter);
        RebuildTexts(mode, converter);
        return false;
    }

    ++_caretPosition;
    SyncRawCursorFromCaret(mode, converter);
    RebuildTexts(mode, converter);
    return true;
}

bool CompositionState::Empty() const
{
    return _rawInput.empty();
}

bool CompositionState::HasCandidates() const
{
    const Segment* segment = GetFocusedSegment();
    return segment != nullptr && !segment->candidates.empty();
}

const std::wstring& CompositionState::GetRawInput() const
{
    return _rawInput;
}

const std::wstring& CompositionState::GetReading() const
{
    return _reading;
}

const std::wstring& CompositionState::GetPreedit() const
{
    return _preedit;
}

const std::vector<LONG>& CompositionState::GetBoundaries() const
{
    return _boundaries;
}

LONG CompositionState::GetRawCursor() const
{
    return _rawCursor;
}

LONG CompositionState::GetCaretPosition() const
{
    return _caretPosition;
}

LONG CompositionState::GetPreeditCursor() const
{
    return _preeditCursor;
}

void CompositionState::SetPhase(CompositionPhase phase)
{
    _phase = phase;
}

CompositionPhase CompositionState::GetPhase() const
{
    return _phase;
}

const std::vector<std::wstring>& CompositionState::GetCandidates() const
{
    const Segment* segment = GetFocusedSegment();
    return (segment != nullptr) ? segment->candidates : EmptyCandidates();
}

bool CompositionState::StartConversion(const KanaKanjiConverter& kanaKanjiConverter, InputMode mode, const RomajiKanaConverter& converter)
{
    if (_rawInput.empty())
    {
        return false;
    }

    SyncRawCursorFromCaret(mode, converter);

    ConversionSession session;
    session.originalCaretPosition = _caretPosition;

    const ConversionResult result = kanaKanjiConverter.Convert(_reading);
    if (result.candidates.empty())
    {
        return false;
    }

    const ConversionCandidate& candidate = result.candidates[0];
    _boundaries.clear();
    for (int boundary : result.bestBoundaries)
    {
        _boundaries.push_back(static_cast<LONG>(boundary));
    }

    for (const BunsetsuConversion& bunsetsu : candidate.bunsetsu)
    {
        if (bunsetsu.length <= 0)
        {
            continue;
        }

        const LONG start = static_cast<LONG>(bunsetsu.start);
        const LONG end = static_cast<LONG>(bunsetsu.start + bunsetsu.length);
        if (start < 0 || end > static_cast<LONG>(_reading.size()) || start >= end)
        {
            continue;
        }

        Segment segment;
        segment.start = start;
        segment.end = end;
        segment.rawText = bunsetsu.reading;

        const LONG rawStart = RawCursorFromVisibleCursor(_rawInput, start, InputMode::Hiragana, converter);
        const LONG rawEnd = RawCursorFromVisibleCursor(_rawInput, end, InputMode::Hiragana, converter);
        const std::wstring rawSegment = _rawInput.substr(rawStart, rawEnd - rawStart);

        segment.candidates = bunsetsu.alternatives;
        AppendUniqueCandidate(&segment.candidates, bunsetsu.surface);
        AppendUniqueCandidate(&segment.candidates, segment.rawText);
        AppendUniqueCandidate(&segment.candidates, BuildPreeditText(rawSegment, InputMode::FullwidthKatakana, converter));
        AppendUniqueCandidate(&segment.candidates, rawSegment);
        AppendUniqueCandidate(&segment.candidates, ToFullwidth(rawSegment));

        if (!segment.candidates.empty())
        {
            int selectedIndex = FindCandidateIndex(segment.candidates, bunsetsu.surface);
            if (selectedIndex < 0)
            {
                selectedIndex = 0;
            }

            segment.selectedCandidateIndex = selectedIndex;
            segment.currentDisplayText = segment.candidates[selectedIndex];
        }
        else
        {
            segment.selectedCandidateIndex = -1;
            segment.currentDisplayText = bunsetsu.surface;
        }

        session.segments.push_back(segment);
    }

    if (session.segments.empty())
    {
        return false;
    }

    session.focusedSegmentIndex = -1;
    _conversionSession = session;
    _phase = CompositionPhase::Converting;
    RebuildConversionDisplay();
    return true;
}

void CompositionState::EnterCandidateSelecting()
{
    if (HasFocusedSegment())
    {
        _phase = CompositionPhase::CandidateSelecting;
    }
}

bool CompositionState::BeginSegmentSelection()
{
    const int focusIndex = FindFirstUncommittedSegment();
    if (focusIndex < 0)
    {
        return false;
    }

    _conversionSession.focusedSegmentIndex = focusIndex;
    _phase = CompositionPhase::CandidateSelecting;
    RebuildConversionDisplay();
    return true;
}

bool CompositionState::SelectNextCandidate()
{
    Segment* segment = GetFocusedSegment();
    if (segment == nullptr || segment->candidates.empty())
    {
        return false;
    }

    int nextIndex = segment->selectedCandidateIndex + 1;
    if (nextIndex >= static_cast<int>(segment->candidates.size()))
    {
        nextIndex = 0;
    }

    UpdateSegmentSelection(*segment, nextIndex);
    _phase = CompositionPhase::CandidateSelecting;
    RebuildConversionDisplay();
    return true;
}

bool CompositionState::SelectPrevCandidate()
{
    Segment* segment = GetFocusedSegment();
    if (segment == nullptr || segment->candidates.empty())
    {
        return false;
    }

    int nextIndex = segment->selectedCandidateIndex - 1;
    if (nextIndex < 0)
    {
        nextIndex = static_cast<int>(segment->candidates.size()) - 1;
    }

    UpdateSegmentSelection(*segment, nextIndex);
    _phase = CompositionPhase::CandidateSelecting;
    RebuildConversionDisplay();
    return true;
}

bool CompositionState::SelectFirstCandidate()
{
    Segment* segment = GetFocusedSegment();
    if (segment == nullptr || segment->candidates.empty())
    {
        return false;
    }

    UpdateSegmentSelection(*segment, 0);
    _phase = CompositionPhase::CandidateSelecting;
    RebuildConversionDisplay();
    return true;
}

bool CompositionState::SelectLastCandidate()
{
    Segment* segment = GetFocusedSegment();
    if (segment == nullptr || segment->candidates.empty())
    {
        return false;
    }

    UpdateSegmentSelection(*segment, static_cast<int>(segment->candidates.size()) - 1);
    _phase = CompositionPhase::CandidateSelecting;
    RebuildConversionDisplay();
    return true;
}

bool CompositionState::MoveFocusLeft()
{
    if (!HasFocusedSegment())
    {
        return false;
    }

    const int previousFocus = FindPrevUncommittedSegment(_conversionSession.focusedSegmentIndex - 1);
    if (previousFocus < 0)
    {
        return false;
    }

    _conversionSession.focusedSegmentIndex = previousFocus;
    if (_phase == CompositionPhase::Converting || _phase == CompositionPhase::CandidateSelecting)
    {
        RebuildConversionDisplay();
    }
    return true;
}

bool CompositionState::MoveFocusRight()
{
    if (!HasFocusedSegment())
    {
        return false;
    }

    const int nextFocus = FindNextUncommittedSegment(_conversionSession.focusedSegmentIndex + 1);
    if (nextFocus < 0)
    {
        return false;
    }

    _conversionSession.focusedSegmentIndex = nextFocus;
    if (_phase == CompositionPhase::Converting || _phase == CompositionPhase::CandidateSelecting)
    {
        RebuildConversionDisplay();
    }
    return true;
}

bool CompositionState::CommitFocusedSegment()
{
    Segment* segment = GetFocusedSegment();
    if (segment == nullptr)
    {
        return false;
    }

    segment->isCommitted = true;

    const int nextFocus = FindNextUncommittedSegment(_conversionSession.focusedSegmentIndex + 1);
    if (nextFocus >= 0)
    {
        _conversionSession.focusedSegmentIndex = nextFocus;
        _phase = CompositionPhase::CandidateSelecting;
    }
    else
    {
        _conversionSession.focusedSegmentIndex = -1;
        _phase = CompositionPhase::Converting;
    }

    RebuildConversionDisplay();
    return true;
}

bool CompositionState::HasFocusedSegment() const
{
    return GetFocusedSegment() != nullptr;
}

bool CompositionState::HasUncommittedSegments() const
{
    for (const Segment& segment : _conversionSession.segments)
    {
        if (!segment.isCommitted)
        {
            return true;
        }
    }

    return false;
}

bool CompositionState::HasSelectedCandidate() const
{
    return !_conversionSession.segments.empty();
}

const std::wstring& CompositionState::GetSelectedCandidate() const
{
    const Segment* segment = GetFocusedSegment();
    if (segment != nullptr)
    {
        return segment->currentDisplayText;
    }

    return _preedit;
}

void CompositionState::CancelConversion(InputMode mode, const RomajiKanaConverter& converter)
{
    if (!_conversionSession.segments.empty())
    {
        _caretPosition = _conversionSession.originalCaretPosition;
    }

    ResetConversionSession();
    _phase = _rawInput.empty() ? CompositionPhase::Idle : CompositionPhase::Composing;
    RebuildTexts(mode, converter);
}

std::vector<CompositionState::DisplaySpan> CompositionState::GetDisplaySpans() const
{
    std::vector<DisplaySpan> spans;
    LONG cursor = 0;

    for (size_t i = 0; i < _conversionSession.segments.size(); ++i)
    {
        const Segment& segment = _conversionSession.segments[i];
        DisplaySpan span;
        span.start = cursor;
        cursor += static_cast<LONG>(segment.currentDisplayText.size());
        span.end = cursor;
        span.focused = (static_cast<int>(i) == _conversionSession.focusedSegmentIndex);
        spans.push_back(span);
    }

    return spans;
}

std::wstring CompositionState::GetHiraganaText(const RomajiKanaConverter& converter) const
{
    return BuildPreeditText(_rawInput, InputMode::Hiragana, converter);
}

std::wstring CompositionState::GetKatakanaText(const RomajiKanaConverter& converter) const
{
    return BuildPreeditText(_rawInput, InputMode::FullwidthKatakana, converter);
}

std::wstring CompositionState::GetHalfwidthRomanText() const
{
    return _rawInput;
}

std::wstring CompositionState::GetFullwidthRomanText() const
{
    return ToFullwidth(_rawInput);
}

int CompositionState::GetSelectedCandidateIndex() const
{
    const Segment* segment = GetFocusedSegment();
    return (segment != nullptr) ? segment->selectedCandidateIndex : -1;
}

void CompositionState::SetSelectedCandidateIndex(int index)
{
    Segment* segment = GetFocusedSegment();
    if (segment == nullptr)
    {
        return;
    }

    UpdateSegmentSelection(*segment, index);
    RebuildConversionDisplay();
}

void CompositionState::RebuildTexts(InputMode mode, const RomajiKanaConverter& converter)
{
    _reading = converter.ConvertFromRaw(_rawInput);
    _preedit = BuildPreeditText(_rawInput, mode, converter);

    SyncCaretFromRawCursor(mode, converter);
    _preeditCursor = _caretPosition;
    RebuildBoundaries();
    SyncLegacyBuffer();
}

void CompositionState::RebuildBoundaries()
{
    _boundaries = BuildDefaultBoundaries(_reading);
}

void CompositionState::RebuildConversionDisplay()
{
    std::wstring converted;
    LONG focusCursor = 0;

    for (size_t i = 0; i < _conversionSession.segments.size(); ++i)
    {
        converted += _conversionSession.segments[i].currentDisplayText;
        if (static_cast<int>(i) == _conversionSession.focusedSegmentIndex)
        {
            focusCursor = static_cast<LONG>(converted.size());
        }
    }

    _preedit = converted;
    _preeditCursor = focusCursor;
    SyncLegacyBuffer();
}

void CompositionState::ResetConversionSession()
{
    _conversionSession.segments.clear();
    _conversionSession.focusedSegmentIndex = -1;
    _conversionSession.originalCaretPosition = 0;
}

void CompositionState::SyncRawCursorFromCaret(InputMode mode, const RomajiKanaConverter& converter)
{
    const LONG visibleLength = static_cast<LONG>(BuildPreeditText(_rawInput, mode, converter).size());
    if (_caretPosition < 0)
    {
        _caretPosition = 0;
    }
    if (_caretPosition > visibleLength)
    {
        _caretPosition = visibleLength;
    }

    _rawCursor = RawCursorFromVisibleCursor(_rawInput, _caretPosition, mode, converter);
}

void CompositionState::SyncCaretFromRawCursor(InputMode mode, const RomajiKanaConverter& converter)
{
    if (_rawCursor < 0)
    {
        _rawCursor = 0;
    }
    if (_rawCursor > static_cast<LONG>(_rawInput.size()))
    {
        _rawCursor = static_cast<LONG>(_rawInput.size());
    }

    const std::wstring visiblePrefix = BuildPreeditText(_rawInput.substr(0, _rawCursor), mode, converter);
    _caretPosition = static_cast<LONG>(visiblePrefix.size());
}

LONG CompositionState::GetReadingCursor(const RomajiKanaConverter& converter) const
{
    const std::wstring readingPrefix = converter.ConvertFromRaw(_rawInput.substr(0, _rawCursor));
    return static_cast<LONG>(readingPrefix.size());
}

int CompositionState::FindPrevUncommittedSegment(int startIndex) const
{
    for (int index = startIndex; index >= 0; --index)
    {
        if (!_conversionSession.segments[index].isCommitted)
        {
            return index;
        }
    }

    return -1;
}

int CompositionState::FindFirstUncommittedSegment() const
{
    return FindNextUncommittedSegment(0);
}

int CompositionState::FindNextUncommittedSegment(int startIndex) const
{
    for (int index = startIndex; index < static_cast<int>(_conversionSession.segments.size()); ++index)
    {
        if (!_conversionSession.segments[index].isCommitted)
        {
            return index;
        }
    }

    return -1;
}

CompositionState::Segment* CompositionState::GetFocusedSegment()
{
    if (_conversionSession.focusedSegmentIndex < 0 ||
        _conversionSession.focusedSegmentIndex >= static_cast<int>(_conversionSession.segments.size()))
    {
        return nullptr;
    }

    return &_conversionSession.segments[_conversionSession.focusedSegmentIndex];
}

const CompositionState::Segment* CompositionState::GetFocusedSegment() const
{
    if (_conversionSession.focusedSegmentIndex < 0 ||
        _conversionSession.focusedSegmentIndex >= static_cast<int>(_conversionSession.segments.size()))
    {
        return nullptr;
    }

    return &_conversionSession.segments[_conversionSession.focusedSegmentIndex];
}

void CompositionState::UpdateSegmentSelection(Segment& segment, int index)
{
    if (segment.candidates.empty())
    {
        segment.selectedCandidateIndex = -1;
        segment.currentDisplayText = segment.rawText;
        return;
    }

    if (index < 0)
    {
        index = 0;
    }
    if (index >= static_cast<int>(segment.candidates.size()))
    {
        index = static_cast<int>(segment.candidates.size()) - 1;
    }

    segment.selectedCandidateIndex = index;
    segment.currentDisplayText = segment.candidates[index];
}

void CompositionState::SyncLegacyBuffer()
{
    _legacyBuffer.SetRaw(_rawInput, _rawCursor);
    _legacyBuffer.SetSurface(_preedit, _preeditCursor);
    _legacyBuffer.ClearLiveConversionText();
}

std::wstring CompositionState::BuildPreeditText(const std::wstring& raw, InputMode mode, const RomajiKanaConverter& converter)
{
    switch (mode)
    {
    case InputMode::FullwidthKatakana:
        return HiraganaToFullwidthKatakana(converter.ConvertFromRaw(raw));
    case InputMode::HalfwidthKatakana:
        return HiraganaToHalfwidthKatakana(converter.ConvertFromRaw(raw));
    case InputMode::FullwidthAlphanumeric:
        return ToFullwidth(raw);
    case InputMode::DirectInput:
        return raw;
    case InputMode::Hiragana:
    default:
        return converter.ConvertFromRaw(raw);
    }
}

LONG CompositionState::RawCursorFromVisibleCursor(const std::wstring& raw, LONG visibleCursor, InputMode mode, const RomajiKanaConverter& converter)
{
    if (visibleCursor <= 0)
    {
        return 0;
    }

    LONG bestRaw = 0;
    const LONG rawLength = static_cast<LONG>(raw.size());
    for (LONG rawCursor = 0; rawCursor <= rawLength; ++rawCursor)
    {
        const std::wstring visiblePrefix = BuildPreeditText(raw.substr(0, rawCursor), mode, converter);
        const LONG currentVisibleCursor = static_cast<LONG>(visiblePrefix.size());
        if (currentVisibleCursor <= visibleCursor)
        {
            bestRaw = rawCursor;
            continue;
        }

        break;
    }

    return bestRaw;
}

std::vector<LONG> CompositionState::NormalizeBoundaries(const std::wstring& text, const std::vector<LONG>& boundaries)
{
    std::vector<LONG> result = boundaries;
    const LONG textLength = static_cast<LONG>(text.size());

    result.erase(
        std::remove_if(
            result.begin(),
            result.end(),
            [textLength](LONG boundary)
            {
                return boundary <= 0 || boundary >= textLength;
            }),
        result.end());

    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    result.push_back(textLength);
    return result;
}

std::vector<LONG> CompositionState::BuildDefaultBoundaries(const std::wstring& text)
{
    std::vector<LONG> boundaries;
    const LONG textLength = static_cast<LONG>(text.size());
    LONG segmentStart = 0;

    for (LONG index = 0; index < textLength; ++index)
    {
        if (!IsParticleBoundaryCandidate(text[index]))
        {
            continue;
        }

        if (index > segmentStart)
        {
            boundaries.push_back(index);
        }

        boundaries.push_back(index + 1);
        segmentStart = index + 1;
    }

    return NormalizeBoundaries(text, boundaries);
}

bool CompositionState::IsParticleBoundaryCandidate(WCHAR ch)
{
    switch (ch)
    {
    case 0x306F:
    case 0x304C:
    case 0x3092:
    case 0x306B:
    case 0x3067:
    case 0x3068:
    case 0x3078:
    case 0x3082:
    case 0x3084:
    case 0x306E:
    case 0x306D:
    case 0x3088:
    case 0x304B:
    case 0x306A:
    case 0x3001:
    case 0x3002:
        return true;
    default:
        return false;
    }
}

std::wstring CompositionState::ToFullwidth(const std::wstring& src)
{
    if (src.empty())
    {
        return L"";
    }

    int required = LCMapStringW(
        LOCALE_INVARIANT,
        LCMAP_FULLWIDTH,
        src.c_str(),
        static_cast<int>(src.size()),
        NULL,
        0);

    if (required <= 0)
    {
        return src;
    }

    std::wstring result(required, L'\0');
    LCMapStringW(
        LOCALE_INVARIANT,
        LCMAP_FULLWIDTH,
        src.c_str(),
        static_cast<int>(src.size()),
        &result[0],
        required);
    return result;
}

std::wstring CompositionState::HiraganaToFullwidthKatakana(const std::wstring& src)
{
    if (src.empty())
    {
        return L"";
    }

    int required = LCMapStringW(
        LOCALE_INVARIANT,
        LCMAP_KATAKANA,
        src.c_str(),
        static_cast<int>(src.size()),
        NULL,
        0);

    if (required <= 0)
    {
        return src;
    }

    std::wstring result(required, L'\0');
    LCMapStringW(
        LOCALE_INVARIANT,
        LCMAP_KATAKANA,
        src.c_str(),
        static_cast<int>(src.size()),
        &result[0],
        required);
    return result;
}

std::wstring CompositionState::HiraganaToHalfwidthKatakana(const std::wstring& src)
{
    std::wstring katakana = HiraganaToFullwidthKatakana(src);
    if (katakana.empty())
    {
        return katakana;
    }

    int required = LCMapStringW(
        LOCALE_INVARIANT,
        LCMAP_HALFWIDTH,
        katakana.c_str(),
        static_cast<int>(katakana.size()),
        NULL,
        0);

    if (required <= 0)
    {
        return katakana;
    }

    std::wstring result(required, L'\0');
    LCMapStringW(
        LOCALE_INVARIANT,
        LCMAP_HALFWIDTH,
        katakana.c_str(),
        static_cast<int>(katakana.size()),
        &result[0],
        required);
    return result;
}
