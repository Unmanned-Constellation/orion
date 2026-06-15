# Architecture and dependency graph

## Internal architecture

Internal CMake targets organized by layer. `orion_tests` links every library and is omitted here to avoid a fan-in spider — see the [Library responsibilities](#library-responsibilities) table below.

```{mermaid}
flowchart TD
    classDef iface fill:#dcfce7,stroke:#16a34a,stroke-width:2px,color:#14532d
    classDef stat  fill:#dbeafe,stroke:#3b82f6,stroke-width:2px,color:#1e3a8a
    classDef share fill:#fef9c3,stroke:#ca8a04,stroke-width:2px,color:#713f12
    classDef exe   fill:#fee2e2,stroke:#dc2626,stroke-width:2px,color:#7f1d1d
    classDef plan  fill:#f8fafc,stroke:#94a3b8,stroke-width:1px,stroke-dasharray:5 5,color:#64748b

    subgraph Core [Core Libraries]
        orion_clock["orion_clock<br/>INTERFACE"]:::iface
        orion_proto["orion_proto<br/>STATIC"]:::stat
        orion_topic["orion_topic<br/>STATIC"]:::stat
    end

    subgraph Middleware [Application & Transport Middleware]
        orion_app["orion_app<br/>STATIC"]:::stat
        orion_transport["orion_transport<br/>SHARED"]:::share
    end

    subgraph Services [Service Libraries]
        orion_main["orion_main<br/>STATIC"]:::stat
        orion_clock_service["orion_clock_service<br/>STATIC"]:::stat
        orion_perception["orion_perception<br/>STATIC"]:::stat
        rtdetr_parser["rtdetr_parser<br/>SHARED"]:::plan
    end

    subgraph Executables [Executables]
        clock_service{{"clock_service"}}:::exe
        orion_transport_benchmarks{{"orion_transport_benchmarks"}}:::exe
    end

    orion_clock --> orion_app
    orion_proto --> orion_transport
    orion_app   --> orion_main
    orion_app & orion_proto & orion_transport & orion_topic --> orion_clock_service
    orion_proto & orion_transport                           --> orion_perception
    orion_perception -.->|dlopen| rtdetr_parser

    orion_clock_service & orion_main --> clock_service
    orion_transport & orion_proto    --> orion_transport_benchmarks
```

## External dependencies

Which Conan and system packages feed into each internal target.

```{mermaid}
flowchart LR
    classDef ext  fill:#f3f4f6,stroke:#9ca3af,stroke-width:1px,color:#374151
    classDef plan fill:#f8fafc,stroke:#94a3b8,stroke-width:1px,stroke-dasharray:5 5,color:#64748b
    classDef tgt  fill:#ffffff,stroke:#4b5563,stroke-width:2px,color:#1f2937

    protobuf(["protobuf"]):::ext
    abseil(["abseil"]):::ext
    zenoh(["zenoh-c / zenoh-cpp ①"]):::ext
    backwardcpp(["backward-cpp"]):::ext
    libdw(["libdw ②"]):::plan
    spdlog(["spdlog"]):::ext
    cli11(["cli11"]):::ext
    gtest(["gtest"]):::ext
    gbench(["benchmark"]):::ext

    orion_proto["orion_proto"]:::tgt
    orion_transport["orion_transport"]:::tgt
    orion_app["orion_app"]:::tgt
    orion_main["orion_main"]:::tgt
    orion_tests["orion_tests"]:::tgt
    orion_transport_benchmarks["orion_transport_benchmarks"]:::tgt

    protobuf & abseil    --> orion_proto
    zenoh                --> orion_transport
    backwardcpp & spdlog --> orion_app
    libdw               -.-> orion_app
    cli11                --> orion_main
    gtest                --> orion_tests
    gbench               --> orion_transport_benchmarks
```

① `zenoh-c` and `zenoh-cpp` are not in ConanCenter. They are maintained as
local recipes under `conan/recipes/` — see
[Dependency Management](dependency-management.md).

② `libdw` (from `libdw-dev`) is a system library used by `backward-cpp` for
full DWARF symbolization (file names, line numbers, inlined frames). The `dw`
backend is active. `libdw-dev` is installed in the devcontainer and must be
present on the Jetson deployment sysroot.

---

## Library responsibilities

| Target | Type | Responsibility |
|---|---|---|
| `orion_clock` | INTERFACE | Abstract `TimeSource` interface + `WallClock`, `ManualClock`, `SimClock`, `CoordinatedClock`. Zero deps beyond stdlib. |
| `orion_proto` | STATIC | Compiled protobuf message bindings for all `.proto` files under `proto/orion/v1/`. |
| `orion_topic` | STATIC | Canonical Zenoh topic name builders (`sensing::detections`, `clock::simTime`, etc.). Validates `vehicle_id` at call time. Zero deps beyond stdlib. |
| `orion_app` | STATIC | `ShutdownLatch`, `FrameScheduler`, `CrashHandler`. Depends on `orion_clock` + `backward-cpp` + `spdlog`. |
| `orion_transport` | SHARED | `Session`, `Publisher<T>`, `Subscriber<T>`. Time-agnostic - services supply `captured_at_ns` to `publish()`. Depends on `orion_proto` + Zenoh only. |
| `orion_clock_service` | STATIC | `ClockService` — publishes `SimTimeUpdate` at `FrameScheduler` tick rate, backed by `SimClock`. Depends on `orion_app` + `orion_transport` + `orion_proto`. |
| `orion_main` | STATIC | `ServiceBootstrapper`, `ServiceContext`, `ServiceConfig`, `addServiceConfig` — CLI11-backed arg/env parsing and startup wiring for service `main.cpp` files. `ServiceBootstrapper` is the preferred entry point; it owns `ShutdownLatch`, `CrashHandler`, and logger init. Linked by executables only; never by libraries. |
| `orion_perception` | STATIC | `PerceptionService`, `PerceptionBackend` interface, `FakePerceptionBackend`. With `ORION_ENABLE_DEEPSTREAM=ON`: `DeepStreamBackend` (GStreamer/DeepStream pipeline). Depends on `orion_proto` + `orion_transport`. |
| `rtdetr_parser` | SHARED | Custom `nvinfer` bounding-box parser for Ultralytics RT-DETR-R18 FP16. Loaded by DeepStream at runtime via `dlopen`. Only built when `ORION_ENABLE_DEEPSTREAM=ON`. |

---

## Why `orion_clock` and `orion_transport` have no dependency on each other

Services hold their own `TimeSource` reference and call `clock->nowNs()` at the point
of data capture, passing the result to `Publisher<T>::publish(msg, captured_at_ns)`.
The transport layer is fully time-agnostic - it forwards the timestamp into the
`Envelope` header without knowing or caring what clock produced it.

This means the two libraries are structurally independent:

```
orion_clock      (TimeSource, WallClock, ManualClock, SimClock, CoordinatedClock)
orion_transport  (Session, Publisher, Subscriber)
services         link both independently
```

Keeping them independent makes circular dependencies impossible. `CoordinatedClock`
lives in `orion_clock` and exposes an `update(sim_time_ns)` method - the service
wires the Zenoh subscription and calls `update()` in the callback, so
`CoordinatedClock` itself has no knowledge of Zenoh or transport.

---

## External dependency summary

| Package | Version | Source | Used by | Notes |
|---|---|---|---|---|
| protobuf | 5.29.3 | ConanCenter | `orion_proto` | Also a build tool (`protoc`) via `tool_requires` |
| zenoh-c | 1.9.0 | Local recipe ① | `orion_transport` | Pre-built shared lib from GitHub Releases |
| zenoh-cpp | 1.9.0 | Local recipe ① | `orion_transport` | Header-only C++ wrapper |
| abseil | 20240722.0 | ConanCenter | `orion_proto` | Pinned to avoid symbol conflict with Triton runtime |
| gtest | 1.17.0 | ConanCenter | `orion_tests` | Test only |
| backward-cpp | 1.6 | ConanCenter | `orion_app` | Crash symbolization; `dw` backend — full DWARF (see ②) |
| libdw | system | APT ② | `orion_app` | DWARF symbol resolution for backward-cpp |
| spdlog | 1.17.0 | ConanCenter | `orion_app`, `orion_main` | Async structured logging; logger created by `ServiceBootstrapper` |
| cli11 | 2.6.2 | ConanCenter | `orion_main` | Header-only CLI arg + env var parsing; MIT licence |

---

## Milestone build targets

| Milestone | Status | New targets / features | New dependencies |
|---|---|---|---|
| Baseline | Done | `orion_clock`, `orion_proto`, `orion_app`, `orion_transport` | protobuf, zenoh-c, zenoh-cpp, abseil, gtest |
| M1 - Core Runtime | Done | `SimClock` + `CoordinatedClock` stub in `orion_clock`; `FrameScheduler` in `orion_app` | none |
| M2 - Observability | Done | `CrashHandler` in `orion_app`; logger creation in `ServiceBootstrapper` (`orion_main`) | backward-cpp, libdw, spdlog |
| M3 - Simulation | Done | `CoordinatedClock` Phase 3; `orion_clock_service` + `clock_service` executable | none |
| M4 - Perception | In Progress | `orion_perception`, `DeepStreamBackend`, `rtdetr_parser`; `orion_topic` canonical topic helpers; RT-DETR-R18 FP16 inference on Arducam Darksee via v4l2src | GStreamer (`gstreamer-1.0`, `gstreamer-app-1.0`), DeepStream SDK (Jetson only, `ORION_ENABLE_DEEPSTREAM=ON`) |
