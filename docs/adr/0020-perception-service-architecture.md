# ADR 0020: Perception Service Architecture

## Status
Accepted

## Context

The Perception Service is responsible for camera ingestion, hardware-accelerated
inference, object tracking, and publishing `DetectionFrame` messages to the Zenoh Bus.
ADR-0002 established DeepStream as the inference framework and mandated an abstract
interface to allow x86 builds without hardware. ADR-0016 established the reactive
execution model (start/wait/stop, not `FrameScheduler`). This ADR resolves the
internal architecture deferred by both.

### Hardware constraints

The vision camera is an Arducam Darksee (AR0234, global shutter). It connects via
MIPI CSI but exposes a **V4L2 device node** through an on-board FPGA ISP (ImageEK),
bypassing the Jetson's native Argus/ISP stack entirely. `nvarguscamerasrc` is therefore
unavailable — `v4l2src` is the only correct source element. Camera output is UYVY;
`nvvideoconvert` converts to NV12 in NVMM before `nvstreammux`.

The Orin Nano supports only one active CSI port at a time with this camera.

### Inference target

- **Model:** Ultralytics RT-DETR-R18, COCO pretrained
- **Precision:** FP16 (INT8 deferred until flight footage is available for calibration)
- **Resolution:** 960×600 @ 30fps (33ms frame budget)
- **Tracker:** `nvtracker` with IOU tracker — assigns persistent `track_id` per object

RT-DETR's output tensor layout does not match the built-in `nvinfer` YOLO parser.
A custom `nvdsparsebbox` plugin is required to map RT-DETR outputs to `NvDsObjectMeta`.

## Decision

### Pipeline topology

```
v4l2src
  → nvvideoconvert (UYVY → NV12, NVMM)
  → nvstreammux
  → nvinfer (RT-DETR-R18 FP16, custom nvdsparsebbox parser)
  → nvtracker (IOU)
  → [tee]
      → appsink           (DetectionFrame callback → Zenoh publish)
      → nvdsosd           (bounding box + stats overlay)
          → nvv4l2h264enc
              → rtph264pay
                  → udpsink host=<stream-host> port=<stream-port>
```

The stream host and port are configurable via `--stream-host` and `--stream-port`
CLI args (default: `224.1.1.1:5000`). Any standard player receives it:

```bash
# VLC
vlc rtp://@:5000

# GStreamer
gst-launch-1.0 udpsrc port=5000 ! application/x-rtp,encoding-name=H264 \
  ! rtph264depay ! avdec_h264 ! autovideosink
```

RTSP (`gst-rtsp-server`) is deferred until a frontend with proper session
negotiation exists.

The `appsink` tee is placed **before** `nvdsosd` and encoding. This ensures
`pipeline_latency_ns` measures capture-to-inference, not capture-to-encode, and
the Zenoh publish is not delayed by the encoder.

### `PerceptionBackend` interface

```cpp
class PerceptionBackend {
public:
    using DetectionCallback = std::function<void(DetectionFrame)>;
    virtual void start(DetectionCallback cb) = 0;
    virtual void stop() = 0;
    virtual ~PerceptionBackend() = default;
};
```

The backend fires `DetectionFrame` via callback. It has no knowledge of Zenoh,
`Publisher`, or `TimeSource`. `PerceptionService` owns the `Session`, `Publisher`,
and `TimeSource`, and wires them to the callback:

```cpp
backend_->start([&](DetectionFrame frame) {
    pub_.publish(frame, frame.captured_at_ns);
});
```

### `captured_at_ns` semantics

`captured_at_ns` reflects true frame capture time, not inference completion time.
`DeepStreamBackend` reads the V4L2 buffer PTS (`GST_BUFFER_PTS(buf)`, sourced from
`CLOCK_MONOTONIC`) and corrects it to the `CLOCK_REALTIME` epoch using an offset
computed once at `start()`:

```
clock_offset_ = CLOCK_REALTIME_now - CLOCK_MONOTONIC_now  (computed at start())
captured_at_ns = GST_BUFFER_PTS(buf) + clock_offset_
```

This means `pipeline_latency_ns` in `DetectionFrame` accurately represents the
capture-to-inference latency, not capture-to-callback.

