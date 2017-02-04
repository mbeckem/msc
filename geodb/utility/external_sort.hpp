#ifndef UTILITY_EXTERNAL_SORT_HPP
#define UTILITY_EXTERNAL_SORT_HPP

#include "geodb/common.hpp"

#include <tpie/fractional_progress.h>
#include <tpie/pipelining/merge_sorter.h>
#include <tpie/progress_indicator_base.h>
#include <tpie/progress_indicator_null.h>

namespace geodb {

namespace detail {

// A port of tpie::sort that supports subranges of file streams.
// I.e. it does not neccessarily sort the whole file.
template<typename T, typename Stream, typename Compare>
void generic_sort(Stream& instream,
                  tpie::stream_size_type offset, tpie::stream_size_type size,
                  Compare comp, tpie::progress_indicator_base* indicator)
{
    using namespace tpie;

    geodb_assert(offset < instream.size(), "Offset out of range");
    geodb_assert(size <= instream.size() - offset, "Size out of range");

    fractional_progress fp(indicator);
    fractional_subindicator push(fp, "sort", TPIE_FSI, size, "Write sorted runs");
    fractional_subindicator merge(fp, "sort", TPIE_FSI, size, "Perform merge heap");
    fractional_subindicator output(fp, "sort", TPIE_FSI, size, "Write sorted output");
    fp.init(size);

    instream.seek(offset);

    merge_sorter<T, true, Compare> s(comp);
    s.set_available_memory(get_memory_manager().available());
    s.begin();

    push.init(size);
    for (auto remaining = size; remaining-- > 0; ) {
        geodb_assert(instream.can_read(), "Must be able to read all items");
        s.push(instream.read());
        push.step();
    }
    push.done();
    s.end();

    s.calc(merge);

    instream.seek(offset);

    output.init(size);
    for (auto remaining = size; remaining-- > 0; ) {
        geodb_assert(s.can_pull(), "Must be able to pull all written items");
        instream.write(s.pull());
        output.step();
    }
    output.done();
    fp.done();

    instream.seek(offset);
}

} // namespace detail

/// Sort the items [offset, offset + size) in `instream` using `comp`.
///
/// \param instream The input file.
/// \param offset   The first index of the range to sort.
/// \param size     The size of the range.
/// \param comp     The comparator (less) function.
/// \param progress An optional progress indicator.
///
/// \tparam Stream  A `tpie::file_stream<T>` or `tpie::uncompressed_file_stream<T>`.
template<typename Stream, typename Compare>
void external_sort(Stream& instream,
                   tpie::stream_size_type offset, tpie::stream_size_type size,
                   Compare comp, tpie::progress_indicator_base* progress = nullptr)
{
    return detail::generic_sort<typename Stream::item_type>(instream, offset, size, comp, progress);
}

/// Sort the items [offset, offset + size) in `instream` using `comp`.
///
/// \param instream The input file.
/// \param offset   The first index of the range to sort.
/// \param size     The size of the range.
/// \param comp     The comparator (less) function.
/// \param progress A progress indicator.
///
/// \tparam Stream  A `tpie::file_stream<T>` or `tpie::uncompressed_file_stream<T>`.
template<typename Stream, typename Compare>
void external_sort(Stream& instream,
                   tpie::stream_size_type offset, tpie::stream_size_type size,
                   Compare comp, tpie::progress_indicator_base& progress)
{
    return external_sort(instream, offset, size, comp, &progress);
}

/// Sort the items in `instream` using `comp`.
///
/// \param instream The input file.
/// \param comp     The comparator (less) function.
/// \param progress An optional progress indicator.
///
/// \tparam Stream  A `tpie::file_stream<T>` or `tpie::uncompressed_file_stream<T>`.
template<typename Stream, typename Compare>
void external_sort(Stream& instream, Compare comp, tpie::progress_indicator_base* progress = nullptr) {
    return external_sort(instream, 0, instream.size(), comp, progress);
}


/// Sort the items in `instream` using `comp`.
///
/// \param instream The input file.
/// \param comp     The comparator (less) function.
/// \param progress A progress indicator.
///
/// \tparam Stream  A `tpie::file_stream<T>` or `tpie::uncompressed_file_stream<T>`.
template<typename Stream, typename Compare>
void external_sort(Stream& instream, Compare comp, tpie::progress_indicator_base& progress) {
    return external_sort(instream, comp, &progress);
}

} // namespace geodb

#endif // UTILITY_EXTERNAL_SORT_HPP
