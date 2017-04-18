if (NOT BUILD_OSM)
    message(STATUS "Not building the osm_generator tool. Enable it by setting BUILD_OSM to ON.")
    return()
endif()

message(STATUS "Building the osm_generator tool.")

add_executable(osm_generator main.cpp)
target_link_libraries(osm_generator osrm common)