### `DetectionFrame` schema

Published on `orion/{vehicle_id}/sensing/detections`. Fields:

| Field | Type | Description |
|---|---|---|
| `frame_width` | uint32 | Frame width in pixels |
| `frame_height` | uint32 | Frame height in pixels |
| `camera_id` | string | Source camera identifier within the service instance |
| `pipeline_latency_ns` | uint64 | Elapsed ns from frame capture to appsink callback |
| `detections` | repeated Detection | Zero or more detections in this frame |

Each `Detection`:

| Field | Type | Description |
|---|---|---|
| `bbox` | BoundingBox | Normalized [0,1] coordinates (cx, cy, w, h) |
| `class_id` | uint32 | Model class index |
| `confidence` | float | Detection confidence [0,1] |
| `track_id` | uint64 | Persistent object ID assigned by `nvtracker` |

Bounding box coordinates are normalized to [0,1] relative to `frame_width`/`frame_height`.
Consumers multiply by frame dimensions to recover pixel coordinates.

### Multi-camera design

`camera_id` in `DetectionFrame` allows one `PerceptionService` instance to manage
N cameras via a multi-stream `DeepStreamBackend` (`nvstreammux` batching, batch
inference, `nvmultistreamtiler` tiled output). The pipeline construction loop
iterates a configurable device list and requests a new `nvstreammux` sink pad per
camera. The following are CLI args, not hardcoded constants:

| Arg | Description |
|---|---|
| `--camera-devices` | Ordered list of V4L2 device nodes (`/dev/video0,/dev/video1,...`) |
| `--camera-ids` | Parallel list of `camera_id` strings for each device |
| `--stream-host` / `--stream-port` | UDP output destination |

`nvinfer` `batch-size` is set to match the device count at startup. The tiler
layout is auto-computed as `ceil(sqrt(N)) × ceil(N / ceil(sqrt(N)))` so it
accommodates any N without manual configuration. The tiled stream is one UDP
output regardless of camera count.

The current Stallion v1 hardware supports one active CSI camera; the multi-camera
path is exercised when hardware supports it without code changes.

### `FakePerceptionBackend`

The x86/test double. Constructed with a `std::vector<DetectionFrame>` and an emit
interval. Replays frames sequentially at that interval from a background thread
started by `start()`. Also exposes `emit(DetectionFrame)` for synchronous on-demand
injection in unit tests. Produces fully deterministic output.

### OSD overlay

`nvdsosd` renders in the top-left corner, white text with black border:
- Frame-level: FPS, pipeline latency (ms), detection count
- Per-detection: class name, confidence %, track ID (standard DeepStream bbox labels)

### Build split

| Flag | Backend compiled | Requires |
|---|---|---|
| `ORION_ENABLE_DEEPSTREAM=OFF` (default) | `FakePerceptionBackend` only | Nothing — builds on x86 |
| `ORION_ENABLE_DEEPSTREAM=ON` | `DeepStreamBackend` + `FakePerceptionBackend` | DeepStream SDK, JetPack 6.1 |

`DeepStreamBackend` and the custom `nvdsparsebbox` parser are gated behind
`ORION_ENABLE_DEEPSTREAM`. The rest of `orion_perception` (interface, proto, fake,
service wiring) compiles on x86 unconditionally.

## Consequences

- `v4l2src` replaces `nvarguscamerasrc` throughout. Any future camera that routes
  through the Jetson Argus ISP would require a separate source element configuration.
- The custom `nvdsparsebbox` parser couples the pipeline to Ultralytics RT-DETR-R18's
  specific output tensor layout. Swapping to a different model architecture requires
  updating or replacing the parser.
- FP16 precision is the baseline. INT8 requires calibration data from real flight
  footage and is deferred.
- `pipeline_latency_ns` is the primary per-frame latency signal. Per-stage breakdown
  (pre-process, inference, tracking separately) is deferred to a `PerceptionHealth`
  topic if operator diagnostics require it.
- The encoded video stream is sent over UDP+RTP to a configurable host and port.
  RTSP is deferred until a frontend with proper session negotiation exists.
