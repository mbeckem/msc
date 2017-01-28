include(ExternalProject)

set(DEPS_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(DEPS_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/deps")

### GSL
add_library(gsl INTERFACE)
target_include_directories(gsl SYSTEM INTERFACE "${DEPS_SOURCE_DIR}/GSL")
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    #target_compile_definitions(gsl INTERFACE GSL_THROW_ON_CONTRACT_VIOLATION)
    target_compile_definitions(gsl INTERFACE GSL_TERMINATE_ON_CONTRACT_VIOLATION)
else()
    target_compile_definitions(gsl INTERFACE GSL_UNENFORCED_ON_CONTRACT_VIOLATION)
endif()

add_library(catch INTERFACE)
target_include_directories(catch INTERFACE "${DEPS_SOURCE_DIR}/catch")

### fmt
ExternalProject_Add(project_fmt
    SOURCE_DIR "${DEPS_SOURCE_DIR}/fmt-3.0.0"
    PREFIX     "${DEPS_BINARY_DIR}/fmt-3.0.0"
    CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DFMT_DOC=0 -DFMT_TEST=0
)

ExternalProject_Get_Property(project_fmt INSTALL_DIR)
add_library(fmt INTERFACE)
add_dependencies(fmt project_fmt)
target_include_directories(fmt SYSTEM INTERFACE "${INSTALL_DIR}/include")
target_link_libraries(fmt INTERFACE "${INSTALL_DIR}/lib/libfmt.a")

## tpie
ExternalProject_add(project_tpie
    SOURCE_DIR "${DEPS_SOURCE_DIR}/tpie"
    PREFIX     "${DEPS_BINARY_DIR}/tpie"
    CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DCOMPILE_TEST=0
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
find_package(Boost 1.60 COMPONENTS filesystem system program_options REQUIRED)

add_library(boost INTERFACE)
target_include_directories(boost SYSTEM INTERFACE "${Boost_INCLUDE_DIRS}")
target_link_libraries(boost INTERFACE "${Boost_LIBRARIES}")

### Qt5. Use Qt5::Core etc as link targets.
find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets OpenGL)

### openscenegraph
find_package(OpenSceneGraph REQUIRED COMPONENTS osgDB osgGA osgUtil osgViewer)
add_library(osg INTERFACE)
target_include_directories(osg SYSTEM INTERFACE "${OPENSCENEGRAPH_INCLUDE_DIRS}")
target_link_libraries(osg INTERFACE "${OPENSCENEGRAPH_LIBRARIES}")
