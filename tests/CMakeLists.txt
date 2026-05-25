find_package(GTest REQUIRED CONFIG)
include(GoogleTest)

add_executable(orion_tests transport_test.cpp)

target_link_libraries(
    orion_tests
    PRIVATE GTest::gmock_main orion_transport orion_proto
)

target_compile_features(orion_tests PRIVATE cxx_std_20)

# PRE_TEST defers discovery to ctest time so the build step does not execute
# the test binary. This avoids crashes when building with TSan in Docker
# (where personality(ADDR_NO_RANDOMIZE) is blocked).
gtest_discover_tests(orion_tests DISCOVERY_MODE PRE_TEST)

if(ORION_FUZZING)
    add_subdirectory(fuzz)
endif()
