#ifndef GEODB_UTILITY_FILE_STREAM_ITERATOR_HPP
#define GEODB_UTILITY_FILE_STREAM_ITERATOR_HPP

#include "geodb/common.hpp"

#include <boost/iterator/iterator_facade.hpp>
#include <tpie/file_stream.h>

namespace geodb {

/// A random-access iterator over a file stream.
/// Provides read access only.
template<typename T>
class file_stream_iterator : public boost::iterator_facade<
        file_stream_iterator<T>,            // Derived
        T,                                  // Value
        std::random_access_iterator_tag,    // Traversal
        T,                                  // "Reference"
        tpie::stream_offset_type
    >
{
private:
    tpie::file_stream<T>* m_stream = nullptr;
    tpie::stream_size_type m_offset = 0;

public:
    file_stream_iterator() = default;

    file_stream_iterator(tpie::file_stream<T>& stream)
        : file_stream_iterator(stream, 0)
    {}

    file_stream_iterator(tpie::file_stream<T>& stream,
                         tpie::stream_size_type offset)
        : m_stream(std::addressof(stream))
        , m_offset(offset)
    {}

    /// Returns the stream of this iterator
    /// or nullptr if this iterator was default constructed.
    tpie::file_stream<T>* stream() const {
        return m_stream;
    }

    /// Returns the offset of this iterator as an index.
    /// Offsets are in [0, stream->size()).
    tpie::stream_size_type offset() const {
        return m_offset;
    }

private:
    friend class boost::iterator_core_access;

    T dereference() const {
        geodb_assert(m_stream, "invalid stream");
        geodb_assert(m_offset < m_stream->size(), "dereferencing end iterator");
        if (m_offset != m_stream->offset()) {
            m_stream->seek(m_offset);
        }
        return m_stream->peek();
    }

    void advance(tpie::stream_offset_type n) {
        if (n >= 0) {
            m_offset += n;
        } else {
            m_offset -= tpie::stream_size_type(-n);
        }
    }

    void increment() {
        ++m_offset;
    }

    void decrement() {
        geodb_assert(m_offset != 0, "decrementing past zero index");
        --m_offset;
    }

    bool equal(const file_stream_iterator& other) const {
        geodb_assert(m_stream == other.m_stream,
                     "comparing iterators of different streams");
        return m_offset == other.m_offset;
    }

    tpie::stream_offset_type distance_to(const file_stream_iterator& other) const {
        if (other.offset() >= offset()) {
            return other.offset() - offset();
        } else {
            return - tpie::stream_offset_type(offset() - other.offset());
        }
    }
};


} // namespace geodb

#endif // GEODB_UTILITY_FILE_STREAM_ITERATOR_HPP
