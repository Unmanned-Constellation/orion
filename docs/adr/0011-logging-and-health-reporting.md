# ADR 0011: Structured logging with spdlog and centralized health reporting

## Status
Proposed

## Context
No logging strategy exists today. Services have no structured facility for diagnostic output,
and library code (e.g. `FrameScheduler`) intentionally does not log - overrun visibility is
exposed via `overrunCount()` so callers can route warnings through their own facility. This
gap is problematic for three reasons:

1. **Performance.** Ad-hoc `std::cerr` calls are synchronized (mutex-protected, flushed on
   write). Calling them from a 100 Hz callback introduces unbounded latency into the service
   loop.
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
| Async, lock-free hot path | Yes - `spdlog::async_logger` | No |
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
{service_name}                  - root service logger
{service_name}/{component_name} - per-component child logger
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

// Per-component logger - shares the same sinks as root
auto pipeline_log = std::make_shared<spdlog::async_logger>(
    "perception/pipeline",
    spdlog::sinks_init_list{stderr_sink, file_sink},
    spdlog::thread_pool(),
    spdlog::async_overflow_policy::overrun_oldest);
spdlog::register_logger(pipeline_log);
```

A component receives its logger by name at construction - it does not create or own the
spdlog thread pool or sinks. A future `orion_app` helper (`LoggerFactory`) will encapsulate
the pattern of deriving a component logger from a parent name, so components are not
burdened with sink wiring:

```cpp
// LoggerFactory (proposed - not yet implemented)
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

Errors that affect mission capability - sensor loss, timing violations, failed
initialization - are not just logged; they are published as typed health events on a
dedicated topic:

```
orion/{vehicle_id}/system/health/{service_name}
```

A `HealthPublisher` type (part of `orion_app`) wraps `Session::advertise<Health>()` and
manages fault state on behalf of the service. A Health Monitor microservice subscribes to
`orion/{vehicle_id}/system/health/**` and aggregates system-wide status for operators and
the decision service.

The spdlog CRITICAL level maps to "service is degraded or non-functional." Logging at
CRITICAL always accompanies a `health.raise(...)` call. The two are paired by convention,
not enforced mechanically in this iteration.

---

### Service health state machine

```
INITIALIZING → OPERATIONAL ↔ DEGRADED ↔ FAULTED
                                         ↓
                                    latch.stop()   (unrecoverable init failure)
```

State is **derived** from the active fault set — services never set state directly:

| Active faults | Derived state |
|---|---|
| None | `OPERATIONAL` |
| Any `DEGRADED` severity, no `FAULT` severity | `DEGRADED` |
| Any `FAULT` severity | `FAULTED` |

`INITIALIZING` is the state from `HealthPublisher` construction until the first
`health.operational()` call (successful init) or first `health.raise()` call (init failure).

---

### Fault code system

Each service defines its own scoped fault code enum. The proto carries the code as a
`uint32` so the Health Monitor can forward events without needing to know their meaning.
The description string carries human-readable context for operators.

```cpp
// Per-service fault code enum - defined in the service, not in orion_app
enum class FaultCode : uint32_t {
    IMU_TIMEOUT      = 1,
    GPS_DATA_STALE   = 2,
    OVERRUN_THRESHOLD = 3,
    INIT_FAILED      = 4,
};
```

Severity is separate from the fault code and controls state derivation:

```cpp
enum class Severity {
    DEGRADED,   // service is impaired but still performing its function
    FAULT,      // service is not performing its function
};
```

A fault registry (a static `std::map<uint32_t, std::string>`) maps codes to names for
log formatting. Services register their map with `HealthPublisher` at construction. The
Health Monitor does not interpret codes — it republishes them verbatim.

---

### Control flow: raising and clearing faults

**Raise (from a FrameScheduler tick or subscriber callback):**

```
service detects IMU timeout
  → health.raise(FaultCode::IMU_TIMEOUT, Severity::FAULT, "no response for 50 ms")
      → add to active_faults_
      → re-derive state (e.g. OPERATIONAL → FAULTED)
      → publish FaultEvent{RAISED, code, severity, description, timestamp} immediately
  → logger->critical("IMU timeout - no response for 50 ms")
```

**Clear (service detects recovery):**

```
service tick: IMU responds again
  → health.clear(FaultCode::IMU_TIMEOUT)
      → remove from active_faults_
      → re-derive state (e.g. FAULTED → OPERATIONAL, if no other faults remain)
      → publish FaultEvent{CLEARED, code, timestamp} immediately
```

