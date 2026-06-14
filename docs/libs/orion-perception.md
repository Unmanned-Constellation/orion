# orion_perception

The perception library provides the `PerceptionService`, the `PerceptionBackend` interface,
and `FakePerceptionBackend` for testing. On Jetson hardware (with `ORION_ENABLE_DEEPSTREAM=ON`)
it also builds `DeepStreamBackend` and the `rtdetr_parser` shared library.

---

## Class hierarchy

```{mermaid}
classDiagram
    class PerceptionBackend {
        <<abstract>>
        +start(callback) void
        +stop() void
    }
    class PerceptionService {
        -backend_ PerceptionBackend*
        -publisher_ Publisher~DetectionFrame~
        +start() void
        +stop() void
    }
    class FakePerceptionBackend {
        -script_ vector~ScriptedFrame~
        -callback_ DetectionCallback
        +start(callback) void
        +stop() void
        +emit(frame, captured_at_ns) void
    }
    class ScriptedFrame {
        <<nested>>
        +frame DetectionFrame
        +captured_at_ns uint64_t
    }
    class DeepStreamBackend {
        <<ORION_ENABLE_DEEPSTREAM=ON>>
        -config_ DeepStreamConfig
        -callback_ DetectionCallback
        -clock_offset_ns_ int64_t
        -nvinfer_config_path_ string
        -pipeline_thread_ thread
        +start(callback) void
        +stop() void
        -onBusMessage(bus, msg, user_data)$ gboolean
    }
    class DeepStreamConfig {
        +camera_devices vector~string~
        +camera_ids vector~string~
        +capture_width uint32_t
        +capture_height uint32_t
        +capture_fps uint32_t
        +model_engine_path string
        +custom_lib_path string
        +parse_bbox_func string
        +num_classes uint32_t
        +conf_threshold float
        +tracker_lib_path string
        +stream_host string
        +stream_port uint16_t
    }
    PerceptionBackend <|-- FakePerceptionBackend : tests
    PerceptionBackend <|-- DeepStreamBackend : Jetson hardware
    FakePerceptionBackend *-- ScriptedFrame : script_
    DeepStreamBackend *-- DeepStreamConfig : config_
    PerceptionService --> PerceptionBackend : backend_
    PerceptionService --> Publisher~DetectionFrame~ : publisher_
```

## Service lifecycle

```{mermaid}
sequenceDiagram
    participant main
    participant Svc as PerceptionService
    participant Backend as PerceptionBackend
    participant Pub as Publisher~DetectionFrame~
    participant Zenoh as Zenoh Bus

    main->>Svc: start()
    Svc->>Backend: start(callback)
    Note over Backend: pipeline starts (or script replay fires synchronously)
    Backend-->>Svc: returns immediately

    main->>main: latch.wait() blocks

    Backend->>Svc: callback(DetectionFrame, captured_at_ns)
    Svc->>Pub: publish(frame, captured_at_ns)
    Pub->>Zenoh: transmit Envelope

    main->>Svc: stop()
    Svc->>Backend: stop()
    Note over Backend: pipeline EOS, GLib main loop quits, thread joined
```

---

## Pipeline topology

The `DeepStreamBackend` builds the following GStreamer pipeline programmatically
(not via `gst_parse_launch` strings). Each element is created with
`gst_element_factory_make` and wired explicitly, giving the service typed handles
for runtime reconfiguration and per-element error reporting.

```{mermaid}
flowchart LR
    subgraph Sources["Source Bins (one per camera)"]
        direction TB
        v4l2["v4l2src\n(UYVY, 960×600@30fps)"]
        caps["capsfilter\n(video/x-raw, UYVY)"]
        conv["nvvideoconvert\n(UYVY → NV12)"]
        nvmm["capsfilter\n(memory:NVMM, NV12)"]
        v4l2 --> caps --> conv --> nvmm
    end

    mux["nvstreammux\n(batch N cameras)"]
    infer["nvinfer\n(RT-DETR-R18 FP16\ncustom parser)"]
    tracker["nvtracker\n(IOU tracker\npersistent track_id)"]
    tee["tee"]

    subgraph Appsink["Detection Path"]
        q1["queue"]
        sink["appsink\n(new-sample signal)"]
        q1 --> sink
    end

    subgraph Display["Display Path"]
        q2["queue"]
        osd["nvdsosd\n(OSD probe: FPS,\nlatency, count)"]
        enc["nvv4l2h264enc\n(H.264 hardware)"]
        rtp["rtph264pay\n(RTP packetization)"]
        udp["udpsink\n(host:port UDP)"]
        q2 --> osd --> enc --> rtp --> udp
    end

    nvmm -->|"sink_N pad"| mux
    mux --> infer --> tracker --> tee
    tee --> q1
    tee --> q2
    sink -->|"DetectionFrame\ncallback"| zenoh["Zenoh Bus\norion/{id}/sensing/detections"]
```

