set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets OpenGL)
find_package(OpenSceneGraph REQUIRED COMPONENTS osgDB osgGA osgUtil osgViewer)

set(SOURCES
    main.cpp
    main_window.cpp
    tree_display.cpp
)

set(HEADERS
    main_window.hpp
    scene_renderer.hpp
    tree_display.cpp
)

set(FORMS
    main_window.ui
    tree_display.ui
)
qt5_wrap_ui(FORMS_COMPILED ${FORMS})

add_executable(inspector ${SOURCES} ${HEADERS} ${FORMS_COMPILED})
target_link_libraries(inspector PRIVATE geodb)
target_include_directories(inspector PRIVATE "${OPENSCENEGRAPH_INCLUDE_DIRS}")
target_link_libraries(inspector PRIVATE "${OPENSCENEGRAPH_LIBRARIES}")
target_link_libraries(inspector PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets Qt5::OpenGL)

install(TARGETS inspector RUNTIME DESTINATION bin)
