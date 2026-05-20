find_package(Protobuf REQUIRED)

file(GLOB PROTO_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.proto")

if(PROTO_FILES)
    protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

    add_library(orion_proto STATIC ${PROTO_SRCS} ${PROTO_HDRS})
    target_include_directories(
        orion_proto
        PUBLIC ${CMAKE_CURRENT_BINARY_DIR} ${Protobuf_INCLUDE_DIRS}
    )
    target_link_libraries(orion_proto PUBLIC ${Protobuf_LIBRARIES})
else()
    add_library(orion_proto INTERFACE)
endif()
