set(SOURCES
    common.cpp
    parser.cpp
    trajectory.cpp
)

set(HEADERS
    algorithm.hpp
    bounding_box.hpp
    common.hpp
    date_time.hpp
    filesystem.hpp
    parser.hpp
    point.hpp
    interval.hpp
    interval_set.hpp
    trajectory.hpp
    type_traits.hpp

    utility/as_const.hpp
    utility/id_allocator.hpp
    utility/file_allocator.hpp
    utility/movable_adapter.hpp
    utility/range_utils.hpp
    utility/shared_values.hpp
    utility/temp_dir.hpp
    utility/tuple_utils.hpp

    irwi/base.hpp
    irwi/query.hpp
    irwi/cursor.hpp
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
    irwi/tree.hpp
    irwi/tree_internal.hpp
    irwi/tree_external.hpp
)

add_library(geodb ${SOURCES} ${HEADERS})
target_link_libraries(geodb PUBLIC tpie gsl boost "pthread")

add_executable(main main.cpp)
target_link_libraries(main geodb)
