#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "ILexicon.h"

class MozcSystemLexicon : public ILexicon
{
public:
    explicit MozcSystemLexicon(const std::filesystem::path& directory);

    virtual DictionaryKind GetKind() const override;
    virtual void LookupPrefix(const std::wstring& reading, size_t start, std::vector<LexiconEntry>* out) const override;
    virtual void LookupExact(const std::wstring& reading, std::vector<LexiconEntry>* out) const override;

    bool IsLoaded() const;
    const std::filesystem::path& GetSourceDirectory() const;

private:
    void LoadDirectory(const std::filesystem::path& directory);

    struct Impl;
    std::unique_ptr<Impl> _impl;
};
