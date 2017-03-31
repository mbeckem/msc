#ifndef GEODB_IRWI_TREE_HPP
#define GEODB_IRWI_TREE_HPP

#include "geodb/bounding_box.hpp"
#include "geodb/interval_set.hpp"
#include "geodb/trajectory.hpp"
#include "geodb/irwi/base.hpp"
#include "geodb/irwi/cursor.hpp"
#include "geodb/irwi/label_count.hpp"
#include "geodb/irwi/posting.hpp"
#include "geodb/irwi/query.hpp"
#include "geodb/irwi/tree_insertion.hpp"
#include "geodb/irwi/tree_state.hpp"
#include "geodb/utility/range_utils.hpp"

#include <boost/container/static_vector.hpp>
#include <boost/noncopyable.hpp>
#include <boost/range/adaptor/filtered.hpp>
#include <boost/range/adaptor/map.hpp>
#include <boost/range/algorithm/max_element.hpp>
#include <boost/range/algorithm/min_element.hpp>

#include <algorithm>
#include <set>
#include <map>
#include <ostream>

/// \file
/// Contains the main IRWI Tree class.

namespace geodb {

/// An IRWI Tree stores spatio-textual trajectories.
/// Trajectory units are indexed by both their spatial
/// and textual attributes.
///
/// \tparam StorageSpec
///     The storage type (internal or external).
/// \tparam Lambda
///     The maximum number of intervals in each posting.
///     Intervals are used to represent the set of trajectory ids.
template<typename StorageSpec, u32 Lambda>
class tree {
private:
    using state_type = tree_state<StorageSpec, tree_entry, detail::tree_entry_accessor, Lambda>;

    using storage_type = typename state_type::storage_type;

public:
    using index_type = typename state_type::index_type;
    using index_ptr = typename state_type::index_ptr;
    using const_index_ptr = typename state_type::const_index_ptr;

    using list_type = typename state_type::list_type;
    using list_ptr = typename state_type::list_ptr;
    using const_list_ptr = typename state_type::const_list_ptr;

    using posting_type = typename list_type::posting_type;

private:
    using id_set_type = typename posting_type::id_set_type;

    using node_id_type = typename state_type::node_id;
    using node_ptr = typename state_type::node_ptr;
    using leaf_ptr = typename state_type::leaf_ptr;
    using internal_ptr = typename state_type::internal_ptr;

private:
    /// Represents a node that might be visited in a future query iteration.
    struct candidate_entry {
        node_ptr ptr;
        bounding_box mbb;
        id_set_type ids;
    };

public:
    using value_type = tree_entry;

    using cursor = tree_cursor<state_type>;

public:
    static constexpr u32 lambda() { return state_type::lambda(); }

    /// Maximum fanout for internal nodes.
    static constexpr size_t max_internal_entries() { return state_type::max_internal_entries(); }

    /// Maximum fanout for leaf nodes.
    static constexpr size_t max_leaf_entries() { return state_type::max_leaf_entries(); }

public:
    /// Constructs a new IRWI-tree.
    ///
    /// \param s
    ///     The storage spec. Holds required information
    ///     for opening the storage (e.g. filename).
    ///
    /// \param weight
    ///     Weighting factor for the weighted average between
    ///     spatial and textual cost. A value of 1 yields a classic
    ///     rtree. Must be in [0, 1].
    tree(const StorageSpec& s = StorageSpec(), float weight = 0.5f)
        : state(std::move(s), detail::tree_entry_accessor(), weight)
    {}

    /// Returns the weighting factor for cost calculation.
    float weight() const { return state.weight(); }

    /// Returns the height of the tree. The height is at least 1.
    /// The empty tree has a single, empty leaf node.
    size_t height() const { return storage().get_height(); }

    /// Returns the number of trajectory units.
    size_t size() const { return storage().get_size(); }

    /// True iff the tree is empty.
    bool empty() const { return size() == 0; }

    /// Returns the total number of internal nodes.
    size_t internal_node_count() const { return storage().get_internal_count(); }

    /// Returns the total number of leaf nodes.
    size_t leaf_node_count() const { return storage().get_leaf_count(); }

    /// Returns the total number of nodes.
    size_t node_count() const { return internal_node_count() + leaf_node_count(); }

    /// Returns a cursor pointing to the root of the tree.
    /// \pre `!empty()`.
    cursor root() const {
        if (empty()) {
            throw std::logic_error("calling root() on an empty tree.");
        }
        geodb_assert(storage().get_height() > 0, "invalid height");
        return cursor(&state, storage().get_root());
    }

