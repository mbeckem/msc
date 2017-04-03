### osrm (openstreetmaps)
ExternalProject_Add(project_osrm
    SOURCE_DIR  "${DEPS_SOURCE_DIR}/osrm"
    PREFIX      "${DEPS_BINARY_DIR}/osrm"
    CMAKE_ARGS  -DCMAKE_BUILD_TYPE=Release
                -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
    EXCLUDE_FROM_ALL 1
)
ExternalProject_Get_Property(project_osrm INSTALL_DIR)

add_library(osrm INTERFACE)
add_dependencies(osrm project_osrm)
target_include_directories(osrm SYSTEM INTERFACE "${INSTALL_DIR}/include")
target_include_directories(osrm SYSTEM INTERFACE "${INSTALL_DIR}/include/osrm")
target_link_libraries(osrm INTERFACE "${INSTALL_DIR}/lib/libosrm.a" "-lrt")
