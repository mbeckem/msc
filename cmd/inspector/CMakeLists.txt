set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

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
target_link_libraries(inspector PRIVATE common)
target_link_libraries(inspector PRIVATE osg)
target_link_libraries(inspector PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets Qt5::OpenGL)

install(TARGETS inspector RUNTIME DESTINATION bin)