    /// Inserts a single leaf entry into the tree.
    void insert(const tree_entry& v) {
        using insertion_type = tree_insertion<state_type>;

        insertion_type(state).insert(v, path_buf);
    }

    /// Inserts the new trajectory into the tree.
    /// The trajectory's id must be unique and it must
    /// not already have been inserted.
    void insert(const trajectory& t) {
        // A trajectory is inserted by inserting all its units.
        u32 index = 0;
        for (const trajectory_unit& unit : t.units) {
            tree_entry d{t.id, index, unit};
            insert(d);
            ++index;
        }
    }

    /// FIXME
    void insert(const point_trajectory& t) {
        auto pos = t.entries.begin();
        auto end = t.entries.end();
        u32 index = 0;

        if (pos != end) {
            auto last = pos++;
            for (; pos != end; ++pos) {
                tree_entry e{t.id, index++, trajectory_unit{last->spatial, pos->spatial, last->textual}};
                insert(e);
                last = pos;
            }
        }
    }

    /// Finds all trajectories that satisfy the given query.
    std::vector<trajectory_match> find(const sequenced_query& seq_query) const {
        if (empty()) {
            return {}; // no root.
        }

        const size_t n = seq_query.queries.size();
        std::vector<std::vector<leaf_ptr>> nodes = find_leaves(seq_query.queries);
        if (nodes.empty()) {
            return {};
        }

        geodb_assert(nodes.size() == n, "not enough node lists");

        // Iterate over all leaf nodes and gather the matching entries.
        std::vector<std::map<trajectory_id_type, std::vector<tree_entry>>> candidates(n);
        std::vector<tree_entry> units;
        for (size_t i = 0; i < n; ++i) {
            geodb_assert(!nodes[i].empty(), "node list must be non-empty");
            get_matching_units(seq_query.queries[i], nodes[i], units);
            candidates[i] = group_by_key(units, [](const tree_entry& e) { return e.trajectory_id; });
            sort_groups(candidates[i]);
        }

        // Trajectories must satisfy every simple query and must do so in the correct order.
        return check_order(candidates);
    }

private:
    // ----------------------------------------
    //      Query
    // ----------------------------------------

    /// Finds a set of leaf nodes for each query. These leaves may contain units that satisfy the
    /// associated simple query.
    /// Returns an empty vector if the queries cannot be satisfied at the same time.
    template<typename QueryRange>
    std::vector<std::vector<leaf_ptr>> find_leaves(QueryRange&& queries) const {
        geodb_assert(!empty(), "requires a root node.");

        // Status of a simple query. Simple queries are evaluated in parallel.
        struct state_t {
            state_t(const simple_query& query): query(query) {}

            const simple_query& query;
            std::vector<node_ptr> nodes;                // set of remaining nodes
            std::vector<candidate_entry> candidates;    // children of these nodes
            interval<time_type> time_window;            // time interval containing the candidates
            id_set_type ids;                    // union of ids in candidates
        };

        std::vector<state_t> states(boost::begin(queries), boost::end(queries));
        for (state_t& state : states) {
            state.nodes.push_back(storage().get_root());
        }

        // For every non-leaf level.
        const size_t height = storage().get_height();
        for (size_t level = 1; level < height; ++level) {
            // Gather candidate entries by looking at the inverted index
            // and bounding boxes.
            for (state_t& state : states) {
                auto to_internal = [&](auto ptr) {
                    return this->storage().to_internal(ptr);
                };
                get_matching_entries(state.query, state.nodes | transformed(to_internal), state.candidates);
                state.time_window = get_time_window(state.candidates);
                state.ids = get_ids(state.candidates);
            }

            auto shared_ids = id_set_type::set_intersection(
                        states | transformed_member(&state_t::ids));
            if (shared_ids.empty()) {
                return {}; // No common ids.
            }
            if (!trim_time_windows(states | transformed_member(&state_t::time_window))) {
                return {}; // Time windows contradict each other.
            }

            for (state_t& state : states) {
                state.nodes.clear();
                for (const candidate_entry& c : state.candidates) {
                    // Keep an entry when its time interval overlaps and
                    // it contains relevant ids.
                    if (state.time_window.overlaps({c.mbb.min().t(), c.mbb.max().t()})
                            && !c.ids.intersection_with(shared_ids).empty()) {
                        state.nodes.push_back(c.ptr);
                    }
                }

                if (state.nodes.empty()) {
                    return {}; // This query has no further nodes.
                }
            }
        }

        std::vector<std::vector<leaf_ptr>> result;
        for (const state_t& state : states) {
            std::vector<leaf_ptr> leaves;
            for (node_ptr ptr : state.nodes) {
                leaves.push_back(storage().to_leaf(ptr));
            }
            result.push_back(std::move(leaves));
        }
        return result;
    }

