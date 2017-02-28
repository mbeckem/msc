#ifndef GEODB_IRWI_STRING_MAP_HPP
#define GEODB_IRWI_STRING_MAP_HPP

#include "geodb/common.hpp"
#include "geodb/trajectory.hpp"
#include "geodb/utility/movable_adapter.hpp"

#include <boost/noncopyable.hpp>
#include <boost/iterator/iterator_adaptor.hpp>

namespace geodb {

struct label_mapping {
    label_type id;
    std::string name;

    bool operator==(const label_mapping& other) const {
        return id == other.id && name == other.name;
    }

    bool operator!=(const label_mapping& other) const {
        return !(*this == other);
    }
};

/// A bidirectional mapping class for strings and integral ids.
/// Each string is assigned a unique integer id.
template<typename StorageSpec>
class string_map : boost::noncopyable {
    using storage_type = typename StorageSpec::implementation;

    using storage_iterator = typename storage_type::iterator;

public:

    class iterator : public boost::iterator_adaptor<
        // Derived, Base, Value, Traversal, Reference, Difference
        iterator, storage_iterator, const label_mapping>
    {
    public:
        iterator() = default;

    private:
        friend class string_map;

        explicit iterator(storage_iterator base)
            : iterator::iterator_adaptor(std::move(base)) {}

    private:
        friend class boost::iterator_core_access;

        const label_mapping& dereference() const {
            return *this->base_reference();
        }
    };

    using const_iterator = iterator;

public:
    string_map(StorageSpec s = StorageSpec())
        : m_storage(in_place_t(), std::move(s)) {}

    string_map(string_map&& other) noexcept = default;

    iterator begin() const { return iterator(storage().begin()); }

    iterator end() const { return iterator(storage().end()); }

    size_t size() const { return storage().size(); }

    bool empty() const { return storage().empty(); }

    /// Returns the label's unique id.
    /// The label *must* have been inserted previously.
    label_type label_id(const std::string& label_name) const {
        auto iter = storage().find_by_name(label_name);
        geodb_assert(iter != storage().end(), "label name not found");
        return iter->id;
    }

    /// Returns the string representation of this label id.
    /// The must have been retrieved via this container.
    const std::string& label_name(label_type label_id) const {
        auto iter = storage().find_by_id(label_id);
        geodb_assert(iter != storage().end(), "label id not found");
        return iter->name;
    }

    /// Returns the label's unique id.
    /// A new entry will be created if the label was unknown until now.
    label_type label_id_or_insert(const std::string& label_name) {
        auto iter = storage().find_by_name(label_name);
        if (iter == storage().end()) {
            return insert(label_name);
        }
        return iter->id;
    }

    /// Inserts a new label.
    /// The label must not have been inserted previously.
    label_type insert(const std::string& label_name) {
        label_type id = storage().get_last_id() + 1;
        storage().insert(label_mapping{id, label_name});
        storage().set_last_id(id);
        return id;
    }

private:
    storage_type& storage() { return *m_storage; }
    const storage_type& storage() const { return *m_storage; }

private:
    movable_adapter<storage_type> m_storage;
};

} // namespace geodb

#endif // GEODB_IRWI_STRING_MAP_HPP
