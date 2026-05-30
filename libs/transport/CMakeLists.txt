add_library(orion_transport SHARED src/zenoh_session.cpp)

target_include_directories(orion_transport PUBLIC include PRIVATE src)

target_link_libraries(
    orion_transport
    PRIVATE zenohcxx::zenohc
    PRIVATE orion_proto
)

# zenohcxx::zenohc requires this preprocessor flag to select the zenoh-c backend.
target_compile_definitions(orion_transport PRIVATE ZENOHCXX_ZENOHC)

target_compile_features(orion_transport PUBLIC cxx_std_20)
