#pragma once

#include <memory>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ConversionTypes.h"

class ILexicon;
class LexiconRegistry;

class KanaKanjiConverter
{
public:
    KanaKanjiConverter();

    void AddLexicon(const std::shared_ptr<ILexicon>& lexicon);
    ConversionResult Convert(const std::wstring& reading) const;
    ConversionResult Convert(
        const std::wstring& reading,
        const std::function<bool()>& shouldCancel) const;

private:
    std::shared_ptr<LexiconRegistry> _lexicons;
    std::vector<std::int16_t> _connectionMatrix;
    int _connectionDimension = 0;
};
