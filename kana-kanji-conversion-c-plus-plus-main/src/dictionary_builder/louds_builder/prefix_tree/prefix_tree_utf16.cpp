#include "prefix_tree/prefix_tree_utf16.hpp"

PrefixTreeUtf16::PrefixTreeUtf16()
    : root(std::make_unique<PrefixNodeUtf16>()),
      nextId(1)
{
}

void PrefixTreeUtf16::insert(const std::u16string &word)
{
    PrefixNodeUtf16 *cur = root.get();
    for (char16_t ch : word)
    {
        if (auto *nxt = cur->getChild(ch))
        {
            cur = nxt;
        }
        else
        {
            int id = ++nextId;
            auto node = std::make_unique<PrefixNodeUtf16>();
            node->c = ch;
            node->id = id;

            PrefixNodeUtf16 *raw = node.get();
            cur->addChild(std::move(node));
            cur = raw;
        }
    }
    cur->isWord = true;
}

PrefixNodeUtf16 *PrefixTreeUtf16::getRoot() { return root.get(); }
const PrefixNodeUtf16 *PrefixTreeUtf16::getRoot() const { return root.get(); }
