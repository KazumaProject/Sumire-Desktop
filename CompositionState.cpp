#include "CompositionState.h"

#include <algorithm>
#include <cwctype>

#include "KanaKanjiConverter.h"
#include "RomajiKanaConverter.h"

namespace
{
const std::vector<std::wstring>& EmptyCandidates()
{
    static const std::vector<std::wstring> kEmptyCandidates;
    return kEmptyCandidates;
}

const std::vector<ConversionCandidate>& EmptyConversionCandidates()
{
    static const std::vector<ConversionCandidate> kEmptyConversionCandidates;
    return kEmptyConversionCandidates;
}

const std::vector<CompositionState::CandidateItem>& EmptyCandidateItems()
{
    static const std::vector<CompositionState::CandidateItem> kEmptyCandidateItems;
    return kEmptyCandidateItems;
}

const std::vector<CompositionState::RechunkOption>& EmptyRechunkOptions()
{
    static const std::vector<CompositionState::RechunkOption> kEmptyRechunkOptions;
    return kEmptyRechunkOptions;
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

int FindSegmentIndexByStart(const std::vector<CompositionState::Segment>& segments, LONG start)
{
    for (size_t index = 0; index < segments.size(); ++index)
    {
        if (segments[index].start == start)
        {
            return static_cast<int>(index);
        }
    }

    return -1;
}

int FindSegmentIndexByEnd(const std::vector<CompositionState::Segment>& segments, LONG end)
{
    for (size_t index = 0; index < segments.size(); ++index)
    {
        if (segments[index].end == end)
        {
            return static_cast<int>(index);
        }
    }

    return -1;
}

std::wstring JoinSurfaceRange(
    const std::vector<BunsetsuConversion>& bunsetsu,
    size_t startIndex,
    size_t endIndexExclusive)
{
    std::wstring surface;
    for (size_t index = startIndex; index < endIndexExclusive; ++index)
    {
        surface += bunsetsu[index].surface;
    }
    return surface;
}

void AppendUniqueCandidateItem(
    std::vector<CompositionState::CandidateItem>* candidateItems,
    const CompositionState::CandidateItem& candidateItem)
{
    if (candidateItem.displayText.empty() || candidateItems == nullptr)
    {
        return;
    }

    for (const CompositionState::CandidateItem& existing : *candidateItems)
    {
        if (existing.startSegmentIndex == candidateItem.startSegmentIndex &&
            existing.endSegmentIndex == candidateItem.endSegmentIndex &&
            existing.displayText == candidateItem.displayText)
        {
            return;
        }
    }

    candidateItems->push_back(candidateItem);
}

bool HasDifferentSegmentation(
    const std::vector<CompositionState::Segment>& currentSegments,
    int replaceSegmentStart,
    int replaceSegmentEnd,
    const std::vector<CompositionState::Segment>& replacementSegments)
{
    const size_t currentCount = static_cast<size_t>(replaceSegmentEnd - replaceSegmentStart + 1);
    if (currentCount != replacementSegments.size())
    {
        return true;
    }

    for (size_t index = 0; index < currentCount; ++index)
    {
        const CompositionState::Segment& current = currentSegments[static_cast<size_t>(replaceSegmentStart) + index];
        const CompositionState::Segment& replacement = replacementSegments[index];
        if (current.start != replacement.start || current.end != replacement.end)
        {
            return true;
        }
    }

    return false;
}

std::wstring BuildRechunkLabel(const std::vector<CompositionState::Segment>& segments)
{
    std::wstring label;
    for (size_t index = 0; index < segments.size(); ++index)
    {
        if (!label.empty())
        {
            label += L" / ";
        }

        label += segments[index].rawText;
    }

    return label;
}

std::wstring ToLowerAscii(const std::wstring& text)
{
    std::wstring result = text;
    for (wchar_t& ch : result)
    {
        ch = static_cast<wchar_t>(towlower(ch));
    }

    return result;
}

std::wstring ToUpperAscii(const std::wstring& text)
{
    std::wstring result = text;
    for (wchar_t& ch : result)
    {
        ch = static_cast<wchar_t>(towupper(ch));
    }

    return result;
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
    _liveConversionText.clear();
    _liveConversionCursor = 0;
    _liveConversionCandidates.clear();
    _liveConversionReadingCache.clear();
    _alphabeticPreeditActive = false;
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
    if (_phase == CompositionPhase::Converting ||
        _phase == CompositionPhase::CandidateSelecting ||
        _phase == CompositionPhase::RechunkSelecting)
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
    if (_phase == CompositionPhase::Converting ||
        _phase == CompositionPhase::CandidateSelecting ||
        _phase == CompositionPhase::RechunkSelecting)
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
    if (_phase == CompositionPhase::RechunkSelecting)
    {
        return !_conversionSession.rechunkOptions.empty();
    }

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
    return !_liveConversionText.empty() ? _liveConversionText : _preedit;
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
    return !_liveConversionText.empty() ? _liveConversionCursor : _preeditCursor;
}

void CompositionState::SetPhase(CompositionPhase phase)
{
    _phase = phase;
}

CompositionPhase CompositionState::GetPhase() const
{
    return _phase;
}

void CompositionState::RefreshLiveConversionPreview(
    const KanaKanjiConverter& kanaKanjiConverter,
    InputMode mode,
    const RomajiKanaConverter& converter,
    bool enabled)
{
    if (!enabled ||
        _phase != CompositionPhase::Composing ||
        _rawInput.empty() ||
        (_caretPosition + 1) < static_cast<LONG>(_preedit.size()) ||
        mode != InputMode::Hiragana ||
        _alphabeticPreeditActive)
    {
        ClearLiveConversionPreview();
        SyncLegacyBuffer();
        return;
    }

    if (_liveConversionReadingCache == _reading && !_liveConversionText.empty())
    {
        SyncLegacyBuffer();
        return;
    }

    const ConversionResult result = kanaKanjiConverter.Convert(_reading);
    _liveConversionCandidates = result.candidates;
    _liveConversionReadingCache = _reading;
    if (_liveConversionCandidates.empty() || _liveConversionCandidates[0].surface.empty())
    {
        ClearLiveConversionPreview();
        SyncLegacyBuffer();
        return;
    }

    _liveConversionText = _liveConversionCandidates[0].surface;
    _liveConversionCursor = static_cast<LONG>(_liveConversionText.size());
    SyncLegacyBuffer();
}

bool CompositionState::HasLiveConversionPreview() const
{
    return !_liveConversionText.empty();
}

bool CompositionState::HasLiveConversionPreviewForCurrentReading() const
{
    return !_liveConversionText.empty() && _liveConversionReadingCache == _reading;
}

void CompositionState::ApplyLiveConversionPreview(
    const std::wstring& reading,
    const std::vector<ConversionCandidate>& candidates)
{
    if (_phase != CompositionPhase::Composing ||
        _alphabeticPreeditActive ||
        reading != _reading ||
        candidates.empty() ||
        candidates[0].surface.empty())
    {
        ClearLiveConversionPreview();
        SyncLegacyBuffer();
        return;
    }

    _liveConversionReadingCache = reading;
    _liveConversionCandidates = candidates;
    _liveConversionText = candidates[0].surface;
    _liveConversionCursor = static_cast<LONG>(_liveConversionText.size());
    SyncLegacyBuffer();
}

void CompositionState::ClearLiveConversionPreviewState()
{
    ClearLiveConversionPreview();
    SyncLegacyBuffer();
}

void CompositionState::SetAlphabeticPreeditActive(bool active)
{
    _alphabeticPreeditActive = active;
    if (active)
    {
        ClearLiveConversionPreview();
    }
    SyncLegacyBuffer();
}

bool CompositionState::IsAlphabeticPreeditActive() const
{
    return _alphabeticPreeditActive;
}

const std::vector<std::wstring>& CompositionState::GetCandidates() const
{
    if (_phase == CompositionPhase::RechunkSelecting)
    {
        return !_conversionSession.rechunkLabels.empty()
            ? _conversionSession.rechunkLabels
            : EmptyCandidates();
    }

    const Segment* segment = GetFocusedSegment();
    return (segment != nullptr) ? segment->candidates : EmptyCandidates();
}

const std::vector<ConversionCandidate>& CompositionState::GetConversionCandidates() const
{
    return !_conversionSession.candidatePaths.empty()
        ? _conversionSession.candidatePaths
        : EmptyConversionCandidates();
}

const std::vector<CompositionState::CandidateItem>& CompositionState::GetCandidateItems() const
{
    return !_conversionSession.candidateItems.empty()
        ? _conversionSession.candidateItems
        : EmptyCandidateItems();
}

const std::vector<CompositionState::RechunkOption>& CompositionState::GetRechunkOptions() const
{
    return !_conversionSession.rechunkOptions.empty()
        ? _conversionSession.rechunkOptions
        : EmptyRechunkOptions();
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

    if (_alphabeticPreeditActive)
    {
        const std::wstring halfMixed = _rawInput;
        const std::wstring fullMixed = ToFullwidth(_rawInput);
        const std::wstring halfLower = ToLowerAscii(_rawInput);
        const std::wstring fullLower = ToFullwidth(halfLower);
        const std::wstring halfUpper = ToUpperAscii(_rawInput);
        const std::wstring fullUpper = ToFullwidth(halfUpper);

        Segment segment;
        segment.start = 0;
        segment.end = static_cast<LONG>(_rawInput.size());
        segment.rawText = _rawInput;
        AppendUniqueCandidate(&segment.candidates, halfMixed);
        AppendUniqueCandidate(&segment.candidates, fullMixed);
        AppendUniqueCandidate(&segment.candidates, halfLower);
        AppendUniqueCandidate(&segment.candidates, fullLower);
        AppendUniqueCandidate(&segment.candidates, halfUpper);
        AppendUniqueCandidate(&segment.candidates, fullUpper);
        segment.selectedCandidateIndex = 0;
        segment.currentDisplayText = segment.candidates[0];
        session.segments.push_back(segment);

        BunsetsuConversion bunsetsu;
        bunsetsu.start = 0;
        bunsetsu.length = static_cast<int>(_rawInput.size());
        bunsetsu.reading = _rawInput;
        bunsetsu.surface = _rawInput;
        bunsetsu.alternatives = segment.candidates;

        ConversionCandidate candidate;
        candidate.surface = halfMixed;
        candidate.reading = _rawInput;
        candidate.bunsetsu.push_back(bunsetsu);
        session.candidatePaths.push_back(candidate);

        CandidateItem candidateItem;
        candidateItem.start = 0;
        candidateItem.end = static_cast<LONG>(_rawInput.size());
        candidateItem.startSegmentIndex = 0;
        candidateItem.endSegmentIndex = 0;
        candidateItem.sourceCandidateIndex = 0;
        candidateItem.reading = _rawInput;
        for (const std::wstring& candidateText : segment.candidates)
        {
            candidateItem.displayText = candidateText;
            AppendUniqueCandidateItem(&session.candidateItems, candidateItem);
        }

        session.focusedSegmentIndex = -1;
        _conversionSession = session;
        _phase = CompositionPhase::Converting;
        UpdateBoundariesFromSegments();
        RebuildConversionDisplay();
        return true;
    }

    const ConversionResult result = kanaKanjiConverter.Convert(_reading);
    if (result.candidates.empty())
    {
        return false;
    }

    session.candidatePaths = result.candidates;

    const ConversionCandidate& candidate = session.candidatePaths[0];

    for (const BunsetsuConversion& bunsetsu : candidate.bunsetsu)
    {
        if (bunsetsu.length <= 0)
        {
            continue;
        }

        Segment segment = BuildSegmentFromBunsetsu(bunsetsu, converter);
        if (segment.start >= segment.end)
        {
            continue;
        }

        session.segments.push_back(segment);
    }

    if (session.segments.empty())
    {
        return false;
    }

    for (size_t pathIndex = 0; pathIndex < session.candidatePaths.size(); ++pathIndex)
    {
        const ConversionCandidate& pathCandidate = session.candidatePaths[pathIndex];
        const std::vector<BunsetsuConversion>& bunsetsu = pathCandidate.bunsetsu;

        for (size_t startIndex = 0; startIndex < bunsetsu.size(); ++startIndex)
        {
            LONG start = static_cast<LONG>(bunsetsu[startIndex].start);
            LONG end = start;

            for (size_t endIndex = startIndex; endIndex < bunsetsu.size(); ++endIndex)
            {
                end = static_cast<LONG>(bunsetsu[endIndex].start + bunsetsu[endIndex].length);
                const int startSegmentIndex = FindSegmentIndexByStart(session.segments, start);
                const int endSegmentIndex = FindSegmentIndexByEnd(session.segments, end);
                if (startSegmentIndex < 0 || endSegmentIndex < 0 || startSegmentIndex > endSegmentIndex)
                {
                    continue;
                }

                CandidateItem candidateItem;
                candidateItem.start = start;
                candidateItem.end = end;
                candidateItem.startSegmentIndex = startSegmentIndex;
                candidateItem.endSegmentIndex = endSegmentIndex;
                candidateItem.sourceCandidateIndex = static_cast<int>(pathIndex);
                candidateItem.reading = _reading.substr(
                    static_cast<size_t>(start),
                    static_cast<size_t>(end - start));
                candidateItem.displayText = JoinSurfaceRange(bunsetsu, startIndex, endIndex + 1);
                AppendUniqueCandidateItem(&session.candidateItems, candidateItem);

                if (startSegmentIndex == endSegmentIndex)
                {
                    AppendUniqueCandidate(&session.segments[static_cast<size_t>(startSegmentIndex)].candidates, candidateItem.displayText);
                }
            }
        }
    }

    session.focusedSegmentIndex = -1;
    _conversionSession = session;
    _phase = CompositionPhase::Converting;
    UpdateBoundariesFromSegments();
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

bool CompositionState::BeginRechunkSelection(const RomajiKanaConverter& converter)
{
    if (!HasFocusedSegment())
    {
        return false;
    }

    const std::vector<RechunkOption> options = BuildRechunkOptionsForFocusedSegment(converter);
    if (options.empty())
    {
        return false;
    }

    _conversionSession.rechunkOptions = options;
    _conversionSession.rechunkLabels.clear();
    _conversionSession.rechunkLabels.reserve(options.size());
    for (const RechunkOption& option : options)
    {
        _conversionSession.rechunkLabels.push_back(option.label);
    }

    _phase = CompositionPhase::RechunkSelecting;
    UpdateRechunkSelection(0);
    RebuildConversionDisplay();
    return true;
}

bool CompositionState::SelectNextCandidate()
{
    if (_phase == CompositionPhase::RechunkSelecting)
    {
        if (_conversionSession.rechunkOptions.empty())
        {
            return false;
        }

        int nextIndex = _conversionSession.selectedRechunkOptionIndex + 1;
        if (nextIndex >= static_cast<int>(_conversionSession.rechunkOptions.size()))
        {
            nextIndex = 0;
        }

        UpdateRechunkSelection(nextIndex);
        RebuildConversionDisplay();
        return true;
    }

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
    if (_phase == CompositionPhase::RechunkSelecting)
    {
        if (_conversionSession.rechunkOptions.empty())
        {
            return false;
        }

        int nextIndex = _conversionSession.selectedRechunkOptionIndex - 1;
        if (nextIndex < 0)
        {
            nextIndex = static_cast<int>(_conversionSession.rechunkOptions.size()) - 1;
        }

        UpdateRechunkSelection(nextIndex);
        RebuildConversionDisplay();
        return true;
    }

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
    if (_phase == CompositionPhase::RechunkSelecting)
    {
        if (_conversionSession.rechunkOptions.empty())
        {
            return false;
        }

        UpdateRechunkSelection(0);
        RebuildConversionDisplay();
        return true;
    }

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
    if (_phase == CompositionPhase::RechunkSelecting)
    {
        if (_conversionSession.rechunkOptions.empty())
        {
            return false;
        }

        UpdateRechunkSelection(static_cast<int>(_conversionSession.rechunkOptions.size()) - 1);
        RebuildConversionDisplay();
        return true;
    }

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

bool CompositionState::SelectCandidatePage(int delta, int pageSize)
{
    if (pageSize <= 0 || !HasCandidates())
    {
        return false;
    }

    const int candidateCount = static_cast<int>(GetCandidates().size());
    if (candidateCount <= 0)
    {
        return false;
    }

    int selectedIndex = GetSelectedCandidateIndex();
    if (selectedIndex < 0 || selectedIndex >= candidateCount)
    {
        selectedIndex = 0;
    }

    const int pageCount = (candidateCount + pageSize - 1) / pageSize;
    const int rowIndex = selectedIndex % pageSize;
    int pageIndex = selectedIndex / pageSize;
    pageIndex += delta;
    while (pageIndex < 0)
    {
        pageIndex += pageCount;
    }
    while (pageIndex >= pageCount)
    {
        pageIndex -= pageCount;
    }

    int targetIndex = pageIndex * pageSize + rowIndex;
    if (targetIndex >= candidateCount)
    {
        targetIndex = candidateCount - 1;
    }

    SetSelectedCandidateIndex(targetIndex);
    return true;
}

bool CompositionState::MoveFocusLeft()
{
    if (_phase == CompositionPhase::RechunkSelecting)
    {
        return SelectPrevCandidate();
    }

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
    if (_phase == CompositionPhase::RechunkSelecting)
    {
        return SelectNextCandidate();
    }

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

bool CompositionState::ApplySelectedRechunkOption()
{
    RechunkOption* option = GetSelectedRechunkOption();
    if (option == nullptr)
    {
        return false;
    }

    if (option->replaceSegmentStart < 0 ||
        option->replaceSegmentEnd < option->replaceSegmentStart ||
        option->replaceSegmentEnd >= static_cast<int>(_conversionSession.segments.size()))
    {
        return false;
    }

    std::vector<Segment>& segments = _conversionSession.segments;
    const std::vector<Segment>::iterator eraseBegin = segments.begin() + option->replaceSegmentStart;
    const std::vector<Segment>::iterator eraseEnd = segments.begin() + option->replaceSegmentEnd + 1;
    segments.erase(eraseBegin, eraseEnd);
    segments.insert(
        segments.begin() + option->replaceSegmentStart,
        option->replacementSegments.begin(),
        option->replacementSegments.end());

    _conversionSession.focusedSegmentIndex = option->replaceSegmentStart;
    _conversionSession.rechunkOptions.clear();
    _conversionSession.rechunkLabels.clear();
    _conversionSession.selectedRechunkOptionIndex = -1;
    _phase = CompositionPhase::CandidateSelecting;
    UpdateBoundariesFromSegments();
    RebuildConversionDisplay();
    return true;
}

bool CompositionState::CancelRechunkSelection()
{
    if (_phase != CompositionPhase::RechunkSelecting)
    {
        return false;
    }

    _conversionSession.rechunkOptions.clear();
    _conversionSession.rechunkLabels.clear();
    _conversionSession.selectedRechunkOptionIndex = -1;
    _phase = CompositionPhase::CandidateSelecting;
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
    if (_phase == CompositionPhase::RechunkSelecting)
    {
        return !_conversionSession.rechunkOptions.empty();
    }

    return !_conversionSession.segments.empty();
}

const std::wstring& CompositionState::GetSelectedCandidate() const
{
    const RechunkOption* option = GetSelectedRechunkOption();
    if (option != nullptr)
    {
        return option->label;
    }

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
    if (_phase == CompositionPhase::RechunkSelecting)
    {
        return _conversionSession.selectedRechunkOptionIndex;
    }

    const Segment* segment = GetFocusedSegment();
    return (segment != nullptr) ? segment->selectedCandidateIndex : -1;
}

void CompositionState::SetSelectedCandidateIndex(int index)
{
    if (_phase == CompositionPhase::RechunkSelecting)
    {
        UpdateRechunkSelection(index);
        RebuildConversionDisplay();
        return;
    }

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
    ClearLiveConversionPreview();

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
    LONG focusCursor = -1;
    ClearLiveConversionPreview();

    for (size_t i = 0; i < _conversionSession.segments.size(); ++i)
    {
        converted += _conversionSession.segments[i].currentDisplayText;
        if (static_cast<int>(i) == _conversionSession.focusedSegmentIndex)
        {
            focusCursor = static_cast<LONG>(converted.size());
        }
    }

    if (focusCursor < 0)
    {
        focusCursor = static_cast<LONG>(converted.size());
    }

    _preedit = converted;
    _preeditCursor = focusCursor;
    SyncLegacyBuffer();
}

void CompositionState::ResetConversionSession()
{
    _conversionSession.segments.clear();
    _conversionSession.candidatePaths.clear();
    _conversionSession.candidateItems.clear();
    _conversionSession.rechunkOptions.clear();
    _conversionSession.rechunkLabels.clear();
    _conversionSession.selectedRechunkOptionIndex = -1;
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

CompositionState::RechunkOption* CompositionState::GetSelectedRechunkOption()
{
    if (_conversionSession.selectedRechunkOptionIndex < 0 ||
        _conversionSession.selectedRechunkOptionIndex >= static_cast<int>(_conversionSession.rechunkOptions.size()))
    {
        return nullptr;
    }

    return &_conversionSession.rechunkOptions[_conversionSession.selectedRechunkOptionIndex];
}

const CompositionState::RechunkOption* CompositionState::GetSelectedRechunkOption() const
{
    if (_conversionSession.selectedRechunkOptionIndex < 0 ||
        _conversionSession.selectedRechunkOptionIndex >= static_cast<int>(_conversionSession.rechunkOptions.size()))
    {
        return nullptr;
    }

    return &_conversionSession.rechunkOptions[_conversionSession.selectedRechunkOptionIndex];
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

void CompositionState::UpdateRechunkSelection(int index)
{
    if (_conversionSession.rechunkOptions.empty())
    {
        _conversionSession.selectedRechunkOptionIndex = -1;
        return;
    }

    if (index < 0)
    {
        index = 0;
    }
    if (index >= static_cast<int>(_conversionSession.rechunkOptions.size()))
    {
        index = static_cast<int>(_conversionSession.rechunkOptions.size()) - 1;
    }

    _conversionSession.selectedRechunkOptionIndex = index;
}

void CompositionState::UpdateBoundariesFromSegments()
{
    _boundaries.clear();
    for (size_t index = 0; index + 1 < _conversionSession.segments.size(); ++index)
    {
        _boundaries.push_back(_conversionSession.segments[index].end);
    }
}

std::vector<CompositionState::RechunkOption> CompositionState::BuildRechunkOptionsForFocusedSegment(
    const RomajiKanaConverter& converter) const
{
    std::vector<RechunkOption> options;
    if (!HasFocusedSegment())
    {
        return options;
    }

    const int focusedIndex = _conversionSession.focusedSegmentIndex;
    const int segmentCount = static_cast<int>(_conversionSession.segments.size());
    for (int spanLength = 1; spanLength <= segmentCount; ++spanLength)
    {
        for (int startIndex = 0; startIndex + spanLength <= segmentCount; ++startIndex)
        {
            const int endIndex = startIndex + spanLength - 1;
            if (focusedIndex < startIndex || focusedIndex > endIndex)
            {
                continue;
            }

            bool containsCommitted = false;
            for (int index = startIndex; index <= endIndex; ++index)
            {
                if (_conversionSession.segments[index].isCommitted)
                {
                    containsCommitted = true;
                    break;
                }
            }
            if (containsCommitted)
            {
                continue;
            }

            const LONG readingStart = _conversionSession.segments[startIndex].start;
            const LONG readingEnd = _conversionSession.segments[endIndex].end;
            for (size_t candidateIndex = 0; candidateIndex < _conversionSession.candidatePaths.size(); ++candidateIndex)
            {
                const std::vector<BunsetsuConversion>& bunsetsu =
                    _conversionSession.candidatePaths[candidateIndex].bunsetsu;
                if (bunsetsu.empty())
                {
                    continue;
                }

                size_t matchStart = 0;
                while (matchStart < bunsetsu.size() &&
                    static_cast<LONG>(bunsetsu[matchStart].start) != readingStart)
                {
                    ++matchStart;
                }
                if (matchStart >= bunsetsu.size())
                {
                    continue;
                }

                LONG coveredEnd = readingStart;
                size_t matchEnd = matchStart;
                while (matchEnd < bunsetsu.size() && coveredEnd < readingEnd)
                {
                    coveredEnd = static_cast<LONG>(bunsetsu[matchEnd].start + bunsetsu[matchEnd].length);
                    ++matchEnd;
                }
                if (coveredEnd != readingEnd)
                {
                    continue;
                }

                std::vector<Segment> replacementSegments;
                replacementSegments.reserve(matchEnd - matchStart);
                for (size_t bunsetsuIndex = matchStart; bunsetsuIndex < matchEnd; ++bunsetsuIndex)
                {
                    Segment replacement = BuildSegmentFromBunsetsu(bunsetsu[bunsetsuIndex], converter);
                    replacement.isCommitted = false;
                    replacementSegments.push_back(replacement);
                }

                if (!HasDifferentSegmentation(
                    _conversionSession.segments,
                    startIndex,
                    endIndex,
                    replacementSegments))
                {
                    continue;
                }

                RechunkOption option;
                option.readingStart = readingStart;
                option.readingEnd = readingEnd;
                option.replaceSegmentStart = startIndex;
                option.replaceSegmentEnd = endIndex;
                option.sourceCandidateIndex = static_cast<int>(candidateIndex);
                option.reading = _reading.substr(
                    static_cast<size_t>(readingStart),
                    static_cast<size_t>(readingEnd - readingStart));
                option.label = BuildRechunkLabel(replacementSegments);
                option.surfaceText = JoinSurfaceRange(bunsetsu, matchStart, matchEnd);
                option.replacementSegments = replacementSegments;

                bool duplicated = false;
                for (const RechunkOption& existing : options)
                {
                    if (existing.replaceSegmentStart == option.replaceSegmentStart &&
                        existing.replaceSegmentEnd == option.replaceSegmentEnd &&
                        existing.label == option.label)
                    {
                        duplicated = true;
                        break;
                    }
                }

                if (!duplicated)
                {
                    options.push_back(option);
                }
            }
        }

        if (!options.empty())
        {
            break;
        }
    }

    return options;
}

CompositionState::Segment CompositionState::BuildSegmentFromBunsetsu(
    const BunsetsuConversion& bunsetsu,
    const RomajiKanaConverter& converter) const
{
    Segment segment;
    segment.start = static_cast<LONG>(bunsetsu.start);
    segment.end = static_cast<LONG>(bunsetsu.start + bunsetsu.length);
    segment.rawText = bunsetsu.reading;

    if (segment.start < 0 || segment.end > static_cast<LONG>(_reading.size()) || segment.start >= segment.end)
    {
        return segment;
    }

    const LONG rawStart = RawCursorFromVisibleCursor(_rawInput, segment.start, InputMode::Hiragana, converter);
    const LONG rawEnd = RawCursorFromVisibleCursor(_rawInput, segment.end, InputMode::Hiragana, converter);
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

    return segment;
}

void CompositionState::SyncLegacyBuffer()
{
    _legacyBuffer.SetRaw(_rawInput, _rawCursor);
    _legacyBuffer.SetSurface(_preedit, _preeditCursor);
    _legacyBuffer.EnableLiveConversion(!_liveConversionText.empty());
    if (_liveConversionText.empty())
    {
        _legacyBuffer.ClearLiveConversionText();
    }
    else
    {
        _legacyBuffer.SetLiveConversionText(_liveConversionText);
    }
}

void CompositionState::ClearLiveConversionPreview()
{
    _liveConversionText.clear();
    _liveConversionCursor = 0;
    _liveConversionCandidates.clear();
    _liveConversionReadingCache.clear();
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
