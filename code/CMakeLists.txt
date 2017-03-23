cmake_minimum_required(VERSION 3.4)
project(msc)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(Functions)
include(Profile)

option(SANITIZE_UNDEFINED "Enable undefined behaviour sanitizer (gnu and clang only)" OFF)

# Release build by default.
if(NOT CMAKE_BUILD_TYPE)
    message("Defaulting to \"Release\" build type")
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

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -fvisibility=hidden")
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror")
    endif()

    if (SANITIZE_UNDEFINED)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined" )
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=undefined")
    endif()
endif()

if ((CMAKE_CXX_COMPILER_ID MATCHES "GNU") AND (CMAKE_BUILD_TYPE STREQUAL "Release"))
    set(CMAKE_AR "gcc-ar")
    set(CMAKE_RANLIB "gcc-ranlib")
    set(CMAKE_NM "gcc-nm")
    add_compile_options(-flto)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto=4")
endif()

include_directories("${PROJECT_SOURCE_DIR}")

add_definitions(-DGEODB_ID_SET_BLOOM)

include(deps/Dependencies.cmake)
add_subdirectory(geodb)
add_subdirectory(cmd)
add_subdirectory(test)

