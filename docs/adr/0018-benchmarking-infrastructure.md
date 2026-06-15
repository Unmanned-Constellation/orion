# ADR 0018: Benchmarking Infrastructure for Zenoh Transport

## Status
Accepted

## Context

ADR-0001 selected Zenoh as the pub-sub transport on the basis of its shared-memory
path and low per-message overhead. At 100 Hz the frame period is 10ms. If a single
publish→callback roundtrip consumes a material fraction of that budget, downstream
services have less time for application logic and the timing guarantees of the
FrameScheduler are undermined.

No quantitative baseline for Zenoh latency or throughput exists in the codebase.
Before deploying services to the Jetson Orin Nano, a benchmark suite is needed to:

1. Confirm that same-process Zenoh latency fits within the frame budget.
2. Characterise how latency scales with payload size across the range of messages
   the system will produce (small control messages up to larger sensor payloads).
3. Establish a baseline that future transport changes can be compared against.

## Decision

### Framework: Google Benchmark

Use **Google Benchmark** (`benchmark/1.x` via Conan) as the benchmarking framework.
It handles warmup iterations, wall-time measurement, statistical aggregation, and
structured output (JSON/CSV) out of the box. It is the de-facto standard for C++
microbenchmarks and integrates naturally alongside GoogleTest without conflicts.

Alternative considered: hand-rolled timing loops with `std::chrono`. Rejected
because they require manual warmup, outlier handling, and reporting — all solved
problems in Google Benchmark.

### Location: `benchmarks/` top-level directory

Benchmarks live in `benchmarks/`, parallel to `tests/`, producing a separate
`orion_transport_benchmarks` binary. Tests verify correctness; benchmarks measure performance.
Mixing them in a single binary conflates two distinct intents and makes both harder
to invoke selectively.

### CI policy: build only, do not run

CI builds `orion_transport_benchmarks` to catch compilation errors but does not execute it.
Benchmark results on shared CI runners are meaningless — virtualization, thermal
throttling, and competing workloads produce numbers that cannot be compared across
runs. Benchmark runs are a deliberate act on target hardware (Jetson Orin Nano).

### Scope: same-process latency first, cross-process as follow-up

**Phase 1 (this ADR):** same-process benchmarks where publisher and subscriber
share a process. This exercises Zenoh's shared-memory path and establishes the
latency floor — the best the transport can possibly do.

**Phase 2 (follow-up ADR):** cross-process benchmarks where publisher and
subscriber are separate processes routed through the Zenoh Router. This reflects
the realistic production topology. A dedicated driver binary coordinates with the
main benchmark binary via a sync topic. Deferred until the same-process baseline
is established and the Router integration is stable.

### Payload sizes: four points across the expected range

| Size  | Rationale                                               |
|-------|---------------------------------------------------------|
| 16 B  | `SimTimeUpdate` — smallest real message in the system   |
| 256 B | Small control message (future `GimbalCommand` estimate) |
| 1 KB  | Sensor metadata (`DetectionMetadata` estimate)          |
| 64 KB | Upper bound — large nav state or diagnostic payload     |

Benchmarks are parameterised over all four sizes so the latency curve is visible
in a single run.

### Latency target

Same-process p99 publish→callback latency must be **under 1ms** at all four
payload sizes on the Jetson Orin Nano. This leaves ≥9ms per 100 Hz frame for
application logic. Results outside this bound are not a build failure but a
signal to investigate the transport configuration or reconsider the transport
choice.

### Metrics captured

- **Latency:** end-to-end time from `Publisher::publish()` call to subscriber
  callback entry, measured via `std::atomic<uint64_t>` timestamps on both sides.
  Reported as p50, p90, p99 across N iterations.
- **Throughput:** maximum sustained messages/sec before the subscriber callback
  queue falls behind, measured by publishing in a tight loop and counting
  delivered messages over a fixed wall-time window.

## Consequences

- `benchmark` is added to `conanfile.py` as a test dependency; `conan.lock` is
  updated and the devcontainer image is rebuilt to bake the package in.
- A new `benchmarks/` CMake subdirectory is added to the root `CMakeLists.txt`,
  gated behind an `ORION_BENCHMARKS` option (off by default, like `ORION_FUZZING`).
- CI adds a build step for `ORION_BENCHMARKS=ON` but does not execute the binary.
- Cross-process benchmarking is explicitly deferred; a follow-up ADR will address
  it once same-process baselines are established.
- The p99 < 1ms target applies to the Jetson Orin Nano. Developer machines
  (x86_64 devcontainers) may produce different numbers; results on non-target
  hardware are indicative only.
