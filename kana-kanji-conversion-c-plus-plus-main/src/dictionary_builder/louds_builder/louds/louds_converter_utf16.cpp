#include "louds/louds_converter_utf16.hpp"
#include <queue>
#include <algorithm>
#include <vector>

LOUDSUtf16 ConverterUtf16::convert(const PrefixNodeUtf16 *rootNode) const
{
    LOUDSUtf16 louds;
    std::queue<const PrefixNodeUtf16 *> q;
    q.push(rootNode);

    while (!q.empty())
    {
        const PrefixNodeUtf16 *node = q.front();
        q.pop();

        if (node && node->hasChild())
        {
            std::vector<std::pair<char16_t, const PrefixNodeUtf16 *>> ordered;
            ordered.reserve(node->children.size());
            for (const auto &kv : node->children)
                ordered.emplace_back(kv.first, kv.second.get());

            std::sort(ordered.begin(), ordered.end(),
                      [](const auto &a, const auto &b)
                      { return a.first < b.first; });

            for (const auto &kv : ordered)
            {
                const char16_t label = kv.first;
                const PrefixNodeUtf16 *child = kv.second;

                q.push(child);
                louds.LBSTemp.push_back(true);
                louds.labels.push_back(label);
                louds.isLeafTemp.push_back(child->isWord);
            }
        }

        louds.LBSTemp.push_back(false);
        louds.isLeafTemp.push_back(false);
    }

    louds.convertListToBitVector();
    return louds;
}
