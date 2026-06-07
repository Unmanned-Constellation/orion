# ADR 0019: CLI and environment variable configuration with CLI11

## Status
Accepted

## Context

Every Orion microservice needs configuration at startup: at minimum a vehicle ID,
a log level, and any service-specific tuning parameters. The current
`clock_service/main.cpp` handles this with hand-rolled `requireEnv` /
`optEnvDouble` helpers using `std::getenv`. This approach has several problems as
services multiply:

- No `--help` output — an operator has no way to discover what a binary accepts.
- No `--version` output — no way to confirm which build is running on the Jetson.
- No type validation — a malformed `SIM_RATE_HZ=abc` silently calls `std::stod`
  and throws an unrelated exception at an unexpected point.
- Duplicated boilerplate — every `main.cpp` reimplements the same env-var lookup
  pattern for the common fields (`VEHICLE_ID`, `LOG_LEVEL`).
- `NOLINT(concurrency-mt-unsafe)` suppression required on every `std::getenv` call.

Additionally, the existing codebase uses an `ORION_` prefix on environment
variables (e.g. `ORION_VEHICLE_ID`, `ORION_LOG_LEVEL`). This prefix is redundant
— all variables belong to Orion services by definition — and is dropped as part
of this ADR. References in earlier ADRs and code reflect the old convention;
this ADR supersedes them.

## Decision

### Framework: CLI11

Use **CLI11** (header-only, MIT licence, ConanCenter) for argument and environment
variable parsing in all microservice `main.cpp` files.

CLI11 supports:
- Named flags (`--vehicle-id alpha`) with env var fallbacks (`VEHICLE_ID=alpha`)
- CLI flags take precedence over env vars when both are present
- Required vs optional options with defaults
- Type-safe parsing with clear error messages on type mismatch
- Auto-generated `--help` from option descriptions
- `--version` with a caller-supplied version string
- Exit code 1 + usage hint on misconfiguration, before any application code runs

Alternatives considered:
- **Boost.Program_options** — rejected; pulls in a large Boost dependency for a
  solved problem.
- **cxxopts** — viable but less actively maintained than CLI11 and no env var
  support without extra glue.
- **Hand-rolled** — current approach; does not scale past one service.

### Environment variable naming: no prefix

Environment variable names are unprefixed. `VEHICLE_ID`, `LOG_LEVEL`, `SIM_SCALE`
— not `ORION_VEHICLE_ID`, `ORION_LOG_LEVEL`, `ORION_SIM_SCALE`. The `ORION_`
prefix added no information and is dropped retroactively across all services and
documentation.

### `orion_main` — shared config library

A new `orion_main` static library in `libs/main/` owns the shared startup
scaffolding. It links `orion_app` and `CLI11::CLI11` and is the only target that
depends on CLI11. Service libraries (`orion_clock_service`, etc.) remain CLI11-free.

`orion_main` exposes a `ServiceConfig` struct and a `parseServiceConfig` function:

```cpp
namespace orion::app
{

struct ServiceConfig
{
    std::string vehicle_id;
    std::string log_level{"info"};
};

auto parseServiceConfig(CLI::App& app) -> ServiceConfig;

} // namespace orion::app
```

`parseServiceConfig` registers the common options on the provided `CLI::App`
instance. The caller then adds service-specific options before calling
`app.parse(argc, argv)`:

```cpp
int main(int argc, char** argv)
{
    auto latch = orion::app::ShutdownLatch{};
    auto crash = orion::app::CrashHandler{};

    auto app = CLI::App{"clock-service"};
    app.set_version_flag("--version", ORION_VERSION_STRING);

    auto cfg      = orion::app::ServiceConfig{};
    auto scale    = 1.0;
    auto rate_hz  = 100.0;

    orion::app::addServiceConfig(app, cfg);
    app.add_option("--scale", scale, "Sim speed relative to wall time")
       ->envname("SIM_SCALE");
    app.add_option("--rate-hz", rate_hz, "Publish rate in Hz")
       ->envname("SIM_RATE_HZ");

    CLI11_PARSE(app, argc, argv); // prints help/error and exits on failure

    orion::app::LoggerFactory::init(cfg.log_level);
    // ...
}
```

`CLI11_PARSE` is a CLI11 macro that calls `app.parse()`, catches `CLI::ParseError`,
prints the message to stderr, and calls `exit()` with the appropriate code. This
runs before `LoggerFactory::init()` — config errors are reported directly to
stderr, not via spdlog. This is intentional: logging cannot be initialized before
the config is known.

### `--version` output

`--version` prints `<tag> (<build-type>, <arch>)`, for example:

```
v0.3.0 (Debug, amd64)
```

The version string is constructed in `cmake/Version.cmake` from `ORION_VERSION`
(already derived from `git describe --tags`), `CMAKE_BUILD_TYPE`, and
`CMAKE_SYSTEM_PROCESSOR`, and injected as `ORION_VERSION_STRING` via a
`configure_file` step. Each service executable links the generated header.

### Error handling

CLI11 default error handling is used without modification. On a missing required
option or type mismatch, CLI11 writes to stderr and exits with code 1. No custom
error handler is registered. This is correct: config errors occur before
`LoggerFactory` is initialized, so spdlog is not available.

### Migrating `clock_service`

`clock_service/main.cpp` is updated as part of the implementing PR:
- `requireEnv` / `optEnvDouble` helpers are removed
- `ORION_VEHICLE_ID` → `VEHICLE_ID`, `ORION_SIM_SCALE` → `SIM_SCALE`,
  `ORION_SIM_RATE_HZ` → `SIM_RATE_HZ`
- `main()` gains `int argc, char** argv` parameters
- `NOLINT(concurrency-mt-unsafe)` suppressions are removed

## Consequences

- All services get `--help` and `--version` for free from `orion_main`.
- Config errors produce a clear, consistent message before any application code
  runs — no cryptic exceptions from deep in initialization.
- CLI flags override env vars — operators can override systemd unit file config
  without editing unit files.
- `ORION_` prefix is dropped retroactively. Existing deployments using
  `ORION_VEHICLE_ID` etc. must update their systemd unit files.
- `orion_main` is a new CMake target — service `main.cpp` files link it instead
  of `orion_app` directly. Libraries never link `orion_main`.
- CLI11 is a Conan dependency of `orion_main` only; no other library sees it.
- The `ORION_LOG_LEVEL` reference in ADR-0011 is superseded by `LOG_LEVEL`.
