find_package(Doxygen)

if(NOT DOXYGEN_FOUND)
    message(WARNING "Doxygen is needed to build the documentation.")
    return()
endif()

message(STATUS "Doxygen was found. The \"doc\" target is available.")

set(doxyfile_in ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
set(doxyfile ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

configure_file(${doxyfile_in} ${doxyfile} @ONLY)

add_custom_target(doc
    COMMAND ${DOXYGEN_EXECUTABLE} ${doxyfile}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Generating API documentation with Doxygen"
    VERBATIM)

install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/html DESTINATION share/doc)

set(SOURCES
    Doxyfile.in
)

# Make documentation files show up in the IDE.
add_custom_target(doc-ide SOURCES ${SOURCES})