The `appsink` tee is placed **before** `nvdsosd` so the `DetectionFrame` callback — and
therefore `pipeline_latency_ns` — is measured at inference completion, not after the
display path.

---

## Latency measurement

`pipeline_latency_ns` in every `DetectionFrame` spans from pixel capture to the moment
the `appsink` callback fires. It is computed entirely within `DeepStreamBackend` and
requires no clock coordination with downstream consumers.

```{mermaid}
sequenceDiagram
    participant Sensor as Camera Sensor
    participant V4L2 as v4l2src
    participant DS as DeepStream Pipeline
    participant App as appsink callback
    participant Zenoh as Zenoh Bus
    participant GS as Ground Station

    Sensor->>V4L2: frame captured (CLOCK_MONOTONIC t₀)
    note over V4L2: GstBuffer.pts = t₀ (CLOCK_MONOTONIC)
    V4L2->>DS: buffer enters pipeline
    DS->>DS: nvvideoconvert (UYVY→NV12)
    DS->>DS: nvstreammux (batch)
    DS->>DS: nvinfer (RT-DETR-R18, ~15-25ms)
    DS->>DS: nvtracker (IOU, ~1-3ms)
    DS->>App: new-sample fires (CLOCK_REALTIME t₁)
    note over App: captured_at_ns = t₀ + clock_offset\n(clock_offset = REALTIME - MONOTONIC\ncomputed once at start())
    note over App: pipeline_latency_ns = t₁ - captured_at_ns
    App->>Zenoh: publish DetectionFrame\n(captured_at_ns, pipeline_latency_ns)
    Zenoh->>GS: subscriber receives frame
    note over GS: end-to-end latency =\nnow() - captured_at_ns
```

### Clock domain offset

GStreamer timestamps use `CLOCK_MONOTONIC`. Orion's `WallClock` uses `CLOCK_REALTIME`.
`DeepStreamBackend::start()` samples both clocks atomically at startup:

```
clock_offset_ns = CLOCK_REALTIME_now − CLOCK_MONOTONIC_now
captured_at_ns  = frame_meta->buf_pts + clock_offset_ns
```

The offset is stable for the lifetime of the process (both clocks advance at the same
rate; only their epochs differ).

The `nvdsosd` OSD overlay (display path) uses the same formula to compute the
latency figure shown in the on-screen text:

```
osd_latency_ms = (CLOCK_REALTIME_now − (frame_meta->buf_pts + clock_offset_ns)) / 1e6
```

This gives an accurate end-to-pipeline latency at the point the OSD probe fires
(i.e. including the display path after the tee).

---

## Multi-camera support

`DeepStreamConfig` accepts a list of camera devices and IDs. `nvstreammux` batches all
sources into a single inference pass — more GPU-efficient than running N separate
pipelines.

```{mermaid}
flowchart LR
    cam0["v4l2src /dev/video0\ncamera_id = forward"]
    cam1["v4l2src /dev/video1\ncamera_id = downward"]

    mux["nvstreammux\n(batch_size = N)\nsink_0, sink_1, ..."]
    infer["nvinfer\n(single TRT engine\nbatch inference)"]
    tiler["nvmultistreamtiler\n(ceil(√N) × ceil(N/√N)\nauto-layout)"]

    cam0 -->|"sink_0"| mux
    cam1 -->|"sink_1"| mux
    mux --> infer
    infer -->|"...tracker → tee"| tiler
    tiler -->|"tiled frame"| udp["single UDP stream\nboth cameras"]
```

Each `DetectionFrame` carries a `camera_id` field (mapped from
`NvDsFrameMeta.source_id` using the `camera_ids` config list). Downstream consumers
filter by `camera_id` if they only need one source.

---

