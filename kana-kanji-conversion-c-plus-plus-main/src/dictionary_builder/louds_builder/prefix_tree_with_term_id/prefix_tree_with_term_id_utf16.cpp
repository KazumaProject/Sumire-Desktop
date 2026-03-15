#include "prefix_tree_with_term_id/prefix_tree_with_term_id_utf16.hpp"

PrefixTreeWithTermIdUtf16::PrefixTreeWithTermIdUtf16()
    : root_(std::make_unique<PrefixNodeWithTermIdUtf16>())
{
}

void PrefixTreeWithTermIdUtf16::insert(const std::u16string &word, int32_t termId)
{
    PrefixNodeWithTermIdUtf16 *cur = root_.get();
    for (char16_t c : word)
    {
        auto it = cur->children.find(c);
        if (it == cur->children.end())
        {
            auto node = std::make_unique<PrefixNodeWithTermIdUtf16>();
            PrefixNodeWithTermIdUtf16 *raw = node.get();
            cur->children.emplace(c, std::move(node));
            cur = raw;
        }
        else
        {
            cur = it->second.get();
        }
    }

    cur->isWord = true;
    cur->termId = termId;
}
