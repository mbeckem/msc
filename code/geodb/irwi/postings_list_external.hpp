#ifndef GEODB_IRWI_POSTINGS_LIST_EXTERNAL_HPP
#define GEODB_IRWI_POSTINGS_LIST_EXTERNAL_HPP

#include "geodb/common.hpp"
#include "geodb/filesystem.hpp"
#include "geodb/irwi/base.hpp"
#include "geodb/utility/movable_adapter.hpp"
#include "geodb/utility/file_stream_iterator.hpp"

#include <boost/noncopyable.hpp>
#include <tpie/file_stream.h>

/// \file
/// Postings list backend for external storage.
/// Every postings list is stored in its own file.

namespace geodb {

class postings_list_external;

template<typename Posting>
class postings_list_external_impl;

class postings_list_external {
private:
    fs::path path;

public:
    postings_list_external(fs::path path): path(std::move(path)) {}

private:
    template<typename StorageSpec, u32 Lambda>
    friend class postings_list;

    template<typename Posting>
    using implementation = postings_list_external_impl<Posting>;

    template<typename Posting>
    movable_adapter<implementation<Posting>>
    construct() const {
        return {in_place_t(), path};
    }
};

/// External storage for a posting list is implemented on top of
/// a uncompressed TPIE file stream. Elements can be accessed in
/// a random access like fashion. However, sequential access is faster.
template<typename Posting>
class postings_list_external_impl : boost::noncopyable {
public:
    using posting_type = Posting;

    using iterator = file_stream_iterator<posting_type>;

public:
    postings_list_external_impl(const fs::path& path)
        : m_entries()
    {
         m_entries.open(path.string());
    }

    postings_list_external_impl(postings_list_external_impl&&) = delete;

    iterator begin() const {
        return iterator(m_entries, 0);
    }

    iterator end() const {
        return iterator(m_entries, size());
    }

    void push_back(const posting_type& value) {
        seek(size());
        write(value);
    }

    void pop_back() {
        geodb_assert(size() > 0, "must not be empty");
        m_entries.truncate(size() - 1);
    }

    void clear() {
        m_entries.truncate(0);
    }

    void set(const iterator& pos, const posting_type& value) {
        geodb_assert(pos.offset() < size(), "index must be in range");
        seek(pos.offset());
        write(value);
    }

    size_t size() const {
        return m_entries.size();
    }

private:
    posting_type read() const {
        return m_entries.read();
    }

    void write(const posting_type& value) {
        m_entries.write(value);
    }

    void seek(size_t index) const {
        if (m_entries.offset() != index) {
            m_entries.seek(index);
        }
    }

private:
    mutable tpie::file_stream<posting_type> m_entries;
};

} // namespace geodb

#endif // GEODB_IRWI_POSTINGS_LIST_EXTERNAL_HPP
