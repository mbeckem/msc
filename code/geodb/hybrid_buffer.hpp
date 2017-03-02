#ifndef GEODB_HYBRID_BUFFER_HPP
#define GEODB_HYBRID_BUFFER_HPP

#include "geodb/common.hpp"

#include <boost/iterator/iterator_facade.hpp>
#include <boost/variant.hpp>
#include <tpie/file_stream.h>

namespace geodb {

/// A simple buffer in internal memory.
/// It allows insertion at the end and iteration over its elements.
template<typename Value>
class internal_buffer {
private:
    using vector_t = std::vector<Value>;
    using iterator_t = typename vector_t::const_iterator;

public:
    using value_type = Value;

    class iterator : public boost::iterator_facade<
            iterator,
            value_type,
            boost::forward_traversal_tag,
            value_type
    >
    {
    private:
        iterator_t m_inner;

    public:
        iterator() = default;

    private:
        friend class internal_buffer;

        iterator(iterator_t inner)
            : m_inner(std::move(inner))
        {}

    private:
        friend class boost::iterator_core_access;

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

public:
    internal_buffer() = default;

    internal_buffer(internal_buffer&& other) noexcept = default;

    internal_buffer& operator=(internal_buffer&& other) noexcept = default;

    /// Returns the begin iterator.
    iterator begin() const { return m_buffer.begin(); }

    /// Returns the past-the-end iterator.
    iterator end() const { return m_buffer.end(); }

    /// Appends a value at the end.
    void append(const Value& value) {
        m_buffer.push_back(value);
    }

    /// Returns the number of items in this buffer.
    size_t size() const {
        return m_buffer.size();
    }

private:
    vector_t m_buffer;
};

/// An external buffer stores its items on disk.
/// It is backed by a tpie::file_stream.
template<typename Value, size_t block_size>
class external_buffer {
private:
    using stream_t = tpie::file_stream<Value>;
    using offset_t = tpie::stream_size_type;

public:
    using value_type = Value;

    class iterator : public boost::iterator_facade<
            iterator,
            Value,
            boost::forward_traversal_tag,
            Value
    >
    {
    private:
        stream_t* m_stream = nullptr;
        offset_t m_offset = 0;

    public:
        iterator() = default;

    private:
        friend class external_buffer;

        iterator(stream_t& stream, offset_t offset)
            : m_stream(&stream)
            , m_offset(offset)
        {}

    private:
        friend class boost::iterator_core_access;

        value_type dereference() const {
            geodb_assert(m_stream, "dereferencing invalid iterator");
            geodb_assert(m_offset < m_stream->size(), "dereferencing end iterator");
            if (m_stream->offset() != m_offset) {
                m_stream->seek(m_offset);
            }
            return m_stream->read();
        }

        void increment() {
            geodb_assert(m_stream, "incrementing invalid iterator");
            geodb_assert(m_offset < m_stream->size(), "incrementing past the end");
            ++m_offset;
        }

        bool equal(const iterator& other) const {
            geodb_assert(m_stream == other.m_stream,
                         "comparing iterators to different streams");
            return m_offset == other.m_offset;
        }
    };

    using const_iterator = iterator;

public:
    /// Creates a new external buffer on disk.
    external_buffer()
        : m_stream(tpie::make_unique<stream_t>(stream_t::calculate_block_factor(block_size)))
    {
        m_stream->open();
    }

    external_buffer(external_buffer&&) noexcept = default;

    external_buffer& operator=(external_buffer&&) noexcept = default;

    /// Returns the begin iterator.
    iterator begin() const { return iterator(*m_stream, 0); }

    /// Returns the past-the-end iterator.
    iterator end() const { return iterator(*m_stream, m_stream->size()); }

    /// Appends a value at the end of this buffer.
    void append(const Value& value) {
        m_stream->seek(m_stream->size());
        m_stream->write(value);
    }

    /// Returns the number of items in this buffer.
    size_t size() const { return m_stream->size(); }

private:
    tpie::unique_ptr<stream_t> m_stream;
};

/// A hybrid buffer stores its elements in internal memory
/// until a certain threshold is reached.
/// Then, all items are moved to external storage.
///
/// \tparam Value       The value type.
/// \tparam block_size  The block size (on disk).
template<typename Value, size_t block_size>
class hybrid_buffer {
public:
    using value_type = Value;

private:
    using internal_backend = internal_buffer<Value>;

    using external_backend = external_buffer<Value, block_size>;

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
        friend class hybrid_buffer;

        iterator(iterator_t inner)
            : m_inner(std::move(inner))
        {}

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

private:
    backend_t m_backend;
    size_t m_limit = 0;

public:
    /// Contructs a new buffer with a default item limit.
    hybrid_buffer()
        : hybrid_buffer((block_size * 4) / sizeof(Value))
    {}

    /// Constructs a new buffer with the given item limit.
    hybrid_buffer(size_t limit)
        : m_backend()
        , m_limit(limit)
    {}

    hybrid_buffer(hybrid_buffer&&) noexcept = default;

    hybrid_buffer& operator=(hybrid_buffer&&) noexcept = default;

    /// Returns the iterator to the first element (or `end()` if the buffer is empty).
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

    /// Appends a value. If the size exceeds the specified item limit,
    /// the storage is will be moved to disk.
    void append(const Value& value) {
        boost::apply_visitor([&](auto&& backend) {
            this->append_impl(backend, value);
        }, m_backend);
    }

    /// Returns the current number of items in this buffer.
    size_t size() const {
        return boost::apply_visitor([&](auto&& backend) {
            return backend.size();
        }, m_backend);
    }

    /// Returns the limit (in number of items) at which the storage
    /// will be moved to disk automatically.
    size_t limit() const {
        return m_limit;
    }

    /// Manually move the storage to disk. This operation cannot be reversed.
    void make_external() {
        if (is_external()) {
            return;
        }

        internal_backend& internal = boost::get<internal_backend>(m_backend);

        external_backend external;
        for (const auto& value : internal) {
            external.append(value);
        }
        m_backend = std::move(external); // Invalidates reference to `internal`
    }

    /// Returns true if the buffer resides in internal storage.
    bool is_internal() const {
        return boost::get<internal_backend>(&m_backend) != nullptr;
    }

    /// Returns true if the buffer resides in external storage.
    bool is_external() const {
        return boost::get<external_backend>(&m_backend) != nullptr;
    }

private:
    void append_impl(internal_backend& backend, const Value& value) {
        geodb_assert(backend.size() <= m_limit, "internal backend grew too large");

        backend.append(value);
        if (backend.size() > m_limit) {
            make_external();
            // reference to `backend` is invalid now.
        }
    }

    void append_impl(external_backend& backend, const Value& value) {
        backend.append(value);
    }
};

} // namespace geodb

#endif // GEODB_HYBRID_BUFFER_HPP
