add_library(orion_clock INTERFACE)

target_include_directories(orion_clock INTERFACE include)

target_compile_features(orion_clock INTERFACE cxx_std_20)
