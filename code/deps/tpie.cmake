## tpie

ExternalProject_add(project_tpie
    SOURCE_DIR "${DEPS_SOURCE_DIR}/tpie"
    PREFIX     "${DEPS_BINARY_DIR}/tpie"
    CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release
                -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
                -DCOMPILE_TEST=0
    EXCLUDE_FROM_ALL 1
)

ExternalProject_Get_Property(project_tpie INSTALL_DIR)
add_library(tpie INTERFACE)
add_dependencies(tpie project_tpie)
target_include_directories(tpie SYSTEM INTERFACE "${INSTALL_DIR}/include")
target_link_libraries(tpie INTERFACE "${INSTALL_DIR}/lib/libtpie.a")
