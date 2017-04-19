#ifndef GEODB_IRWI_BULK_LOAD_QUICKLOAD_HPP
#define GEODB_IRWI_BULK_LOAD_QUICKLOAD_HPP

#include "geodb/common.hpp"
#include "geodb/external_list.hpp"
#include "geodb/irwi/base.hpp"
#include "geodb/irwi/bulk_load_common.hpp"
#include "geodb/irwi/label_count.hpp"
#include "geodb/irwi/tree_state.hpp"
#include "geodb/irwi/tree_insertion.hpp"
#include "geodb/irwi/tree_quickload.hpp"
#include "geodb/utility/file_allocator.hpp"
#include "geodb/utility/temp_dir.hpp"
#include "geodb/utility/range_utils.hpp"

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <gsl/span>
#include <tpie/file_stream.h>
#include <tpie/progress_indicator_base.h>
#include <tpie/queue.h>
#include <tpie/serialization_stream.h>
#include <tpie/tempname.h>

#include <unordered_map>

/// \file
/// Contains an implemention of the quickload algorithm.

namespace geodb {

/// A quick_load_tree is a small, in-memory version of an IRWI tree.
/// Values are inserted as usual until the tree reaches a certain number of leaves,
/// at which point all leaf nodes are flushed to disk and then deleted from internal memory.
/// However, the internal nodes are retained to distribute any remaining values.
template<typename Value, typename Accessor, size_t block_size, u32 fanout_leaf, u32 fanout_internal>
class quick_load_tree {
    // TODO: Make our own storage implementation derived from the normal internal storage.
    // Inverted indices and postings can grow large and should then be stored in external memory.
    using storage_spec = quickload::tree_storage<block_size, fanout_leaf, fanout_internal>;

    using value_type = Value;

    // We use Lambda == 0 because the id sets are unneccesary (the tree
    // is never going to be queried).
    using state_type = tree_state<storage_spec, Value, Accessor, 0>;

    using storage_type = typename state_type::storage_type;

    using node_ptr = typename state_type::node_ptr;

    using internal_ptr = typename state_type::internal_ptr;

    using leaf_ptr = typename state_type::leaf_ptr;

public:
    /// Unique identifier for a node in this tree.
    using node_id = typename state_type::node_id;

private:
    /// Small IRWI-Tree in internal memory.
    /// Always has at most m_max_leaves leaf nodes.
    state_type m_state;

    /// Holds node paths when inserting new values.
    std::vector<internal_ptr> m_path_buf;

    /// True after the leaves have been flushed.
    /// At this point, entries are inserted into overflow buckets.
    bool m_leaves_flushed = false;

public:
    quick_load_tree(Accessor accessor, double weight, size_t posting_cache_blocks)
        : m_state(storage_spec(posting_cache_blocks), std::move(accessor), weight)
    {}

    /// Inserts the value into the tree. Grows the tree if necessary.
    /// Can only be used before the leaves have been flushed.
    /// The number of leaves must not have reached the limit.
    void insert(const value_type& v) {
        geodb_assert(!m_leaves_flushed, "Leaves have already been flushed");
        tree_insertion<state_type>(state()).insert(v, m_path_buf);
    }

    /// Find a leaf for the insertion of `v` but do not actually insert the value.
    /// Instead, update the parents of the leaf to reflect the insertion and return
    /// the leaf node's unique id.
    /// Can only be used after the leaves have been flushed.
    node_id simulate_insert(const value_type& v) {
        geodb_assert(m_leaves_flushed, "Leaves should have been flushed.");

        // Find the leaf, but dont insert the value there.
        tree_insertion<state_type> ins(state());
        std::vector<internal_ptr>& path = m_path_buf;
        leaf_ptr leaf = ins.traverse_tree(v, path);

        // Return the leaf id to the caller so that the value can be placed into the
        // appropriate bucket.
        return storage().get_id(leaf);
    }

