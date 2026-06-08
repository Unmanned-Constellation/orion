add_library(orion_clock_service STATIC src/clock_service.cpp)

target_include_directories(orion_clock_service PUBLIC include)

target_compile_features(orion_clock_service PUBLIC cxx_std_20)

target_link_libraries(
    orion_clock_service
    PUBLIC orion_clock orion_app orion_transport orion_proto
)

add_executable(clock_service src/main.cpp)

target_compile_features(clock_service PRIVATE cxx_std_20)

target_include_directories(clock_service PRIVATE ${CMAKE_BINARY_DIR}/generated)

target_link_libraries(clock_service PRIVATE orion_clock_service orion_main)
