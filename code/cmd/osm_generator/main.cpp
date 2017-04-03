#include "common/common.hpp"

#include <boost/program_options.hpp>

#include <osrm/engine_config.hpp>
#include <osrm/osrm.hpp>
#include <osrm/route_parameters.hpp>
#include <osrm/status.hpp>
#include <osrm/storage_config.hpp>

using namespace geodb;
namespace po = boost::program_options;

static std::string path;

static void parse_options(int argc, char** argv) {
    unused(argc, argv);
}

int main(int argc, char** argv) {
    return tpie_main([&]{
        parse_options(argc, argv);

        osrm::EngineConfig config;
        config.use_shared_memory = false;

        return 0;
    });
}
