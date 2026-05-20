add_library(orion_transport SHARED src/transport.cpp)

target_include_directories(orion_transport PUBLIC include PRIVATE src)

target_link_libraries(
    orion_transport
    PUBLIC zenohcxx::zenohc
    PUBLIC orion_proto
)

target_compile_features(orion_transport PUBLIC cxx_std_17)
