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

find_package(Git)
set(ORION_VERSION "dev")
if(Git_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(GIT_TAG)
        set(ORION_VERSION ${GIT_TAG})
    endif()
endif()

find_package(Doxygen)
if(DOXYGEN_FOUND)
    add_custom_target(
        docs
        COMMAND
            ${CMAKE_COMMAND} -E env "ORION_PROJECT_VERSION=${ORION_VERSION}"
            ${CMAKE_COMMAND} -E make_directory
            ${CMAKE_SOURCE_DIR}/docs/_build/doxygen
        COMMAND
            ${CMAKE_COMMAND} -E env "ORION_PROJECT_VERSION=${ORION_VERSION}"
            ${DOXYGEN_EXECUTABLE} ${CMAKE_SOURCE_DIR}/docs/Doxyfile
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running Doxygen (Version: ${ORION_VERSION})"
        VERBATIM
    )
endif()

find_program(RUN_CLANG_TIDY run-clang-tidy)
if(RUN_CLANG_TIDY)
    add_custom_target(
        tidy
        COMMAND
            ${RUN_CLANG_TIDY} -p ${CMAKE_BINARY_DIR}
            "^${CMAKE_SOURCE_DIR}/(libs|proto|tests)/"
        COMMENT "Running clang-tidy"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS orion_proto
        VERBATIM
    )
endif()

find_program(GERSEMI gersemi)
if(GERSEMI)
    add_custom_target(
        format-cmake
        COMMAND ${GERSEMI} --respect-ignore-files -i ${CMAKE_SOURCE_DIR}
        COMMENT "Running gersemi"
        VERBATIM
    )
endif()

if(ORION_SANITIZE)
    add_compile_options(
        -fsanitize=address,undefined
        -fno-sanitize-recover=all
        -fno-omit-frame-pointer
    )
    add_link_options(-fsanitize=address,undefined)
endif()

if(ORION_TSAN)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()

if(ORION_COVERAGE)
    add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
    add_link_options(-fprofile-instr-generate)

    find_program(LLVM_PROFDATA llvm-profdata-18 HINTS /usr/bin)
    find_program(LLVM_COV llvm-cov-18 HINTS /usr/bin)
    if(LLVM_PROFDATA AND LLVM_COV)
        add_custom_target(
            coverage-report
            COMMAND
                /bin/bash -c
                "${LLVM_PROFDATA} merge -sparse ${CMAKE_BINARY_DIR}/cov-*.profraw -o ${CMAKE_BINARY_DIR}/coverage.profdata"
            COMMAND
                ${LLVM_COV} report "${CMAKE_BINARY_DIR}/tests/orion_tests"
                "-instr-profile=${CMAKE_BINARY_DIR}/coverage.profdata"
                "--ignore-filename-regex=(build/|/_deps/)"
            COMMENT "Generating LLVM coverage report"
            VERBATIM
        )
    endif()
endif()

if(ORION_FUZZING)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()
