#pragma once

#include <memory>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ConversionTypes.h"

class ILexicon;
class LexiconRegistry;
class ZenzClient;

class KanaKanjiConverter
{
public:
    struct ConvertOptions
    {
        bool useZenz = true;
        bool zenzOnly = false;
    };

    KanaKanjiConverter();

    void AddLexicon(const std::shared_ptr<ILexicon>& lexicon);
    ConversionResult Convert(const std::wstring& reading) const;
    ConversionResult Convert(const std::wstring& reading, const ConvertOptions& options) const;
    ConversionResult Convert(
        const std::wstring& reading,
        const std::function<bool()>& shouldCancel) const;
    ConversionResult Convert(
        const std::wstring& reading,
        const std::function<bool()>& shouldCancel,
        const ConvertOptions& options) const;
    bool IsZenzEnabled() const;

private:
    std::shared_ptr<LexiconRegistry> _lexicons;
    std::shared_ptr<ZenzClient> _zenzClient;
    std::vector<std::int16_t> _connectionMatrix;
    int _connectionDimension = 0;
};
