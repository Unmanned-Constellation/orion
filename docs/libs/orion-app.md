# orion_app - Application Runtime Library

Service lifecycle and observability primitives for Orion microservices.

## Overview

`orion_app` provides the components every Orion service wires up at startup:
shutdown signalling, time-triggered execution, and crash diagnostics.

For most services, `ServiceBootstrapper` (in `orion_main`) is the preferred way to wire
these up — it owns `ShutdownLatch` and `CrashHandler` internally and returns a ready
`ServiceContext`. See [`orion_main`](orion-main.md) for the bootstrapper pattern.

When direct control is needed (tests, custom services), the components are independent
RAII objects constructed at the top of `main()` in a fixed order:

```cpp
int main()
{
    orion::app::ShutdownLatch latch;               // 1. block SIGINT/SIGTERM before any threads
    orion::app::CrashHandler  crash;               // 2. register crash signal actions
    auto clock = std::make_shared<orion::clock::WallClock>();
    orion::app::FrameScheduler sched(100.0, clock, &latch); // 3. time-triggered loop
    // ... register callbacks, construct session ...
    sched.run();
}
```

`orion_app` is a **STATIC** library. It depends on `orion_clock` and `backward-cpp`.

## Service startup and shutdown sequence

```{mermaid}
sequenceDiagram
    participant main
    participant Latch as ShutdownLatch
    participant Crash as CrashHandler
    participant Sched as FrameScheduler
    participant OS

    main->>Latch: ShutdownLatch()
    Note over Latch: pthread_sigmask blocks SIGINT/SIGTERM<br/>on main thread; watcher thread spawned
    main->>Crash: CrashHandler()
    Note over Crash: installs handlers for SIGSEGV/SIGABRT/etc<br/>on an mmap'd alternate stack
    main->>Sched: FrameScheduler(rate_hz, clock, &latch)
    main->>Sched: every(N, callback) ...
    main->>Sched: run()
    Note over Sched: tick loop begins; main thread blocks here

    OS-->>Latch: SIGTERM or SIGINT
    Note over Latch: watcher thread receives signal via sigwait()
    Latch->>Latch: stop() → stopped_ = true, cv notified
    Sched->>Sched: observes latch.stopped() after next sleepUntil
    Sched-->>main: run() returns
    Note over main: ~FrameScheduler, ~CrashHandler, ~ShutdownLatch<br/>restore signal handlers, join watcher thread
```

---

## `CrashHandler`

```cpp
#include "orion/app/crash_handler.hpp"
namespace orion::app
```

RAII crash handler that prints a symbolized stack trace to stderr on fatal
signals before the process exits. Registers signal actions at construction and
restores the previous handlers at destruction, making it composable in test
harnesses.

### Registered signals

`SIGSEGV`, `SIGABRT`, `SIGFPE`, `SIGILL`, `SIGBUS`.

### Handler output

```
[CrashHandler] caught SIGSEGV - stack trace:
Stack trace (most recent call last):
#5  ...
#4  ...
#0  /workspaces/orion/build/Debug/src/my_service at my_service.cpp:42
```

The header line (`[CrashHandler] caught SIG...`) is written via `write()` which
is async-signal-safe. Symbolization follows via `backward::Printer` with
colorization and source snippets disabled — output is grep-friendly in
aggregated systemd logs.

### Alternate signal stack

The handler runs on a 128 KB alternate stack allocated via `mmap` (bypasses the
heap allocator, which may itself be corrupt at crash time). A `PROT_NONE` guard
page is placed immediately after the stack so handler overflow faults cleanly.
`SA_ONSTACK` ensures the kernel delivers crash signals onto this stack, making
stack-overflow faults (`SIGSEGV` on stack exhaust) survivable.

### Signal flags

| Flag | Purpose |
|---|---|
| `SA_SIGINFO` | Handler receives `siginfo_t` and `ucontext_t`; faulting frame is included in the trace |
| `SA_ONSTACK` | Signal delivered on the alternate stack |
| `SA_RESETHAND` | Disposition reset to `SIG_DFL` after first delivery — prevents handler re-entry on a second fault |

`SA_RESETHAND` means the handler fires at most once per signal per process
lifetime. A second fault hits `SIG_DFL` directly (core dump), which is safer
than looping.

### Constructor preconditions

- Asserts that no crash signal is currently blocked on the calling thread.
  Blocking a synchronous fault signal is undefined behaviour.
- Asserts that no other `CrashHandler` instance exists in this process
  (double-registration corrupts handler state).

### Usage

```cpp
int main()
{
    orion::app::ShutdownLatch latch;  // must be first — sets signal mask
    orion::app::CrashHandler  crash;  // must follow latch
    // ...
}
```

### Symbol resolution

Symbolization quality depends on the `backward-cpp` backend and build type:

| Build | Backend | Output |
|---|---|---|
| Debug | `dw` | Function names + file/line numbers + inlined frames |
| Release (stripped) | `dw` | Address-only; symbolize offline with `addr2line` |

See [Offline symbolization](#offline-symbolization) below.

### Method reference

| Method | Description |
|---|---|
| `CrashHandler()` | Allocates alternate stack, registers signal actions. Asserts on blocked signals or duplicate instance. |
| `~CrashHandler()` | Restores previous signal handlers, disables alternate stack, releases mmap. |

---

## `FrameScheduler`

```cpp
#include "orion/app/frame_scheduler.hpp"
namespace orion::app
```

Single-threaded, time-triggered executor. Runs registered callbacks
sequentially on one thread at integer sub-multiples of the minor frame rate.
See [`orion_clock`](orion-clock.md) for the full `FrameScheduler` reference —
it is documented alongside the clock types it depends on.

---

## `ShutdownLatch`

```cpp
#include "orion/app/shutdown_latch.hpp"
namespace orion::app
```

Blocks `main()` until SIGTERM or SIGINT is received, or until `stop()` is
called programmatically. Also acts as the run-loop exit signal for
`FrameScheduler`. See [`orion_clock`](orion-clock.md) for the full
`ShutdownLatch` reference.

---

## CMake integration

```cmake
target_link_libraries(my_service PRIVATE orion_app)
```

`orion_app` is a STATIC library. Linking it pulls in `orion_clock` and `spdlog` transitively.

### Debug symbol splitting (Release builds)

`cmake/DebugSymbols.cmake` provides `orion_split_debug_symbols(target)` which
post-processes a Release binary to separate DWARF symbols into a `.dbg` sidecar
file:

```cmake
include(cmake/DebugSymbols.cmake)
orion_split_debug_symbols(my_service)
```

This produces:
- `my_service` — stripped deployable binary (smaller, suitable for the Jetson eMMC)
- `my_service.dbg` — DWARF symbols, installed to `lib/debug/` via the
  `debug-symbols` CMake install component

The three `objcopy` steps run automatically as a `POST_BUILD` command. No manual
invocation is required.

---

(offline-symbolization)=
## Offline symbolization

When a stripped binary crashes on the Jetson, `CrashHandler` prints address-only
frames. To reconstruct file and line information on a development workstation:

1. Retrieve the `.dbg` file for the exact build that crashed.
2. Place it alongside the stripped binary (or use `--debug-link` to point
   `addr2line` at it explicitly).
3. Run `addr2line`:

```bash
# Cross-compiled ARM64 binary:
aarch64-linux-gnu-addr2line -e my_service.dbg -f -C 0x00000055555562d4

# Native x86_64:
addr2line -e my_service.dbg -f -C 0x00007f3a2c4b1a22
```

The `.dbg` file must correspond to the exact build artifact — address offsets
are not stable between builds.