    /// Invokes the callback for all leaves of this tree and then delete
    /// them all.
    /// The callback will be invoked with two arguments: the current leaf's id
    /// and a gsl::span<const value_type> holding all values of that leaf.
    template<typename Callback>
    void flush_leaves(Callback&& cb) {
        geodb_assert(!m_leaves_flushed, "Leaves were already flushed");
        m_leaves_flushed = true;

        std::array<value_type, fanout_leaf> values;
        for_each_leaf([&](leaf_ptr leaf) {
            const u32 count = storage().get_count(leaf);
            for (u32 i = 0; i < count; ++i) {
                values[i] = storage().get_data(leaf, i);
            }

            node_id id = storage().get_id(leaf);
            auto span = gsl::span<const value_type>(values).first(count);
            cb(id, span);
        });

        storage().cut_leaves();
    }

    /// Returns the current number of leaf nodes.
    size_t leaf_node_count() const {
        return storage().get_leaf_count();
    }

private:
    state_type& state() { return m_state; }

    storage_type& storage() { return state().storage(); }

    const storage_type& storage() const { return m_state.storage(); }

    template<typename Callback>
    void for_each_leaf(Callback&& cb) {
        if (storage().get_height() == 0) {
            return;
        }

        if (storage().get_height() == 1) {
            cb(storage().to_leaf(storage().get_root()));
            return;
        } 
        
        for_each_leaf(storage().to_internal(storage().get_root()), 1, cb);        
    }

    template<typename Callback>
    void for_each_leaf(internal_ptr n, size_t height, Callback&& cb) {
        geodb_assert(height < storage().get_height(), "must not reach leaf level");

        const u32 count = storage().get_count(n);

        if (height + 1 == storage().get_height()) {
            // Next level is the leaf level.
            for (u32 i = 0; i < count; ++i) {
                cb(storage().to_leaf(storage().get_child(n, i)));
            }
            return;
        }

        // Next level contains internal nodes.
        for (u32 i = 0; i < count; ++i) {
            for_each_leaf(storage().to_internal(storage().get_child(n, i)), height + 1, cb);
        }
    }
};

namespace quick_load_pass_ {

using namespace boost::multi_index;

struct id_tag {};
struct order_tag {};

// Node states are indexed by their node id (for fast lookup)
// and can be traversed in insertion order (for deterministic order).
// Otherwise, the order would be unspecified because the node ids are based
// on heap pointers.
template<typename NodeState, typename NodeId>
using container = multi_index_container<
    NodeState,
    indexed_by<
        ordered_unique<tag<id_tag>, member<
            NodeState, NodeId, &NodeState::id
        >>,
        sequenced<tag<order_tag>>
    >
>;

} // namespace quick_load_pass_

/// A quick load pass reads node entries from a lower level and combines them
/// into nodes for the current level.
/// This process is repeated until only one node remains.
/// This node becomes the root of the tree.
template<typename Value, typename Accessor, size_t block_size, u32 fanout_leaf, u32 fanout_internal>
class quick_load_pass {
    using tree_type = quick_load_tree<Value, Accessor, block_size, fanout_leaf, fanout_internal>;

    using node_id = typename tree_type::node_id;

    using bucket_allocator = file_allocator<u64>;

public:
    using file_stream = tpie::file_stream<Value>;

private:
    struct node_state : boost::noncopyable {
        /// The leaf that this bucket belongs to.
        /// The content of the leaf is stored in a contiguous
        /// subrange of m_leaf_buffer.
        node_id id = 0;

        /// Offset of the first entry of the original leaf.
        u64 value_offset = 0;

        /// Number of entries of the original leaf.
        u64 value_count = 0;

        /// True if the bucket has been initialized.
        mutable bool has_bucket = false;

        /// The id of the bucket (if any), allocated using m_bucket_alloc.
        mutable u64 bucket_id = 0;

        /// Handle to the bucket (if any).
        mutable file_stream bucket;

        node_state(node_id id,
                   u64 value_offset,
                   u64 value_count)
            : id(id)
            , value_offset(value_offset)
            , value_count(value_count)
        {}
    };

    using id_tag = quick_load_pass_::id_tag;
    using order_tag = quick_load_pass_::order_tag;
    using node_map = quick_load_pass_::container<node_state, node_id>;

