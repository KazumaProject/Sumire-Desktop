#include "louds_with_term_id/louds_converter_with_term_id_utf16.hpp"

#include <algorithm>
#include <queue>
#include <utility>
#include <vector>

LOUDSWithTermIdUtf16 ConverterWithTermIdUtf16::convert(const PrefixNodeWithTermIdUtf16 *rootNode) const
{
    LOUDSWithTermIdUtf16 louds;

    std::queue<const PrefixNodeWithTermIdUtf16 *> q;
    q.push(rootNode);

    // The LOUDSWithTermIdUtf16 constructor already pushed the root termId (-1).
    // For each dequeued node (except root dummy already accounted for), we will
    // append exactly one termId entry, aligned with the 0-delimiter we emit.
    bool isFirst = true;

    while (!q.empty())
    {
        const PrefixNodeWithTermIdUtf16 *node = q.front();
        q.pop();

        if (!isFirst)
        {
            // nodeId increments in BFS order; push termId for this node.
            louds.termIdByNodeIdTemp.push_back(node && node->isWord ? node->termId : -1);
        }
        isFirst = false;

        if (node && node->hasChild())
        {
            std::vector<std::pair<char16_t, const PrefixNodeWithTermIdUtf16 *>> ordered;
            ordered.reserve(node->children.size());
            for (const auto &kv : node->children)
                ordered.emplace_back(kv.first, kv.second.get());

            std::sort(ordered.begin(), ordered.end(),
                      [](const auto &a, const auto &b)
                      { return a.first < b.first; });

            for (const auto &kv : ordered)
            {
                const char16_t label = kv.first;
                const PrefixNodeWithTermIdUtf16 *child = kv.second;

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
