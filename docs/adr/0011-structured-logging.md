# ADR 0011: Structured logging with spdlog

## Status
Accepted

## Context

Services running on the Jetson emit diagnostic output today by writing directly
to `stderr`. This works for development but has several problems in production:

- No log levels — every `fprintf` or `std::cerr` write is equally loud.
- No per-component filtering — silencing one noisy service silences all output.
- No structured fields — grep-based analysis is the only option; timestamp
  correlation across services is manual.
- Synchronous I/O on the hot path — a stalled stderr write blocks the calling
  thread, which is unacceptable in a 50 Hz scheduler loop.

Services run under systemd, which captures stderr to the journal automatically.
The logging layer must write to `stderr` so that existing supervisor
infrastructure captures it without modification. A persistent file log is also
required for offline debugging on the Jetson without a live systemd session.

## Decision

Use **spdlog** (MIT licence, ConanCenter) as the logging backend. A
`LoggerFactory` class in `namespace orion::app` is the single point through
which all code obtains named loggers. Direct spdlog API usage outside
`LoggerFactory` and `orion_app` is not permitted.

### Logger naming — dot-hierarchical

Logger names encode component hierarchy using dot-separated segments:

```
autonomy
autonomy.stabilizer
autonomy.stabilizer.pid
gimbal
gimbal.kinematics
```

The first segment is always the service name. Every log line carries the logger
name as a structured field, making per-component filtering a grep:

```bash
grep "autonomy.stabilizer" /var/log/orion/autonomy/autonomy.log
```

Cross-component causality is preserved because all components of a service
write to the same file in timestamp order — adjacent lines from `stabilizer`
and `pid` are immediately correlated without manual timestamp alignment across
separate files.

### One file per service

`init()` takes the service name, creates
`/var/log/orion/<service_name>/` if it does not exist, and opens
`/var/log/orion/<service_name>/<service_name>.log` as a rotating file sink.
All loggers within the process share this file.

```
/var/log/orion/
├── autonomy/
│   └── autonomy.log
└── gimbal/
    └── gimbal.log
```

### LoggerFactory

`LoggerFactory` is a non-constructible class with only static methods. It owns
the async thread pool, the stderr sink, and the file sink.

```cpp
class LoggerFactory {
public:
    static void init(std::string_view service_name,
                     spdlog::level::level_enum level = spdlog::level::info);
    static auto get(std::string_view name) -> std::shared_ptr<spdlog::logger>;
    static void setLevel(spdlog::level::level_enum level);
    static void flush();

    LoggerFactory()                                = delete;
    LoggerFactory(const LoggerFactory&)            = delete;
    LoggerFactory& operator=(const LoggerFactory&) = delete;
};
```

`init()` must be called once at process startup, after `ShutdownLatch` and
`CrashHandler` are constructed. Calling `get()` before `init()` asserts false.

`get()` returns an existing logger by name or creates one that shares the
process-wide sinks if it does not exist. The same `shared_ptr` is returned
for repeated calls with the same name.

`flush()` drains the async queue before process exit. `ShutdownLatch` calls
it in its destructor; services that bypass `ShutdownLatch` must call it
manually.

### Usage pattern

```cpp
int main()
{
    orion::app::ShutdownLatch latch;
    orion::app::CrashHandler  crash;
    orion::app::LoggerFactory::init("autonomy");

    auto stabilizer = Stabilizer("autonomy.stabilizer");
    // ...
}
```

Each class receives its fully-qualified logger name as a constructor argument.
It calls `LoggerFactory::get(name)` to obtain its own logger and appends
suffixes when constructing children:

```cpp
class Stabilizer {
public:
    explicit Stabilizer(std::string_view name)
        : log_(orion::app::LoggerFactory::get(name))
        , pid_(std::string(name) + ".pid")
    {}

private:
    std::shared_ptr<spdlog::logger> log_;
    PidController                   pid_;
};

class PidController {
public:
    explicit PidController(std::string_view name)
        : log_(orion::app::LoggerFactory::get(name))
    {}

private:
    std::shared_ptr<spdlog::logger> log_;
};
```

