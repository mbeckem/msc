#ifndef GEODB_IRWI_BULK_LOAD_COMMON_HPP
#define GEODB_IRWI_BULK_LOAD_COMMON_HPP

#include "geodb/algorithm.hpp"
#include "geodb/bounding_box.hpp"
#include "geodb/common.hpp"
#include "geodb/external_list.hpp"
#include "geodb/trajectory.hpp"
#include "geodb/type_traits.hpp"
#include "geodb/irwi/tree.hpp"
#include "geodb/utility/noop.hpp"

#include <tpie/serialization2.h>
#include <tpie/serialization_stream.h>

#include <boost/optional.hpp>

/// \file
/// Common definitions used by different bulk loading strategies.

namespace geodb {

template<typename Tree, typename Derived>
class bulk_load_common {
protected:
    using tree_type = Tree;
    using state_type = typename tree_type::state_type;
    using storage_type = typename state_type::storage_type;

    using node_ptr = typename state_type::node_ptr;
    using leaf_ptr = typename state_type::leaf_ptr;
    using internal_ptr = typename state_type::internal_ptr;

    using posting_type = typename state_type::posting_type;
    using posting_data_type = typename posting_type::data_type;

    using index_builder = typename storage_type::index_builder_type;
    using index_builder_ptr = typename storage_type::index_builder_ptr;

    using const_index_ptr = typename state_type::const_index_ptr;

    using const_list_ptr = typename state_type::const_list_ptr;
    using list_type = typename state_type::list_type;
    using list_summary = typename list_type::summary_type;

    /// Represents a complete subtree assembled by the bulk loading algorithm.
    /// This subtree will be inserted into the existing tree at the appropriate level.
    struct subtree_result {
        node_ptr root;
        size_t height;
        size_t size;

        subtree_result(node_ptr root, size_t height, size_t size)
            : root(root)
            , height(height)
            , size(size)
        {
            geodb_assert(height > 0, "invalid height");
            geodb_assert(size > 0, "empty subtree");
        }
    };

    /// The precomputed summary of a lower-level node.
    struct node_summary {
        node_ptr ptr{};
        bounding_box mbb;
        posting_data_type total;     ///< Summary of `index->total`
        u64 labels_begin = 0;
        u64 labels_size = 0;
    };

    /// The precomputed summary of a <label, postings_list> pair.
    struct label_summary {
        label_type label{};
        posting_data_type summary;
    };

    /// The transformation of a label summary object
    /// to a label combined with the complete posting.
    /// This can be done when the index of the child
    /// in its parent is known.
    struct label_posting {
        label_type label{};
        posting_type posting;

        label_posting(label_type label, const posting_type& posting)
            : label(label)
            , posting(posting)
        {}

        // Ordered by label. Items with the same label
        // are ordered by their node index.
        bool operator<(const label_posting& other) const {
            if (label != other.label) {
                return label < other.label;
            }
            return posting.node() < other.posting.node();
        }
    };

    using label_summary_list = external_list<label_summary, storage_type::get_block_size()>;

    struct level_files {
        temp_dir directory;
        fs::path summary_file;
        fs::path label_summary_dir;

        level_files()
            : directory()
            , summary_file(directory.path() / "summaries.data")
            , label_summary_dir(ensure_directory(directory.path() / "label_summaries"))
        {}
    };

    struct last_level_streams {
        tpie::file_stream<node_summary> summaries;
        label_summary_list label_summaries;

        last_level_streams(const level_files& files)
            : summaries()
            , label_summaries(files.label_summary_dir, state_type::max_internal_entries(), true)
        {
            summaries.open(files.summary_file.string(), tpie::open::read_only);
        }

    };

    struct next_level_streams {
        tpie::file_stream<node_summary> summaries;
        label_summary_list label_summaries;

