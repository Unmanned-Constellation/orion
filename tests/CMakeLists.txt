find_package(GTest REQUIRED CONFIG)
include(GoogleTest)

add_executable(orion_tests transport_test.cpp)

target_link_libraries(orion_tests PRIVATE GTest::gmock_main orion_transport)

target_compile_features(orion_tests PRIVATE cxx_std_20)

gtest_discover_tests(orion_tests)
