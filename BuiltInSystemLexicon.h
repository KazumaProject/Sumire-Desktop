#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "ILexicon.h"

class BuiltInSystemLexicon : public ILexicon
{
public:
    BuiltInSystemLexicon();

    virtual DictionaryKind GetKind() const override;
    virtual void LookupPrefix(const std::wstring& reading, size_t start, std::vector<LexiconEntry>* out) const override;
    virtual void LookupExact(const std::wstring& reading, std::vector<LexiconEntry>* out) const override;

private:
    void AppendEntries(const std::wstring& key, std::vector<LexiconEntry>* out) const;

    std::unordered_map<std::wstring, std::vector<std::wstring>> _dictionary;
};
