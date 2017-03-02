#ifndef GEODB_IRWI_POSTINGS_LIST_EXTERNAL_HPP
#define GEODB_IRWI_POSTINGS_LIST_EXTERNAL_HPP

#include "geodb/common.hpp"
#include "geodb/filesystem.hpp"
#include "geodb/irwi/base.hpp"
#include "geodb/utility/movable_adapter.hpp"

#include <boost/noncopyable.hpp>
#include <tpie/uncompressed_stream.h>

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
    postings_list_external_impl(const fs::path& path)
        : m_accessor()
        , m_entries(1.0, &m_accessor)
    {
         m_entries.open(path.string());
    }

    postings_list_external_impl(postings_list_external_impl&&) = delete;

    using posting_type = Posting;

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

    void set(size_t index, const posting_type& value) {
        geodb_assert(index < size(), "index must be in range");
        seek(index);
        write(value);
    }

    posting_type get(size_t index) const {
        geodb_assert(index < size(), "index must be in range");
        seek(index);
        return read();
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
    // Using our own instance avoids a memory leak in tpie::file_base_crtp.
    tpie::default_file_accessor m_accessor;
    mutable tpie::uncompressed_stream<posting_type> m_entries;
};

} // namespace geodb

#endif // GEODB_IRWI_POSTINGS_LIST_EXTERNAL_HPP
