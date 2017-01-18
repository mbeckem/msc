#ifndef IRWI_POSTINGS_LIST_INTERNAL_HPP
#define IRWI_POSTINGS_LIST_INTERNAL_HPP

#include "geodb/common.hpp"

#include <vector>

namespace geodb {

class postings_list_internal;

template<typename Posting>
class postings_list_internal_impl;

/// Internal posting list storage backed by an in-memory vector.
class postings_list_internal {
public:
    template<typename Posting>
    using implementation = postings_list_internal_impl<Posting>;
};

template<typename Posting>
class postings_list_internal_impl {
public:
    using posting_type = Posting;

    void push_back(const posting_type& value) {
        m_entries.push_back(value);
    }

    void pop_back() {
        geodb_assert(size() > 0, "Must not be empty");
        m_entries.pop_back();
    }

    void clear() {
        m_entries.clear();
    }

    void set(size_t index, const posting_type& value) {
        geodb_assert(index < size(), "index must be in range");
        m_entries[index] = value;
    }

    posting_type get(size_t index) const {
        geodb_assert(index < size(), "index must be in range");
        return m_entries[index];
    }

    size_t size() const {
        return m_entries.size();
    }

    postings_list_internal_impl(postings_list_internal) {}

private:
    std::vector<posting_type> m_entries;
};


} // namespace geodb

#endif // IRWI_POSTINGS_LIST_INTERNAL_HPP
