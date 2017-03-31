#ifndef GEODB_IRWI_STRING_MAP_INTERNAL_HPP
#define GEODB_IRWI_STRING_MAP_INTERNAL_HPP

#include "geodb/irwi/string_map_bimap.hpp"

/// \file
/// Backend for internal storage.

namespace geodb {

class string_map_internal;

class string_map_internal_impl;

/// Implementation of internal storage for string maps.
/// Mappings are kept in a boost::multiindex.
class string_map_internal_impl {

    using map_type = string_map_bimap;

public:
    // ----------------------------------------
    //      Storage interface
    // ----------------------------------------

    using iterator = typename map_type::iterator;

    iterator begin() const { return m_map.begin(); }

    iterator end() const { return m_map.end(); }

    iterator find_by_id(label_type id) const { return m_map.find_by_id(id); }

    iterator find_by_name(const std::string& label) const { return m_map.find_by_name(label); }

    iterator insert(label_mapping m) { return m_map.insert(std::move(m)); }

    size_t size() const { return m_map.size(); }

    bool empty() const { return m_map.empty(); }

    label_type get_last_id() const {
        return m_last_id;
    }

    void set_last_id(label_type last_id) {
        m_last_id = last_id;
    }

public:
    string_map_internal_impl() = default;

    string_map_internal_impl(string_map_internal_impl&&) noexcept = default;

private:
    label_type m_last_id = 0;
    map_type m_map;
};

class string_map_internal {
public:
    string_map_internal() = default;

private:
    template<typename StorageSpec>
    friend class string_map;

    using implementation = string_map_internal_impl;

    implementation construct() const {
        return {};
    }
};

} // namespace geodb

#endif // GEODB_IRWI_STRING_MAP_INTERNAL_HPP
