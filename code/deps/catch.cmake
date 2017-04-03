### catch is unit testing library

add_library(catch INTERFACE)
target_include_directories(catch INTERFACE "${DEPS_SOURCE_DIR}/catch")
