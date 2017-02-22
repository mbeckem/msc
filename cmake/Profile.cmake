# Build type: Profile
set(CMAKE_CXX_FLAGS_PROFILE "-O3 -DNDEBUG -g" CACHE STRING
    "Flags used by the C++ compiler during profile builds."
    FORCE)
set(CMAKE_C_FLAGS_PROFILE "-O3 -DNDEBUG -g" CACHE STRING
    "Flags used by the C compiler during profile builds."
    FORCE)
set(CMAKE_EXE_LINKER_FLAGS_PROFILE
    "" CACHE STRING
    "Flags used for linking binaries during profile builds."
    FORCE)
set(CMAKE_STATIC_LINKER_FLAGS_PROFILE
    "" CACHE STRING
    "Flags used by the shared libraries linker during profile builds."
    FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_PROFILE
    "" CACHE STRING
    "Flags used by the shared libraries linker during profile builds."
    FORCE)

mark_as_advanced(
    CMAKE_CXX_FLAGS_PROFILE
    CMAKE_C_FLAGS_PROFILE
    CMAKE_EXE_LINKER_FLAGS_PROFILE
    CMAKE_STATIC_LINKER_FLAGS_PROFILE
    CMAKE_SHARED_LINKER_FLAGS_PROFILE)
