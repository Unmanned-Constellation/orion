add_executable(fuzz_topic fuzz_topic.cpp)

target_link_libraries(fuzz_topic PRIVATE orion_transport)

target_compile_features(fuzz_topic PRIVATE cxx_std_20)

# -fsanitize=fuzzer provides the libFuzzer entry point; ASan is inherited from
# the fuzz preset's global add_link_options.
target_link_options(fuzz_topic PRIVATE -fsanitize=fuzzer)
