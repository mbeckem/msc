include(ExternalProject)

set(DEPS_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(DEPS_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/deps")

include("${DEPS_SOURCE_DIR}/boost.cmake")
include("${DEPS_SOURCE_DIR}/catch.cmake")
include("${DEPS_SOURCE_DIR}/fmt.cmake")
include("${DEPS_SOURCE_DIR}/gsl.cmake")
include("${DEPS_SOURCE_DIR}/json.cmake")
include("${DEPS_SOURCE_DIR}/osg.cmake")
include("${DEPS_SOURCE_DIR}/osrm.cmake")
include("${DEPS_SOURCE_DIR}/qt.cmake")
include("${DEPS_SOURCE_DIR}/tpie.cmake")
