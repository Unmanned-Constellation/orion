find_package(benchmark REQUIRED CONFIG)

add_executable(orion_transport_benchmarks transport_benchmarks.cpp)

target_link_libraries(
    orion_transport_benchmarks
    PRIVATE benchmark::benchmark_main orion_transport orion_proto
)

target_compile_features(orion_transport_benchmarks PRIVATE cxx_std_20)
