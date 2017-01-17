set(SOURCES
    common.cpp
    parser.cpp
)

set(HEADERS
    common.hpp
    parser.hpp
    date_time.hpp
    filesystem.hpp
    type_traits.hpp

    utility/movable_adapter.hpp
    utility/range_utils.hpp
    utility/tuple_utils.hpp

    utility/id_allocator.hpp
    utility/file_allocator.hpp
    utility/temp_dir.hpp

    algorithm.hpp
    point.hpp
    interval.hpp
    interval_set.hpp
    bounding_box.hpp
    trajectory.hpp

    irwi/base.hpp
    irwi/query.hpp
    irwi/cursor.hpp
    irwi/tree_external.hpp
    irwi/tree_internal.hpp
    irwi/tree.hpp

    irwi/inverted_index.hpp
    irwi/inverted_index_internal.hpp
    irwi/inverted_index_external.hpp

    irwi/posting.hpp
    irwi/postings_list.hpp
    irwi/postings_list_internal.hpp
    irwi/postings_list_external.hpp

    irwi/string_map.hpp
    irwi/string_map_bimap.hpp
    irwi/string_map_internal.hpp
    irwi/string_map_external.hpp
)

add_library(geodb ${SOURCES} ${HEADERS})
target_link_libraries(geodb PUBLIC tpie gsl boost "pthread")

get_filename_component(PARENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)
target_include_directories(geodb PUBLIC "${PARENT_DIR}")

add_executable(main main.cpp)
target_link_libraries(main geodb)
