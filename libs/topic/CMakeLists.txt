add_library(orion_topic STATIC src/topic.cpp)

target_include_directories(orion_topic PUBLIC include)

target_compile_features(orion_topic PUBLIC cxx_std_20)
