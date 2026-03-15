#pragma once

#include "louds_with_term_id/louds_with_term_id_utf16.hpp"
#include "prefix_tree_with_term_id/prefix_tree_with_term_id_utf16.hpp"

class ConverterWithTermIdUtf16
{
public:
    LOUDSWithTermIdUtf16 convert(const PrefixNodeWithTermIdUtf16 *rootNode) const;
};
