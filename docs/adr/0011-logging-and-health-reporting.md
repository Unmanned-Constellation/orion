# ADR 0011: Structured logging with spdlog and centralized health reporting

## Status
Proposed

## Context
No logging strategy exists today. The only diagnostic output in the codebase is a bare
`std::cerr` call inside `PeriodicTimer` for overrun events. This is problematic for three
reasons:

1. **Performance.** `std::cerr` is synchronized (mutex-protected, flushed on write). Calling
   it from a 100 Hz callback introduces unbounded latency into the service loop.
2. **Observability.** Unstructured text on stderr gives operators no way to filter, aggregate,
   or act on log records from multiple services running concurrently on the Jetson.
3. **Crash bias.** Standard C++ idioms (`std::terminate`, `std::abort`, uncaught exceptions)
   treat errors as process-fatal. An autonomy platform that silently exits on an error is
   more dangerous than one that degrades gracefully and reports the fault to an operator.

The third point drives a non-negotiable design constraint: **services must not crash on
recoverable errors**. Instead they report degraded state to a centralized health monitoring
system that operators and supervisors can observe. Fatal crashes (hardware fault, memory
corruption, programming error) are handled by `CrashHandler` (ADR-0010), not by this ADR.

## Decision

Adopt **spdlog** as the logging library. Add a `HealthPublisher` abstraction that services
use to report status, and route CRITICAL-level log records to the Zenoh Bus as typed
health events.

### Why spdlog

| Criterion | spdlog | absl::log (already a dep) |
|---|---|---|
| Async, lock-free hot path | Yes — `spdlog::async_logger` | No |
| Per-logger named instances | Yes | No (process-global) |
| Custom sinks | Yes | Limited |
| ConanCenter | Yes | N/A (transitive only) |
| C++20 | Yes | Yes |
| Arm64 | Yes | Yes |

`absl::log` is a process-global, synchronous logger tightly coupled to Abseil's flag and
`FATAL`-dies-immediately model. It does not support custom output sinks or async dispatch,
making it unsuitable for a real-time microservice loop or for routing records to the Zenoh
Bus. spdlog's async logger uses an internal lock-free queue and a dedicated background thread,
keeping the calling thread's latency bounded.

### Logger hierarchy

A service is composed of multiple components, each with its own named logger. Loggers are
arranged in a two-level naming hierarchy:

```
{service_name}                  — root service logger
{service_name}/{component_name} — per-component child logger
```

Examples: `"perception"`, `"perception/pipeline"`, `"perception/publisher"`.

The **root logger** is constructed at service startup. Component loggers are constructed by
passing the root logger's sink set to a new named logger, so all records flow through the
same sinks by default:

```cpp
// Service startup
auto stderr_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
auto file_sink   = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
    "logs/perception.log", 5 * 1024 * 1024, 3);

auto root = std::make_shared<spdlog::async_logger>(
    "perception",
    spdlog::sinks_init_list{stderr_sink, file_sink},
    spdlog::thread_pool(),
    spdlog::async_overflow_policy::overrun_oldest);
spdlog::register_logger(root);

// Per-component logger — shares the same sinks as root
auto pipeline_log = std::make_shared<spdlog::async_logger>(
    "perception/pipeline",
    spdlog::sinks_init_list{stderr_sink, file_sink},
    spdlog::thread_pool(),
    spdlog::async_overflow_policy::overrun_oldest);
spdlog::register_logger(pipeline_log);
```

A component receives its logger by name at construction — it does not create or own the
spdlog thread pool or sinks. A future `orion_app` helper (`LoggerFactory`) will encapsulate
the pattern of deriving a component logger from a parent name, so components are not
burdened with sink wiring:

```cpp
// LoggerFactory (proposed — not yet implemented)
class LoggerFactory {
public:
    explicit LoggerFactory(std::string service_name, /* sink config */);
    auto child(std::string_view component_name) -> std::shared_ptr<spdlog::logger>;
};
```

Components that write to a **separate file** (e.g. a high-frequency data trace) add an
additional sink to their own logger without affecting the root. This is the only case where
a component logger diverges from the root's sink set.

Log level in production: `WARN` and above. Debug builds default to `DEBUG`. Levels can be
set independently per logger, so a noisy component can be silenced without changing the
root level.

### Health reporting via the Zenoh Bus

Errors that affect mission capability — sensor loss, timing violations, failed
initialization — are not just logged; they are published as typed health events on a
dedicated topic:

```
orion/{vehicle_id}/system/health/{service_name}
```

A future `HealthPublisher` type (part of `orion_app`) wraps `Session::advertise<Health>()` and
is called by services when their operational state changes. A Health Monitor microservice
subscribes to `orion/{vehicle_id}/system/health/**` and aggregates system-wide status for
operators and the decision service.

The spdlog CRITICAL level maps to "service is degraded or non-functional." Logging at
CRITICAL always accompanies a `HealthPublisher::report(State::DEGRADED, ...)` call.
The two are paired by convention, not enforced mechanically in this iteration.

### What services must not do

- Call `std::abort`, `std::terminate`, or `std::exit` on recoverable errors.
- Throw exceptions that propagate out of a `PeriodicTimer` callback (the timer has no
  catch — an uncaught exception terminates the process).
- Use `std::cerr` directly — all diagnostic output goes through the spdlog logger.

## Consequences

- `spdlog` is added to `conanfile.py` and `conan.lock` when implementation begins.
- `PeriodicTimer`'s `std::cerr` overrun report is replaced with `spdlog::warn`.
- `orion_app` gains a `LoggerFactory` type that encapsulates root logger construction
  (thread pool, sinks) and vends named child loggers. Components accept
  `std::shared_ptr<spdlog::logger>` — they do not call `spdlog::get` or construct sinks.
- Services gain a new startup dependency: constructing `LoggerFactory` before any component
  that takes a logger, and before `PeriodicTimer`.
- The `orion/system/health/**` topic namespace is reserved. The `Health` proto message and
  `HealthPublisher` type are deferred to the implementation PR.
- Off-board consumers can subscribe to health topics over the existing Zenoh connection —
  no additional bridge or sidecar required.
- Services that fail initialization (e.g. cannot open a sensor) should log CRITICAL,
  publish a FAILED health event, and then exit cleanly via `latch.stop()`. A supervisor
  restart policy handles re-launch. This avoids both silent failure and panic-style crashes.
