include_guard(GLOBAL)

# orion_split_debug_symbols(<target>)
#
# Post-build step that extracts DWARF debug symbols into a sidecar <binary>.dbg
# file and strips them from the deployable binary. The stripped binary retains
# a GNU debug link so that debuggers and addr2line can locate the sidecar
# automatically when it is placed alongside the binary.
#
# Only runs for EXECUTABLE and SHARED_LIBRARY targets in Release builds.
# The .dbg file is installed to lib/debug/ as part of the debug-symbols component.
#
# Usage:
#   include(cmake/DebugSymbols.cmake)
#   orion_split_debug_symbols(my_service)
function(orion_split_debug_symbols TARGET_NAME)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        return()
    endif()

    get_target_property(TARGET_TYPE ${TARGET_NAME} TYPE)
    if(NOT TARGET_TYPE MATCHES "(EXECUTABLE|SHARED_LIBRARY)")
        return()
    endif()

    if(NOT CMAKE_OBJCOPY)
        find_program(CMAKE_OBJCOPY NAMES objcopy llvm-objcopy)
    endif()

    if(NOT CMAKE_OBJCOPY)
        message(
            WARNING
            "objcopy not found — skipping debug symbol split for ${TARGET_NAME}"
        )
        return()
    endif()

    set(BINARY_PATH "$<TARGET_FILE:${TARGET_NAME}>")
    set(DEBUG_FILE_PATH "${BINARY_PATH}.dbg")

    add_custom_command(
        TARGET ${TARGET_NAME}
        POST_BUILD
        COMMAND
            ${CMAKE_OBJCOPY} --only-keep-debug "${BINARY_PATH}"
            "${DEBUG_FILE_PATH}"
        COMMAND ${CMAKE_OBJCOPY} --strip-debug "${BINARY_PATH}"
        COMMAND
            ${CMAKE_OBJCOPY} --add-gnu-debuglink="${DEBUG_FILE_PATH}"
            "${BINARY_PATH}"
        COMMENT "Splitting debug symbols: ${TARGET_NAME} -> ${TARGET_NAME}.dbg"
        VERBATIM
    )

    install(
        FILES "${DEBUG_FILE_PATH}"
        DESTINATION lib/debug
        COMPONENT debug-symbols
        OPTIONAL
    )
endfunction()
