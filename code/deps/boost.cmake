### boost
set(Boost_USE_STATIC_LIBS       ON)
set(Boost_USE_MULTITHREADED     ON)
set(Boost_USE_STATIC_RUNTIME    OFF)
find_package(Boost 1.60 COMPONENTS filesystem system program_options REQUIRED)

add_library(boost INTERFACE)
target_include_directories(boost SYSTEM INTERFACE "${Boost_INCLUDE_DIRS}")
target_link_libraries(boost INTERFACE "${Boost_LIBRARIES}")
