#ifndef IRWI_STRING_MAP_EXTERNAL_HPP
#define IRWI_STRING_MAP_EXTERNAL_HPP

#include "geodb/common.hpp"
#include "geodb/filesystem.hpp"
#include "geodb/irwi/string_map.hpp"
#include "geodb/irwi/string_map_bimap.hpp"

#include <boost/noncopyable.hpp>
#include <tpie/uncompressed_stream.h>

namespace geodb {

class string_map_external;

class string_map_external_impl;

class string_map_external {
public:
    string_map_external(fs::path path)
        : path(std::move(path)) {}

    fs::path path;

    using implementation = string_map_external_impl;
};

class string_map_external_impl : boost::noncopyable {

    using map_type = string_map_bimap;

private:
    // ----------------------------------------
    //      Storage interface
    // ----------------------------------------

    template<typename S>
    friend class string_map;

    using iterator = typename map_type::iterator;

    iterator begin() const { return m_map.begin(); }

    iterator end() const { return m_map.end(); }

    iterator find_by_id(label_type id) const { return m_map.find_by_id(id); }

    iterator find_by_name(const std::string& name) const { return m_map.find_by_name(name); }

    iterator insert(label_mapping m) {
        if (m_stream.offset() != m_stream.size()) {
            m_stream.seek(m_stream.size());
        }
        write_raw(m.id);
        write(m.name);
        return m_map.insert(std::move(m));
    }

    size_t size() const { return m_map.size(); }

    bool empty() const { return m_map.empty(); }

    label_type get_last_id() const { return m_last_id; }

    void set_last_id(label_type id) { m_last_id = id; }

public:
    string_map_external_impl(string_map_external params) {
        m_stream.open(params.path.string());
        if (m_stream.size() > 0) {
            read_raw(m_last_id);
            while (m_stream.can_read()) {
                label_mapping m;
                read_raw(m.id);
                read(m.name);
                m_map.insert(std::move(m));
            }
        } else {
            // Make sure that the first n bytes are used for the id.
            write_raw(m_last_id);
        }
    }

    ~string_map_external_impl() {
        m_stream.seek(0);
        write_raw(m_last_id);
    }

private:
    void read(std::string& s) {
        size_t size;
        read_raw(size);
        s.resize(size);
        m_stream.read(s.begin(), s.end());
    }

    void write(const std::string& s) {
        size_t size = s.size();
        write_raw(size);
        m_stream.write(s.begin(), s.end());
    }

    template<typename T>
    void read_raw(T& t) {
        static_assert(std::is_trivially_copyable<T>::value, "");
        char* buf = reinterpret_cast<char*>(&t);
        m_stream.read(buf, buf + sizeof(T));
    }

    template<typename T>
    void write_raw(const T& t) {
        static_assert(std::is_trivially_copyable<T>::value, "");
        const char* buf = reinterpret_cast<const char*>(&t);
        m_stream.write(buf, buf + sizeof(T));
    }

private:
    /// The map is backed by a file on disk. When a new entry
    /// is being inserted, a copy of that entry will be appended to the file.
    tpie::uncompressed_stream<char> m_stream; // TODO: Just how dumb is a 1-byte large element?

    /// The label id.
    label_type m_last_id = 0;

    /// Contains the mappings in internal storage for fast lookup performance.
    map_type m_map;
};

} // namespace geodb

#endif // IRWI_STRING_MAP_EXTERNAL_HPP
