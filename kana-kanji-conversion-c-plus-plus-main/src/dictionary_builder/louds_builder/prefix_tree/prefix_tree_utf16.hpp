#pragma once
#include <unordered_map>
#include <memory>
#include <string>
#include <atomic>
#include <cstdint>

struct PrefixNodeUtf16
{
    char16_t c{u' '};
    int id{-1};
    bool isWord{false};

    std::unordered_map<char16_t, std::unique_ptr<PrefixNodeUtf16>> children;

    bool hasChild() const { return !children.empty(); }

    PrefixNodeUtf16 *getChild(char16_t ch)
    {
        auto it = children.find(ch);
        return (it == children.end()) ? nullptr : it->second.get();
    }

    const PrefixNodeUtf16 *getChild(char16_t ch) const
    {
        auto it = children.find(ch);
        return (it == children.end()) ? nullptr : it->second.get();
    }

    void addChild(std::unique_ptr<PrefixNodeUtf16> node)
    {
        const char16_t ch = node->c;
        if (children.find(ch) == children.end())
        {
            children.emplace(ch, std::move(node));
        }
    }
};

class PrefixTreeUtf16
{
public:
    PrefixTreeUtf16();

    void insert(const std::u16string &word);

    PrefixNodeUtf16 *getRoot();
    const PrefixNodeUtf16 *getRoot() const;

private:
    std::unique_ptr<PrefixNodeUtf16> root;
    std::atomic<int> nextId;
};
