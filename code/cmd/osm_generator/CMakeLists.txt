if (NOT BUILD_OSM)
    message(STATUS "Not building the osm_generator")
    return()
endif()

add_executable(osm_generator main.cpp)
target_link_libraries(osm_generator osrm common)
