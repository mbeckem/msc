#ifndef GEODB_IRWI_INVERTED_INDEX_INTERNAL_HPP
#define GEODB_IRWI_INVERTED_INDEX_INTERNAL_HPP

#include "geodb/common.hpp"
#include "geodb/irwi/inverted_index.hpp"
#include "geodb/irwi/postings_list.hpp"
#include "geodb/irwi/postings_list_internal.hpp"

/// \file
/// Internal storage backend for the inverted index.

namespace geodb {

class inverted_index_internal_storage;

template<u32 Lambda>
class inverted_index_internal_storage_impl;

class inverted_index_internal_storage {
public:
    inverted_index_internal_storage() = default;

private:
    template<typename StorageSpec, u32 Lambda>
    friend class inverted_index;

    template<u32 Lambda>
    using implementation = inverted_index_internal_storage_impl<Lambda>;

    template<u32 Lambda>
    implementation<Lambda> construct() const {
        return {};
    }
};

/// Internal storage for an inverted index.
/// Backed by an in-memory map of (label -> postings list) entries.
template<u32 Lambda>
class inverted_index_internal_storage_impl {
public:
    // ----------------------------------------
    //      Storage interface for inverted index
    // ----------------------------------------

    using list_type = postings_list<postings_list_internal, Lambda>;

    using list_ptr = list_type*;

    using const_list_ptr = const list_type*;

private:
    using map_type = std::map<label_type, list_type>;

public:
    using iterator_type = typename map_type::const_iterator;

    iterator_type begin() const { return m_lists.begin(); }

    iterator_type end() const { return m_lists.end(); }

    iterator_type find(label_type label) const {
        return m_lists.find(label);
    }

    iterator_type create(label_type label) {
        return m_lists.emplace(label, list_type()).first;
    }

    label_type label(iterator_type iter) const {
        geodb_assert(iter != m_lists.end(), "dereferencing end iterator");
        return iter->first;
    }

    list_ptr list(iterator_type iter) {
        geodb_assert(iter != m_lists.end(), "dereferencing end iterator");
        return const_cast<list_ptr>(&(iter->second));
    }

    const_list_ptr const_list(iterator_type iter) const {
        geodb_assert(iter != m_lists.end(), "dereferencing end iterator");
        return &(iter->second);
    }

    list_ptr total_list() {
        return m_total.get();
    }

    const_list_ptr const_total_list() const {
        return m_total.get();
    }

    size_t size() const {
        return m_lists.size();
    }

public:
    inverted_index_internal_storage_impl()
        : m_total(tpie::make_unique<list_type>())
    {}

    inverted_index_internal_storage_impl(inverted_index_internal_storage_impl&&) noexcept = default;

private:
    tpie::unique_ptr<list_type> m_total;
    map_type m_lists;
};

} // namespace geodb

#endif // GEODB_IRWI_INVERTED_INDEX_INTERNAL_HPP
