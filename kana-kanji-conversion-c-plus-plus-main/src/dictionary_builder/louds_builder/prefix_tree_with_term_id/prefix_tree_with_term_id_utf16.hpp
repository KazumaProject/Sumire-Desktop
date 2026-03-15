#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>

// UTF-16 prefix tree node that can carry a terminal termId.
//
// Kotlin side uses a "PrefixTreeWithTermId" to map an input yomi string to a
// dense "termId" (0..N-1) based on the sorted yomi key order.
// This C++ version stores the termId at terminal nodes.

struct PrefixNodeWithTermIdUtf16
{
    std::map<char16_t, std::unique_ptr<PrefixNodeWithTermIdUtf16>> children;
    bool isWord = false;
    int32_t termId = -1; // valid only when isWord==true

    bool hasChild() const { return !children.empty(); }
};

class PrefixTreeWithTermIdUtf16
{
public:
    PrefixTreeWithTermIdUtf16();

    // Inserts a word. If this word is a terminal, sets/overwrites termId.
    void insert(const std::u16string &word, int32_t termId);

    const PrefixNodeWithTermIdUtf16 *root() const { return root_.get(); }
    PrefixNodeWithTermIdUtf16 *root() { return root_.get(); }

private:
    std::unique_ptr<PrefixNodeWithTermIdUtf16> root_;
};
