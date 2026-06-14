#pragma once

#ifdef ORION_ENABLE_DEEPSTREAM

#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include <glib.h>
#include <gst/gst.h>

#include "orion/perception/perception_backend.hpp"

namespace orion::perception
{

/// Configuration for the DeepStream inference pipeline.
struct DeepStreamConfig
{
    // Camera sources — parallel lists, must be the same length.
    std::vector<std::string> camera_devices; // e.g. ["/dev/video0"]
    std::vector<std::string> camera_ids;     // e.g. ["forward"]

    // Capture parameters
    uint32_t capture_width{960};
    uint32_t capture_height{600};
    uint32_t capture_fps{30};

    // Inference
    std::string model_engine_path;                       // path to TensorRT .engine file
    std::string custom_lib_path;                         // path to rtdetr_parser.so
    std::string parse_bbox_func{"NvDsInferParseRtDetr"}; // exported symbol in custom_lib_path
    uint32_t    num_classes{80};                         // COCO pretrained = 80
    float       conf_threshold{0.5F};

    // Tracker
    std::string tracker_lib_path{
        "/opt/nvidia/deepstream/deepstream/lib/libnvds_mot_iou.so"}; // IOU MOT tracker

    // Stream output (UDP+RTP)
    std::string stream_host{"224.1.1.1"};
    uint16_t    stream_port{5000};
};

/// DeepStream-backed PerceptionBackend for Jetson hardware.
///
/// Builds a programmatic GStreamer/DeepStream pipeline:
///   v4l2src → nvvideoconvert → nvstreammux → nvinfer (RT-DETR-R18 FP16)
///     → nvtracker (IOU) → [tee]
///         → appsink  (DetectionFrame → callback)
///         → nvdsosd → nvv4l2h264enc → rtph264pay → udpsink
///
/// @see ADR-0020
class DeepStreamBackend final : public PerceptionBackend
{
  public:
    explicit DeepStreamBackend(DeepStreamConfig config);

    /// @cond
    DeepStreamBackend(const DeepStreamBackend&)                    = delete;
    auto operator=(const DeepStreamBackend&) -> DeepStreamBackend& = delete;
    DeepStreamBackend(DeepStreamBackend&&)                         = delete;
    auto operator=(DeepStreamBackend&&) -> DeepStreamBackend&      = delete;
    ~DeepStreamBackend() override;
    /// @endcond

    /// Builds the pipeline, computes the clock-domain offset, and starts inference.
    /// Returns immediately — pipeline runs on an internal GLib main loop thread.
    void start(DetectionCallback callback) override;

    /// Stops the pipeline and joins the internal thread.
    void stop() override;

  private:
    void buildPipeline();
    void addSourceBin(uint32_t index);
    void computeClockOffset();

    static auto onNewSample(GstElement* sink, DeepStreamBackend* self) -> GstFlowReturn;
    static auto onOsdSinkProbe(GstPad*          pad,
                               GstPadProbeInfo* info,
                               gpointer         user_data) -> GstPadProbeReturn;
    static auto onBusMessage(GstBus* bus, GstMessage* msg, gpointer user_data) -> gboolean;

    void processBuffer(GstBuffer* buf, GstClockTime monotonic_pts);

    DeepStreamConfig  config_;
    DetectionCallback callback_;
    int64_t           clock_offset_ns_{0};    // CLOCK_REALTIME - CLOCK_MONOTONIC at start()
    std::string       nvinfer_config_path_{}; // temp file written at start(), deleted at stop()

    GstElement* pipeline_{nullptr};
    GstElement* mux_{nullptr};
    GstElement* appsink_{nullptr};
    GstElement* osd_{nullptr};
    GMainLoop*  loop_{nullptr};
    std::thread pipeline_thread_;

    // FPS tracking (exponential moving average, updated per frame)
    double       fps_ema_{0.0};
    GstClockTime last_pts_{GST_CLOCK_TIME_NONE};
};

} // namespace orion::perception

#endif // ORION_ENABLE_DEEPSTREAM
