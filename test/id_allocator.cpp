#include <catch.hpp>

#include "geodb/utility/id_allocator.hpp"
#include "geodb/utility/temp_dir.hpp"

using namespace geodb;

TEST_CASE("id allocator allocates", "[id-allocator]") {
    tpie::temp_file tmp;

    {
        id_allocator<int> ids(tmp.path());
        REQUIRE(ids.alloc() == 1);
        REQUIRE(ids.alloc() == 2);
    }

    {
        // stored in exeternal memory.
        id_allocator<int> ids(tmp.path());
        REQUIRE(ids.alloc() == 3);
    }

    {
        // reuses freed ids.
        id_allocator<int> ids(tmp.path());
        ids.free(1);
        ids.free(2);
        ids.free(3);

        REQUIRE(ids.alloc() < 4);
        REQUIRE(ids.alloc() < 4);
        REQUIRE(ids.alloc() < 4);

        // first "unfreed" id.
        REQUIRE(ids.alloc() == 4);
    }
}
