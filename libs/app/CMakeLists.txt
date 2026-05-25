add_library(orion_app INTERFACE)

target_include_directories(orion_app INTERFACE include)

target_compile_features(orion_app INTERFACE cxx_std_20)

target_link_libraries(orion_app INTERFACE orion_clock)
