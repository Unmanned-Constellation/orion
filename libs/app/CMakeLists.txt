find_package(Backward REQUIRED CONFIG)

add_library(orion_app STATIC src/crash_handler.cpp)

target_include_directories(orion_app PUBLIC include)

target_compile_features(orion_app PUBLIC cxx_std_20)

target_link_libraries(orion_app PUBLIC orion_clock PRIVATE Backward::Backward)
