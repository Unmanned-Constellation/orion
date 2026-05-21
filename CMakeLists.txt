cmake_minimum_required(VERSION 3.26)

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
    set(ORION_VERSION "0.0.0")
    message(STATUS "No version tag found — defaulting to ${ORION_VERSION}")
endif()

project(orion VERSION ${ORION_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

add_compile_options(-fdiagnostics-color=always -fpic)

find_program(CCACHE ccache)
if(CCACHE)
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE})
endif()

include(cmake/tools.cmake)

find_package(protobuf CONFIG REQUIRED)
find_package(zenohcxx REQUIRED)

orion_suppress_dep_warnings(
    protobuf::libprotobuf
    zenohc::lib
    zenohcxx::zenohc
)

add_subdirectory(proto)
add_subdirectory(libs/transport)