        next_level_streams(const level_files& files)
            : summaries()
            , label_summaries(files.label_summary_dir, 2)
        {
            summaries.open(files.summary_file.string());
        }
    };


public:
    /// Loads the given stream of leaf entries into the tree referenced by this loader.
    ///
    /// This is the main bulk loading function and should be called by the user.
    /// The implementation depends on the concrete subclass.
    void load(tpie::file_stream<tree_entry>& entries) {
        const u64 size = entries.size();

        if (size == 0) {
            return;
        }

        entries.seek(0);
        subtree_result result = derived()->load_impl(entries);
        geodb_assert(result.size == size, "must have loaded entire entry set");

        tree_insertion<state_type> inserter{state()};
        inserter.insert_node(result.root, result.height, size);
    }

protected:
    /// An iterator that visits the label summaries of a single node.
    /// It maps the summary to a posting automatically.
    class label_iterator : public boost::iterator_facade<
            label_iterator,
            label_posting,
            std::input_iterator_tag,
            const label_posting&>
    {
        /// The file we're iterating over.
        const label_summary_list* m_file = nullptr;

        /// The node we belong to (i.e. its index in its parent tree node).
        u32 m_node = 0;

        /// The current position in the file.
        u64 m_position = 0;

        /// The marker for the end of this sublist.
        u64 m_end = 0;

        /// We cache the current entry to avoid excessive disk
        /// operations.
        mutable boost::optional<label_posting> m_cached;

    public:
        label_iterator() = default;

        label_iterator(const label_summary_list& file,
                       u32 node, u64 position, u64 end)
            : m_file(&file)
            , m_node(node)
            , m_position(position)
            , m_end(end)
        {
            geodb_assert(m_position <= m_end, "position out of bounds");
            geodb_assert(m_end <= file.size(), "list size out of bounds");
        }

    private:
        friend class boost::iterator_core_access;

        const label_posting& dereference() const {
            geodb_assert(m_file, "dereferencing invalid iterator");
            geodb_assert(m_position != m_end, "dereferencing end iterator");
            if (!m_cached) {
                const label_summary ls = (*m_file)[m_position];
                m_cached.emplace(ls.label, posting_type(m_node, ls.summary));
            }
            return *m_cached;
        }

        void increment() {
            geodb_assert(m_file, "incrementing invalid iterator");
            geodb_assert(m_position != m_end, "position out of bounds");

            ++m_position;
            m_cached.reset();
        }

        bool equal(const label_iterator& other) const {
            geodb_assert(m_file == other.m_file,
                         "comparing iterators of different files");
            geodb_assert(m_node == other.m_node,
                         "comparing iterators of different nodes");
            geodb_assert(m_end == other.m_end,
                         "comparing iterators of different sublists");
            return m_position == other.m_position;
        }
    };

