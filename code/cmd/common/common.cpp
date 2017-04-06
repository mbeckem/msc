#include "common/common.hpp"

template class geodb::tree_external<block_size, leaf_fanout_override, internal_fanout_override>;
template class geodb::tree<external_storage, lambda>;
