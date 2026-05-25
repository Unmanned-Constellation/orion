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

    subgraph External ["📦 System & Conan Dependencies"]
        direction TB
        protobuf(["protobuf/5.29.3"]):::external
        zenohc(["zenoh-c/1.9.0 ①"]):::external
        zenohcpp(["zenoh-cpp/1.9.0 ①"]):::external
        abseil(["abseil/20240722.0"]):::external
        gtest(["gtest/1.17.0"]):::external

        backwardcpp(["backward-cpp (M2)"]):::planned
        libdw(["libdw (M2)"]):::planned
        spdlog(["spdlog (M2)"]):::planned
    end

    subgraph Modules ["⚙️ CMake Targets"]
        direction TB
        orion_clock["orion_clock<br/>(INTERFACE)"]:::interface
        orion_proto["orion_proto<br/>(STATIC)"]:::staticLib
        orion_app["orion_app<br/>(INTERFACE)"]:::interface
        orion_transport["orion_transport<br/>(SHARED)"]:::sharedLib
        orion_sim_clock["orion_sim_clock<br/>(INTERFACE · M3)"]:::planned
    end

    subgraph Executables ["🚀 Tests"]
        orion_tests{{"orion_tests"}}:::executable
    end

    %% External to Module Edges
    protobuf    --> orion_proto
    abseil      --> orion_proto
    zenohc      --> orion_transport
    zenohcpp    --> orion_transport

    backwardcpp -.-> orion_app
    libdw       -.-> orion_app
    spdlog      -.-> orion_app

    %% Internal Module Edges
    orion_clock -->|publicly linked| orion_transport
    orion_clock --> orion_app
    orion_proto --> orion_transport

    orion_clock     -. M3 .-> orion_sim_clock
    orion_transport -. M3 .-> orion_sim_clock

    %% Executable Edges
    orion_clock     --> orion_tests
    orion_app       --> orion_tests
    orion_transport --> orion_tests
    orion_proto     --> orion_tests
    orion_sim_clock -.-> orion_tests
    gtest           --> orion_tests
```

① `zenoh-c` and `zenoh-cpp` are not in ConanCenter. They are maintained as
local recipes under `conan/recipes/` — see
[Dependency Management](dependency-management.md).

---

## Library responsibilities

| Target | Type | Responsibility |
|---|---|---|
| `orion_clock` | INTERFACE | Abstract `Clock` interface + `WallClock`, `ManualClock`, `SimClock`. Zero deps beyond stdlib. |
| `orion_proto` | STATIC | Compiled protobuf message bindings for all `.proto` files under `proto/orion/v1/`. |
| `orion_app` | INTERFACE | `ShutdownLatch`, `FrameScheduler`, `CrashHandler` (M2), `LoggerFactory` (M2). Depends on `orion_clock`. |
| `orion_transport` | SHARED | `Session`, `Publisher<T>`, `Subscriber<T>`. Stamps messages with injected `Clock`. Depends on `orion_clock` + `orion_proto` + Zenoh. |
| `orion_sim_clock` | INTERFACE (M3) | `ExternalClock` — replay-mode clock driven by `Subscriber<SimTimeUpdate>`. Sits above both `orion_clock` and `orion_transport` to avoid a circular dependency. |

---

## Why `orion_clock` has zero dependencies

Services inject a `shared_ptr<Clock>` at construction. `orion_transport` links
`orion_clock` **publicly** so that `Publisher<T>::publish` stamps
`MessageHeader::published_at_ns` using the injected clock. This means simulation
timestamps are correct in published messages with no service code changes — only
the concrete `Clock` passed to `Session::create` changes.

If `orion_clock` took a dependency on `orion_transport`, `ExternalClock` (which
depends on `Subscriber<T>`) would create a cycle:

```
orion_clock → orion_transport → orion_clock   ✗ circular
```

The solution is `orion_sim_clock` — a separate target above both — which is why
`ExternalClock` does not live in `orion_clock` itself.

---

## External dependency summary

| Package | Version | Source | Used by | Notes |
|---|---|---|---|---|
| protobuf | 5.29.3 | ConanCenter | `orion_proto` | Also a build tool (`protoc`) via `tool_requires` |
| zenoh-c | 1.9.0 | Local recipe ① | `orion_transport` | Pre-built shared lib from GitHub Releases |
| zenoh-cpp | 1.9.0 | Local recipe ① | `orion_transport` | Header-only C++ wrapper |
| abseil | 20240722.0 | ConanCenter | `orion_proto` | Pinned to avoid symbol conflict with Triton runtime |
| gtest | 1.17.0 | ConanCenter | `orion_tests` | Test only |
| backward-cpp | TBD | ConanCenter (M2) | `orion_app` | Header-only; crash symbolization |
| libdw | system | APT (M2) | `orion_app` | DWARF symbol resolution for backward-cpp |
| spdlog | TBD | ConanCenter (M2) | `orion_app` | Async structured logging |

---

## Milestone build targets

| Milestone | New targets | New dependencies |
|---|---|---|
| Current | `orion_clock`, `orion_proto`, `orion_app`, `orion_transport` | protobuf, zenoh-c, zenoh-cpp, abseil, gtest |
| M1 — Core Runtime | `SimClock` in `orion_clock`; `FrameScheduler` in `orion_app` | none |
| M2 — Observability | `CrashHandler`, `LoggerFactory`, `HealthPublisher` in `orion_app` | backward-cpp, libdw, spdlog |
| M3 — Simulation | `orion_sim_clock` (new target), `ExternalClock` | none |