## RT-DETR custom parser

DeepStream's built-in `nvinfer` parser expects YOLO-style output. RT-DETR uses a
different output layout. `rtdetr_parser.so` implements the
`NvDsInferParseRtDetr` function (or any other symbol set via
`DeepStreamConfig::parse_bbox_func` / `--parse-bbox-func` / `PARSE_BBOX_FUNC`)
loaded by `nvinfer` at runtime via `dlopen`.

`conf_threshold` is passed into the custom parser via a temporary
`nvinfer` config file written to `/tmp/orion_nvinfer_<pid>.cfg` at pipeline
build time. The file is unlinked after `gst_element_set_state(GST_STATE_PLAYING)`
— `nvinfer` reads it during the state transition and does not need it afterwards.

**Expected output tensor** (Ultralytics RT-DETR-R18, `model.export(format='engine', half=True)`):

| Tensor | Shape | Layout |
|---|---|---|
| `output0` | `[batch, 300, nc+4]` | `[cx, cy, w, h, score_0 … score_nc]` — normalized to model input dims |

The parser extracts the highest-scoring class per detection, filters by
`conf_threshold`, and converts normalized `[cx, cy, w, h]` to pixel-space
`[left, top, width, height]` for `NvDsObjectMeta`.

> **Note:** Verify the output tensor layout against the actual exported model.
> Ultralytics may change the export format across versions. Run
> `polygraphy inspect model model.engine --mode=full` to confirm tensor shapes.

---

## Build configuration

| CMake option | Default | Description |
|---|---|---|
| `ORION_ENABLE_DEEPSTREAM` | `OFF` | Compile `DeepStreamBackend` and `rtdetr_parser.so`. Requires DeepStream SDK and GStreamer on the build host. |

```bash
# x86 devcontainer — FakePerceptionBackend only, no hardware deps
cmake --preset debug

# Jetson deploy build
cmake --preset release -DORION_ENABLE_DEEPSTREAM=ON
```

### Runtime configuration (CLI / env)

| Flag | Env var | Default | Description |
|---|---|---|---|
| `--tracker-lib` | `TRACKER_LIB` | `/opt/nvidia/deepstream/deepstream/lib/libnvds_mot_iou.so` | Path to DeepStream MOT tracker `.so` |
| `--parse-bbox-func` | `PARSE_BBOX_FUNC` | `NvDsInferParseRtDetr` | Exported symbol name for the bbox parser in `custom_lib_path` |
| `--conf-threshold` | `CONF_THRESHOLD` | `0.5` | Detection confidence threshold `[0, 1]` |

When `ORION_ENABLE_DEEPSTREAM=OFF` (the default), `orion_perception` compiles on any
host and links only against `orion_proto` and `orion_transport`. `DeepStreamBackend`
and `rtdetr_parser.so` are not built.

---

## Pipeline error handling

`DeepStreamBackend` installs a GLib bus watch (`gst_bus_add_watch`) during
`buildPipeline()`. `onBusMessage` handles two message types:

| Message | Action |
|---|---|
| `GST_MESSAGE_ERROR` | Logs the element name, error string, and debug info via `g_printerr`; quits the GLib main loop. |
| `GST_MESSAGE_EOS` | Quits the GLib main loop (all source streams ended). |

When the main loop quits (for either reason), `pipeline_thread_` returns and
`stop()` joins it cleanly. The `ShutdownLatch` in `main.cpp` must be triggered
externally (e.g. SIGINT) for the service to exit after an EOS — the pipeline
does not self-terminate the process.

---

## Service lifecycle

`PerceptionService` follows the reactive start/wait/stop pattern from ADR-0016:

```cpp
auto backend = DeepStreamBackend{config};
auto svc     = PerceptionService{backend, session.advertise<DetectionFrame>(topic)};

svc.start();   // builds pipeline, starts GLib main loop thread, returns immediately
latch.wait();  // main thread blocks until ShutdownLatch fires
svc.stop();    // quits main loop, joins thread, sets pipeline to NULL
```

The pipeline runs on a dedicated `std::thread` driving a `GMainLoop`. The
`new-sample` appsink signal fires on GStreamer's internal streaming thread, not the
main loop thread. The `DetectionFrame` callback (and therefore `Publisher::publish`)
is called from that streaming thread — it is thread-safe via Zenoh's internal locking.