    using id_iterator = typename node_map::template index_iterator<id_tag>::type;
    using order_iterator = typename node_map::template index_iterator<order_tag>::type;

private:
    /// Contains the temporary bucket files.
    temp_dir m_bucket_dir;

    /// Allocates bucket files in `m_bucket_dir`.
    bucket_allocator m_bucket_alloc;

    /// Contains leaves that have been flushed to disk.
    file_stream m_leaf_buffer;

    /// Mapping from leaf id to node state instance.
    /// Used after the leaves of a tree have been flushed to disk.
    node_map m_nodes;

    /// A small IRWI tree that fits into memory.
    /// It is wrapped into an optional<T> to emulate
    /// move assignment.
    boost::optional<tree_type> m_tree;

    /// Maximum number of leaf nodes until we switch to external buckets.
    size_t m_max_leaves = 0;

    /// Number of cached blocks for all internal nodes.
    size_t m_cache_blocks = 0;

    // Tree parameters
    Accessor m_accessor;
    double m_weight = 0;

public:
    quick_load_pass(size_t max_leaves, size_t cache_blocks, Accessor accessor, double weight)
        : m_bucket_dir("buckets")
        , m_bucket_alloc(m_bucket_dir.path(), ".bucket")
        , m_max_leaves(max_leaves)
        , m_cache_blocks(cache_blocks)
        , m_accessor(std::move(accessor))
        , m_weight(weight)
    {
        clear_tree();
        m_leaf_buffer.open();
    }

    /// Reads instances of `Value` from the source and pushes
    /// groups of `Value` to the target.
    /// These groups represent leaf nodes assembled by the algorithm.
    /// The target is responsible for actually creating the
    /// appropriate nodes on disk.
    ///
    /// \param source
    ///     A stream containing leaf values for the current pass.
    /// \param target
    ///     A function object that accepts a gsl::span<const Value>.
    ///     Every span represents the content of a single leaf node
    ///     created by the algorithm.
    template<typename NextLevel>
    void run(file_stream& source, NextLevel&& target) {
        // The todo queue contains references to the buckets that
        // still have to be processed in the future.
        tpie::queue<u64> todo;
        run(source, target, todo);
        if (todo.empty()) {
            // The whole tree fit into memory.
            return;
        }

        // Handle entries in todo. The files were allocated
        // as buckets and have to be freed individually.
        // Recursive calls to run() might add their own new
        // entries into `todo`.
        file_stream bucket;
        while (!todo.empty()) {
            const u64 bucket_id = todo.pop();
            open_bucket(bucket, bucket_id);

            run(bucket, target, todo);

            bucket.close();
            free_bucket(bucket_id);
        }
    }

private:
    /// Read values from `source` and create leaf nodes, which are then being passed to `target`.
    /// References to buckets that still have to be handled will be saved into `todo`.
    template<typename NextLevel>
    void run(file_stream& source, NextLevel&& target, tpie::queue<u64>& todo) {
        source.seek(0);

        while (source.can_read() && m_tree->leaf_node_count() < m_max_leaves) {
            m_tree->insert(source.read());
        }

        if (!source.can_read()) {
            // No more values, i.e. the entire source dataset fit.
            m_tree->flush_leaves([&](node_id leaf, gsl::span<const Value> data) {
                unused(leaf);
                target(data);
            });
            clear_tree();
            return;
        }

        // Flush all leaves to disk and remember their state in m_nodes.
        // They do not yet have associated buckets - those are only constructed when needed.
        m_tree->flush_leaves([this](node_id leaf, gsl::span<const Value> data) {
            create_state(leaf, data);
        });

        // Put all remaining entries into buckets.
        while (source.can_read()) {
            const Value& v = source.read();
            node_id leaf = m_tree->simulate_insert(v);
            insert_into_bucket(leaf, v);
        }

        // A leaf with an empty bucket will be forwarded to the next level,
        // the others are placed into the todo list.
        // Iterate in insertion order for deterministic processing.
        for (const node_state& n : m_nodes.template get<order_tag>()) {
            if (!n.has_bucket) {
                forward_leaf(n, target);
            } else {
                geodb_assert(n.bucket_id != 0, "must have a bucket");
                todo.push(n.bucket_id);
            }
        }

        clear_tree();
        m_nodes.clear();
        m_leaf_buffer.truncate(0);
    }

