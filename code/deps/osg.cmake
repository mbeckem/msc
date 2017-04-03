### openscenegraph
find_package(OpenSceneGraph REQUIRED COMPONENTS osgDB osgGA osgUtil osgViewer)
if (OPENSCENEGRAPH_FOUND)
    add_library(osg INTERFACE)
    target_include_directories(osg SYSTEM INTERFACE "${OPENSCENEGRAPH_INCLUDE_DIRS}")
    target_link_libraries(osg INTERFACE "${OPENSCENEGRAPH_LIBRARIES}")
endif()
