#include <catch.hpp>

#include "geodb/utility/file_allocator.hpp"
#include "geodb/utility/temp_dir.hpp"

using namespace geodb;

TEST_CASE("file_allocator allocates files", "[file-allocator]") {
    temp_dir dir;

    int id1 = 0, id2 = 0, id3 = 0;
    {
        file_allocator<int> fa(dir.path());

        id1 = fa.alloc();
        id2 = fa.alloc();
        REQUIRE(id1 != id2);
        REQUIRE(fa.path(id1) != fa.path(id2));
    }

    {
        // restore state from file.
        file_allocator<int> fa(dir.path());

        id3 = fa.alloc();
        REQUIRE(id3 != id1);
        REQUIRE(id3 != id2);
    }

    {
        // remove files.
        file_allocator<int> fa(dir.path());

        fs::path f1, f2, f3;
        f1 = fa.path(id1);
        f2 = fa.path(id2);
        f3 = fa.path(id3);

        fs::ofstream create1(f1, std::ios_base::trunc);
        fs::ofstream create2(f2, std::ios_base::trunc);
        fs::ofstream create3(f3, std::ios_base::trunc);

        REQUIRE(fs::exists(f1));
        REQUIRE(fs::exists(f2));
        REQUIRE(fs::exists(f3));

        fa.free(id1);
        fa.free(id2);
        fa.free(id3);

        REQUIRE(!fs::exists(f1));
        REQUIRE(!fs::exists(f2));
        REQUIRE(!fs::exists(f3));
    }
}

TEST_CASE("file_allocator file names", "[file-allocator]") {
    temp_dir dir;

    file_allocator<int> fa(dir.path(), ".suffix123");

    int id = (fa.alloc(), fa.alloc(), fa.alloc());
    fs::path relative = fs::relative(fa.path(id), dir.path());
    fs::path expected = std::to_string(id) + ".suffix123";
    REQUIRE(relative == expected);
}
