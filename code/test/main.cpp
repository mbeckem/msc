#define CATCH_CONFIG_RUNNER
#include <catch.hpp>

#include <tpie/tpie.h>

#include "geodb/common.hpp"

int main(int argc, char** argv) {
    int result = 0;

    tpie::tpie_init();
    {
        result = Catch::Session().run(argc, argv);
    }
    tpie::tpie_finish();
    return result;
}
