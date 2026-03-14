#include "LexiconRegistry.h"

#include <algorithm>

#include "ILexicon.h"

void LexiconRegistry::AddLexicon(const std::shared_ptr<ILexicon>& lexicon)
{
    if (!lexicon)
    {
        return;
    }

    _lexicons.push_back(lexicon);
}

void LexiconRegistry::LookupPrefix(const std::wstring& reading, size_t start, std::vector<LexiconEntry>* out) const
{
    if (out == nullptr)
    {
        return;
    }

    out->clear();

    for (const std::shared_ptr<ILexicon>& lexicon : _lexicons)
    {
        std::vector<LexiconEntry> entries;
        lexicon->LookupPrefix(reading, start, &entries);
        AppendUnique(out, entries);
    }

    std::sort(
        out->begin(),
        out->end(),
        [](const LexiconEntry& lhs, const LexiconEntry& rhs)
        {
            if (lhs.wordCost != rhs.wordCost)
            {
                return lhs.wordCost < rhs.wordCost;
            }

            if (lhs.reading != rhs.reading)
            {
                return lhs.reading < rhs.reading;
            }

            return lhs.surface < rhs.surface;
        });
}

void LexiconRegistry::LookupExact(const std::wstring& reading, std::vector<LexiconEntry>* out) const
{
    if (out == nullptr)
    {
        return;
    }

    out->clear();

    for (const std::shared_ptr<ILexicon>& lexicon : _lexicons)
    {
        std::vector<LexiconEntry> entries;
        lexicon->LookupExact(reading, &entries);
        AppendUnique(out, entries);
    }

    std::sort(
        out->begin(),
        out->end(),
        [](const LexiconEntry& lhs, const LexiconEntry& rhs)
        {
            if (lhs.wordCost != rhs.wordCost)
            {
                return lhs.wordCost < rhs.wordCost;
            }

            return lhs.surface < rhs.surface;
        });
}

void LexiconRegistry::AppendUnique(std::vector<LexiconEntry>* destination, const std::vector<LexiconEntry>& source)
{
    for (const LexiconEntry& entry : source)
    {
        bool merged = false;
        for (LexiconEntry& current : *destination)
        {
            if (IsSameEntry(current, entry))
            {
                if (entry.wordCost < current.wordCost)
                {
                    current = entry;
                }

                merged = true;
                break;
            }
        }

        if (!merged)
        {
            destination->push_back(entry);
        }
    }
}

bool LexiconRegistry::IsSameEntry(const LexiconEntry& lhs, const LexiconEntry& rhs)
{
    return lhs.reading == rhs.reading &&
        lhs.surface == rhs.surface &&
        lhs.dictionaryKind == rhs.dictionaryKind &&
        lhs.leftId == rhs.leftId &&
        lhs.rightId == rhs.rightId &&
        lhs.posId == rhs.posId;
}
