include(ExternalProject)

### GSL
add_library(gsl INTERFACE)
target_include_directories(gsl SYSTEM INTERFACE "GSL")
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    #target_compile_definitions(gsl INTERFACE GSL_THROW_ON_CONTRACT_VIOLATION)
    target_compile_definitions(gsl INTERFACE GSL_TERMINATE_ON_CONTRACT_VIOLATION)
else()
    target_compile_definitions(gsl INTERFACE GSL_UNENFORCED_ON_CONTRACT_VIOLATION)
endif()

add_library(catch INTERFACE)
target_include_directories(catch INTERFACE "catch")

### fmt
ExternalProject_Add(project_fmt
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fmt-3.0.0"
    PREFIX     "${CMAKE_CURRENT_BINARY_DIR}/fmt-3.0.0"
    CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DFMT_DOC=0
)

ExternalProject_Get_Property(project_fmt INSTALL_DIR)
add_library(fmt INTERFACE)
add_dependencies(fmt project_fmt)
target_include_directories(fmt SYSTEM INTERFACE "${INSTALL_DIR}/include")
target_link_libraries(fmt INTERFACE "${INSTALL_DIR}/lib/libfmt.a")

## tpie
ExternalProject_add(project_tpie
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tpie"
    PREFIX     "${CMAKE_CURRENT_BINARY_DIR}/tpie"
    CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
)

ExternalProject_Get_Property(project_tpie INSTALL_DIR)
add_library(tpie INTERFACE)
add_dependencies(tpie project_tpie)
target_include_directories(tpie SYSTEM INTERFACE "${INSTALL_DIR}/include")
target_link_libraries(tpie INTERFACE "${INSTALL_DIR}/lib/libtpie.a")

### boost
set(Boost_USE_STATIC_LIBS       ON)
set(Boost_USE_MULTITHREADED     ON)
set(Boost_USE_STATIC_RUNTIME    OFF)
find_package(Boost 1.60 COMPONENTS filesystem system REQUIRED)

add_library(boost INTERFACE)
target_include_directories(boost SYSTEM INTERFACE "${Boost_INCLUDE_DIRS}")
target_link_libraries(boost INTERFACE "${Boost_LIBRARIES}")