    /// Build an internal node from a vector of child node summaries.
    /// The summaries themselves are of fixed size, so this still uses constant memory
    /// (the upper bound is the tree's fanout).
    ///
    /// Every child may have many labels in it's subtree, which means an
    /// unbounded number of label_summary entries.
    /// However, these entries can be processed incrementally, thus using
    /// only constant memory (but linear time).
    ///
    /// The node_summaries point to a contiguous sequence of label_summaries
    /// in the given external list.
    ///
    /// This function returns a pointer to the new internal node.
    internal_ptr build_internal_node(const std::vector<node_summary>& summaries,
                                     const label_summary_list& label_summaries)
    {
        geodb_assert(summaries.size() <= state_type::max_internal_entries(),
                     "Too many entries for an internal node");

        internal_ptr node = storage().create_internal();
        index_builder_ptr builder = storage().index_builder(node);

        // A range of ranges. Every individual range is sorted.
        std::vector<boost::iterator_range<label_iterator>> child_labels;
        child_labels.reserve(summaries.size());

        // Iterate over every child summary and fill the new node
        // with bounding boxes and pointers.
        // Save the label summary range for later use.
        {
            u32 count = 0;
            for (const node_summary& ns : summaries) {
                storage().set_mbb(node, count, ns.mbb);
                storage().set_child(node, count, ns.ptr);
                builder->total().append(posting_type(count, ns.total));

                u64 labels_begin = ns.labels_begin;
                u64 labels_end = labels_begin + ns.labels_size;
                child_labels.emplace_back(label_iterator(label_summaries, count, labels_begin, labels_end),
                                          label_iterator(label_summaries, count, labels_end, labels_end));

                ++count;
            }
            storage().set_count(node, count);
        }

        {
            // We now iterate over the labels of every child in sorted order.
            // In other words, we visit each label as a single group.
            // For every label, we see every posting that has this label sequentially.
            boost::optional<label_type> current_label;
            boost::optional<list_type> current_list;
            for_each_sorted(child_labels, [&](const label_posting& lp) {
                if (!current_label || *current_label != lp.label) {
                    current_list.emplace(builder->push(lp.label));
                    current_label = lp.label;
                }
                current_list->append(lp.posting);
            });

            builder->build();
        }
        return node;
    }

protected:
    /// Writes the node summary of `leaf` to the given output file.
    void write_summary(next_level_streams& streams, leaf_ptr leaf)
    {
        geodb_assert(streams.summaries.offset() == streams.summaries.size(),
                     "File must be positioned at the end.");

        // Keep summaries for single leaves in memory.
        // The memory used for this is in O(leaf_size) and thus very small.
        std::vector<trajectory_id_type> all_ids;
        std::map<label_type, std::vector<trajectory_id_type>> label_ids;

        node_summary& node_sum = m_node_summary_buf;
        label_summary& label_sum = m_label_summary_buf;

        const u32 count = storage().get_count(leaf);
        for (u32 i = 0; i < count; ++i) {
            tree_entry entry = storage().get_data(leaf, i);

            all_ids.push_back(entry.trajectory_id);
            label_ids[entry.unit.label].push_back(entry.trajectory_id);
        }

        // Create a new node entry containing index information and mbb
        // and write it to the file.
        node_sum.ptr = leaf;
        node_sum.mbb = state().get_mbb(leaf);
        node_sum.labels_begin = streams.label_summaries.size();
        node_sum.labels_size = label_ids.size();
        node_sum.total = make_summary(all_ids, count);
        streams.summaries.write(node_sum);

        // Write a summary for each label.
        for (auto& pair : label_ids) {
            label_sum.label = pair.first;
            label_sum.summary = make_summary(pair.second, pair.second.size());
            streams.label_summaries.append(label_sum);
        }
    }

    /// Writes the node summary of `internal` to the given output file.
    void write_summary(next_level_streams& streams, internal_ptr internal)
    {
        geodb_assert(streams.summaries.offset() == streams.summaries.size(),
                     "File must be positioned at the end.");

        node_summary& node_sum = m_node_summary_buf;
        label_summary& label_sum = m_label_summary_buf;

        const_index_ptr index = storage().const_index(internal);

        node_sum.ptr = internal;
        node_sum.mbb = state().get_mbb(internal);
        node_sum.total = make_summary(index->total());
        node_sum.labels_begin = streams.label_summaries.size();
        node_sum.labels_size = index->size();
        streams.summaries.write(node_sum);

        // Summarize every label.
        for (auto entry : *index) {
            label_sum.label = entry.label();
            label_sum.summary = make_summary(entry.postings_list());
            streams.label_summaries.append(label_sum);
        }
    }

private:

    posting_data_type make_summary(const_list_ptr list) {
        list_summary sum = list->summarize();
        posting_data_type data;
        data.count(sum.count);
        data.id_set(sum.trajectories);
        return data;
    }

    posting_data_type make_summary(std::vector<trajectory_id_type>& ids, u64 units) {
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

        posting_data_type data;
        data.count(units);
        data.id_set({ids.begin(), ids.end()});
        return data;
    }

protected:
    /// Reads one node pointer from the given input file.
    /// The file must be readable.
    node_ptr read_root_ptr(tpie::file_stream<node_summary>& summaries) {
        geodb_assert(summaries.size() == 1, "file contains more than one node summary.");
        geodb_assert(summaries.offset() == 0, "file must be positioned at the start.");

        return summaries.read().ptr;
    }

protected:
    tree_type& tree() const { return m_tree; }
    state_type& state() const { return tree().state; }
    storage_type& storage() const { return state().storage(); }

private:
    Derived* derived() { return static_cast<Derived*>(this); }

protected:
    explicit bulk_load_common(tree_type& tree)
        : m_tree(tree)
    {}

private:
    tree_type& m_tree;
    node_summary m_node_summary_buf;
    label_summary m_label_summary_buf;
};

} // namespace geodb

#endif // GEODB_IRWI_BULK_LOAD_COMMON_HPP
