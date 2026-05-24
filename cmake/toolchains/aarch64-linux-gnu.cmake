# Cross-compilation toolchain for aarch64-linux-gnu using clang-18.
#
# Prerequisites (installed in the cross-arm64 CI job):
#   clang-18             — compiler with multi-target support
#   gcc-aarch64-linux-gnu — provides the sysroot at /usr/aarch64-linux-gnu
#
# The Conan-generated toolchain is included at the end so that package paths
# and any Conan-injected flags layer on top of the cross settings.

set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_SYSROOT /usr/aarch64-linux-gnu)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(_conan_toolchain
    "${CMAKE_CURRENT_LIST_DIR}/../../build/CrossArm64/conan_toolchain.cmake"
)
if(EXISTS "${_conan_toolchain}")
    include("${_conan_toolchain}")
endif()
unset(_conan_toolchain)
