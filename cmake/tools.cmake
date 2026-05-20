find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    file(
        GLOB_RECURSE ALL_SOURCE_FILES
        ${CMAKE_SOURCE_DIR}/*.cpp
        ${CMAKE_SOURCE_DIR}/*.hpp
    )
    list(FILTER ALL_SOURCE_FILES EXCLUDE REGEX "/_deps/|/build/")
    add_custom_target(
        format-clang
        COMMAND ${CLANG_FORMAT} -i ${ALL_SOURCE_FILES}
        COMMENT "Running clang-format"
    )
endif()

find_package(Doxygen)
if(DOXYGEN_FOUND)
    configure_file(
        ${CMAKE_SOURCE_DIR}/Doxyfile.in
        ${CMAKE_BINARY_DIR}/Doxyfile
        @ONLY
    )
    add_custom_target(
        docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_BINARY_DIR}/Doxyfile
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
            "^${CMAKE_SOURCE_DIR}/(libs|proto)/.*\\.cpp$"
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
    list(FILTER ALL_CMAKE_FILES EXCLUDE REGEX "/_deps/|/build/")
    add_custom_target(
        format-cmake
        COMMAND ${GERSEMI} -i ${ALL_CMAKE_FILES}
        COMMENT "Running gersemi"
    )
endif()
