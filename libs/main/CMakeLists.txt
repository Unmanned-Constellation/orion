find_package(CLI11 REQUIRED CONFIG)

add_library(orion_main STATIC src/service_config.cpp)

target_include_directories(orion_main PUBLIC include)

target_compile_features(orion_main PUBLIC cxx_std_20)

target_link_libraries(orion_main PUBLIC orion_app CLI11::CLI11)