    /// Returns matching candiate entries for the given list of internal nodes.
    template<typename InternalNodeRange>
    void get_matching_entries(const simple_query& q, const InternalNodeRange& nodes,
                              std::vector<candidate_entry>& result) const
    {
        result.clear();
        for (internal_ptr ptr : nodes) {
            auto index = storage().const_index(ptr);

            // Retrieve all child entries from the inverted index that contain
            // any of the labels in "q.labels".
            std::unordered_map<entry_id_type, id_set_type> matches;
            index->matching_children(q.labels, matches);

            // Test the resulting matches against the query rectangle (which includes the time dimension).
            for (auto& pair : matches) {
                auto child = pair.first;
                auto& ids = pair.second;

                auto mbb = storage().get_mbb(ptr, child);
                if (mbb.intersects(q.rect)) {
                    result.push_back({ storage().get_child(ptr, child), mbb, std::move(ids) });
                }
            }
        }
    }

    /// Returns the time interval that contains the time intervals of all entries.
    template<typename CandidateEntryRange>
    interval<time_type> get_time_window(const CandidateEntryRange& entries) const {
        using boost::range::min_element;
        using boost::range::max_element;

        if (entries.empty()) {
            return {};
        }

        auto begin = min_element(entries | transformed([](const candidate_entry& e) { return e.mbb.min().t(); }));
        auto end = max_element(entries | transformed([](const candidate_entry& e) { return e.mbb.max().t(); }));
        return interval<time_type>(*begin, *end);
    }

    /// Returns the union of all id sets in `entries`.
    template<typename CandidateEntryRange>
    id_set_type get_ids(const CandidateEntryRange& entries) const {
        return id_set_type::set_union(entries | transformed_member(&candidate_entry::ids));
    }

    /// Trims the time windows by discarding impossible-to-fulfill situations.
    /// Every time window in the input range belongs to a simple query (in
    /// the correct order).
    ///
    /// Simple queries must be matched in order by all trajectories.
    /// Let w1 = [b1, e1], w2 = [b2, e2] be time windows for two simple queries q1 and q2,
    /// where q1 preceeds q2 in the ordering of the sequenced query.
    /// If b2 < b1, none of the trajectory units in [b2, b1] that belong to q2
    /// can satisfy the overall query because there cannot be any earlier trajectory units
    /// satisfying q1 within the same trajectory.
    /// We can therefore adjust w2 to [b2, e2], which reduces the search space for q2.
    ///
    /// An analogue reasoning holds for the end of w1.
    ///
    /// If some of the intervals become empty, then fulfilling the query is impossible
    /// for all known trajectories.
    ///
    /// \return
    ///     False, if the query operation cannot return any results.
    ///     True if the search should continue.
    template<typename TimeWindowRange>
    bool trim_time_windows(TimeWindowRange&& windows) const {
        BOOST_CONCEPT_ASSERT(( boost::ForwardRangeConcept<TimeWindowRange> ));

        auto i = boost::begin(windows);
        auto e = boost::end(windows);

        if (i == e)
            return false;

        // Iterate over all adjacent pairs.
        auto n = std::next(i);
        for (; n != e; ++i, ++n) {
            interval<time_type>& w1 = *i;
            interval<time_type>& w2 = *n;

            const time_type w1_end = std::min(w1.end(), w2.end());
            const time_type w2_begin = std::max(w1.begin(), w2.begin());
            if (w1_end < w1.begin() || w2_begin > w2.end()) {
                // Interval empty!
                return false;
            }
            w1 = { w1.begin(), w1_end };
            w2 = { w2_begin, w2.end() };
        }
        return true;
    }

