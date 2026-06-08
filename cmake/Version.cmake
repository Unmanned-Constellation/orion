execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_DESCRIBE
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT GIT_DESCRIBE)
    set(GIT_DESCRIBE "unknown")
endif()

set(ORION_VERSION_STRING
    "${GIT_DESCRIBE} (${CMAKE_BUILD_TYPE}, ${CMAKE_SYSTEM_PROCESSOR})"
)

configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/version.hpp.in
    ${CMAKE_BINARY_DIR}/generated/orion/version.hpp
    @ONLY
)
