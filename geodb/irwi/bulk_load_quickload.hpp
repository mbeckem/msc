#ifndef IRWI_BULK_LOAD_QUICKLOAD_HPP
#define IRWI_BULK_LOAD_QUICKLOAD_HPP

#include "geodb/common.hpp"
#include "geodb/irwi/base.hpp"
#include "geodb/irwi/tree_state.hpp"
#include "geodb/irwi/tree_insertion.hpp"
#include "geodb/irwi/tree_internal.hpp"

namespace geodb {

template<typename Value, typename Accessor, u32 fanout_internal, u32 fanout_leaf>
class quick_load_tree {
    using storage_spec = tree_internal<fanout_internal, fanout_leaf>;

    using value_type = Value;

    using state_type = tree_state<storage_spec, Value, Accessor, 1>;

    using storage_type = typename state_type::storage_type;

    using internal_ptr = typename state_type::internal_ptr;

    using leaf_ptr = typename state_type::leaf_ptr;

private:
    /// Maximum number of leaves until external buffers are used.
    size_t m_max_leaves = 0;

    /// Small IRWI-Tree in internal memory.
    /// Always has at most m_max_leaves leaf nodes.
    state_type m_state;

    /// Holds node paths when inserting new values.
    std::vector<internal_ptr> m_path_buf;

public:
    explicit quick_load_tree(size_t max_leaves, Accessor accessor, float weight = 0.5)
        : m_max_leaves(max_leaves)
        , m_state(storage_spec(), Accessor(), weight, std::move(accessor))
    {}

    void insert(const value_type& v) {
        tree_insertion<state_type> ins(state());

        std::vector<internal_ptr>& path = m_path_buf;
        leaf_ptr leaf = ins.find_leaf(path, v);
        if (get_count(leaf) < fanout_leaf || storage().get_leaf_count() < m_max_leaves) {

        }
    }

private:
    state_type& state() const { return m_state; }

    storage_type& storage() { return state().storage(); }

};

} // namespace geodb

#endif // IRWI_BULK_LOAD_QUICKLOAD_HPP
