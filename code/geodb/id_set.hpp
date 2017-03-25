#ifndef GEODB_ID_SET_HPP
#define GEODB_ID_SET_HPP

#include "geodb/bloom_filter.hpp"
#include "geodb/common.hpp"
#include "geodb/interval_set.hpp"

namespace geodb {

#if defined(GEODB_ID_SET_INTERVALS)

template<u32 Lambda>
using id_set = static_interval_set<u64, Lambda>;

template<u32 Lambda>
struct id_set_binary {
    size_t size;
    interval<u64> intervals[Lambda];
};

template<u32 Lambda>
void to_binary(const id_set<Lambda>& set, id_set_binary<Lambda>& binary) {
    binary.size = set.size();
    std::copy(set.begin(), set.end(), binary.intervals);
}

template<u32 Lambda>
void from_binary(id_set<Lambda>& set, const id_set_binary<Lambda>& binary) {
    set.assign(binary.intervals, binary.intervals + binary.size);
}

#elif defined(GEODB_ID_SET_BLOOM)

template<u32 Lambda>
using id_set = bloom_filter<u64, Lambda * sizeof(interval<u64>) * 8>;

template<u32 Lambda>
using id_set_binary = id_set<Lambda>;

template<u32 Lambda>
void to_binary(const id_set<Lambda>& set, id_set_binary<Lambda>& binary) {
    binary = set;
}

template<u32 Lambda>
void from_binary(id_set<Lambda>& set, const id_set_binary<Lambda>& binary) {
    set = binary;
}

#else
    #error Unsupported id set type.
#endif

} // namespace geodb

#endif // GEODB_ID_SET_HPP
