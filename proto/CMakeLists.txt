file(GLOB_RECURSE PROTO_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.proto")

if(PROTO_FILES)
    protobuf_generate_cpp(
        PROTO_SRCS
        PROTO_HDRS
        ${PROTO_FILES}
        IMPORT_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    add_library(orion_proto STATIC ${PROTO_SRCS} ${PROTO_HDRS})
    target_include_directories(orion_proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(orion_proto PUBLIC protobuf::libprotobuf)
else()
    add_library(orion_proto INTERFACE)
endif()