    /// Takes the leaf's entries and forwards them to the target.
    /// The leaf must not have an associated bucket.
    template<typename NextLevel>
    void forward_leaf(const node_state& n, NextLevel&& target) {
        geodb_assert(!n.has_bucket, "Cannot forward nodes with non-empty buckets");
        geodb_assert(n.value_count <= fanout_leaf, "Too many leaf values");

        std::array<Value, fanout_leaf> data;
        m_leaf_buffer.seek(n.value_offset);
        for (u64 i = 0; i < n.value_count; ++i) {
            data[i] = m_leaf_buffer.read();
        }
        target(gsl::span<const Value>(data).first(n.value_count));
    }

    /// Creates a new state for the given leaf.
    /// Appends the leaf entries to the leaf buffer and stores their position.
    void create_state(node_id leaf, gsl::span<const Value> data) {
        geodb_assert(data.size() > 0 && data.size() <= fanout_leaf,
                     "invalid number of leaf entries");
        geodb_assert(m_leaf_buffer.offset() == m_leaf_buffer.size(),
                     "buffer must be at end");

        auto& index = m_nodes.template get<id_tag>();
        id_iterator pos;
        bool inserted;
        std::tie(pos, inserted) = index.emplace(leaf, m_leaf_buffer.offset(), data.size());
        geodb_assert(inserted, "A bucket for the given leaf already exists.");

        for (const Value& v : data) {
            m_leaf_buffer.write(v);
        }
    }

    /// Inserts `v` into the bucket associated with `leaf`.
    /// Lazily creates the bucket on first use.
    void insert_into_bucket(node_id leaf, const Value& v) {
        const node_state& n = find_state(leaf);

        // Allocate and initialize a new bucket if none exists yet.
        if (!n.has_bucket) {
            n.has_bucket = true;
            n.bucket_id = alloc_bucket();
            open_bucket(n.bucket, n.bucket_id);

            // Write the content of the leaf to the start of the new bucket.
            m_leaf_buffer.seek(n.value_offset);
            for (u64 i = 0; i < n.value_count; ++i) {
                n.bucket.write(m_leaf_buffer.read());
            }
        }

        n.bucket.write(v);
    }

    /// Finds the bucket associated with the given leaf.
    const node_state& find_state(node_id leaf) {
        auto& index = m_nodes.template get<id_tag>();
        id_iterator pos = index.find(leaf);
        geodb_assert(pos != index.end(), "There is no bucket for the given leaf.");
        return *pos;
    }

    /// Clear the tree.
    void clear_tree() {
        m_tree.reset();
        m_tree.emplace(m_accessor, m_weight, m_cache_blocks);
    }

    u64 alloc_bucket() {
        return m_bucket_alloc.alloc();
    }

    void free_bucket(u64 id) {
        m_bucket_alloc.free(id);
    }

    void open_bucket(file_stream& bucket, u64 id) {
        bucket.open(m_bucket_alloc.path(id).string());
    }
};

/// An entry of this type represents a complete node of the lower levels,
/// including its mbb and index summary.
/// These entries are used in higher-level passes of the quickload algorithm,
/// where they are treated as ordinary leaf entires (with a different accessor function)
/// in order to form complete "leaves". These pseudo-leaves will then become
/// the internal nodes of the next level.
struct pseudo_leaf_entry {
    /// Points to the complete summary of this node.
    /// This summary contains, among other things, the pointer to the
    /// real node (in external storage) represented by this entry.
    u64 summary_ptr = 0;

    /// Index of the first <label, count> tuple.
    u64 labels_begin = 0;

    /// Number of <label, count> objects.
    u64 labels_size = 0;

    /// Total number of trajectory units in the subtree
    /// represented by this entry.
    u64 unit_count = 0;

