#ifndef GEODB_HYBRID_MAP_HPP
#define GEODB_HYBRID_MAP_HPP

#include "geodb/common.hpp"
#include "geodb/utility/movable_adapter.hpp"
#include "geodb/utility/temp_dir.hpp"

#include <boost/iterator/iterator_facade.hpp>
#include <boost/variant.hpp>
#include <tpie/btree.h>
#include <tpie/tempname.h>

#include <map>
#include <memory>

/// \file
/// Contains a map that lives either in external or internal storage.

namespace geodb {

namespace hybrid_map_ {

// Returns a mutable iterator when given a mutable reference to the
// map and a const iterator.
template<typename Map>
typename Map::iterator
mutable_iter(Map& map, typename Map::const_iterator pos) {
    return map.erase(pos, pos);
}

} // namespace hybrid_map_

/// A simple map in internal memory.
/// Exposes the same interface as \ref external_map and \ref hybrid_map.
template<typename Key, typename Value>
class internal_map {
    using map_t = std::map<Key, Value>;

    using map_iterator_t = typename map_t::const_iterator;

public:
    using value_type = std::pair<const Key, Value>;

    using key_Type = Key;

    using mapped_type = Value;

    class iterator : public boost::iterator_facade<
            iterator,
            value_type,
            boost::forward_traversal_tag,
            value_type
        >
    {
    private:
        map_iterator_t m_inner;

    public:
        iterator() = default;

    private:
        friend class internal_map;

        iterator(map_iterator_t inner)
            : m_inner(std::move(inner))
        {}

        const map_iterator_t& inner() const {
            return m_inner;
        }

    private:
        friend class boost::iterator_core_access;

        // Transform to value type instead
        // of reference in order to expose the
        // same interface as external_map.
        value_type dereference() const {
            return *m_inner;
        }

        void increment() {
            ++m_inner;
        }

        bool equal(const iterator& other) const {
            return m_inner == other.m_inner;
        }
    };

    using const_iterator = iterator;

private:
    map_t m_map;

public:
    /// Create an empty map.
    internal_map() = default;

    internal_map(internal_map&&) noexcept = default;

    internal_map& operator=(internal_map&&) noexcept = default;

    /// Returns the iterator to the first element (or `end()`).
    iterator begin() const { return m_map.begin(); }

    /// Returns the past-the-end iterator.
    iterator end() const { return m_map.end(); }

    /// Returns an iterator to an element with the given `key`
    /// or `end()` if no such element exists.
    iterator find(const Key& key) const {
        return m_map.find(key);
    }

    /// Inserts a <key, value> mapping.
    ///
    /// Does nothing if the key already exists in the map.
    /// Returns true if the mapping was inserted.
    bool insert(const Key& key, const Value& value) {
        bool inserted;
        std::tie(std::ignore, inserted) = m_map.emplace(key, value);
        return inserted;
    }

    /// Replaces the value at the given iterator.
    /// The key remains the same.
    void replace(iterator pos, const Value& value) {
        hybrid_map_::mutable_iter(m_map, pos.inner())->second = value;
    }

    /// Returns the number of items in this map.
    size_t size() const { return m_map.size(); }
};

/// A map in (temporary) external storage backed by a btree.
/// An instance only uses constant space in internal memory.
///
/// Both key and value types must be trivially copyable.
///
/// \tparam Key         The key type.
/// \tparam Value       The value type.
/// \tparam block_size  The block size on disk.
template<typename Key, typename Value, size_t block_size>
class external_map {
private:
#pragma pack(push, 1)
    struct entry {
        Key key;
        Value value;
    };
#pragma pack(pop)

    struct key_access {
        Key operator()(const entry& e) const { return e.key; }
    };

    using tree_t = tpie::btree<entry,
        tpie::btree_key<key_access>,
        tpie::btree_external,
        tpie::btree_blocksize<block_size>
    >;

    using tree_iterator_t = typename tree_t::iterator;

public:
    using value_type = std::pair<const Key, Value>;

    using key_type = Key;

    using mapped_type = Value;

