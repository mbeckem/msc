cmake_minimum_required(VERSION 2.8)
find_package(TPIE REQUIRED)
include_directories(${TPIE_INCLUDE_DIRS})
add_executable(helloworld helloworld.cpp)
target_link_libraries(helloworld ${TPIE_LIBRARIES})
