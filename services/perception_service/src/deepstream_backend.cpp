#ifdef ORION_ENABLE_DEEPSTREAM

#include "orion/perception/deepstream_backend.hpp"

#include <ctime>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gstnvdsmeta.h>
#include <nvdsmeta_schema.h>

#include "orion/v1/detection.pb.h"

namespace orion::perception
{

namespace
{

auto getRealtimeNs() -> int64_t
{
    auto ts = timespec{};
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
}

auto getMonotonicNs() -> int64_t
{
    auto ts = timespec{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
}

template <typename T>
auto checkedMake(const char* element, const char* name) -> T*
{
    auto* el = gst_element_factory_make(element, name);
    if (!el)
    {
        throw std::runtime_error{std::string{"Failed to create GStreamer element: "} + element};
    }
    return reinterpret_cast<T*>(el); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
}

} // namespace

// ── Construction / destruction ────────────────────────────────────────────────

DeepStreamBackend::DeepStreamBackend(DeepStreamConfig config) : config_(std::move(config))
{
    if (config_.camera_devices.empty())
    {
        throw std::invalid_argument{"DeepStreamConfig: camera_devices must not be empty"};
    }
    if (config_.camera_devices.size() != config_.camera_ids.size())
    {
        throw std::invalid_argument{
            "DeepStreamConfig: camera_devices and camera_ids must have the same length"};
    }
}

DeepStreamBackend::~DeepStreamBackend() { stop(); }

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void DeepStreamBackend::start(DetectionCallback callback)
{
    callback_ = std::move(callback);

    gst_init(nullptr, nullptr);
    computeClockOffset();
    buildPipeline();

    gst_element_set_state(pipeline_, GST_STATE_PLAYING);

    loop_            = g_main_loop_new(nullptr, FALSE);
    pipeline_thread_ = std::thread{[this] { g_main_loop_run(loop_); }};
}

void DeepStreamBackend::stop()
{
    if (loop_ && g_main_loop_is_running(loop_))
    {
        g_main_loop_quit(loop_);
    }
    if (pipeline_thread_.joinable())
    {
        pipeline_thread_.join();
    }
    if (pipeline_)
    {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }
    if (loop_)
    {
        g_main_loop_unref(loop_);
        loop_ = nullptr;
    }
}

// ── Clock domain offset ───────────────────────────────────────────────────────

void DeepStreamBackend::computeClockOffset()
{
    // Sample both clocks as close together as possible to minimise error.
    // GStreamer buffer PTS uses CLOCK_MONOTONIC; Orion uses CLOCK_REALTIME.
    auto monotonic   = getMonotonicNs();
    auto realtime    = getRealtimeNs();
    clock_offset_ns_ = realtime - monotonic;
}

// ── Pipeline construction ─────────────────────────────────────────────────────

void DeepStreamBackend::buildPipeline()
{
    pipeline_ = GST_ELEMENT(gst_pipeline_new("orion-perception"));

    // ── Muxer (shared across all cameras) ────────────────────────────────────
    mux_ = checkedMake<GstElement>("nvstreammux", "mux");
    g_object_set(mux_,
                 "batch-size",
                 static_cast<guint>(config_.camera_devices.size()),
                 "width",
                 static_cast<guint>(config_.capture_width),
                 "height",
                 static_cast<guint>(config_.capture_height),
                 "batched-push-timeout",
                 33'000, // 33 ms in microseconds ≈ 30fps
                 nullptr);
    gst_bin_add(GST_BIN(pipeline_), mux_);

    // ── Source bins (one per camera) ──────────────────────────────────────────
    const auto num_cameras = config_.camera_devices.size();
    for (auto idx = uint32_t{0}; idx < num_cameras; ++idx)
    {
        addSourceBin(idx);
    }

    // ── Inference ─────────────────────────────────────────────────────────────
    auto* infer = checkedMake<GstElement>("nvinfer", "infer");
    g_object_set(infer,
                 "model-engine-file",
                 config_.model_engine_path.c_str(),
                 "custom-lib-path",
                 config_.custom_lib_path.c_str(),
                 "parse-bbox-func-name",
                 "NvDsInferParseRtDetr",
                 "batch-size",
                 static_cast<guint>(num_cameras),
                 "network-mode",
                 2, // FP16
                 "num-detected-classes",
                 static_cast<guint>(config_.num_classes),
                 "process-mode",
                 1, // primary detector
                 nullptr);
    gst_bin_add(GST_BIN(pipeline_), infer);

    // ── Tracker ───────────────────────────────────────────────────────────────
    auto* tracker = checkedMake<GstElement>("nvtracker", "tracker");
    g_object_set(tracker,
                 "ll-lib-file",
                 "/opt/nvidia/deepstream/deepstream/lib/libnvds_mot_iou.so",
                 "tracker-width",
                 static_cast<guint>(config_.capture_width),
                 "tracker-height",
                 static_cast<guint>(config_.capture_height),
                 nullptr);
    gst_bin_add(GST_BIN(pipeline_), tracker);

    // ── Tee ───────────────────────────────────────────────────────────────────
    auto* tee = checkedMake<GstElement>("tee", "tee");
    gst_bin_add(GST_BIN(pipeline_), tee);

    // ── Appsink branch ───────────────────────────────────────────────────────
    auto* appsink_queue = checkedMake<GstElement>("queue", "appsink-queue");
    appsink_            = checkedMake<GstElement>("appsink", "appsink");
    g_object_set(appsink_, "emit-signals", TRUE, "sync", FALSE, nullptr);
    g_signal_connect(appsink_, "new-sample", G_CALLBACK(onNewSample), this);
    gst_bin_add_many(GST_BIN(pipeline_), appsink_queue, appsink_, nullptr);

    // ── Display branch ────────────────────────────────────────────────────────
    auto* osd_queue = checkedMake<GstElement>("queue", "osd-queue");
    osd_            = checkedMake<GstElement>("nvdsosd", "osd");
    g_object_set(osd_, "process-mode", 1, nullptr); // GPU mode
    auto* encoder = checkedMake<GstElement>("nvv4l2h264enc", "encoder");
    auto* rtp_pay = checkedMake<GstElement>("rtph264pay", "rtp-pay");
    g_object_set(rtp_pay, "config-interval", 1, "pt", 96, nullptr);
    auto* udp_sink = checkedMake<GstElement>("udpsink", "udp-sink");
    g_object_set(udp_sink,
                 "host",
                 config_.stream_host.c_str(),
                 "port",
                 static_cast<gint>(config_.stream_port),
                 "sync",
                 FALSE,
                 nullptr);
    gst_bin_add_many(GST_BIN(pipeline_), osd_queue, osd_, encoder, rtp_pay, udp_sink, nullptr);

    // ── Link the shared path: mux → infer → tracker → tee ────────────────────
    if (!gst_element_link_many(mux_, infer, tracker, tee, nullptr))
    {
        throw std::runtime_error{"Failed to link mux→infer→tracker→tee"};
    }

    // ── Link appsink branch: tee → queue → appsink ───────────────────────────
    if (!gst_element_link_many(tee, appsink_queue, appsink_, nullptr))
    {
        throw std::runtime_error{"Failed to link tee→appsink branch"};
    }

    // ── Link display branch: tee → queue → osd → encoder → rtp → udp ────────
    if (!gst_element_link_many(tee, osd_queue, osd_, encoder, rtp_pay, udp_sink, nullptr))
    {
        throw std::runtime_error{"Failed to link tee→display branch"};
    }

    // ── OSD stats probe ───────────────────────────────────────────────────────
    auto* osd_sink_pad = gst_element_get_static_pad(osd_, "sink");
    gst_pad_add_probe(osd_sink_pad, GST_PAD_PROBE_TYPE_BUFFER, onOsdSinkProbe, this, nullptr);
    gst_object_unref(osd_sink_pad);
}

void DeepStreamBackend::addSourceBin(uint32_t index)
{
    const auto& device    = config_.camera_devices[index];
    const auto  name_base = "cam-" + std::to_string(index);

    auto* src = checkedMake<GstElement>("v4l2src", (name_base + "-src").c_str());
    g_object_set(src, "device", device.c_str(), nullptr);

    auto* caps_filter = checkedMake<GstElement>("capsfilter", (name_base + "-caps").c_str());
    auto* caps        = gst_caps_new_simple("video/x-raw",
                                     "format",
                                     G_TYPE_STRING,
                                     "UYVY",
                                     "width",
                                     G_TYPE_INT,
                                     static_cast<gint>(config_.capture_width),
                                     "height",
                                     G_TYPE_INT,
                                     static_cast<gint>(config_.capture_height),
                                     "framerate",
                                     GST_TYPE_FRACTION,
                                     static_cast<gint>(config_.capture_fps),
                                     1,
                                     nullptr);
    g_object_set(caps_filter, "caps", caps, nullptr);
    gst_caps_unref(caps);

    auto* convert   = checkedMake<GstElement>("nvvideoconvert", (name_base + "-convert").c_str());
    auto* nvmm_caps = checkedMake<GstElement>("capsfilter", (name_base + "-nvmm-caps").c_str());
    auto* nvmm =
        gst_caps_new_simple("video/x-raw(memory:NVMM)", "format", G_TYPE_STRING, "NV12", nullptr);
    g_object_set(nvmm_caps, "caps", nvmm, nullptr);
    gst_caps_unref(nvmm);

    gst_bin_add_many(GST_BIN(pipeline_), src, caps_filter, convert, nvmm_caps, nullptr);

    if (!gst_element_link_many(src, caps_filter, convert, nvmm_caps, nullptr))
    {
        throw std::runtime_error{"Failed to link source bin for " + device};
    }

    // Request a sink pad on the muxer and link
    const auto pad_name = "sink_" + std::to_string(index);
    auto*      mux_pad  = gst_element_request_pad_simple(mux_, pad_name.c_str());
    auto*      src_pad  = gst_element_get_static_pad(nvmm_caps, "src");
    if (gst_pad_link(src_pad, mux_pad) != GST_PAD_LINK_OK)
    {
        throw std::runtime_error{"Failed to link source bin to mux for " + device};
    }
    gst_object_unref(src_pad);
    gst_object_unref(mux_pad);
}

// ── Appsink callback ──────────────────────────────────────────────────────────

auto DeepStreamBackend::onNewSample(GstElement* sink, DeepStreamBackend* self) -> GstFlowReturn
{
    auto* sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample)
    {
        return GST_FLOW_ERROR;
    }

