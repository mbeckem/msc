#ifndef GEODB_UTILITY_SHARED_VALUES_HPP
#define GEODB_UTILITY_SHARED_VALUES_HPP

#include "geodb/common.hpp"

#include <boost/smart_ptr/intrusive_ptr.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/noncopyable.hpp>

#include <type_traits>

/// \file
/// A class that keeps track of (and reuses) its opened instances.

namespace geodb {

namespace shared_instances_ {
    using namespace boost::multi_index;
    using boost::multi_index_container;

    class lookup_tag;

    template<typename Entry, typename Key>
    using container = multi_index_container<
        Entry,
        indexed_by<
            hashed_unique<tag<lookup_tag>, member<Entry, Key, &Entry::key>>
        >
    >;
}

/// A class that opens some resource and associates it with a key.
/// Open values are reference counted and tracked by this instance.
/// Opening the same key will return the existing value until
/// all references to this value go out of scope.
///
/// \note This class is not thread-safe. Reference counts are not atomic.
template<typename Key, typename Value>
class shared_instances : boost::noncopyable
{
private:
    // Each entry represents an active key + value pair.
    // Entries are ref-counted: when the refcount reaches 0
    // the entry is removed from the parent's lookup table.
    struct entry : boost::noncopyable {
        entry(shared_instances* p, const Key& k, Value&& v)
            : key(std::move(k))
            , parent(p)
            , value(std::move(v))
        {}

        ~entry() {
            geodb_assert(refs == 0, "there are still references to this entry.");
        }

        Key key;

        // Mutable: The caller requires non-const access,
        // but multiindex only allows const access for its elements.
        // Values are not part of the key, so this is ok.
        mutable shared_instances* parent;
        mutable int refs = 0;
        mutable Value value;
    };

    friend void intrusive_ptr_add_ref(const entry* e) {
        ++e->refs;
        geodb_assert(e->refs >= 1, "invalid refcount");
    }

    friend void intrusive_ptr_release(const entry* e) {
        geodb_assert(e->refs >= 1, "invalid refcount");
        if (--e->refs == 0) {
            e->parent->erase(*e);
        }
    }

    using intrusive_t = boost::intrusive_ptr<const entry>;
    using container_t = shared_instances_::container<entry, Key>;
    using lookup_t = shared_instances_::lookup_tag;

    // Reference counted pointer using boost::intrusiv_ptr.
    template<typename T>
    struct pointer_impl {
    private:
        intrusive_t p;

    private:
        friend class shared_instances;
        friend class pointer_impl<const T>;

        explicit pointer_impl(intrusive_t ptr): p(std::move(ptr)) {}

    public:
        pointer_impl() = default;
        pointer_impl(std::nullptr_t) {}
        pointer_impl(const pointer_impl&) = default;
        pointer_impl(pointer_impl&&) = default;

        pointer_impl& operator=(std::nullptr_t) { reset(); return *this; }
        pointer_impl& operator=(const pointer_impl&) = default;
        pointer_impl& operator=(pointer_impl&&) = default;

        T* get() const {
            if (!*this) return nullptr;
            return &p->value;
        }

        T* operator->() const {
            geodb_assert(p, "dereferencing null pointer");
            return &p->value;
        }

        T& operator*() const {
            geodb_assert(p, "dereferencing null pointer");
            return p->value;
        }

        explicit operator bool() const {
            return static_cast<bool>(p);
        }

        // Conversion to const pointer.
        operator pointer_impl<const T>() const & {
            return pointer_impl<const T>(p);
        }

        operator pointer_impl<const T>() && {
            return pointer_impl<const T>(std::move(p));
        }

        void reset() { p.reset(); }
    };

    template<typename T1, typename T2>
    inline friend bool operator==(const pointer_impl<T1>& a, const pointer_impl<T2>& b) {
        return raw(a) == raw(b);
    }

    template<typename T1, typename T2>
    friend bool operator!=(const pointer_impl<T1>& a, const pointer_impl<T2>& b) {
        return !(a == b);
    }

