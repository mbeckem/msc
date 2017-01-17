set(SOURCES
    main.cpp
    id_allocator.cpp
    file_allocator.cpp
    inverted_index.cpp
    irwi.cpp
    point.cpp
    bounding_box.cpp
    trajectory.cpp
    tuple_utils.cpp
    movable_adapter.cpp
    postings_list.cpp
    interval.cpp
    interval_set.cpp
    string_map.cpp
    parser.cpp
    algorithm.cpp
    range_utils.cpp
)

add_executable("${PROJECT_NAME}-test" ${SOURCES})
target_link_libraries("${PROJECT_NAME}-test" catch geodb)
