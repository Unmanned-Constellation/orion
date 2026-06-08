find_package(benchmark REQUIRED CONFIG)

add_executable(orion_benchmarks transport_bench.cpp)

target_link_libraries(
    orion_benchmarks
    PRIVATE benchmark::benchmark_main orion_transport orion_proto
)

target_compile_features(orion_benchmarks PRIVATE cxx_std_20)
