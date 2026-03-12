#include "CompositionState.h"

#include "RomajiKanaConverter.h"
#include "TextService.h"

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
    _preeditCursor = 0;
    _candidates.clear();
    _selectedCandidateIndex = -1;
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
    _candidates.clear();
    _selectedCandidateIndex = -1;
    _phase = CompositionPhase::Composing;
    RebuildTexts(mode, converter);
}

bool CompositionState::Backspace(InputMode mode, const RomajiKanaConverter& converter)
{
    if (_rawInput.empty() || _rawCursor <= 0)
    {
        return false;
    }

    LONG eraseStart = _rawCursor - 1;
    if (mode == InputMode::Hiragana ||
        mode == InputMode::HalfwidthKatakana ||
        mode == InputMode::FullwidthKatakana)
    {
        const std::wstring rawPrefix = _rawInput.substr(0, _rawCursor);
        const std::wstring visiblePrefix = BuildPreeditText(rawPrefix, mode, converter);
        LONG visibleCursor = static_cast<LONG>(visiblePrefix.size());
        LONG targetVisibleCursor = (visibleCursor > 0) ? (visibleCursor - 1) : 0;
        eraseStart = RawCursorFromVisibleCursor(_rawInput, targetVisibleCursor, mode, converter);
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
    _candidates.clear();
    _selectedCandidateIndex = -1;
    _phase = _rawInput.empty() ? CompositionPhase::Idle : CompositionPhase::Composing;
    RebuildTexts(mode, converter);

    return true;
}

bool CompositionState::Delete(InputMode mode, const RomajiKanaConverter& converter)
{
    if (_rawInput.empty() || _rawCursor >= static_cast<LONG>(_rawInput.size()))
    {
        return false;
    }

    _rawInput.erase(_rawInput.begin() + _rawCursor);
    _candidates.clear();
    _selectedCandidateIndex = -1;
    _phase = _rawInput.empty() ? CompositionPhase::Idle : CompositionPhase::Composing;
    RebuildTexts(mode, converter);

    return true;
}

bool CompositionState::MoveLeft(InputMode mode, const RomajiKanaConverter& converter)
{
    if (_rawCursor <= 0)
    {
        _rawCursor = 0;
        RebuildTexts(mode, converter);
        return false;
    }

    --_rawCursor;
    RebuildTexts(mode, converter);
    return true;
}

bool CompositionState::MoveRight(InputMode mode, const RomajiKanaConverter& converter)
{
    LONG end = static_cast<LONG>(_rawInput.size());
    if (_rawCursor >= end)
    {
        _rawCursor = end;
        RebuildTexts(mode, converter);
        return false;
    }

    ++_rawCursor;
    RebuildTexts(mode, converter);
    return true;
}

bool CompositionState::Empty() const
{
    return _rawInput.empty();
}

bool CompositionState::HasCandidates() const
{
    return !_candidates.empty();
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

LONG CompositionState::GetRawCursor() const
{
    return _rawCursor;
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

std::vector<std::wstring>& CompositionState::MutableCandidates()
{
    return _candidates;
}

const std::vector<std::wstring>& CompositionState::GetCandidates() const
{
    return _candidates;
}

void CompositionState::StartConversion(const std::vector<std::wstring>& candidates)
{
    _candidates = candidates;
    if (_candidates.empty())
    {
        _selectedCandidateIndex = -1;
        return;
    }

    _selectedCandidateIndex = 0;
    _preedit = _candidates[0];
    _preeditCursor = static_cast<LONG>(_preedit.size());
    _phase = CompositionPhase::Converting;
    SyncLegacyBuffer();
}

void CompositionState::EnterCandidateSelecting()
{
    if (!_candidates.empty())
    {
        if (_candidates.size() >= 2)
        {
            _selectedCandidateIndex = 1;
        }
        else
        {
            _selectedCandidateIndex = 0;
        }

        _preedit = _candidates[_selectedCandidateIndex];
        _preeditCursor = static_cast<LONG>(_preedit.size());
        _phase = CompositionPhase::CandidateSelecting;
        SyncLegacyBuffer();
    }
}

bool CompositionState::SelectNextCandidate()
{
    if (_candidates.empty())
    {
        return false;
    }

    _selectedCandidateIndex = (_selectedCandidateIndex + 1) % static_cast<int>(_candidates.size());
    _preedit = _candidates[_selectedCandidateIndex];
    _preeditCursor = static_cast<LONG>(_preedit.size());
    _phase = CompositionPhase::CandidateSelecting;
    SyncLegacyBuffer();
    return true;
}

bool CompositionState::SelectPrevCandidate()
{
    if (_candidates.empty())
    {
        return false;
    }

    if (_selectedCandidateIndex <= 0)
    {
        _selectedCandidateIndex = static_cast<int>(_candidates.size()) - 1;
    }
    else
    {
        --_selectedCandidateIndex;
    }

    _preedit = _candidates[_selectedCandidateIndex];
    _preeditCursor = static_cast<LONG>(_preedit.size());
    _phase = CompositionPhase::CandidateSelecting;
    SyncLegacyBuffer();
    return true;
}

bool CompositionState::SelectFirstCandidate()
{
    if (_candidates.empty())
    {
        return false;
    }

    _selectedCandidateIndex = 0;
    _preedit = _candidates[_selectedCandidateIndex];
    _preeditCursor = static_cast<LONG>(_preedit.size());
    _phase = CompositionPhase::CandidateSelecting;
    SyncLegacyBuffer();
    return true;
}

bool CompositionState::SelectLastCandidate()
{
    if (_candidates.empty())
    {
        return false;
    }

    _selectedCandidateIndex = static_cast<int>(_candidates.size()) - 1;
    _preedit = _candidates[_selectedCandidateIndex];
    _preeditCursor = static_cast<LONG>(_preedit.size());
    _phase = CompositionPhase::CandidateSelecting;
    SyncLegacyBuffer();
    return true;
}

bool CompositionState::HasSelectedCandidate() const
{
    return _selectedCandidateIndex >= 0 &&
        _selectedCandidateIndex < static_cast<int>(_candidates.size());
}

const std::wstring& CompositionState::GetSelectedCandidate() const
{
    if (HasSelectedCandidate())
    {
        return _candidates[_selectedCandidateIndex];
    }

    return _preedit;
}

void CompositionState::CancelConversion(InputMode mode, const RomajiKanaConverter& converter)
{
    _candidates.clear();
    _selectedCandidateIndex = -1;
    _phase = _rawInput.empty() ? CompositionPhase::Idle : CompositionPhase::Composing;
    RebuildTexts(mode, converter);
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
    return _selectedCandidateIndex;
}

void CompositionState::SetSelectedCandidateIndex(int index)
{
    _selectedCandidateIndex = index;
}

void CompositionState::RebuildTexts(InputMode mode, const RomajiKanaConverter& converter)
{
    _reading = converter.ConvertFromRaw(_rawInput);
    _preedit = BuildPreeditText(_rawInput, mode, converter);

    std::wstring rawPrefix = _rawInput.substr(0, _rawCursor);
    std::wstring preeditPrefix = BuildPreeditText(rawPrefix, mode, converter);
    _preeditCursor = static_cast<LONG>(preeditPrefix.size());

    SyncLegacyBuffer();
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
    LONG rawLength = static_cast<LONG>(raw.size());
    for (LONG rawCursor = 0; rawCursor <= rawLength; ++rawCursor)
    {
        const std::wstring visiblePrefix = BuildPreeditText(raw.substr(0, rawCursor), mode, converter);
        LONG currentVisibleCursor = static_cast<LONG>(visiblePrefix.size());
        if (currentVisibleCursor <= visibleCursor)
        {
            bestRaw = rawCursor;
            continue;
        }

        break;
    }

    return bestRaw;
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
