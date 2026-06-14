#ifdef ORION_ENABLE_DEEPSTREAM

#include <cstdint>
#include <string>
#include <vector>

#include <CLI/CLI.hpp>

#include "orion/app/service_bootstrapper.hpp"
#include "orion/perception/deepstream_backend.hpp"
#include "orion/perception/perception_service.hpp"
#include "orion/transport/session.hpp"
#include "orion/v1/detection.pb.h"

// NOLINTNEXTLINE(bugprone-exception-escape)
auto main(int argc, char** argv) -> int
{
    auto ds_config = orion::perception::DeepStreamConfig{};

    auto bootstrap = orion::app::ServiceBootstrapper{"perception-service"};
    bootstrap.withOptions([&](CLI::App& app) {
        // ── Camera sources ────────────────────────────────────────────────────
        app.add_option(
               "--camera-device", ds_config.camera_devices, "V4L2 device path (e.g. /dev/video0)")
            ->envname("CAMERA_DEVICE")
            ->required()
            ->expected(1, 10);
        app.add_option("--camera-id", ds_config.camera_ids, "Logical camera name (e.g. forward)")
            ->envname("CAMERA_ID")
            ->required()
            ->expected(1, 10);

        // ── Capture parameters ────────────────────────────────────────────────
        app.add_option("--capture-width", ds_config.capture_width, "Capture width in pixels")
            ->envname("CAPTURE_WIDTH");
        app.add_option("--capture-height", ds_config.capture_height, "Capture height in pixels")
            ->envname("CAPTURE_HEIGHT");
        app.add_option("--capture-fps", ds_config.capture_fps, "Capture frame rate")
            ->envname("CAPTURE_FPS");

        // ── Inference ─────────────────────────────────────────────────────────
        app.add_option(
               "--model-engine", ds_config.model_engine_path, "Path to TensorRT .engine file")
            ->envname("MODEL_ENGINE")
            ->required();
        app.add_option("--custom-lib", ds_config.custom_lib_path, "Path to rtdetr_parser.so")
            ->envname("CUSTOM_LIB");
        app.add_option("--num-classes", ds_config.num_classes, "Number of detection classes")
            ->envname("NUM_CLASSES");
        app.add_option(
               "--conf-threshold", ds_config.conf_threshold, "Detection confidence threshold [0,1]")
            ->envname("CONF_THRESHOLD");
        app.add_option(
               "--tracker-lib", ds_config.tracker_lib_path, "Path to DeepStream MOT tracker .so")
            ->envname("TRACKER_LIB");
        app.add_option(
               "--parse-bbox-func", ds_config.parse_bbox_func, "Bbox parser symbol in custom-lib")
            ->envname("PARSE_BBOX_FUNC");

        // ── Stream output ─────────────────────────────────────────────────────
        app.add_option("--stream-host", ds_config.stream_host, "UDP stream destination host")
            ->envname("STREAM_HOST");
        app.add_option("--stream-port", ds_config.stream_port, "UDP stream destination port")
            ->envname("STREAM_PORT");

        app.final_callback([&]() {
            if (ds_config.camera_devices.size() != ds_config.camera_ids.size())
            {
                throw CLI::ValidationError{"--camera-device/--camera-id",
                                           "must have the same number of values"};
            }
        });
    });
    auto ctx   = bootstrap.run(argc, argv);
    auto topic = "orion/" + ctx.vehicle_id + "/sensing/detections";

    ctx.log->info("starting — vehicle={} cameras={} model={}",
                  ctx.vehicle_id,
                  ds_config.camera_devices.size(),
                  ds_config.model_engine_path);

    auto session   = orion::transport::Session::create({ctx.vehicle_id, "perception-service"});
    auto publisher = session.advertise<orion::v1::DetectionFrame>(topic);

    auto backend = orion::perception::DeepStreamBackend{std::move(ds_config)};
    auto svc     = orion::perception::PerceptionService{
        backend, std::move(publisher)}; // NOLINT(misc-const-correctness)

    svc.start();
    ctx.log->info("running — topic={}", topic);

    ctx.latch->wait();

    ctx.log->info("stopping");
    svc.stop();
    ctx.log->info("stopped");
}

#endif // ORION_ENABLE_DEEPSTREAM