    /// The bounding box that contains all items in
    /// the subtree represented by this entry.
    bounding_box mbb;
};

/// The quick load algorithm loads a set of leaf entries by creating small trees in memory
/// (their max size is specified using a constructor parameter).
/// The small tree is used for distributing all entries into buckets, which are in processed
/// using recursion.
///
/// If a bucket receives fewer items than the fanout of the tree, it becomes a node of the
/// tree on disk. This is done for every level of the tree, until only one node remains.
template<typename Tree>
class quick_loader : public bulk_load_common<Tree, quick_loader<Tree>> {
    using common_t = typename quick_loader::bulk_load_common;

    using typename common_t::tree_type;
    using typename common_t::state_type;
    using typename common_t::storage_type;
    using typename common_t::subtree_result;

    using node_ptr = typename state_type::node_ptr;
    using internal_ptr = typename state_type::internal_ptr;
    using leaf_ptr = typename state_type::leaf_ptr;

    using index_ptr = typename state_type::index_ptr;
    using const_index_ptr = typename state_type::const_index_ptr;

    using list_ptr = typename state_type::list_ptr;

    using posting_type = typename state_type::posting_type;

    using node_summary = typename common_t::node_summary;
    using label_summary = typename common_t::label_summary;
    using list_summary = typename common_t::list_summary;

    using file_allocator_type = file_allocator<u64>;

    using label_count_list = external_list<label_count, storage_type::get_block_size()>;

    using leaf_pass_t = quick_load_pass<
        tree_entry, detail::tree_entry_accessor,
        storage_type::get_block_size(),
        tree_type::max_leaf_entries(), tree_type::max_internal_entries()>;

    class pseudo_leaf_entry_accessor;

    using internal_pass_t = quick_load_pass<
        pseudo_leaf_entry, pseudo_leaf_entry_accessor,
        storage_type::get_block_size(),
        tree_type::max_internal_entries(), tree_type::max_internal_entries()>;

    struct level_files : common_t::level_files {
        fs::path entry_file;
        fs::path label_counts_dir;

        level_files()
            : common_t::level_files()
            , entry_file(this->directory.path() / "entries.data")
            , label_counts_dir(ensure_directory(this->directory.path() / "label_counts"))
        {}
    };

    struct next_level_t : common_t::next_level_streams {
        next_level_t(const level_files& files)
            : common_t::next_level_streams(files)
            , label_counts(files.label_counts_dir, 1)
        {
            entries.open(files.entry_file.string());
        }

        tpie::file_stream<pseudo_leaf_entry> entries;
        label_count_list label_counts;
    };

    struct last_level_t : common_t::last_level_streams {
        last_level_t(const level_files& files, size_t cache_blocks)
            : common_t::last_level_streams(files)
            , label_counts(files.label_counts_dir, cache_blocks)
        {
            entries.open(files.entry_file.string());
        }

        tpie::file_stream<pseudo_leaf_entry> entries;
        label_count_list label_counts;
    };

    struct params_t {
        size_t max_leaves;
        size_t max_internal;
        size_t total_cache_blocks;

        params_t(size_t memory, size_t blocks_per_internal) {
            // Available blocks in memory.
            size_t blocks = memory / block_size;

            // Minimum fanout for internal nodes.
            size_t min_fanout = state_type::min_internal_entries();

            // Cost of an internal node (including the additional cache).
            double internal_cost = 1 + blocks_per_internal;

            // Total leaf cost (including additional cost for internal nodes).
            double leaf_cost = 1 + (double(internal_cost) / double(min_fanout - 1));

            max_leaves = blocks / leaf_cost;
            max_internal = max_leaves / (min_fanout - 1);
            total_cache_blocks = max_internal * blocks_per_internal;
        }
    };

public:
    /// Creates a new instance from the given tree and the specified maximum number of nodes.
    ///
    /// \param tree         The target of the bulk loading operation.
    explicit quick_loader(Tree& tree, size_t blocks_per_internal)
        : common_t(tree)
        , m_params(tpie::get_memory_manager().available(), blocks_per_internal)
        , m_weight(tree.weight())
    {
        if (m_params.max_leaves < 2)
            throw std::logic_error("Must have enough space for at least two leaf nodes.");
    }

private:
    friend common_t;