    class iterator : public boost::iterator_facade<
            iterator,
            value_type,
            boost::forward_traversal_tag,
            value_type
        >
    {
    private:
        tree_iterator_t m_inner;

    public:
        iterator() = default;

    private:
        friend class external_map;

        iterator(tree_iterator_t inner)
            : m_inner(std::move(inner))
        {}

        const tree_iterator_t& inner() const {
            return m_inner;
        }

    private:
        friend class boost::iterator_core_access;

        value_type dereference() const {
            entry e = *m_inner;
            return value_type(e.key, e.value);
        }

        void increment() {
            ++m_inner;
        }

        bool equal(const iterator& other) const {
            return m_inner == other.m_inner;
        }
    };

    using const_iterator = iterator;

private:
    temp_dir m_dir{"external_map"};
    tpie::unique_ptr<tree_t> m_tree;

public:
    /// Create an empty map.
    external_map()
        : m_tree(tpie::make_unique<tree_t>((m_dir.path() / "map.tree").string()))
    {}

    external_map(external_map&&) noexcept = default;

    external_map& operator=(external_map&&) noexcept = default;

    /// Returns an iterator to the first element (or `end()`).
    iterator begin() const { return m_tree->begin(); }

    /// Returns the past-the-end iterator.
    iterator end() const { return m_tree->end(); }

    /// Returns an iterator that points to the item with the given key,
    /// or `end()` if no such item exists.
    iterator find(const Key& k) const {
        return m_tree->find(k);
    }

    /// Inserts a <key, value> mapping.
    ///
    /// Does nothing if the key already exists in the map.
    /// Returns true if the mapping was inserted.
    bool insert(const Key& k, const Value& v) {
        if (m_tree->find(k) != m_tree->end()) {
            return false;
        }

        m_tree->insert(entry{k, v});
        return true;
    }

    /// Replaces the value at the given iterator.
    /// The key remains the same.
    void replace(iterator pos, const Value& value) {
        geodb_assert(pos != end(), "accessing end iterator");
        Key key = pos.inner()->key;
        m_tree->replace(pos.inner(), entry{key, value});
    }

    /// Returns the number of items in this map.
    size_t size() const {
        return m_tree->size();
    }
};

/// A hybrid map stores its elements in memory
/// until a certain threshold (the maximum element number)
/// is reached.
///
/// At that point, all elements are moved into an btree
/// in external storage.
///
/// Insertions that trigger the migration to disk invalidate
/// all iterators.
///
/// Note: Key and Value types must be trivially copyable.
/// The key type must be comparable using < and =.
///
/// \tparam Key         The key type.
/// \tparam Value       The value type.
/// \tparam block_size  The block size for the external storage.
template<typename Key, typename Value, size_t block_size>
class hybrid_map {
public:
    using value_type = std::pair<const Key, Value>;

    using key_type = Key;

    using mapped_type = Value;

private:
    using internal_backend = internal_map<Key, Value>;

    using external_backend = external_map<Key, Value, block_size>;

    using backend_t = boost::variant<internal_backend, external_backend>;

    using internal_iterator = typename internal_backend::iterator;

    using external_iterator = typename external_backend::iterator;

    using iterator_t = boost::variant<internal_iterator, external_iterator>;

public:
    class iterator : public boost::iterator_facade<
        iterator,
        value_type,
        boost::forward_traversal_tag,
        value_type>
    {
    private:
        iterator_t m_inner;

    public:
        iterator() = default;

    private:
        friend class hybrid_map;

        iterator(iterator_t inner)
            : m_inner(std::move(inner)) {}

        const iterator_t& inner() const {
            return m_inner;
        }

    private:
        friend class boost::iterator_core_access;

        value_type dereference() const {
            return boost::apply_visitor([&](auto&& inner) {
                return *inner;
            }, m_inner);
        }

        void increment() {
            boost::apply_visitor([&](auto&& inner) {
                ++inner;
            }, m_inner);
        }

        bool equal(const iterator& other) const {
            return m_inner == other.m_inner;
        }
    };

