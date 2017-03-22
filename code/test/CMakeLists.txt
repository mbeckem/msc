set(SOURCES
    algorithm.cpp
    bloom_filter.cpp
    bounding_box.cpp
    file_allocator.cpp
    hilbert.cpp
    hybrid_buffer.cpp
    hybrid_map.cpp
    id_allocator.cpp
    interval.cpp
    interval_set.cpp
    inverted_index.cpp
    irwi.cpp
    main.cpp
    movable_adapter.cpp
    parser.cpp
    point.cpp
    postings_list.cpp
    range_utils.cpp
    raw_stream.cpp
    shared_values.cpp
    string_map.cpp
    trajectory.cpp
    tree_internals.cpp
    tuple_utils.cpp
)

add_executable("${PROJECT_NAME}-test" ${SOURCES})
target_compile_options("${PROJECT_NAME}-test" PRIVATE "-Wno-parentheses")
target_link_libraries("${PROJECT_NAME}-test" catch geodb)
