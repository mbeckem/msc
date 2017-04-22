#ifndef GEODB_BLOOM_FILTER_HPP
#define GEODB_BLOOM_FILTER_HPP

#include "geodb/common.hpp"
#include "geodb/type_traits.hpp"

#include <boost/math/constants/constants.hpp>
#include <boost/range/functions.hpp>
#include <boost/range/metafunctions.hpp>

#include <array>
#include <ostream>

/// \file
/// Contains a bloom filter implementation.

namespace geodb {

/// Returns the murmur3 hash for the given byte sequence.
std::array<u64, 2> murmur3(const u8* data, size_t size);

/// Returns the murmur3 hash for a given value.
/// The value is reinterpreted as a sequence of bytes, then hashed.
template<typename T>
std::array<u64, 2> murmur3(const T& value) {
    static_assert(std::is_trivially_copyable<T>::value, "");
    return murmur3(reinterpret_cast<const u8*>(std::addressof(value)), sizeof(value));
}

/// A basic bloom filter.
///
/// This class is trivially copyable and thus serializable by the TPIE IO classes.
/// It takes approximately `ceil(bits / 8)` bytes of storage.
template<typename T, u32 Bits>
class bloom_filter {
    static_assert(std::is_trivially_copyable<T>::value,
                  "The type must be trivially copyable, since its raw data "
                  "is being hashed.");

private:
    /// Returns the number of uint64_t fields required to store the
    /// requested number of bits (they are 64 bits each).
    static constexpr u32 fields() {
        return (Bits + 63) / 64;
    }

    /// The number of different hash values computed
    /// for every value.
    static constexpr u32 hashes() {
        return 5;
    }

    using data_t = std::array<u64, fields()>;

    using hashes_t = std::array<u64, hashes()>;

public:
    using value_type = T;

    static constexpr u32 bits() { return Bits; }

public:
    /// Returns the approximate error rate when inserting the given number of elements
    /// into a bloom filter with the current number of bits.
    static double error_rate(u64 elements) {
        double k = hashes();
        double n = elements;
        double m = Bits;
        return std::pow(1.0 - std::pow(1.0 - 1.0/m, k * n), k);
    }

    /// Computes the union of the given range of bloom filters.
    /// They must all be of this type.
    template<typename BloomFilters>
    static bloom_filter set_union(const BloomFilters& range) {
        bloom_filter result;
        for (const bloom_filter& filter : range) {
            for (u32 i = 0; i < fields(); ++i) {
                result.m_data[i] |= filter.m_data[i];
            }
        }
        return result;
    }

    /// Computes the intersection of the given range of bloom filters.
    /// They must all be of this type.
    template<typename BloomFilters>
    static bloom_filter set_intersection(const BloomFilters& range) {
        using const_iter = typename boost::range_const_iterator<const BloomFilters>::type;
        using value_type = typename boost::range_value<const BloomFilters>::type;

        static_assert(std::is_same<value_type, bloom_filter>::value,
                      "The range must contain bloom filter values.");

        const_iter first = boost::const_begin(range);
        const_iter last = boost::const_end(range);
        if (first == last) {
            return bloom_filter();
        }

        bloom_filter result = *first;
        ++first;
        for (; first != last; ++first) {
            const bloom_filter& filter = *first;

            for (u32 i = 0; i < fields(); ++i) {
                result.m_data[i] &= filter.m_data[i];
            }
        }
        return result;
    }

public:
    bloom_filter() = default;

    bloom_filter(std::initializer_list<T> items)
        : bloom_filter(items.begin(), items.end())
    {}

    template<typename Iter>
    bloom_filter(Iter first, Iter last) {
        for (; first != last; ++first) {
            add(*first);
        }
    }

    /// Inserts the value into the bloom filter.
    /// \post `contains(value) == true`.
    void add(const value_type& value) {
        for (u64 hash : compute_hashes(value)) {
            set(index(hash));
        }
    }

    /// Clears the instance and adds all values in the given iterator range.
    template<typename Iter>
    void assign(Iter first, Iter last) {
        clear();
        for (; first != last; ++first) {
            add(*first);
        }
    }

    /// Resets the instance.
    /// \post `empty() == true`.
    void clear() {
        m_data = data_t{};
    }

    /// Returns true if this bloom filter is empty,
    /// i.e. true iff all bits are zero.
    bool empty() const {
        for (u64 v : m_data) {
            if (v != 0) {
                return false;
            }
        }
        return true;
    }

    /// Returns true if the value is stored in this instance.
    ///
    /// This might be a false positive, but can never be
    /// a false negative.
    /// In other words, returning true means "probably".
    bool contains(const value_type& value) const {
        for (u64 hash : compute_hashes(value)) {
            if (!test(index(hash))) {
                return false;
            }
        }
        return true;
    }

    /// Returns the union of `*this` and `other`.
    bloom_filter union_with(const bloom_filter& other) const {
        bloom_filter result = *this;
        for (u32 i = 0; i < fields(); ++i) {
            result.m_data[i] |= other.m_data[i];
        }
        return result;
    }

    /// Returns the intersection of `*this` and `other`.
    bloom_filter intersection_with(const bloom_filter& other) const {
        bloom_filter result = *this;
        for (u32 i = 0; i < fields(); ++i) {
            result.m_data[i] &= other.m_data[i];
        }
        return result;
    }

private:
    /// Returns the bit index for the given hash.
    u32 index(u64 hash) const {
        return hash % Bits;
    }

    /// Computes the hashes for the given value by computing the 128 bit murmur hash.
    /// Then treats both 64 bit components of the murmur hash as independent hash functions
    /// (they're uniformly distributed, so this should be OK) and uses them to emulate N
    /// hash functions.
    ///
    /// For the last part, see "Building a better Bloom Filter" by Adam Kirsch and Michael Mitzenmacher.
    hashes_t compute_hashes(const value_type& value) const {
        std::array<u64, 2> murmur = murmur3(value);

        hashes_t result;
        for (u32 i = 0; i < hashes(); ++i) {
            result[i] = murmur[0] + i * murmur[1];
        }
        return result;
    }

    /// Sets the bit with the given index to "1".
    void set(u32 index) {
        geodb_assert(index < Bits, "bit index out of bounds");

        u32 element_index = index / 64;
        u32 bit_index = index % 64;
        m_data[element_index] |= (1 << bit_index);
    }

    /// Returns true if the bit with the given index is set.
    bool test(u32 index) const {
        geodb_assert(index < Bits, "bit index out of bounds");

        u32 element_index = index / 64;
        u32 bit_index = index % 64;
        return m_data[element_index] & (1 << bit_index);
    }

private:
    friend bool operator==(const bloom_filter& a, const bloom_filter& b) {
        for (u32 i = 0; i < fields(); ++i) {
            if (a.m_data[i] != b.m_data[i]) {
                return false;
            }
        }
        return true;
    }

    friend bool operator!=(const bloom_filter& a, const bloom_filter& b) {
        for (u32 i = 0; i < fields(); ++i) {
            if (a.m_data[i] == b.m_data[i]) {
                return false;
            }
        }
        return true;
    }

    friend std::ostream& operator<<(std::ostream& o, const bloom_filter& filter) {
        for (u32 i = 0; i < Bits; ++i) {
            o << (filter.test(i) ? '1' : '0');
        }
        return o;
    }

private:
    data_t m_data{};
};

} // namespace geodb

#endif // GEODB_BLOOM_FILTER_HPP
