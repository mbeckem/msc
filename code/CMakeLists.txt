cmake_minimum_required(VERSION 3.4)
project(msc)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(Functions)
include(Profile)

option(SANITIZE_UNDEFINED "Enable undefined behaviour sanitizer (gnu and clang only)" OFF)
option(USE_BLOOM "Use bloom filters instead of interval sets in the inverted index" OFF)
option(USE_NAIVE_NODE_BUILDING, "Use the unoptimized node loading algorithm." OFF)
option(BUILD_OSM "Build the openstreetmaps generator." OFF)
option(BUILD_INSPECTOR "Build the inspector tool. Requires Qt5 and libopenscenegraph." OFF)

# Release build by default.
if(NOT CMAKE_BUILD_TYPE)
    message(STATUS "Defaulting to \"Release\" build type.")
    set(CMAKE_BUILD_TYPE "Release"
        CACHE STRING
        "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel Profile."
        FORCE)
endif()

# Write all output files to the project's (binary-) root,
# regardless of where they are declared in the folder structure.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

# Set compiler specific flags.
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_EXTENSIONS OFF)
message(STATUS "Build configuration: ${CMAKE_BUILD_TYPE}")
if ((CMAKE_CXX_COMPILER_ID MATCHES "GNU") OR (CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
    # -O1 instead of -O2 for more helpful debug information.
    # set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O1 -g -DNDEBUG")

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-maybe-uninitialized -fvisibility=hidden")
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror")
    endif()

    if (SANITIZE_UNDEFINED)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined" )
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=undefined")
    endif()
endif()

if ((CMAKE_CXX_COMPILER_ID MATCHES "GNU") AND (CMAKE_BUILD_TYPE STREQUAL "Release"))
    message(STATUS "Link time optimization is enabled.")
    set(CMAKE_AR "gcc-ar")
    set(CMAKE_RANLIB "gcc-ranlib")
    set(CMAKE_NM "gcc-nm")
    add_compile_options(-flto)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto=4")
endif()

include_directories("${PROJECT_SOURCE_DIR}")

if (USE_BLOOM)
    add_definitions(-DGEODB_ID_SET_BLOOM)
    message(STATUS "Using bloom filters")
else()
    add_definitions(-DGEODB_ID_SET_INTERVALS)
    message(STATUS "Using interval sets")
endif()

if (USE_NAIVE_NODE_BUILDING)
    add_definitions(-DGEODB_NAIVE_NODE_BUILDING)
    message(STATUS "Using naive node building algorithm")
endif()

include(deps/Dependencies.cmake)
add_subdirectory(doc)
add_subdirectory(geodb)
add_subdirectory(cmd)
add_subdirectory(test)