    /// Runs the quickload algorithm on the given set of leaf entries.
    /// Creates the tree bottom-up and invokes the quick_load_pass class
    /// for every individual level.
    subtree_result load_impl(tpie::file_stream<tree_entry>& input) {
        fmt::print("Quickload parameters:\n"
                   "    max-leaves: {}\n"
                   "    max-internal: {}   (approximated worst case)\n"
                   "    cache-blocks: {}\n",
                   m_params.max_leaves,
                   m_params.max_internal,
                   m_params.total_cache_blocks);

        // Points to the files of the current level.
        level_files files;

        u64 count = 0;
        {
            fmt::print("Creating leaf nodes.\n");
            count = create_leaves(input, files);
            geodb_assert(count > 0, "invalid node count");
        }

        // Loop until only one node remains.
        size_t height = 1;
        while (count > 1) {
            fmt::print("Creating internal nodes at height {}.\n", height + 1);

            // Points to the files of the next level.
            level_files next_files;

            count = create_internals(files, next_files);
            geodb_assert(count > 0, "invalid node count");

            files = next_files;
            ++height;
        }

        return subtree_result(get_root(files), height, input.size());
    }

private:
    node_ptr get_root(const level_files& last_level) {
        tpie::file_stream<node_summary> reader;
        reader.open(last_level.summary_file.string());
        return common_t::read_root_ptr(reader);
    }

    /// Create a level of leaf nodes from a sequence of leaf entries by using the
    /// quickload_pass template.
    u64 create_leaves(tpie::file_stream<tree_entry>& source, const level_files& next_files) {
        u64 created_nodes = 0;
        next_level_t next_level(next_files);

        auto node_callback = [&](gsl::span<const tree_entry> entries) {
            geodb_assert(entries.size() > 0, "Entry set is empty");
            geodb_assert(size_t(entries.size()) <= storage_type::max_leaf_entries(),
                         "Too many leaf entries");

            // Create a new leaf in the tree.
            const leaf_ptr leaf = storage().create_leaf();
            const u32 count = entries.size();

            // Count the number of label<->units for the next level.
            std::map<label_type, u32> label_count_map;
            for (u32 i = 0; i < count; ++i) {
                const tree_entry& entry = entries[i];
                storage().set_data(leaf, i, entry);

                ++label_count_map[entry.unit.label];
            }
            storage().set_count(leaf, count);

            const u64 summary_ptr = next_level.summaries.size();
            common_t::write_summary(next_level, leaf);

            label_count_list& label_counts = next_level.label_counts;
            const u64 labels_size = label_count_map.size();
            const u64 labels_begin = label_counts.size();
            for (auto& pair : label_count_map) {
                label_counts.append({ pair.first, pair.second });
            }

            // The pseudo leaf represents this all entries of this leaf
            // and will be used as a leaf entry in the next pass.
            pseudo_leaf_entry entry;
            entry.summary_ptr = summary_ptr;
            entry.labels_begin = labels_begin;
            entry.labels_size = labels_size;
            entry.unit_count = count;
            entry.mbb = state().get_mbb(leaf);
            next_level.entries.write(entry);

            ++created_nodes;
        };

        leaf_pass_t pass(m_params.max_leaves, m_params.total_cache_blocks,
                         detail::tree_entry_accessor(), m_weight);
        pass.run(source, node_callback);
        return created_nodes;
    }

    /// Makes pseudo_leaf_entry instances act like ordinary irwi leaf entries.
    class pseudo_leaf_entry_accessor {
    private:
        struct label_count_iterator;

        label_count_list& label_counts;

    public:
        pseudo_leaf_entry_accessor(label_count_list& label_counts)
            : label_counts(label_counts)
        {}

        trajectory_id_type get_id(const pseudo_leaf_entry& e) const {
            unused(e);
            return 0; // Does not matter, the tree is never queried.
        }

        bounding_box get_mbb(const pseudo_leaf_entry& e) const {
            return e.mbb;
        }

