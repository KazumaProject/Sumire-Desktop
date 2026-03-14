#pragma once

#include <memory>
#include <vector>

#include "ConversionTypes.h"

class ILexicon;

class LexiconRegistry
{
public:
    void AddLexicon(const std::shared_ptr<ILexicon>& lexicon);
    void LookupPrefix(const std::wstring& reading, size_t start, std::vector<LexiconEntry>* out) const;
    void LookupExact(const std::wstring& reading, std::vector<LexiconEntry>* out) const;

private:
    static void AppendUnique(std::vector<LexiconEntry>* destination, const std::vector<LexiconEntry>& source);
    static bool IsSameEntry(const LexiconEntry& lhs, const LexiconEntry& rhs);

    std::vector<std::shared_ptr<ILexicon>> _lexicons;
};