    template<typename T>
    friend bool operator==(const pointer_impl<T>& ptr, std::nullptr_t) {
        return raw(ptr) == nullptr;
    }

    template<typename T>
    friend bool operator==(std::nullptr_t, const pointer_impl<T>& ptr) {
        return raw(ptr) == nullptr;
    }

    template<typename T>
    friend bool operator!=(const pointer_impl<T>& ptr, std::nullptr_t) {
        return raw(ptr) != nullptr;
    }

    template<typename T>
    friend bool operator!=(std::nullptr_t, const pointer_impl<T>& ptr) {
        return raw(ptr) != nullptr;
    }

public:
    using key_type = Key;
    using value_type = Value;

    using pointer = pointer_impl<Value>;
    using const_pointer = pointer_impl<const Value>;

    // iteration support could be useful ...

public:
    shared_instances() = default;

    shared_instances(shared_instances&& other) noexcept
        : m_entries(std::move(other.m_entries)) {}

    ~shared_instances() {
        geodb_assert(empty(), "there are still references to values of this container");
    }

    /// Either returns an existing value associated with `key`
    /// or creates a new one by calling the factory function.
    /// Returns a handle to the value in any case.
    ///
    /// Handles are ref-counted. Repeated calls to `open()` with the same
    /// key will return the same instance until the ref-count reaches zero again,
    /// at which point the instance will be destroyed.
    ///
    /// \param key
    ///     The key that uniquely identifies the value.
    /// \param factory
    ///     A function that will be called should there be no
    ///     existing value for the current key.
    ///     The function must return an instance of `value_type`.
    ///
    /// \return
    ///     A pointer to the value.
    ///
    /// \warning Pointers created using this instance must not outlive it.
    template<typename FactoryFunction>
    pointer open(const Key& key, FactoryFunction&& factory) {
        auto& index = m_entries.template get<lookup_t>();
        auto iter = index.find(key);
        if (iter != index.end()) {
            return pointer(to_intrusive(iter));
        }

        std::tie(iter, std::ignore) = index.emplace(this, key, factory());
        return pointer(to_intrusive(iter));
    }

    /// Convert a const_pointer to a pointer. Pointers share the same reference count.
    /// Requires mutable access to this instance.
    pointer convert(const_pointer ptr) {
        return pointer(std::move(ptr.p));
    }

    /// Returns the value associated with the given key or a null pointer
    /// if there is no such value.
    const_pointer get(const Key& key) const {
        auto& index = m_entries.template get<lookup_t>();
        auto iter = index.find(key);
        if (iter != index.end()) {
            return const_pointer(to_intrusive(iter));
        }
        return const_pointer();
    }

    /// Returns true if this instances stores a value for the given key.
    bool contains(const Key& key) const {
        auto& index = m_entries.template get<lookup_t>();
        auto iter = index.find(key);
        return iter != index.end();
    }

    /// Returns the number of currently opened values.
    size_t size() const {
        return m_entries.size();
    }

    /// Returns true iff there are no value in use.
    bool empty() const {
        return size() == 0;
    }

private:
    friend class entry;

    /// Erases this entry.
    /// \warning Deletes the entry!
    void erase(const entry& e) {
        auto& index = m_entries.template get<lookup_t>();
        index.erase(index.iterator_to(e)); // deletes e!
    }

    /// Returns the raw entry address for the given pointer wrapper.
    /// This value will be null if the pointer itself is null.
    template<typename T>
    static const entry* raw(const pointer_impl<T>& ptr) {
        return ptr.p.get();
    }

    /// Convert the iterator to a reference counted pointer.
    /// Iterator must point to a valid entry.
    template<typename Iter>
    static intrusive_t to_intrusive(const Iter& iter) {
        const entry& e = *iter;
        return intrusive_t(&e);
    }

private:
    container_t m_entries;
};

} // namespace geodb

#endif // GEODB_UTILITY_SHARED_VALUES_HPP