        u64 get_total_count(const pseudo_leaf_entry& e) const {
            return e.unit_count;
        }

        auto get_label_counts(const pseudo_leaf_entry& e) const {
            return boost::make_iterator_range(
                        label_count_iterator(label_counts, e.labels_begin, 0),
                        label_count_iterator(label_counts, e.labels_begin, e.labels_size));
        }

    private:
        /// Iterates over a contiguous list of \ref label_count objects
        /// in a file. The list starts at stream offset `base`.
        class label_count_iterator : public boost::iterators::iterator_facade<
                label_count_iterator,           // Derived
                label_count,                    // Value
                boost::forward_traversal_tag,   // Traversal
                label_count                     // "Reference"
            >
        {
        private:
            label_count_list* label_counts = nullptr;
            u64 base = 0;
            u64 index = 0;

        public:
            label_count_iterator() = default;

            label_count_iterator(label_count_list& label_counts,
                                 u64 base,
                                 u64 index)
                : label_counts(&label_counts)
                , base(base)
                , index(index)
            {}

        private:
            friend class boost::iterator_core_access;

            bool equal(const label_count_iterator& other) const {
                geodb_assert(label_counts == other.label_counts,
                             "Comparing iterators of different files");
                geodb_assert(base == other.base,
                             "Comparing iterators of different lists");
                return index == other.index;
            }

            label_count dereference() const {
                geodb_assert(label_counts, "dereferencing invalid iterator");

                return (*label_counts)[base + index];
            }

            void increment() {
                geodb_assert(label_counts, "incrementing invalid iterator");
                ++index;
            }
        };
    };

    /// Creates a new level of internal nodes by passing the set entries from the last
    /// level to the quick_load_pass class.
    u64 create_internals(const level_files& last_files, const level_files& next_files) {
        static constexpr size_t cache_blocks = tree_type::max_internal_entries() * 4;

        last_level_t last_level(last_files, cache_blocks);
        next_level_t next_level(next_files);

        u64 created_nodes = 0;
        std::vector<node_summary> summaries;
        summaries.reserve(state_type::max_internal_entries());

        auto node_callback = [&](gsl::span<const pseudo_leaf_entry> entries) {
            geodb_assert(entries.size() > 0, "Entry set is empty");
            geodb_assert(size_t(entries.size()) <= storage_type::max_internal_entries(),
                         "Too many pseudo leaf entries");

            // The summaries are not read linearly.
            // We have to follow the summary pointer instead.
            summaries.clear();
            auto& last_summaries = last_level.summaries;
            for (const pseudo_leaf_entry& entry : entries) {
                u64 summary_ptr = entry.summary_ptr;
                last_summaries.seek(summary_ptr);
                summaries.push_back(last_summaries.peek());
            }

            internal_ptr node = common_t::build_internal_node(summaries, last_level.label_summaries);
            const_index_ptr index = storage().const_index(node);

            const u64 summary_ptr = next_level.summaries.size();
            common_t::write_summary(next_level, node);

            const u64 labels_ptr = next_level.label_counts.size();
            for (auto entry : *index) {
                next_level.label_counts.append({entry.label(), entry.postings_list()->summarize_count()});
            }

            // Represents this subtree in the next level.
            pseudo_leaf_entry entry;
            entry.summary_ptr = summary_ptr;
            entry.labels_begin = labels_ptr;
            entry.labels_size = index->size();
            entry.mbb = state().get_mbb(node);
            entry.unit_count = index->total()->summarize_count();
            next_level.entries.write(entry);

            ++created_nodes;
        };

        internal_pass_t pass(m_params.max_leaves, m_params.total_cache_blocks,
                             pseudo_leaf_entry_accessor(last_level.label_counts), m_weight);
        pass.run(last_level.entries, node_callback);
        return created_nodes;
    }

private:
    using common_t::storage;
    using common_t::state;

private:
    /// Quickload parameters (derived from the available memory
    /// and the space per internal node).
    const params_t m_params;

    /// beta
    const double m_weight;
};

} // namespace geodb

#endif // GEODB_IRWI_BULK_LOAD_QUICKLOAD_HPP