    auto* buf           = gst_sample_get_buffer(sample);
    auto  monotonic_pts = GST_BUFFER_PTS(buf);
    self->processBuffer(buf, monotonic_pts);

    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

void DeepStreamBackend::processBuffer(GstBuffer* buf, GstClockTime monotonic_pts)
{
    const auto callback_realtime_ns = getRealtimeNs();

    auto* batch_meta = gst_buffer_get_nvds_batch_meta(buf);
    if (!batch_meta)
    {
        return;
    }

    // One DetectionFrame per source_id in the batch
    auto frames = std::unordered_map<uint32_t, orion::v1::DetectionFrame>{};

    for (auto* fl = batch_meta->frame_meta_list; fl != nullptr; fl = fl->next)
    {
        auto* frame_meta = static_cast<NvDsFrameMeta*>(
            fl->data); // NOLINT(cppcoreguidelines-pro-type-static-cast-downcast)
        const auto source_id = frame_meta->source_id;
        const auto captured_at =
            static_cast<uint64_t>(static_cast<int64_t>(frame_meta->buf_pts) + clock_offset_ns_);

        auto& det_frame = frames[source_id];
        if (source_id < config_.camera_ids.size())
        {
            det_frame.set_camera_id(config_.camera_ids[source_id]);
        }
        det_frame.set_frame_width(config_.capture_width);
        det_frame.set_frame_height(config_.capture_height);
        det_frame.set_pipeline_latency_ns(static_cast<uint64_t>(callback_realtime_ns) -
                                          captured_at);

        for (auto* ol = frame_meta->obj_meta_list; ol != nullptr; ol = ol->next)
        {
            auto* obj_meta = static_cast<NvDsObjectMeta*>(
                ol->data); // NOLINT(cppcoreguidelines-pro-type-static-cast-downcast)
            auto* det = det_frame.add_detections();

            // Normalize pixel coords to [0,1]
            const auto& r     = obj_meta->detector_bbox_info.org_bbox_coords;
            const auto  inv_w = 1.0F / static_cast<float>(config_.capture_width);
            const auto  inv_h = 1.0F / static_cast<float>(config_.capture_height);
            auto*       bbox  = det->mutable_bbox();
            bbox->set_cx((r.left + r.width * 0.5F) * inv_w);
            bbox->set_cy((r.top + r.height * 0.5F) * inv_h);
            bbox->set_w(r.width * inv_w);
            bbox->set_h(r.height * inv_h);

            det->set_class_id(static_cast<uint32_t>(obj_meta->class_id));
            det->set_confidence(obj_meta->confidence);
            det->set_track_id(obj_meta->object_id); // assigned by nvtracker
        }

        // Fire callback immediately after each frame is built
        callback_(det_frame, captured_at);
    }

    // Update FPS EMA using the batch buffer PTS
    if (GST_CLOCK_TIME_IS_VALID(last_pts_) && monotonic_pts > last_pts_)
    {
        const auto     frame_dt = static_cast<double>(monotonic_pts - last_pts_) / 1e9;
        const auto     inst_fps = 1.0 / frame_dt;
        constexpr auto alpha    = 0.1; // smoothing factor
        fps_ema_ = (fps_ema_ == 0.0) ? inst_fps : alpha * inst_fps + (1.0 - alpha) * fps_ema_;
    }
    last_pts_ = monotonic_pts;
}

// ── OSD stats probe ───────────────────────────────────────────────────────────

auto DeepStreamBackend::onOsdSinkProbe(GstPad* /*pad*/,
                                       GstPadProbeInfo* info,
                                       gpointer         user_data) -> GstPadProbeReturn
{
    auto* self = static_cast<DeepStreamBackend*>(user_data);
    auto* buf  = GST_PAD_PROBE_INFO_BUFFER(info);

    auto* batch_meta = gst_buffer_get_nvds_batch_meta(buf);
    if (!batch_meta)
    {
        return GST_PAD_PROBE_OK;
    }

    for (auto* fl = batch_meta->frame_meta_list; fl != nullptr; fl = fl->next)
    {
        auto* frame_meta = static_cast<NvDsFrameMeta*>(
            fl->data); // NOLINT(cppcoreguidelines-pro-type-static-cast-downcast)

        auto* display_meta       = nvds_acquire_display_meta_from_pool(batch_meta);
        display_meta->num_labels = 1;

        // Count detections for this frame
        auto det_count = 0;
        for (auto* ol = frame_meta->obj_meta_list; ol; ol = ol->next)
        {
            ++det_count;
        }

        const auto latency_ms  = static_cast<double>(frame_meta->ntp_timestamp) / 1e6; // approx
        auto&      text_params = display_meta->text_params[0];
        // Format: "FPS: 29.8 | Latency: 22ms | Det: 3"
        const auto stats_text = std::string{"FPS: "} +
                                std::to_string(static_cast<int>(self->fps_ema_ + 0.5)) +
                                " | Lat: " + std::to_string(static_cast<int>(latency_ms)) + "ms" +
                                " | Det: " + std::to_string(det_count);

        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) — owned by DeepStream pool
        text_params.display_text = g_strdup(stats_text.c_str());
        text_params.x_offset     = 20;
        text_params.y_offset     = 40;
        text_params.font_params.font_name =
            const_cast<char*>("Mono"); // NOLINT(cppcoreguidelines-pro-type-const-cast)
        text_params.font_params.font_size  = 14;
        text_params.font_params.font_color = {1.0F, 1.0F, 1.0F, 1.0F}; // white
        text_params.set_bg_clr             = 1;
        text_params.text_bg_clr            = {0.0F, 0.0F, 0.0F, 0.7F}; // black, semi-transparent

        nvds_add_display_meta_to_frame(frame_meta, display_meta);
    }

    return GST_PAD_PROBE_OK;
}

} // namespace orion::perception

#endif // ORION_ENABLE_DEEPSTREAM
