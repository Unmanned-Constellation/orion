option(
    ORION_ENABLE_DEEPSTREAM
    "Build DeepStream-dependent perception backend (Jetson only)"
    OFF
)

add_library(orion_perception STATIC src/perception_service.cpp)

target_include_directories(orion_perception PUBLIC include)
target_compile_features(orion_perception PUBLIC cxx_std_20)
target_link_libraries(orion_perception PUBLIC orion_proto orion_transport)

if(ORION_ENABLE_DEEPSTREAM)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0 gstreamer-app-1.0)

    # DeepStream SDK headers — installed by the DeepStream package on Jetson.
    set(DEEPSTREAM_INCLUDE_DIR
        "/opt/nvidia/deepstream/deepstream/sources/includes"
        CACHE PATH
        "DeepStream SDK include directory"
    )

    target_sources(orion_perception PRIVATE src/deepstream_backend.cpp)

    target_compile_definitions(orion_perception PUBLIC ORION_ENABLE_DEEPSTREAM)

    target_include_directories(
        orion_perception
        PRIVATE ${GSTREAMER_INCLUDE_DIRS} ${DEEPSTREAM_INCLUDE_DIR}
    )

    target_link_libraries(orion_perception PRIVATE ${GSTREAMER_LIBRARIES})

    # ── RT-DETR custom nvinfer parser ─────────────────────────────────────────
    # Compiled as a shared library — loaded by nvinfer at runtime via dlopen.
    # NOT linked into orion_perception.
    add_library(rtdetr_parser SHARED src/rtdetr_parser.cpp)

    target_compile_features(rtdetr_parser PRIVATE cxx_std_20)
    target_compile_definitions(rtdetr_parser PRIVATE ORION_ENABLE_DEEPSTREAM)

    target_include_directories(rtdetr_parser PRIVATE ${DEEPSTREAM_INCLUDE_DIR})

    # ── perception_service executable ─────────────────────────────────────────
    add_executable(perception_service src/main.cpp)

    target_compile_features(perception_service PRIVATE cxx_std_20)
    target_compile_definitions(
        perception_service
        PRIVATE ORION_ENABLE_DEEPSTREAM
    )

    target_include_directories(
        perception_service
        PRIVATE ${CMAKE_BINARY_DIR}/generated
    )

    target_link_libraries(
        perception_service
        PRIVATE orion_perception orion_main orion_topic
    )
endif()
