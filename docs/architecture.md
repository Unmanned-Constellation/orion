# Architecture and dependency graph

## CMake target dependency graph

The diagram below shows all CMake targets, their dependency edges, and the
external packages that feed into them. Solid arrows are current dependencies.
Dashed arrows are planned targets not yet implemented.

```{mermaid}
flowchart LR
    %% Semantic Style Definitions
    classDef external   fill:#f3f4f6,stroke:#9ca3af,stroke-width:1px,color:#111827
    classDef interface  fill:#dcfce7,stroke:#22c55e,stroke-width:2px,color:#14532d
    classDef staticLib  fill:#dbeafe,stroke:#3b82f6,stroke-width:2px,color:#1e3a8a
    classDef sharedLib  fill:#fef08a,stroke:#eab308,stroke-width:2px,color:#713f12
    classDef executable fill:#fee2e2,stroke:#ef4444,stroke-width:2px,color:#7f1d1d
    classDef planned    fill:#f8fafc,stroke:#64748b,stroke-width:2px,stroke-dasharray: 5 5,color:#475569

    subgraph External ["System & Conan Dependencies"]
        direction TB
        protobuf(["protobuf/5.29.3"]):::external
        zenohc(["zenoh-c/1.9.0 ①"]):::external
        zenohcpp(["zenoh-cpp/1.9.0 ①"]):::external
        abseil(["abseil/20240722.0"]):::external
        gtest(["gtest/1.17.0"]):::external
        backwardcpp(["backward-cpp/1.6"]):::external

        libdw(["libdw (M2) ②"]):::planned
        spdlog(["spdlog (M2)"]):::planned
    end

    subgraph Modules ["CMake Targets"]
        direction TB
        orion_clock["orion_clock<br/>(INTERFACE)"]:::interface
        orion_proto["orion_proto<br/>(STATIC)"]:::staticLib
        orion_app["orion_app<br/>(STATIC)"]:::staticLib
        orion_transport["orion_transport<br/>(SHARED)"]:::sharedLib
    end

    subgraph Executables ["Executables"]
        orion_tests{{"orion_tests"}}:::executable
        clock_service{{"clock_service (M3)"}}:::planned
    end

    %% External to Module Edges
    protobuf    --> orion_proto
    abseil      --> orion_proto
    zenohc      --> orion_transport
    zenohcpp    --> orion_transport

    backwardcpp --> orion_app
    libdw       -.-> orion_app
    spdlog      -.-> orion_app

    %% Internal Module Edges
    orion_proto --> orion_transport
    orion_clock --> orion_app

    %% Executable Edges
    orion_clock     --> orion_tests
    orion_app       --> orion_tests
    orion_transport --> orion_tests
    orion_proto     --> orion_tests
    gtest           --> orion_tests

    orion_transport -.-> clock_service
    orion_proto     -.-> clock_service
```

① `zenoh-c` and `zenoh-cpp` are not in ConanCenter. They are maintained as
local recipes under `conan/recipes/` - see
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
| `orion_app` | STATIC | `ShutdownLatch`, `FrameScheduler`, `CrashHandler`. `LoggerFactory` (M2), `HealthPublisher` (M2). Depends on `orion_clock` + `backward-cpp`. |
| `orion_transport` | SHARED | `Session`, `Publisher<T>`, `Subscriber<T>`. Time-agnostic - services supply `captured_at_ns` to `publish()`. Depends on `orion_proto` + Zenoh only. |

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
| spdlog | TBD | ConanCenter (M2) | `orion_app` | Async structured logging |

---

## Milestone build targets

| Milestone | Status | New targets / features | New dependencies |
|---|---|---|---|
| Baseline | Done | `orion_clock`, `orion_proto`, `orion_app`, `orion_transport` | protobuf, zenoh-c, zenoh-cpp, abseil, gtest |
| M1 - Core Runtime | Done | `SimClock` + `CoordinatedClock` stub in `orion_clock`; `FrameScheduler` in `orion_app` | none |
| M2 - Observability | In progress | `CrashHandler` ✓; `LoggerFactory`, `HealthPublisher` in `orion_app` | backward-cpp ✓, libdw (pending), spdlog (pending) |
| M3 - Simulation | Planned | `CoordinatedClock` Phase 3 full implementation; Clock Service publisher | none |