    /// Retrieves all leaf entries that match the given query.
    ///
    /// \param q            The query that must be satisfied.
    /// \param leaves       A subset of all leaf nodes in the tree. At this stage the leaves should
    ///                     already have been filtered to avoid searching in irrelevant leaves.
    /// \param[out] result  Will contain the matching entries.
    template<typename LeafPtrRange>
    void get_matching_units(const simple_query& q, const LeafPtrRange& leaves,
                            std::vector<tree_entry>& result) const
    {
        result.clear();
        for (leaf_ptr leaf : leaves) {
            const u32 count = storage().get_count(leaf);
            for (u32 i = 0; i < count; ++i) {
                const tree_entry data = storage().get_data(leaf, i);
                if (data.unit.intersects(q.rect) && contains(q.labels, data.unit.label)) {
                    result.push_back(data);
                }
            }
        }
    }

    /// Takes a range of trajectory_id -> [units] maps (one map for each simple query).
    /// Returns a set of trajectories that have their matches correctly ordered in time.
    template<typename CandidateTractoriesRange>
    std::vector<trajectory_match> check_order(const CandidateTractoriesRange& candidates) const {
        geodb_assert(!boost::empty(candidates), "range must not be empty");

        std::vector<trajectory_match> matches;
        for (trajectory_id_type id : *boost::begin(candidates) | boost::adaptors::map_keys) {
            u32 unit = 0;
            std::vector<unit_match> unit_matches;

            for (const auto& map : candidates) {
                auto iter = map.find(id);
                if (iter == map.end()) {
                    // the trajectory must have matching units for every simple query.
                    goto skip;
                }

                const std::vector<tree_entry>& entries = iter->second;
                geodb_assert(entries.size() > 0, "no matching entries");

                // Return the matching trajectory units to the user.
                for (const tree_entry& e : entries) {
                    unit_matches.emplace_back(e.unit_index, e.unit);
                }

                // Find the min & max unit index (for time ordering, later queries must
                // have higher indices).
                auto unit_indices = entries | transformed_member(&tree_entry::unit_index);
                u32 min_unit = *boost::min_element(unit_indices);
                u32 max_unit = *boost::max_element(unit_indices);

                if (min_unit >= unit) {
                    unit = max_unit;
                } else {
                    // time ordering violated (unit comes before the last max unit index).
                    goto skip;
                }
            }

            // A trajectory only matches if there are matching units for every simple query.
            // This code will be skipped (see the goto above) if that is not the case.
            matches.push_back({id, std::move(unit_matches)});

        skip:
            continue;
        }
        return matches;
    }

    /// Sorts every tree_entry vector in the given map by unit index.
    template<typename CandidateMap>
    void sort_groups(CandidateMap& map) const {
        for (auto& pair : map) {
            std::vector<tree_entry>& entries = pair.second;
            std::sort(entries.begin(), entries.end(), [&](const tree_entry& a, const tree_entry& b) {
                return a.unit_index < b.unit_index;
            });
        }
    }

private:
    template<typename State>
    friend class tree_cursor;

private:
    storage_type& storage() { return state.storage(); }
    const storage_type& storage() const { return state.storage(); }

    template<typename Tree, typename Derived>
    friend class bulk_load_common;

private:
    state_type state;
    std::vector<internal_ptr> path_buf;
};

template<typename Cursor>
void dump(std::ostream& o, Cursor c, int indent_length = 0) {
    auto indent = [&]() -> std::ostream& {
        for (int i = 0; i < indent_length; ++i) {
            o << "  ";
        }
        return o;
    };

    if (c.is_internal()) {
        indent() << "Type: Internal\n";

        auto index = c.inverted_index();

        indent_length++;
        indent() << "Index:\n";
        indent_length++;

        indent() << "Total: ";
        const auto list = index->total();
        for (const auto& e : *list) {
            o << e << " ";
        }
        o << "\n";

        for (const auto& entry : *index) {
            indent() << "Label " << entry.label() << ": ";

            const auto list = entry.postings_list();
            for (const auto& e : *list) {
                o << e << " ";
            }
            o << "\n";
        }
        indent_length--;
        indent_length--;

        const size_t size = c.size();
        for (size_t i = 0; i < size; ++i) {
            indent() << "Child " << i << ": " << c.mbb(i) << "\n";
            dump(o, c.child(i), indent_length+1);
        }
    } else {
        indent() << "Type: Leaf\n";
        const size_t size = c.size();
        for (size_t i = 0; i < size; ++i) {
            tree_entry e = c.value(i);
            indent() << "Child " << i << ": " << e.trajectory_id << "[" << e.unit_index << "] " << e.unit << "\n";
        }
    }
}

} // namespace geodb

#endif // GEODB_IRWI_TREE_HPP
