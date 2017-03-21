#ifndef GEODB_IRWI_BASE_HPP
#define GEODB_IRWI_BASE_HPP

#include "geodb/common.hpp"
#include "geodb/vector.hpp"
#include "geodb/bounding_box.hpp"
#include "geodb/trajectory.hpp"

namespace geodb {

template<typename StorageSpec, u32 Lambda>
class tree;

template<typename StorageSpec, typename Value, typename Accessor, u32 Lambda>
class tree_state;

template<typename State>
class tree_cursor;

template<typename StorageSpec, u32 Lambda>
class inverted_index;

template<u32 Lambda>
class postings_list_summary;

template<typename StorageSpec, u32 Lambda>
class postings_list;

template<u32 Lambda>
class posting;

template<typename StorageSpec>
class string_map;

template<typename State, typename Derived>
class bulk_load_common;

/// IRWI leaf entries represent trajectory units.
struct tree_entry {
    trajectory_id_type trajectory_id = 0;   ///< Index of the trajectory this unit belongs to.
    u32 unit_index = 0;                     ///< Index of this unit within the trajectory.
    trajectory_unit unit;

    tree_entry() = default;

    tree_entry(trajectory_id_type trajectory_id, u32 unit_index, const trajectory_unit& unit)
        : trajectory_id(trajectory_id)
        , unit_index(unit_index)
        , unit(unit)
    {}
};

inline bool operator==(const tree_entry& a, const tree_entry& b) {
    return a.trajectory_id == b.trajectory_id
            && a.unit_index == b.unit_index
            && a.unit == b.unit;
}

inline bool operator!=(const tree_entry& a, const tree_entry& b) {
    return !(a == b);
}

static_assert(std::is_trivially_copyable<tree_entry>::value, "Must be trivially copyable");

namespace detail {

struct tree_entry_accessor {
    struct label_count {
        static const constexpr u64 count = 1;

        label_type label;
    };

    trajectory_id_type get_id(const tree_entry& e) const {
        return e.trajectory_id;
    }

    bounding_box get_mbb(const tree_entry& e) const {
        return e.unit.get_bounding_box();
    }

    constexpr u64 get_total_count(const tree_entry& e) const {
        (void) e;
        return 1;
    }

    std::array<label_count, 1> get_label_counts(const tree_entry& e) const {
        return {{ e.unit.label }};
    }
};

} // namespace detail

} // namespace geodb

#endif // GEODB_IRWI_BASE_HPP
