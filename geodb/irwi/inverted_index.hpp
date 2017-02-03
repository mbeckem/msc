#ifndef IRWI_INVERTED_INDEX_HPP
#define IRWI_INVERTED_INDEX_HPP

#include "geodb/filesystem.hpp"
#include "geodb/irwi/base.hpp"
#include "geodb/irwi/postings_list.hpp"
#include "geodb/utility/file_allocator.hpp"
#include "geodb/utility/movable_adapter.hpp"

#include <boost/optional.hpp>
#include <boost/iterator/iterator_adaptor.hpp>
#include <boost/noncopyable.hpp>

#include <map>
#include <ostream>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace geodb {

/// An inverted file belongs to an internal node of the IRWI tree.
/// This datastructure stores a \ref postings_list for each label
/// stored in the subtree of the owning internal node.
///
/// \tparam StorageSpec
///     The storage specifier (internal or external).
/// \tparam Lambda
///     The maximum number of intervals for each posting's id set.
template<typename StorageSpec, u32 Lambda>
class inverted_index {
    using storage_type = typename StorageSpec::template implementation<Lambda>;

    using storage_iterator = typename storage_type::iterator_type;

    /// A (const or non-const) reference points to a valid entry
    /// of the inverted index.
    /// The postings list can be opened by calling \ref postings_list().
    /// The list must only not be opened more than once.
    ///
    /// The non-const version of this class returns a mutable handle to the
    /// postings list. The label cannot be modified in either version.
    ///
    /// Use the `reference` or `const_reference` typedefs to refer to this type.
    template<bool is_const>
    class reference_impl {
        using index_type = std::conditional_t<is_const, const inverted_index, inverted_index>;

    public:
        using list_handle = std::conditional_t<is_const,
            typename inverted_index::const_list_handle,
            typename inverted_index::list_handle>;

        // convertible non-const -> const
        reference_impl(const reference_impl<false>& other)
            : index(other.index), base(other.base) {}

        label_type label() const {
            return index->get_label(base);
        }

        list_handle postings_list() const {
            return index->get_postings_list(base);
        }

    private:
        friend class inverted_index;

        reference_impl(index_type* index, const storage_iterator& base)
            : index(index), base(base) {}

        index_type* index;
        storage_iterator base;
    };

    /// A (const or non-const) iterator that points to some
    /// entry of the inverted index. Dereferencing such an iterator
    /// returns a \ref reference_impl.
    ///
    /// Use the `iterator` or `const_iterator` typedefs to refer to this type.
    template<bool is_const>
    class iterator_impl : public boost::iterator_adaptor<
            iterator_impl<is_const>,    // Self (CRTP)
            storage_iterator,           // Base iterator
            reference_impl<is_const>,   // Value
            boost::use_default,         // Traversal
            reference_impl<is_const>>   // Reference
    {

        using index_type = std::conditional_t<is_const, const inverted_index, inverted_index>;

    public:
        iterator_impl() = default;

        // convertible non-const -> const
        iterator_impl(const iterator_impl<false>& other):
            iterator_impl(other.index, other.base()) {}

    private:
        friend class boost::iterator_core_access;

        reference_impl<is_const> dereference() const {
            geodb_assert(index, "derefencing invalid iterator");
            return reference_impl<is_const>(index, this->base());
        }

    private:
        friend class inverted_index;

        iterator_impl(index_type* index, storage_iterator base)
            : iterator_impl::iterator_adaptor(std::move(base))
            , index(index) {}

        index_type* index = nullptr;
    };

public:
    using iterator = iterator_impl<false>;
    using const_iterator = iterator_impl<true>;

    using reference = reference_impl<false>;
    using const_reference = reference_impl<true>;

    using list_type = typename storage_type::list_type;
    using list_handle = typename storage_type::list_handle;
    using const_list_handle = typename storage_type::const_list_handle;

    using posting_type = typename list_type::posting_type;

public:
    iterator begin() { return iterator(this, storage().begin()); }
    iterator end() { return iterator(this, storage().end()); }

    const_iterator begin() const { return const_iterator(this, storage().begin()); }
    const_iterator end() const { return const_iterator(this, storage().end()); }

    /// Returns an iterator pointing to the postings list of the given label
    /// or \ref end() if there is no such list.
    iterator find(label_type label) {
        return convert(static_cast<const inverted_index&>(*this).find(label));
    }

    /// \copydoc find() const
    const_iterator find(label_type label) const {
        return const_iterator(this, storage().find(label));
    }

    /// Creates a new postings list for the given label.
    /// The label must *not* already exist.
    /// Returns an iterator pointing to the new list.
    ///
    /// \pre `find(label) == end()`.
    iterator create(label_type label) {
        geodb_assert(find(label) == end(), "label entry must not exist");
        return iterator(this, storage().create(label));
    }

    /// Returns the iterator to the entry associated with the label.
    /// If no such entry exists, a new one will be created.
    iterator find_or_create(label_type label) {
        auto iter = find(label);
        if (iter == end()) {
            return create(label);
        }
        return iter;
    }

    /// Returns a handle to the `total` list.
    /// The total list contains an entry for every child of the node.
    list_handle total() {
        return storage().total_list();
    }

    /// \copydoc total()
    const_list_handle total() const {
        return storage().const_total_list();
    }

    /// Queries the inverted file for entries matching any of the given labels.
    /// \param labels
    ///     The query labels. Any entry matching at least one of the labels
    ///     will be returned.
    /// \param[out] entries
    ///     The map will be filled with an element for each matching child entry.
    ///     The index of the child entry is used as the map key. The map
    ///     value is the union of trajectory identifiers stored within
    ///     the entry's subtree.
    void matching_children(const std::unordered_set<label_type>& labels,
                           std::unordered_map<entry_id_type, interval_set<trajectory_id_type>>& entries) const
    {
        entries.clear();

        // inserts a new entry or updates the existing one.
        auto insert_or_merge = [&](const posting_type& p) {
            auto iter = entries.find(p.node());
            if (iter == entries.end()) {
                entries.emplace(p.node(), p.id_set());
                return;
            }

            auto& set = iter->second;
            set = set.union_with(p.id_set());
        };

        const auto e = end();
        for (label_type label : labels) {
            auto iter = find(label);
            if (iter == e) {
                continue;
            }

            auto list = iter->postings_list();
            std::for_each(list->begin(), list->end(), insert_or_merge);
        }
    }

private:
    // const iterator -> iterator private conversion.
    iterator convert(const_iterator i) {
        geodb_assert(i.index == this, "iterator belongs to different container");
        return iterator(this, i.base());
    }

    label_type get_label(const storage_iterator& i) const {
        return storage().label(i);
    }

    list_handle get_postings_list(const storage_iterator& i) {
        return storage().list(i);
    }

    const_list_handle get_postings_list(const storage_iterator& i) const {
        return storage().const_list(i);
    }

    storage_type& storage() {
        return *m_storage;
    }

    const storage_type& storage() const {
        return *m_storage;
    }

public:
    inverted_index(StorageSpec s = StorageSpec())
        : m_storage(in_place_t(), std::move(s)) {}

private:
    movable_adapter<storage_type> m_storage;
};

} // namespace geodb

#endif // IRWI_INVERTED_INDEX_HPP
