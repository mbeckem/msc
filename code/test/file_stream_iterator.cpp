#include <catch.hpp>

#include "geodb/utility/file_stream_iterator.hpp"

using namespace geodb;

TEST_CASE("file stream iteration", "[file-stream-iterator]") {
    tpie::file_stream<int> stream;
    stream.open();

    int data[] = {1, 4, 5, 6, 7, 9, 11, 33};

    for (int i : data) {
        stream.write(i);
    }

    const auto begin = file_stream_iterator<int>(stream, 0);
    const auto end = file_stream_iterator<int>(stream, stream.size());

    REQUIRE(begin.stream() == &stream);
    REQUIRE(end - begin == 8);
    REQUIRE(begin - end == -8);
    REQUIRE(std::equal(begin, end, std::begin(data), std::end(data)));

    size_t index = 0;
    for (auto pos = begin; pos != end; ++pos) {
        REQUIRE(index == pos.offset());
        ++index;
    }
}