    using const_iterator = iterator;

public:
    static constexpr size_t limit_for_blocks(size_t blocks) {
        return (blocks * block_size) / (sizeof(Key) + sizeof(Value));
    }

private:
    backend_t m_backend;
    size_t m_limit = 0;

public:
    /// Constructs a new map that keeps 2 blocks in memory
    /// before it switches to external storage.
    hybrid_map()
        : hybrid_map(limit_for_blocks(2))
    {}

    /// Constructs a new map with the given size limit.
    /// When the size of the map exceeds the limit,
    /// all items will be moved into external storage.
    explicit hybrid_map(size_t limit)
        : m_backend()
        , m_limit(limit)
    {}

    hybrid_map(hybrid_map&&) noexcept = default;

    hybrid_map& operator=(hybrid_map&&) noexcept = default;

    /// Returns an iterator to the first element (or `end()`).
    iterator begin() const {
        return boost::apply_visitor([&](auto&& backend) -> iterator_t {
            return backend.begin();
        }, m_backend);
    }

    /// Returns the past-the-end iterator.
    iterator end() const {
        return boost::apply_visitor([&](auto&& backend) -> iterator_t {
            return backend.end();
        }, m_backend);
    }

    /// Returns an iterator that points to the item with the given key,
    /// or `end()` if no such item exists.
    iterator find(const Key& key) const {
        return boost::apply_visitor([&](auto&& backend) -> iterator_t {
            return backend.find(key);
        }, m_backend);
    }

    /// Inserts a <key, value> mapping.
    ///
    /// Does nothing if the key already exists in the map.
    /// Returns true if the mapping was inserted.
    ///
    /// If the in-memory storage grows too large, all items will
    /// be moved to disk.
    bool insert(const Key& key, const Value& value) {
        return boost::apply_visitor([&](auto&& backend) {
            return this->insert_impl(backend, key, value);
        }, m_backend);
    }

    /// Replaces the value at the given iterator.
    /// The key remains the same.
    void replace(const iterator& pos, const Value& value) {
        boost::apply_visitor([&](auto&& inner_iterator) {
            this->replace_impl(inner_iterator, value);
        }, pos.inner());
    }

    /// Returns the size limit for internal memory storage.
    size_t limit() const {
        return m_limit;
    }

    /// Returns the number of items in this map.
    size_t size() const {
        return boost::apply_visitor([&](auto&& backend) {
           return backend.size();
        }, m_backend);
    }

    /// Manually move all items to external storage.
    ///
    /// This is done automatically when the map reaches `limit()`
    /// entires.
    /// Does nothing if this map already uses external storage.
    ///
    /// \post `is_external()`.
    void make_external() {
        if (is_external()) {
            return;
        }

        internal_backend& internal = boost::get<internal_backend>(m_backend);

        external_backend external;
        for (const auto& pair : internal) {
            external.insert(pair.first, pair.second);
        }
        m_backend = std::move(external); // Invalidates reference `internal`.
    }

    /// Returns true if the items in this map are located in memory.
    bool is_internal() const {
        return boost::get<internal_backend>(&m_backend) != nullptr;
    }

    /// Returns true if the items in this map are located on disk.
    bool is_external() const {
        return boost::get<external_backend>(&m_backend) != nullptr;
    }

private:
    bool insert_impl(internal_backend& backend, const Key& key, const Value& value) {
        geodb_assert(backend.size() <= m_limit, "internal backend grew too large.");

        bool inserted = backend.insert(key, value);
        if (inserted && backend.size() > m_limit) {
            make_external();
            // reference to `backend` is invalid because the variant
            // was modified.
        }
        return inserted;
    }

    bool insert_impl(external_backend& backend, const Key& key, const Value& value) {
        return backend.insert(key, value);
    }

    void replace_impl(const internal_iterator& pos, const Value& value) {
        internal_backend* internal = boost::get<internal_backend>(&m_backend);
        geodb_assert(internal != nullptr, "using internal iterator but storage is external.");
        internal->replace(pos, value);
    }

    void replace_impl(const external_iterator& pos, const Value& value) {
        external_backend* external = boost::get<external_backend>(&m_backend);
        geodb_assert(external != nullptr, "using external iterator but storage is internal.");
        external->replace(pos, value);
    }
};

} // namespace geodb

#endif // GEODB_HYBRID_MAP_HPP
