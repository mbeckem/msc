#ifndef IRWI_TREE_HPP
#define IRWI_TREE_HPP

#include "geodb/bounding_box.hpp"
#include "geodb/interval_set.hpp"
#include "geodb/trajectory.hpp"
#include "geodb/irwi/base.hpp"
#include "geodb/irwi/cursor.hpp"
#include "geodb/irwi/inverted_index.hpp"
#include "geodb/irwi/query.hpp"
#include "geodb/utility/movable_adapter.hpp"
#include "geodb/utility/range_utils.hpp"

#include <boost/container/static_vector.hpp>
#include <boost/noncopyable.hpp>
#include <boost/range/adaptor/filtered.hpp>
#include <boost/range/adaptor/map.hpp>
#include <boost/range/adaptor/reversed.hpp>
#include <boost/range/algorithm/max_element.hpp>
#include <boost/range/algorithm/min_element.hpp>

#include <algorithm>
#include <set>
#include <map>
#include <ostream>

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
    using storage_type = typename StorageSpec::template implementation<entry, Lambda>;

public:
    using index_type = typename storage_type::index_type;

private:
    using index_handle = typename storage_type::index_handle;

    using const_index_handle = typename storage_type::const_index_handle;

    using list_handle = typename index_type::list_handle;

    using const_list_handle = typename index_type::const_list_handle;

public:
    using posting_type = typename index_type::posting_type;

private:
    using id_set_type = typename posting_type::trajectory_id_set_type;

    using dynamic_id_set_type = interval_set<trajectory_id_type>;

    using node_id_type = typename storage_type::node_id_type;

    using node_ptr = typename storage_type::node_ptr;

    using leaf_ptr = typename storage_type::leaf_ptr;

    using internal_ptr = typename storage_type::internal_ptr;

    /// Represents a node that might be visited in a future query iteration.
    struct candidate_entry {
        node_ptr ptr;
        bounding_box mmb;
        dynamic_id_set_type ids;
    };

    /// A label id and a count of trajectory units.
    struct label_count {
        label_type label;
        u64 count;
    };

    /// Summary of the inverted index of some node.
    /// Uses O(num_labels * Lambda) space.
    struct index_summary {
        id_set_type total_tids;
        u64 total_units = 0;
        std::unordered_map<label_type, u64> label_units;
        std::unordered_map<label_type, id_set_type> label_tids;
    };

