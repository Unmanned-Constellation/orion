#ifdef ORION_ENABLE_DEEPSTREAM

#include <cstdint>
#include <string>
#include <vector>

#include <CLI/CLI.hpp>

#include "orion/app/crash_handler.hpp"
#include "orion/app/logger_factory.hpp"
#include "orion/app/service_config.hpp"
#include "orion/app/shutdown_latch.hpp"
#include "orion/perception/deepstream_backend.hpp"
#include "orion/perception/perception_service.hpp"
#include "orion/transport/session.hpp"
#include "orion/v1/detection.pb.h"
#include "orion/version.hpp"

// NOLINTNEXTLINE(bugprone-exception-escape)
auto main(int argc, char** argv) -> int
{
    auto latch = orion::app::ShutdownLatch{};
    auto crash = orion::app::CrashHandler{};

    auto app = CLI::App{"perception-service"};
    app.set_version_flag("--version", ORION_VERSION_STRING);

    auto cfg = orion::app::ServiceConfig{};
    orion::app::addServiceConfig(app, cfg);

    // ── Camera sources ────────────────────────────────────────────────────────
    auto camera_devices = std::vector<std::string>{};
    auto camera_ids     = std::vector<std::string>{};
    app.add_option("--camera-device", camera_devices, "V4L2 device path (e.g. /dev/video0)")
        ->envname("CAMERA_DEVICE")
        ->required()
        ->expected(1, 10);
    app.add_option("--camera-id", camera_ids, "Logical camera name (e.g. forward)")
        ->envname("CAMERA_ID")
        ->required()
        ->expected(1, 10);

    // ── Capture parameters ────────────────────────────────────────────────────
    auto capture_width  = uint32_t{960};
    auto capture_height = uint32_t{600};
    auto capture_fps    = uint32_t{30};
    app.add_option("--capture-width", capture_width, "Capture width in pixels")
        ->envname("CAPTURE_WIDTH");
    app.add_option("--capture-height", capture_height, "Capture height in pixels")
        ->envname("CAPTURE_HEIGHT");
    app.add_option("--capture-fps", capture_fps, "Capture frame rate")->envname("CAPTURE_FPS");

    // ── Inference ─────────────────────────────────────────────────────────────
    auto model_engine_path = std::string{};
    auto custom_lib_path   = std::string{"/opt/orion/lib/rtdetr_parser.so"};
    auto num_classes       = uint32_t{80};
    auto conf_threshold    = float{0.5F};
    app.add_option("--model-engine", model_engine_path, "Path to TensorRT .engine file")
        ->envname("MODEL_ENGINE")
        ->required();
    app.add_option("--custom-lib", custom_lib_path, "Path to rtdetr_parser.so")
        ->envname("CUSTOM_LIB");
    app.add_option("--num-classes", num_classes, "Number of detection classes")
        ->envname("NUM_CLASSES");
    app.add_option("--conf-threshold", conf_threshold, "Detection confidence threshold [0,1]")
        ->envname("CONF_THRESHOLD");

    // ── Stream output ─────────────────────────────────────────────────────────
    auto stream_host = std::string{"224.1.1.1"};
    auto stream_port = uint16_t{5000};
    app.add_option("--stream-host", stream_host, "UDP stream destination host")
        ->envname("STREAM_HOST");
    app.add_option("--stream-port", stream_port, "UDP stream destination port")
        ->envname("STREAM_PORT");

    // ── Transport ─────────────────────────────────────────────────────────────
    auto topic = std::string{};
    app.add_option("--topic", topic, "Zenoh topic for DetectionFrame messages")
        ->envname("DETECTION_TOPIC");

    CLI11_PARSE(app, argc, argv);

    if (camera_devices.size() != camera_ids.size())
    {
        app.exit(CLI::Error{"camera-mismatch",
                            "--camera-device and --camera-id must have the same number of values"});
        return 1;
    }

    if (topic.empty())
    {
        topic = "orion/" + cfg.vehicle_id + "/sensing/detections";
    }

    orion::app::LoggerFactory::init(cfg.log_level);
    auto log = orion::app::LoggerFactory::get("perception-service");
    log->info("starting — vehicle={} cameras={} model={}",
              cfg.vehicle_id,
              camera_devices.size(),
              model_engine_path);

    auto ds_config              = orion::perception::DeepStreamConfig{};
    ds_config.camera_devices    = std::move(camera_devices);
    ds_config.camera_ids        = std::move(camera_ids);
    ds_config.capture_width     = capture_width;
    ds_config.capture_height    = capture_height;
    ds_config.capture_fps       = capture_fps;
    ds_config.model_engine_path = std::move(model_engine_path);
    ds_config.custom_lib_path   = std::move(custom_lib_path);
    ds_config.num_classes       = num_classes;
    ds_config.conf_threshold    = conf_threshold;
    ds_config.stream_host       = std::move(stream_host);
    ds_config.stream_port       = stream_port;

    auto session   = orion::transport::Session::create({cfg.vehicle_id, "perception-service"});
    auto publisher = session.advertise<orion::v1::DetectionFrame>(topic);

    auto backend = orion::perception::DeepStreamBackend{std::move(ds_config)};
    auto svc     = orion::perception::PerceptionService{
        backend, std::move(publisher)}; // NOLINT(misc-const-correctness)

    svc.start();
    log->info("running — topic={}", topic);

    latch.wait();

    log->info("stopping");
    svc.stop();
    log->info("stopped");
}

#endif // ORION_ENABLE_DEEPSTREAM
