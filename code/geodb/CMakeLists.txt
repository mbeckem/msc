set(SOURCES
    common.cpp
    parser.cpp
    trajectory.cpp

    irwi/base.cpp
)

set(HEADERS
    algorithm.hpp
    bounding_box.hpp
    common.hpp
    date_time.hpp
    external_list.hpp
    filesystem.hpp
    hilbert.hpp
    hybrid_buffer.hpp
    hybrid_map.hpp
    parser.hpp
    interval.hpp
    interval_set.hpp
    trajectory.hpp
    type_traits.hpp
    vector.hpp

    utility/as_const.hpp
    utility/id_allocator.hpp
    utility/external_sort.hpp
    utility/file_allocator.hpp
    utility/function_utils.hpp
    utility/movable_adapter.hpp
    utility/range_utils.hpp
    utility/raw_stream.hpp
    utility/shared_values.hpp
    utility/temp_dir.hpp
    utility/tuple_utils.hpp

    irwi/base.hpp
    irwi/block_handle.hpp
    irwi/bulk_load_common.hpp
    irwi/bulk_load_hilbert.hpp
    irwi/bulk_load_str.hpp
    irwi/bulk_load_quickload.hpp
    irwi/cursor.hpp
    irwi/inverted_index.hpp
    irwi/inverted_index_internal.hpp
    irwi/inverted_index_external.hpp
    irwi/label_count.hpp
    irwi/posting.hpp
    irwi/postings_list.hpp
    irwi/postings_list_internal.hpp
    irwi/postings_list_external.hpp
    irwi/query.hpp
    irwi/string_map.hpp
    irwi/string_map_bimap.hpp
    irwi/string_map_internal.hpp
    irwi/string_map_external.hpp
    irwi/tree.hpp
    irwi/tree_external.hpp
    irwi/tree_insertion.hpp
    irwi/tree_internal.hpp
    irwi/tree_partition.hpp
    irwi/tree_state.hpp
)

add_library(geodb ${SOURCES} ${HEADERS})
target_link_libraries(geodb PUBLIC tpie fmt gsl boost "pthread")

add_executable(main main.cpp)
target_link_libraries(main geodb)
