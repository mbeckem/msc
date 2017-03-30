set(HEADERS

)

set(SOURCES
    common.cpp
)

add_library(common ${SOURCES} ${HEADERS})
target_link_libraries(common PUBLIC geodb json)