The service root owns the naming. `PidController` knows nothing about where it
sits in the hierarchy — it receives its fully-qualified name from its parent.
This keeps components reusable and independently testable: pass `"test.pid"`
in a unit test, `"autonomy.stabilizer.pid"` in production.

### Async queue

spdlog's asynchronous mode (`spdlog::init_thread_pool`) is used so that log
calls on the hot path enqueue a message and return immediately. A single
background thread drains the queue and writes to both sinks. Queue capacity is
8192 messages; on overflow the oldest message is discarded
(`async_overflow_policy::overrun_oldest`) to prevent blocking.

### Sinks

All loggers share two sinks, both attached at `init()`:

| Sink | Purpose |
|---|---|
| `stderr_color_sink_mt` | Captured by systemd to the journal; color aids dev terminal readability |
| `rotating_file_sink_mt` | Persistent file at `/var/log/orion/<service>/<service>.log`; max 10 MB, 3 rotated files |

Format string (both sinks):
```
[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v
```

Example output:
```
[2024-01-15 10:23:45.123] [autonomy.stabilizer.pid] [warn] derivative term saturated
```

### Log levels

| Level | Use |
|---|---|
| `trace` | Per-frame internal state; disabled in release builds |
| `debug` | Lifecycle events, configuration dumps |
| `info` | Service startup / shutdown, significant state transitions |
| `warn` | Recoverable anomalies (missed deadline, dropped message) |
| `error` | Non-fatal failures that degrade functionality |
| `critical` | Fatal conditions — log then let `CrashHandler` fire |

The default level is `info`. `ORION_LOG_LEVEL` environment variable overrides
it at startup if set (parsed in `init()`).

### CMake wiring

spdlog is added to `conanfile.py` and linked privately into `orion_app`:

```cmake
find_package(spdlog REQUIRED CONFIG)
target_link_libraries(orion_app PRIVATE spdlog::spdlog)
```

spdlog is a private implementation detail of `LoggerFactory`. Callers hold
`std::shared_ptr<spdlog::logger>` — the spdlog header is included in
`LoggerFactory`'s public header, but no other orion public headers include it
directly.

### Testing strategy

Tests inject a synchronous `ostream_sink` so output is deterministic without
async thread-pool timing.

1. **Init / get** — after `init()`, `get("test.foo")` returns a non-null
   logger; a second `get("test.foo")` returns the same pointer.
2. **Hierarchy** — `get("test.foo.bar")` and `get("test.foo")` return distinct
   loggers; both lines appear in sink output with their respective names.
3. **Level propagation** — `setLevel(warn)` suppresses subsequent `info` calls;
   `warn` calls produce output. Verified via captured sink.
4. **Pre-init death test** — `get()` before `init()` asserts false
   (`EXPECT_DEATH`).
5. **Flush** — messages enqueued before `flush()` appear in sink after
   `flush()` returns.
6. **Name injection** — a `Stabilizer("test.stabilizer")` constructed in a
   test logs under `"test.stabilizer"`, not a hardcoded production name.

## Consequences

- All log output is async — callers on the hot path are never blocked by I/O.
- Dot-hierarchical names make per-component filtering a single grep without
  separate file handles or flush coordination.
- Cross-component causality is preserved — all components of a service write
  to one file in timestamp order.
- Logger names are injected at construction — components are deployment-context
  agnostic and independently testable.
- `/var/log/orion/` must be writable on the Jetson. The devcontainer mounts a
  local equivalent for development.
- The rotating file sink caps log storage at 30 MB per service (10 MB × 3
  files). Missions longer than this will lose the oldest entries; adjust
  rotation limits if longer retention is needed.
- `LoggerFactory::init()` must be called before any `get()` — the assert
  catches misuse at development time.
- spdlog adds a ConanCenter dependency to `orion_app`; it is widely-used and
  stable with no transitive dependencies.
