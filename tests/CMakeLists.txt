find_package(GTest REQUIRED CONFIG)
include(GoogleTest)

add_executable(
    orion_tests
    clock_test.cpp
    clock_service_test.cpp
    crash_handler_test.cpp
    frame_scheduler_test.cpp
    logger_factory_test.cpp
    service_config_test.cpp
    shutdown_latch_test.cpp
    transport_test.cpp
)

target_link_libraries(
    orion_tests
    PRIVATE
        GTest::gmock_main
        orion_main
        orion_app
        orion_clock
        orion_clock_service
        orion_transport
        orion_proto
)

target_compile_features(orion_tests PRIVATE cxx_std_20)

# PRE_TEST defers discovery to ctest time so the build step does not execute
# the test binary. This avoids crashes when building with TSan in Docker
# (where personality(ADDR_NO_RANDOMIZE) is blocked).
gtest_discover_tests(
    orion_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES ENVIRONMENT "GTEST_DEATH_TEST_STYLE=threadsafe"
)

if(ORION_FUZZING)
    add_subdirectory(fuzz)
endif()
