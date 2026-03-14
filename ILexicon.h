#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "ConversionTypes.h"

class ILexicon
{
public:
    virtual ~ILexicon()
    {
    }

    virtual DictionaryKind GetKind() const = 0;
    virtual void LookupPrefix(const std::wstring& reading, size_t start, std::vector<LexiconEntry>* out) const = 0;
    virtual void LookupExact(const std::wstring& reading, std::vector<LexiconEntry>* out) const = 0;
};
