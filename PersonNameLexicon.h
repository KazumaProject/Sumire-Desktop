#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "ILexicon.h"

class PersonNameLexicon : public ILexicon
{
public:
    explicit PersonNameLexicon(const std::filesystem::path& path);

    virtual DictionaryKind GetKind() const override;
    virtual void LookupPrefix(const std::wstring& reading, size_t start, std::vector<LexiconEntry>* out) const override;
    virtual void LookupExact(const std::wstring& reading, std::vector<LexiconEntry>* out) const override;

    bool IsLoaded() const;
    const std::filesystem::path& GetSourcePath() const;

private:
    void LoadFile(const std::filesystem::path& path);

    std::unordered_map<std::wstring, std::vector<LexiconEntry>> _entriesByReading;
    std::vector<size_t> _readingLengths;
    std::filesystem::path _sourcePath;
    bool _loaded = false;
};