public:
    using value_type = entry;

    using cursor = tree_cursor<tree>;

    /// Maximum fanout for internal nodes.
    static constexpr size_t max_internal_entries() {
        return storage_type::max_internal_entries();
    }

    /// Minimum number of entries in a internal node.
    /// Only used by the split algorithm because deletion
    /// is not (yet) supported.
    static constexpr size_t min_internal_entries() {
        return (max_internal_entries() + 2) / 3;
    }

    /// Maximum fanout for leaf nodes.
    static constexpr size_t max_leaf_entries() {
        return storage_type::max_leaf_entries();
    }

    /// Minimal number of entries in a leaf node (except for the root).
    /// Currently only used by the split algorithm because the tree
    /// does not support delete opertions.
    static constexpr size_t min_leaf_entries() {
        return (max_leaf_entries() + 2) / 3;
    }

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
    tree(StorageSpec s = StorageSpec(), float weight = 0.5f)
        : m_weight(weight)
        , m_storage(in_place_t(), std::move(s))
    {
        if (m_weight < 0 || m_weight > 1.f)
            throw std::invalid_argument("weight must be in [0, 1]");
    }

    /// Returns the weighting factor for cost calculation.
    float weight() const { return m_weight; }

    /// Returns the height of the tree. The empty tree has height 0.
    size_t height() const { return storage().get_height(); }

    /// Returns the number of trajectory units.
    size_t size() const { return storage().get_size(); }

    /// True iff the tree is empty.
    bool empty() const { return size() == 0; }

    /// Returns a cursor pointing to the root of the tree.
    /// \pre `!empty()`.
    // TODO optional
    cursor root() const {
        geodb_assert(!empty(), "The tree must not be empty");
        cursor cursor(this);
        cursor.add_to_path(storage().get_root());
        return cursor;
    }

    /// Inserts the new trajectory into the tree.
    /// The trajectory's id must be unique and it must
    /// not already have been inserted.
    void insert(const trajectory& t) {
        // A trajectory is inserted by inserting all its units.
        u32 index = 0;
        for (const trajectory_unit& unit : t.units) {
            entry d{t.id, index, unit};
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
                entry e{t.id, index++, trajectory_unit{last->spatial, pos->spatial, last->textual}};
                insert(e);
                last = pos;
            }
        }
    }

    /// Finds all trajectories that satisfy the given query.
    std::vector<match> find(const sequenced_query& seq_query) const {
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
        std::vector<std::map<trajectory_id_type, std::vector<entry>>> candidates(n);
        std::vector<entry> units;
        for (size_t i = 0; i < n; ++i) {
            geodb_assert(!nodes[i].empty(), "node list must be non-empty");
            get_matching_units(seq_query.queries[i], nodes[i], units);
            candidates[i] = group_by_key(units, [](const entry& e) { return e.trajectory_id; });
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
            dynamic_id_set_type ids;                    // union of ids in candidates
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

            auto shared_ids = dynamic_id_set_type::set_intersection(
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
                    if (state.time_window.overlaps({c.mmb.min().t(), c.mmb.max().t()})
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
            std::unordered_map<entry_id_type, dynamic_id_set_type> matches;
            index->matching_children(q.labels, matches);

            // Test the resulting matches against the query rectangle (which includes the time dimension).
            for (auto& pair : matches) {
                auto child = pair.first;
                auto& ids = pair.second;

                auto mmb = storage().get_mmb(ptr, child);
                if (mmb.intersects(q.rect)) {
                    result.push_back({ storage().get_child(ptr, child), mmb, std::move(ids) });
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

        auto begin = min_element(entries | transformed([](const candidate_entry& e) { return e.mmb.min().t(); }));
        auto end = max_element(entries | transformed([](const candidate_entry& e) { return e.mmb.max().t(); }));
        return interval<time_type>(*begin, *end);
    }

    /// Returns the union of all id sets in `entries`.
    template<typename CandidateEntryRange>
    dynamic_id_set_type get_ids(const CandidateEntryRange& entries) const {
        return dynamic_id_set_type::set_union(entries | transformed_member(&candidate_entry::ids));
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
                            std::vector<entry>& result) const
    {
        result.clear();
        for (leaf_ptr leaf : leaves) {
            const u32 count = storage().get_count(leaf);
            for (u32 i = 0; i < count; ++i) {
                const entry data = storage().get_data(leaf, i);
                if (data.unit.intersects(q.rect) && contains(q.labels, get_label(data))) {
                    result.push_back(data);
                }
            }
        }
    }

    /// Takes a range of trajectory_id -> [units] maps (one map for each simple query).
    /// Returns a set of trajectories that have their matches correctly ordered in time.
    template<typename CandidateTractoriesRange>
    std::vector<match> check_order(const CandidateTractoriesRange& candidates) const {
        geodb_assert(!boost::empty(candidates), "range must not be empty");

        std::vector<match> result;
        for (trajectory_id_type id : *boost::begin(candidates) | boost::adaptors::map_keys) {
            u32 unit = 0;
            std::vector<u32> indices;

            for (const auto& map : candidates) {
                auto iter = map.find(id);
                if (iter == map.end()) {
                    // the trajectory must have matching units for every simple query.
                    goto skip;
                }

                const std::vector<entry>& entries = iter->second;
                geodb_assert(entries.size() > 0, "no matching entries");

                auto unit_indices = entries | transformed_member(&entry::unit_index);
                u32 min_unit = *boost::min_element(unit_indices);
                u32 max_unit = *boost::max_element(unit_indices);
                append(indices, unit_indices);

                if (min_unit >= unit) {
                    unit = max_unit;
                } else {
                    // time ordering violated (unit comes before the last max unit index).
                    goto skip;
                }
            }
            result.push_back({id, std::move(indices)});

        skip:
            continue;
        }
        return result;
    }

private:
    // ----------------------------------------
    //      Insertion
    // ----------------------------------------

    /// Given an internal node, and the mmb + label of a new entry,
    /// find the best child entry within the internal node.
    /// This function respects the spatial cost (i.e. growing the bounding box)
    /// and the textual cost (introducing uncommon labels into a subtree).
    node_ptr find_insertion_entry(internal_ptr n, const bounding_box& b, label_type label) {
        const u32 count = storage().get_count(n);
        geodb_assert(count > 0, "empty internal node");

        // open the postings lists and retrieve the unit counts for all entries.
        std::vector<u64> unit_count;    // Units with the current label
        std::vector<u64> total_count;   // Total units (includes other labels)
        {
            auto index = storage().const_index(n);
            total_count = index->total()->counts(count);

            auto list_pos = index->find(label);
            if (list_pos == index->end()) {
                unit_count.resize(count, 0);
            } else {
                unit_count = list_pos->postings_list()->counts(count);
            }
        }

        // Combined cost function using weight parameter beta.
        const float norm = inverse(max_enlargement(n, b));
        const auto cost = [&](u32 index) {
            const float spatial = spatial_cost(get_mmb(n, index), b, norm);
            const float textual = textual_cost(unit_count[index], total_count[index]);
            return m_weight * spatial + (1.f - m_weight) * textual;
        };

        // Find the entry with the smallest cost.
        // Resolve ties by choosing the entry with the smallest mmb.
        u32 min_index = 0;
        float min_cost = cost(0);
        float min_size = storage().get_mmb(n, 0).size();
        for (u32 i = 1; i < count; ++i) {
            const float c = cost(i);
            const float size = get_mmb(n, i).size();
            if (c < min_cost || (c == min_cost && size < min_size)) {
                min_cost = c;
                min_index = i;
                min_size = size;
            }
        }
        return storage().get_child(n, min_index);
    }

    /// Finds the appropriate leaf node for the insertion of \p d.
    /// Stores the path of internal node parents into \p path.
    leaf_ptr find_insertion_leaf(std::vector<internal_ptr>& path, const entry& d) {
        geodb_assert(storage().get_height() > 0, "empty tree has no leaves");

        path.clear();
        if (storage().get_height() == 1) {
            // Root is the only leaf.
            return storage().to_leaf(storage().get_root());
        }

        const bounding_box mmb = get_mmb(d);
        const label_type label = get_label(d);

        // Iterate until the leaf level has been reached.
        internal_ptr current = storage().to_internal(storage().get_root());
        for (size_t level = 1;; ++level) {
            path.push_back(current);

            const node_ptr child = find_insertion_entry(current, mmb, label);
            if (level + 1 == storage().get_height()) {
                // Next level is the leaf level.
                return storage().to_leaf(child);
            }

            // Continue on the next level.
            current = storage().to_internal(child);
        }

        unreachable("must have reached the leaf level");
    }

    /// Inserts a single trajectory unit into the tree.
    void insert(const entry& d) {
        storage().set_size(storage().get_size() + 1);

        if (storage().get_height() == 0) {
            // The tree was empty. Construct a new leaf
            // and register it as the root.
            const leaf_ptr root = storage().create_leaf();
            insert_entry(root, d);
            storage().set_height(1);
            storage().set_root(root);
            return;
        }

        // Track the path of the leaf, in case we have to split
        // its parents.
        std::vector<internal_ptr> path;

        // Find the leaf that is best suitable to store d.
        const leaf_ptr leaf = find_insertion_leaf(path, d);
        {
            const u32 count = storage().get_count(leaf);
            if (count < max_leaf_entries()) {
                // The leaf has room for this entry.
                insert_entry(leaf, d);
                update_path(path, leaf, get_id(d), get_mmb(d), get_label(d));
                return;
            }
        }

        // The leaf is full. Split it and insert the new entry.
        const leaf_ptr new_leaf = split_and_insert(leaf, d);
        if (path.empty()) {
            // The root was a leaf and has been split.
            internal_ptr root = storage().create_internal();
            insert_entry(root, leaf);
            insert_entry(root, new_leaf);
            storage().set_root(root);
            storage().set_height(2);
            return;
        }

        // There was at least one internal node parent.
        // The content of l has changed, update the parent's mmb and inverted index.
        internal_ptr parent = path.back();
        replace_entry(parent, leaf);
        if (storage().get_count(parent) < max_internal_entries()) {
            // The direct parent has enough space to store the new leaf.
            // Insert the new leaf and notify the parents about the insertion
            // of `d`. p will be up-to-date because ll is going to be inserted
            // and l has been reinserted already.
            insert_entry(parent, new_leaf);
            path.pop_back();
            update_path(path, parent, get_id(d), get_mmb(d), get_label(d));
            return;
        }

        // At this point, we have two internal nodes that need to be inserted
        // into their parent. If there is no parent, then we create a new
        // internal node that becomes our new root.
        internal_ptr new_internal = split_and_insert(parent, new_leaf);
        internal_ptr old_internal = parent;
        path.pop_back();
        while (!path.empty()) {
            parent = path.back();
            replace_entry(parent, old_internal);
            if (storage().get_count(parent) < max_internal_entries()) {
                insert_entry(parent, new_internal);
                path.pop_back();
                update_path(path, parent, get_id(d), get_mmb(d), get_label(d));
                return;
            }

            path.pop_back();
            new_internal = split_and_insert(parent, new_internal);
            old_internal = parent;
        }

        // Two internal nodes remain but there is no parent in path:
        // We need a new root.
        internal_ptr root = storage().create_internal();
        insert_entry(root, old_internal);
        insert_entry(root, new_internal);
        storage().set_root(root);
        storage().set_height(storage().get_height() + 1);
    }

    /// Summarizes a leaf node. This summary will become part of
    /// the new parent's inverted index.
    index_summary summarize(leaf_ptr n) const {
        const u32 count = storage().get_count(n);

        index_summary s;
        for (u32 i = 0 ; i < count; ++i) {
            auto&& data = storage().get_data(n, i);
            const trajectory_id_type tid = get_id(data);
            const label_type label = get_label(data);

            s.total_tids.add(tid);
            ++s.total_units;

            s.label_tids[label].add(tid);
            ++s.label_units[label];
        }
        return s;
    }

    /// Summarizes an internal node by summing the entries of
    /// all postings lists in its inverted index.
    /// This summary will become part of the new parent's inverted index.
    index_summary summarize(internal_ptr n) const {
        // Sums a postings list by summing the count of units in the entries
        // and creating the union of all trajectory id sets.
        std::vector<id_set_type> buffer;
        auto sum_list = [&](const auto& list, id_set_type& ids, u64& count) {
            buffer.clear();
            buffer.reserve(list.size());

            count = 0;
            for (const auto& p : list) {
                count += p.count();
                buffer.push_back(p.id_set());
            }
            ids = id_set_type::set_union(buffer);
        };

        auto index = storage().const_index(n);

        index_summary s;
        sum_list(*index->total(), s.total_tids, s.total_units);
        for (const auto& entry : *index) {
            const label_type label = entry.label();

            auto list = entry.postings_list();

            id_set_type tids;
            u64 count;
            sum_list(*list, tids, count);
            if (count > 0) {
                s.label_units[label] = count;
                s.label_tids[label] = tids;
            }
        }
        return s;
    }

    /// Insert a data entry into a leaf. The leaf must not be full.
    void insert_entry(leaf_ptr leaf, const entry& d) {
        const u32 count = storage().get_count(leaf);
        geodb_assert(count < max_leaf_entries(), "leaf node is full");

        storage().set_data(leaf, count, d);
        storage().set_count(leaf, count + 1);
    }

    /// Inserts a new entry into the internal node `p` that references the
    /// node `c`. `p` must not be full.
    /// The inverted index of `p` will be updated to reflect the insertion of `c`.
    ///
    /// \param p    The parent node.
    /// \param c    The new child node (either a leaf or an internal node).
    template<typename NodePointer>
    void insert_entry(internal_ptr p, NodePointer c) {
        const u32 i = storage().get_count(p);
        geodb_assert(i < max_internal_entries(), "internal node is full");

        storage().set_count(p, i + 1);
        storage().set_mmb(p, i, get_mmb(c));
        storage().set_child(p, i, c);

        index_handle index = storage().index(p);
        const auto sum = summarize(c);

        // Update the postings lists for all labels that occur in the summary.
        // TODO: One map isntead of 2
        for (auto& pair : sum.label_units) {
            label_type label = pair.first;
            u64 count = pair.second;
            if (count > 0) {
                index->find_or_create(label)
                        ->postings_list()
                        ->append(posting_type(i, count, sum.label_tids.at(label)));
            }
        }
        index->total()->append(posting_type(i, sum.total_units, sum.total_tids));
    }

    /// Reinserts a node into its parent.
    /// This function is called when a node `c` has been split.
    /// The parent node `p` will be updated to reflect the changes to `c`.
    ///
    /// \param p    The parent node.
    /// \param c    The child node. The child must already be referenced by `p`.
    ///             Can be either a leaf or an internal node.
    template<typename NodePointer>
    void replace_entry(internal_ptr p, NodePointer c) {
        const u32 id = index_of(p, c);
        const index_summary sum = summarize(c);
        const index_handle index = storage().index(p);

        // Adjust the mmb to fit the node's current entry set.
        storage().set_mmb(p, id, get_mmb(c));

        // Erase all posting list entries for labels that do not occur in c anymore.
        for (auto i = index->begin(), e = index->end(); i != e; ++i) {
            const label_type label = i->label();

            if (sum.label_units.find(label) == sum.label_units.end()) {
                const auto list = i->postings_list();
                const auto pos = list->find(id);
                if (pos != list->end()) {
                    list->remove(pos);
                }
            }
        }

        // Update the entries for labels in 'c'.
        // A label might be new if it was new with the unit that caused the node to split.
        // TODO: 1 map
        for (auto pair : sum.label_units) {
            label_type label = pair.first;
            u64 count = pair.second;
            update_entry(index, label, posting_type(id, count, sum.label_tids.at(label)));
        }
        update_entry(index->total(), posting_type(id, sum.total_units, sum.total_tids));
    }

    /// Update every parent node in the path with the fact that a new
    /// unit has been inserted into the given child, with the provided mmb
    /// and label id.
    void update_path(const std::vector<internal_ptr>& path, node_ptr child,
                     trajectory_id_type tid, const bounding_box& unit_mmb, label_type label_id)
    {
        // Increment the unit count for the given node & label.
        auto increment_entry = [&](const list_handle& list, entry_id_type node) {
            auto pos = list->find(node);
            if (pos == list->end()) {
                // First unit with that label. Make a new entry.
                list->append(posting_type(node, 1, {tid}));
            } else {
                // Replace the entry with an updated version.
                posting_type e = *pos;

                auto set = e.id_set();
                set.add(tid);
                e.id_set(set);
                e.count(e.count() + 1);

                list->set(pos, e);
            }
        };

        // Update mmbs and inverted indices in all parents.
        // The mmb of the child's entry within the parent must fit the new box.
        for (auto parent : path | boost::adaptors::reversed) {
            const u32 i = index_of(parent, child);
            bounding_box mmb = storage().get_mmb(parent, i).extend(unit_mmb);
            storage().set_mmb(parent, i, mmb);

            index_handle index = storage().index(parent);
            increment_entry(index->total(), i);
            increment_entry(index->find_or_create(label_id)->postings_list(), i);

            child = parent;
        }
    }

    /// Updates the posting list entry for the given label. Creates the index entry if necessary.
    void update_entry(const index_handle& index, label_type label, const posting_type& e) {
        const auto iter = index->find_or_create(label);
        const auto list = iter->postings_list();
        update_entry(list, e);
    }

    /// Updates the entry in `list` for the node `e.node()`.
    /// Inserts the entry at the end if no entry for this node existed.
    void update_entry(const list_handle& list, const posting_type& e) {
        const auto pos = list->find(e.node());
        if (pos != list->end()) {
            list->set(pos, e);
        } else {
            list->append(e);
        }
    }

private:
    // ----------------------------------------
    //      Node partitioning
    // ----------------------------------------

    /// Represents a leaf node that is currently overflowing.
    struct overflowing_leaf_node : boost::noncopyable {
        static constexpr size_t max_entries = max_leaf_entries();
        static constexpr size_t min_entries = min_leaf_entries();
        static constexpr size_t N = max_entries + 1;

        /// Exactly N entries since this node is overflowing.
        boost::container::static_vector<entry, N> entries;

        /// All labels in this node.
        std::unordered_set<label_type> labels;
    };

    /// Construct a virtual node that holds all existing entries
    /// and the new one.
    void create(overflowing_leaf_node& o, leaf_ptr n, const entry& extra) {
        geodb_assert(o.entries.size() == 0, "o must be fresh");
        geodb_assert(storage().get_count(n) == max_leaf_entries(), "n must be full");
        for (u32 i = 0; i < u32(max_leaf_entries()); ++i) {
            entry d = storage().get_data(n, i);
            o.entries.push_back(d);
            o.labels.insert(get_label(d));
        }
        o.entries.push_back(extra);
        o.labels.insert(get_label(extra));
    }

    bounding_box get_mmb(const overflowing_leaf_node& n, u32 index) const {
        return get_mmb(n.entries[index]);
    }

    u32 get_count(const overflowing_leaf_node& n) const {
        geodb_assert(n.entries.size() == n.N, "node must be full");
        return n.N;
    }

    auto get_labels(const overflowing_leaf_node& n, u32 index) const {
        return std::array<label_count, 1>{{get_label(n.entries[index]), 1}};
    }

    u64 get_total_units(const overflowing_leaf_node& n, u32 index) const {
        geodb_assert(index < n.entries.size(), "index out of bounds");
        unused(n, index);
        return 1;
    }

    /// Represents an internal node that is currently overflowing.
    struct overflowing_internal_node : boost::noncopyable {
        static constexpr size_t max_entries = max_internal_entries();
        static constexpr size_t min_entries = min_internal_entries();
        static constexpr size_t N = max_entries + 1;

        struct child_data {
            node_ptr ptr;
            bounding_box mmb;
            std::map<label_type, u64> label_units;          // label -> unit count
            std::map<label_type, id_set_type> label_tids;   // label -> trajectory id set
            id_set_type total_tids;
            u64 total_units = 0;
        };

        /// Exactly N entries.
        boost::container::static_vector<child_data, N> entries;
        std::unordered_set<label_type> labels;
    };

    /// Create a new virtual node that holds all existing internal entries of `n`
    /// and the additional new child.
    template<typename NodePointer, typename NodeSummary>
    void create(overflowing_internal_node& o, internal_ptr n, NodePointer child, const NodeSummary& child_summary)
    {
        geodb_assert(o.entries.size() == 0, "o must be fresh");
        geodb_assert(storage().get_count(n) == max_internal_entries(), "n must be full");

        for (u32 i = 0; i < u32(max_internal_entries()); ++i) {
            o.entries.emplace_back();
            auto& entry = o.entries.back();
            entry.ptr = storage().get_child(n, i);
            entry.mmb = storage().get_mmb(n, i);
        }

        {
            // Open the inverted index and get the <label, count> pairs
            // for all already existing entries.
            auto index = storage().const_index(n);

            // Gather totals.
            auto total = index->total();
            for (const posting_type& p : *total) {
                auto& entry = o.entries[p.node()];
                entry.total_units = p.count();
                entry.total_tids = p.id_set();
            }

            // Gather the counts for the other labels.
            for (auto&& entry : *index) {
                const label_type label = entry.label();
                auto list = entry.postings_list();

                // Do not propagate empty lists.
                if (list->size() > 0) {
                    for (const posting_type& p : *list) {
                        auto& entry = o.entries[p.node()];
                        entry.label_units[label] = p.count();
                        entry.label_tids[label] = p.id_set();
                    }
                    o.labels.insert(label);
                }
            }
        }

        // Add the new entry as well.
        o.entries.emplace_back();
        auto& entry = o.entries.back();
        entry.ptr = child;
        entry.mmb = get_mmb(child);
        entry.total_tids = child_summary.total_tids;
        entry.total_units = child_summary.total_units;
        entry.label_units.insert(child_summary.label_units.begin(), child_summary.label_units.end());
        entry.label_tids.insert(child_summary.label_tids.begin(), child_summary.label_tids.end());
        for (auto& pair : entry.label_units) {
            if (pair.second > 0) {
                o.labels.insert(pair.first);
            }
        }
    }

    bounding_box get_mmb(const overflowing_internal_node& o, u32 index) const {
        return o.entries[index].mmb;
    }

    u64 get_total_units(const overflowing_internal_node& o, u32 index) const {
        return o.entries[index].total_units;
    }

    auto get_labels(const overflowing_internal_node& o, u32 index) const {
        auto transform = [](const auto& pair) {
            return label_count{pair.first, pair.second};
        };
        return o.entries[index].label_units | transformed(transform);
    }

    u32 get_count(const overflowing_internal_node& o) const {
        geodb_assert(o.entries.size() == o.N, "node must be full");
        return o.N;
    }

    struct node_part : boost::noncopyable {
        /// Minimal bounding box for all entries.
        bounding_box mmb;

        /// Indices into the overflowing node.
        std::unordered_set<u32> entries;

        /// Map of label -> count.
        std::map<label_type, u64> labels;

        const bounding_box& get_mmb() const {
            return mmb;
        }

        auto get_labels() const {
            auto transform = [](const auto& pair) {
                return label_count{pair.first, pair.second};
            };
            return labels | transformed(transform);
        }

        u64 get_total_units() const {
            return entries.size();
        }

        template<typename LabelCounts>
        void init(u32 i, const bounding_box& b, const LabelCounts& label_counts) {
            entries.insert(i);
            mmb = b;
            for (const label_count& lc : label_counts) {
                labels[lc.label] += lc.count;
            }
        }

        template<typename LabelCounts>
        void add(u32 i, const bounding_box& b, const LabelCounts& label_counts) {
            entries.insert(i);
            mmb = mmb.extend(b);
            for (const label_count& lc : label_counts) {
                labels[lc.label] += lc.count;
            }
        }

        size_t size() const {
            return entries.size();
        }
    };

    /// Splits the leaf into two leaves and inserts the extra entry.
    /// Returns the new leaf.
    leaf_ptr split_and_insert(leaf_ptr old_leaf, const entry& extra) {
        overflowing_leaf_node o;
        create(o, old_leaf, extra);

        // Partition the entries of o.
        node_part left, right;
        partition(o, left, right);

        // Move the entries into place.
        auto assign_entries = [&](node_part& p, leaf_ptr ptr) {
            // Copy the entries from the partition into the new node.
            u32 count = 0;
            for (auto index : p.entries) {
                storage().set_data(ptr, count++, o.entries[index]);
            }
            storage().set_count(ptr, count);

            // Clear the remaining records.
            for (u32 i = count; i < max_leaf_entries(); ++i) {
                storage().set_data(ptr, i, entry());
            }
        };

        leaf_ptr new_leaf = storage().create_leaf();
        assign_entries(left, old_leaf);
        assign_entries(right, new_leaf);
        return new_leaf;
    }

    /// Splits an internal node and inserts the new child entry.
    ///
    /// \param old_internal
    ///     The existing internal node (which must be full).
    /// \param c
    ///     The new leaf or internal node that should be inserted.
    template<typename NodePointer>
    internal_ptr split_and_insert(internal_ptr old_internal, NodePointer c) {
        internal_ptr new_internal = storage().create_internal();
        const auto sum = summarize(c);

        overflowing_internal_node o;
        create(o, old_internal, c, sum);

        node_part left, right;
        partition(o, left, right);

        // old index -> new index. does not say in which node entries are now.
        std::map<u32, u32> index_map;

        // move entries indicated by p into the given node.
        auto assign_entries = [&](node_part& p, internal_ptr ptr) {
            // Copy the entries from the part into the new node.
            u32 count = 0;
            for (auto index : p.entries) {
                storage().set_mmb(ptr, count, o.entries[index].mmb);
                storage().set_child(ptr, count, o.entries[index].ptr);
                index_map[index] = count;
                ++count;
            }
            storage().set_count(ptr, count);

            // Clear the remaining records.
            for (u32 i = count; i < max_internal_entries(); ++i) {
                storage().set_mmb(ptr, i, bounding_box());
                storage().set_child(ptr, i, node_ptr());
            }
        };

        assign_entries(left, old_internal);
        assign_entries(right, new_internal);

        // move_list takes the postings list entries from the given list
        // and stores them in result_left and result_right.
        std::vector<posting_type> result_left, result_right;
        auto split_entries = [&](auto&& list) {
            result_left.clear();
            result_right.clear();

            for (const auto& entry : *list) {
                posting_type p(index_map[entry.node()], entry.count(), entry.id_set());
                if (left.entries.count(entry.node())) {
                    result_left.push_back(p);
                } else {
                    geodb_assert(right.entries.count(entry.node()), "Must be in the other partition");
                    result_right.push_back(p);
                }
            }
        };

        // Iterate over all postings lists and move the entries.
        auto old_index = storage().index(old_internal);
        auto new_index = storage().index(new_internal);

        // Move the label entries into position.
        for (const auto& entry : *old_index) {
            const label_type label = entry.label();
            const auto old_list = entry.postings_list();

            // TODO: Erase if empty? Consider iterator invalidation.
            split_entries(old_list);
            old_list->assign(result_left.begin(), result_left.end());
            if (!result_right.empty()) {
                auto new_list = new_index->create(label)->postings_list();
                new_list->assign(result_right.begin(), result_right.end());
            }
        }

        // Move the entries in "total" into  position.
        {
            auto old_total = old_index->total();
            auto new_total = new_index->total();
            split_entries(old_total);
            old_total->assign(result_left.begin(), result_left.end());
            new_total->assign(result_right.begin(), result_right.end());
        }

        // Add entries for the newly inserted node into the correct index.
        // The required entries can be calcuated by examining the already
        // existing summary.
        auto append_entries = [&](auto& index, entry_id_type id) {
            index->total()->append(posting_type(id, sum.total_units, sum.total_tids));
            // TODO: One map instead of 2.
            for (auto pair : sum.label_units) {
                index->find_or_create(pair.first)
                        ->postings_list()
                        ->append(posting_type(id, pair.second, sum.label_tids.at(pair.first)));
            }
        };
        if (auto old_position = optional_index_of(old_internal, c)) {
            append_entries(old_index, *old_position);
        } else if (auto new_position = optional_index_of(new_internal, c)) {
            append_entries(new_index, *new_position);
        } else {
            unreachable("Node was not found in either parent");
        }

        return new_internal;
    }

    /// Partitions the entries of a single node into two groups.
    /// Implements the quadradic node splitting algorithm for R-Trees
    /// (extended for spatio-textual trajectories).
    ///
    /// \param n            The overflowing node.
    /// \param left         The left partition.
    /// \param right        The right partition.
    template<typename OverflowingNode, typename Part>
    void partition(const OverflowingNode& n, Part& left, Part& right) {
        static constexpr size_t max_entries = OverflowingNode::max_entries;
        static constexpr size_t min_entries = OverflowingNode::min_entries;
        static constexpr size_t N = max_entries + 1;

        geodb_assert(get_count(n) == N, "node must be full");
        geodb_assert(left.size() == 0, "left group must be empty");
        geodb_assert(right.size() == 0, "right group must be empty");

        u32 left_seed, right_seed;
        std::tie(left_seed, right_seed) = pick_seeds(n);
        left.init(left_seed, get_mmb(n, left_seed), get_labels(n, left_seed));
        right.init(right_seed, get_mmb(n, right_seed), get_labels(n, right_seed));

        // Holds the set of unassigned indices.
        boost::container::static_vector<u32, N> remaining;
        for (u32 i = 0; i < N; ++i) {
            if (i != left_seed && i != right_seed) {
                remaining.push_back(i);
            }
        }

        // Partition the remaining entries into left & right.
        // In each iteration, consider all remaining entries.
        // For each entry, select either the left or the right group,
        // depending on which cost value is better.
        // Then, assign the entry that had the highest difference in
        // cost values to its selected group.
        // Repeat until all entries have been assigned or the number of entries
        // for one of the groups has become too high.
        while (!remaining.empty()) {
            // Make sure that each partition will hold at least `min_entries` entries.
            // If one partition size reaches `limit`, all remaining entries will be
            // put into the other one.
            static constexpr size_t limit = max_entries - min_entries;
            auto add_rest = [&](Part& p) {
                for (u32 index : remaining) {
                    p.add(index, get_mmb(n, index), get_labels(n, index));
                }
                remaining.clear();
            };
            if (left.size() == limit) {
                add_rest(right);
                break;
            } else if (right.size() == limit) {
                add_rest(left);
                break;
            }

            // Pick a group for the given leaf element.
            // Pretend that we have an internal node at hand with only
            // 2 entries (the two partially constructed nodes).
            // Returns either the left or the right group, depending
            // on the cost value.
            // The second return value is the (absolute) difference in cost.
            // The higher the difference, the more meaningful the decision.
            auto pick_partition = [&](u32 index) {
                const auto mmb = get_mmb(n, index);
                const float norm = inverse(std::max(enlargement(left.get_mmb(), mmb),
                                                    enlargement(right.get_mmb(), mmb)));
                
                // Normalized and weighted cost of inserting the entry
                // into the given group.
                const auto cost = [&](const Part& p) {
                    const float spatial = spatial_cost(p.get_mmb(), mmb, norm);
                    const float textual = textual_cost(get_labels(n, index), get_total_units(n, index),
                                                       p.get_labels(), p.get_total_units());
                    return m_weight * spatial + (1.0f - m_weight) * textual;
                };

                const float lc = cost(left);
                const float rc = cost(right);
                return lc < rc ? std::make_tuple(&left, rc - lc) : std::make_tuple(&right, lc - rc);
            };

            // Determine the item with the best cost difference.
            u32* best_item = &remaining[0];
            Part* best_part = nullptr;
            float best_diff = 0;
            std::tie(best_part, best_diff) = pick_partition(remaining[0]);
            for (size_t i = 1; i < remaining.size(); ++i) {
                Part* part;
                float diff;
                std::tie(part, diff) = pick_partition(remaining[i]);

                if (diff > best_diff) {
                    best_item = &remaining[i];
                    best_part = part;
                    best_diff = diff;
                }
            }

            // Insert the item into its selected group and remove it from the vector.
            // Repeat until the vector is empty.
            best_part->add(*best_item, get_mmb(n, *best_item), get_labels(n, *best_item));
            std::swap(*best_item, remaining.back());
            remaining.pop_back();
        }

        geodb_assert(left.size() + right.size() == N, "all items must have been distributed");
    }

    /// Picks two distinct entry indices as seeds for two new nodes.
    /// The two entries selected will be the ones that would
    /// produce the hightest cost if they were put into the same node.
    template<typename OverflowingNode>
    std::tuple<u32, u32> pick_seeds(const OverflowingNode& n) const {
        static constexpr u32 N = OverflowingNode::N;

        // normalized wasted space and textual cost, combined using weight beta.
        const float norm = inverse(max_waste(n));
        auto cost = [&](u32 i, u32 j) {
            const float spatial = waste(get_mmb(n, i), get_mmb(n, j)) * norm;
            const float textual = textual_cost(get_labels(n, i), get_total_units(n, i),
                                               get_labels(n, j), get_total_units(n, j));
            return m_weight * spatial + (1.0f - m_weight) * textual;
        };

        // Compute the pair that would produce the maximal cost
        // if its entries were to be placed into the same node.
        float max = -1.f;
        u32 left, right;
        left = right = std::numeric_limits<u32>::max();
        for (u32 i = 0; i < N; ++i) {
            for (u32 j = i + 1; j < N; ++j) {
                const float v = cost(i, j);
                if (v > max) {
                    max = v;
                    left = i;
                    right = j;
                }
            }
        }

        geodb_assert(left < N && right < N && left != right, "invalid seed incides");
        return std::make_tuple(left, right);
    }

private:
    template<typename Tree>
    friend class tree_cursor;

    template<typename Tree>
    friend class index_cursor;

private:
    // ----------------------------------------
    //      Simple Access Functions
    // ----------------------------------------

    storage_type& storage() {
        return *m_storage;
    }

    const storage_type& storage() const {
        return *m_storage;
    }

    trajectory_id_type get_id(const entry& d) const {
        return d.trajectory_id;
    }

    /// TODO: More generic?
    bounding_box get_mmb(const entry& d) const {
        return d.unit.get_bounding_box();
    }

    /// TODO: More generic?
    label_type get_label(const entry& d) const {
        return d.unit.label;
    }

    /// Returns the bounding box for an entry of an internal node.
    bounding_box get_mmb(internal_ptr n, u32 i) const {
        return storage().get_mmb(n, i);
    }

    /// Returns the bounding box for an entry of a leaf node.
    bounding_box get_mmb(leaf_ptr n, u32 i) const {
        return get_mmb(storage().get_data(n, i));
    }

    /// Returns the bounding box that contains all entries
    /// of the given node.
    template<typename NodePointer>
    bounding_box get_mmb(NodePointer n) const {
        const u32 count = storage().get_count(n);

        bounding_box b = get_mmb(n, 0);
        for (u32 i = 1; i < count; ++i) {
            b = b.extend(get_mmb(n, i));
        }
        return b;
    }

    /// Returns the index of `child` in its parent.
    /// It is a programming error if the child does not exist in the parent.
    u32 index_of(internal_ptr parent, node_ptr child) const {
        if (auto i = optional_index_of(parent, child)) {
            return *i;
        }
        unreachable("child not found");
    }

    /// Returns the index of `child` in its parent.
    boost::optional<u32> optional_index_of(internal_ptr parent, node_ptr child) const {
        const u32 count = storage().get_count(parent);
        for (u32 i = 0; i < count; ++i) {
            if (storage().get_child(parent, i) == child) {
                return i;
            }
        }
        return {};
    }

    /// Returns the enlargement that would occurr for the bounding box `e`
    /// should the object represented by `b` be inserted.
    float enlargement(const bounding_box& e, const bounding_box& b) const {
        return e.extend(b).size() - e.size();
    }

    /// Inspects all child entries of the internal node
    /// and measures the enlargement that occurrs
    /// should `b` be inserted into the child's subtree.
    float max_enlargement(internal_ptr n, const bounding_box& b) const {
        const u32 count = storage().get_count(n);
        geodb_assert(count > 0, "empty node");

        float max = enlargement(get_mmb(n, 0), b);
        for (u32 i = 1; i < count; ++i) {
            max = std::max(max, enlargement(get_mmb(n, i), b));
        }

        geodb_assert(max >= 0, "invalid enlargement value");
        return max;
    }

    /// Returns the amount of wasted space should the entries
    /// represented by `a` and `b` be stored in the same node.
    /// This is used by the node splitting algorithm to find the seeds
    /// for the old and the newly created node.
    float waste(const bounding_box& a, const bounding_box& b) const {
        const auto mmb = a.extend(b);
        return std::max(mmb.size() - a.size() - b.size(), 0.f);
    }

    /// Returns the maximal waste value for all bounding box combinations.
    /// Used for normalization of the individual values.
    template<typename Node>
    float max_waste(const Node& n) const {
        const u32 count = get_count(n);
        geodb_assert(count >= 2, "must have at least 2 entries");

        float max = std::numeric_limits<float>::lowest();
        for (u32 i = 0; i < count; ++i) {
            for (u32 j = i + 1; j < count; ++j) {
                max = std::max(max, waste(get_mmb(n, i), get_mmb(n, j)));
            }
        }
        return max;
    }

    /// Returns the spatial cost of inserting the new bounding box `b` into an existing
    /// subtree with bounding box `mmb`.
    /// Uses `norm` for normalization. 
    float spatial_cost(const bounding_box& mmb, const bounding_box& b, float norm) {
        return enlargement(mmb, b) * norm;
    }

    /// Returns the textual cost for inserting a label into an existing subtree.
    ///
    /// \param unit_count
    ///     The number of units in that subtree that have that label.
    /// \param total_count
    ///     The total number of units in that subtree. Must not be zero.
    float textual_cost(u64 unit_count, u64 total_count) const {
        geodb_assert(total_count > 0, "there can be no empty subtrees");
        return 1.0f - float(unit_count) / float(total_count);
    }

    /// Generalized textual cost function.
    /// Computes the cost of merging two entire subtrees.
    ///
    /// \param labels1
    ///     A sorted range of <label, count> objects that contains
    ///     an entry for every label in the first subtree.
    /// \param total_count1
    ///     The total number of units in that subtree.
    /// \param labels2
    ///     A sorted range of <label, count> objects that contain
    ///     an entry for every label in the second subtree.
    /// \param total_count2
    ///     The total number of units in that subtree.
    template<typename LabelCount1, typename LabelCount2>
    float textual_cost(const LabelCount1& labels1, u64 total_count1,
                       const LabelCount2& labels2, u64 total_count2) const
    {
        const float total = total_count1 + total_count2;

        float max = 0;
        shared_labels(labels1, labels2, [&](label_type label, u64 count1, u64 count2) {
            unused(label);
            max = std::max(max, float(count1 + count2) / total);
        });
        return 1.0f - max;
    }

    /// Iterate over two sequences of <label, count> entries. The ranges must be sorted.
    /// The callback will be invoked for every pair of elements with the same label.
    template<typename LabelMap1, typename LabelMap2, typename Callback>
    void shared_labels(const LabelMap1& map1, const LabelMap2& map2, Callback&& cb) const {
        auto iter1 = map1.begin(), end1 = map1.end();
        auto iter2 = map2.begin(), end2 = map2.end();

        while (iter1 != end1 && iter2 != end2) {
            const label_count l1 = *iter1;
            const label_count l2 = *iter2;
            if (l1.label < l2.label) {
                ++iter1;
                geodb_assert(iter1 == end1 || iter1->label > l1.label, "sorted");
            } else if (l1.label > l2.label) {
                ++iter2;
                geodb_assert(iter2 == end2 || iter2->label > l2.label, "sorted");
            } else {
                cb(l1.label, l1.count, l2.count);
                ++iter1;
                ++iter2;
                geodb_assert(iter1 == end1 || iter1->label > l1.label, "sorted");
                geodb_assert(iter2 == end2 || iter2->label > l2.label, "sorted");
            }
        }
    }

    /// Returns the inverse of `value` or 0 if the value is very close to zero.
    /// This is used when 1.0 / value is intended as a normalization factor.
    static float inverse(float value) {
        static constexpr float min = std::numeric_limits<float>::epsilon() / 2.0f;
        if (value <= min)
            return 0;
        return 1.0f / value;
    }

private:
    float m_weight = 0.5; ///< Weight for mixing textual and spatial cost functions.
    movable_adapter<storage_type> m_storage;
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
            indent() << "Child " << i << ": " << c.mmb(i) << "\n";
            dump(o, c.child(i), indent_length+1);
        }
    } else {
        indent() << "Type: Leaf\n";
        const size_t size = c.size();
        for (size_t i = 0; i < size; ++i) {
            entry e = c.value(i);
            indent() << "Child " << i << ": " << e.trajectory_id << "[" << e.unit_index << "] " << e.unit << "\n";
        }
    }
}

} // namespace geodb

#endif // IRWI_TREE_HPP