Fault events are published immediately — they are discrete occurrences and timeliness
matters for the audit trail. The heartbeat (see below) is the periodic snapshot.

Services must not call `raise()` and `clear()` for the same fault code in the same tick.
If that seems necessary, the fault code is too coarse — split it.

---

### Heartbeat

`HealthPublisher` publishes a `ServiceHealth` snapshot at a low fixed rate (default 1 Hz)
on the service's health topic. This allows a subscriber that joins mid-flight to
reconstruct current state without replaying the full fault event history.

```proto
message ServiceHealth {
  enum State {
    INITIALIZING = 0;
    OPERATIONAL  = 1;
    DEGRADED     = 2;
    FAULTED      = 3;
  }

  State              state          = 1;
  uint64             reported_at_ns = 2;
  repeated ActiveFault active_faults = 3;
}

message ActiveFault {
  uint32 fault_code    = 1;
  uint32 severity      = 2;
  string description   = 3;
  uint64 raised_at_ns  = 4;
}

message FaultEvent {
  enum Kind { RAISED = 0; CLEARED = 1; }
  Kind   kind          = 1;
  uint32 fault_code    = 2;
  uint32 severity      = 3;
  string description   = 4;
  uint64 event_at_ns   = 5;
}
```

`ServiceHealth` is published on `orion/{vehicle_id}/system/health/{service_name}`.
`FaultEvent` is published on `orion/{vehicle_id}/system/health/{service_name}/events`.

---

### Initialization flow

```
main()
  → HealthPublisher constructed → state = INITIALIZING, heartbeat starts
  → init sensors, open devices...

  success path:
    → health.operational()
    → state = OPERATIONAL (no active faults)

  failure path:
    → health.raise(FaultCode::INIT_FAILED, Severity::FAULT, reason)
    → logger->critical(reason)
    → latch.stop()     ← clean exit; supervisor policy handles restart
```

---

### Threading model

`raise()` and `clear()` may be called from both the `FrameScheduler` tick thread and Zenoh
subscriber callback threads concurrently. `HealthPublisher` protects `active_faults_` with
an internal mutex. The mutex scope covers fault set mutation and state derivation only —
the Zenoh publish call is outside the lock (Zenoh is independently thread-safe).

---

### What services must not do

- Call `std::abort`, `std::terminate`, or `std::exit` on recoverable errors.
- Throw exceptions that propagate out of a `FrameScheduler` callback (the scheduler has no
  catch - an uncaught exception terminates the process).
- Use `std::cerr` directly - all diagnostic output goes through the spdlog logger.
- Set service state directly - derive it from the fault set via `raise()`/`clear()` only.

## Consequences

- `spdlog` is added to `conanfile.py` and `conan.lock` when implementation begins.
- `FrameScheduler` does not log internally. Services that need overrun visibility poll
  `overrunCount()` and route warnings through their own spdlog logger (e.g. `logger->warn("overrun")`).
  Library code remains log-free per ADR-0008.
- `orion_app` gains a `LoggerFactory` type that encapsulates root logger construction
  (thread pool, sinks) and vends named child loggers. Components accept
  `std::shared_ptr<spdlog::logger>` - they do not call `spdlog::get` or construct sinks.
- Services gain a new startup dependency: constructing `LoggerFactory` and `HealthPublisher`
  before any component that takes a logger, and before `FrameScheduler`.
- Two new proto messages are added: `ServiceHealth` and `FaultEvent`. Two topic suffixes are
  reserved per service: `/health/{service_name}` (heartbeat) and
  `/health/{service_name}/events` (fault raise/clear stream).
- Each service defines its own `FaultCode` enum (scoped, `uint32_t` underlying type).
  `orion_app` provides the `Severity` enum and `HealthPublisher` — it does not own fault codes.
- The Health Monitor is a separate reactive microservice subscribing to
  `orion/{vehicle_id}/system/health/**`. It aggregates system-wide state and is out of scope
  for this ADR.
- Off-board consumers can subscribe to health topics over the existing Zenoh connection -
  no additional bridge or sidecar required.
- Services that fail initialization (e.g. cannot open a sensor) should log CRITICAL,
  publish a FAULTED health event, and then exit cleanly via `latch.stop()`. A supervisor
  restart policy handles re-launch. This avoids both silent failure and panic-style crashes.
