#ifndef GEODB_IRWI_STRING_MAP_BIMAP_HPP
#define GEODB_IRWI_STRING_MAP_BIMAP_HPP

#include "geodb/irwi/string_map.hpp"

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>

/// \file
/// Contains the definition of the internal data structure
/// used by \ref geodb::string_map.

namespace geodb {

namespace string_map_bimap_ {

using namespace boost::multi_index;
using boost::multi_index_container;

struct label_id {};
struct label_name {};
struct insertion_order {};

using container = multi_index_container<
    label_mapping,
    indexed_by<
        ordered_unique<tag<label_id>, member<
            label_mapping, label_type, &label_mapping::id
        >>,
        ordered_unique<tag<label_name>, member<
            label_mapping, std::string, &label_mapping::name
        >>,
        sequenced<tag<insertion_order>>
    >
>;

} // namespace string_map_bimap_

/// A bimap contains a set of mappings (label_name, label_id)
/// where both label_name and label_id are unique across all elements.
/// Both values can be used to find a mapping in the container.
/// Mappings are iterated in insertion order.
class string_map_bimap {
    using index_type = string_map_bimap_::container;
    using id_tag = string_map_bimap_::label_id;
    using name_tag = string_map_bimap_::label_name;
    using order_tag = string_map_bimap_::insertion_order;

public:
    // Elements are iterated in insertion order by default.
    using iterator = typename index_type::template index<order_tag>::type::const_iterator;

public:
    string_map_bimap() {}

    // Custom move constructor because the boost.multiindex
    // one isnt marked as noexcept.
    string_map_bimap(string_map_bimap&& other) noexcept
        : m_index(std::move(other.m_index)) {}

    /// Iterator to the first element (or `end()` if empty).
    iterator begin() const {
        return m_index.get<order_tag>().begin();
    }

    /// Past-the-end iterator.
    iterator end() const {
        return m_index.get<order_tag>().end();
    }

    /// Returns the iterator to the mapping with the given label id
    /// or `end()` if no such mapping exists.
    iterator find_by_id(label_type label) const {
        return project(m_index.get<id_tag>().find(label));
    }

    /// Returns the iterator to the mapping with the given name
    /// or `end()`, if no such mapping exists.
    iterator find_by_name(const std::string& name) const {
        return project(m_index.get<name_tag>().find(name));
    }

    /// Inserts the new mapping.
    /// Both the id and the name must be unused.
    /// \pre `find_by_id(m.id) == end() && find_by_name(m.name) == end()`
    /// \return Returns the iterator to the new element.
    iterator insert(label_mapping m) {
        geodb_assert(find_by_id(m.id) == end(), "id exists");
        geodb_assert(find_by_name(m.name) == end(), "name exists");
        iterator iter;
        bool inserted;
        std::tie(iter, inserted) = m_index.get<order_tag>().push_back(std::move(m));
        geodb_assert(inserted, "value must have been inserted");
        return iter;
    }

    bool empty() const {
        return size() == 0;
    }

    /// Returns the number of (label_id, label_name) mappings.
    size_t size() const {
        return m_index.get<order_tag>().size();
    }

private:
    template<typename OtherIter>
    iterator project(OtherIter it) const {
        return m_index.project<order_tag>(it);
    }

private:
    index_type m_index;
};

} // namespace geodb

#endif // GEODB_IRWI_STRING_MAP_BIMAP_HPP
