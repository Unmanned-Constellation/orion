cmake_minimum_required(VERSION 3.22)

# Read version from the nearest git tag (e.g. v0.1.0).
# See docs/versioning.md for how to tag a release.
execute_process(
    COMMAND git describe --tags --abbrev=0
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_TAG
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(GIT_TAG MATCHES "^v[0-9]+\\.[0-9]+\\.[0-9]+")
    string(REGEX REPLACE "^v" "" ORION_VERSION ${GIT_TAG})
else()
    set(ORION_VERSION "0.1.0")
    message(STATUS "No version tag found — defaulting to ${ORION_VERSION}")
endif()

project(orion VERSION ${ORION_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_program(CCACHE ccache)
if(CCACHE)
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE})
endif()

include(cmake/tools.cmake)

find_package(PkgConfig REQUIRED)
find_package(Protobuf REQUIRED)

include(FetchContent)

# Zenoh C (Rust core) — pin to a stable release before shipping
FetchContent_Declare(
    zenohc
    GIT_REPOSITORY https://github.com/eclipse-zenoh/zenoh-c.git
    GIT_TAG 1.9.0
)
FetchContent_Declare(
    zenohcxx
    GIT_REPOSITORY https://github.com/eclipse-zenoh/zenoh-cpp.git
    GIT_TAG 1.9.0
)
FetchContent_MakeAvailable(zenohc zenohcxx)

add_subdirectory(proto)
add_subdirectory(libs/transport)
