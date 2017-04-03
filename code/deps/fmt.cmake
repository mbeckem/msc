### fmt is used for string formatting.

ExternalProject_Add(project_fmt
    SOURCE_DIR  "${DEPS_SOURCE_DIR}/fmt-3.0.0"
    PREFIX      "${DEPS_BINARY_DIR}/fmt-3.0.0"
    CMAKE_ARGS  -DCMAKE_BUILD_TYPE=Release
                -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
                -DFMT_DOC=0
                -DFMT_TEST=0
    EXCLUDE_FROM_ALL 1
)

ExternalProject_Get_Property(project_fmt INSTALL_DIR)
add_library(fmt INTERFACE)
add_dependencies(fmt project_fmt)
target_include_directories(fmt SYSTEM INTERFACE "${INSTALL_DIR}/include")
target_link_libraries(fmt INTERFACE "${INSTALL_DIR}/lib/libfmt.a")
