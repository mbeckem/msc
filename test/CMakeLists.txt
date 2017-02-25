set(SOURCES
    algorithm.cpp
    bounding_box.cpp
    file_allocator.cpp
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
    tuple_utils.cpp
)

add_executable("${PROJECT_NAME}-test" ${SOURCES})
target_link_libraries("${PROJECT_NAME}-test" catch geodb)
