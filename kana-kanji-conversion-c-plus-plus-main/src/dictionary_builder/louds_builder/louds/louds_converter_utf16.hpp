#pragma once
#include "prefix_tree/prefix_tree_utf16.hpp"
#include "louds/louds_utf16_writer.hpp"

class ConverterUtf16
{
public:
    LOUDSUtf16 convert(const PrefixNodeUtf16 *rootNode) const;
};
