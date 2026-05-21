function(orion_suppress_dep_warnings)
    foreach(_target IN LISTS ARGN)
        if(TARGET ${_target})
            get_target_property(_incs ${_target} INTERFACE_INCLUDE_DIRECTORIES)
            if(_incs)
                set_property(
                    TARGET ${_target}
                    PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_incs}"
                )
            endif()
        endif()
    endforeach()
endfunction()

find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    file(
        GLOB_RECURSE ALL_SOURCE_FILES
        ${CMAKE_SOURCE_DIR}/*.cpp
        ${CMAKE_SOURCE_DIR}/*.hpp
    )
    list(FILTER ALL_SOURCE_FILES EXCLUDE REGEX "/build/")
    add_custom_target(
        format-clang
        COMMAND ${CLANG_FORMAT} -i ${ALL_SOURCE_FILES}
        COMMENT "Running clang-format"
    )
endif()

find_package(Doxygen)
if(DOXYGEN_FOUND)
    add_custom_target(
        docs
        COMMAND
            ${CMAKE_COMMAND} -E make_directory
            ${CMAKE_SOURCE_DIR}/docs/_build/doxygen
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_SOURCE_DIR}/docs/Doxyfile
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running Doxygen"
    )
endif()

find_program(RUN_CLANG_TIDY run-clang-tidy)
if(RUN_CLANG_TIDY)
    add_custom_target(
        tidy
        COMMAND
            ${RUN_CLANG_TIDY} -p ${CMAKE_BINARY_DIR}
            "^${CMAKE_SOURCE_DIR}/(?!build|conan|cmake|docs|scripts).*\\.cpp$"
        COMMENT "Running clang-tidy"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endif()

find_program(GERSEMI gersemi)
if(GERSEMI)
    file(
        GLOB_RECURSE ALL_CMAKE_FILES
        ${CMAKE_SOURCE_DIR}/CMakeLists.txt
        ${CMAKE_SOURCE_DIR}/*.cmake
    )
    list(FILTER ALL_CMAKE_FILES EXCLUDE REGEX "/build/")
    add_custom_target(
        format-cmake
        COMMAND ${GERSEMI} -i ${ALL_CMAKE_FILES}
        COMMENT "Running gersemi"
    )
endif()
