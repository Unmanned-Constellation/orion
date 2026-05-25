# ADR 0010: CrashHandler for symbolized stack traces

## Status
Proposed

## Context
When a service crashes on the Jetson (SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS)
the default behaviour is a core dump or silent termination. Debugging a crash on
an embedded target without a symbolized stack trace requires reproducing the
fault on a development machine, which is often not possible for hardware-induced
or timing-dependent faults.

A crash handler that prints a symbolized stack trace to stderr before the process
exits gives the information needed to diagnose the fault from a log alone.

`ShutdownLatch` already manages SIGINT and SIGTERM via `pthread_sigmask` +
`sigwait`. Crash signals must not be handled via `sigwait` — they are
synchronous faults that must be delivered to the faulting thread as a signal
action, not consumed by a watcher thread.

## Decision

Use **backward-cpp** (header-only, MIT licence) to print a symbolized stack
trace on crash signals. `CrashHandler` is a RAII type that registers signal
actions for SIGSEGV, SIGABRT, SIGFPE, SIGILL, and SIGBUS at construction and
restores the previous handlers at destruction.

```cpp
int main()
{
    orion::app::ShutdownLatch latch;   // blocks SIGINT/SIGTERM — must be first
    orion::app::CrashHandler  crash;   // registers crash signal actions
    // ...
}
```

Key design choices:

**Signal handler safety.** The backward-cpp handler calls `backtrace()` and
`dladdr()` which are not async-signal-safe in the strict POSIX sense. This is
acceptable: crash handlers run in a process that is already dying — correctness
of the rest of the process is not a concern. The alternative (async-signal-safe
only, no symbolization) produces addresses that are useless without a development
machine and `addr2line`.

**Crash signals are not blocked.** `ShutdownLatch` calls `pthread_sigmask` for
SIGINT and SIGTERM only. `CrashHandler` registers signal actions via `sigaction`
and does not touch the signal mask. The two handlers are independent and compose
correctly.

**Symbol resolution.** backward-cpp supports three backends for symbol
resolution: `libdw` (DWARF, best output), `libdwarf`, and `libbfd`. `libdw` is
available on the Jetson via `libdw-dev` and is the chosen backend. Debug builds
must be compiled with `-g` (already set in the `Debug` CMake configuration) for
line-number resolution.

**`orion_app` gains a `libdw` dependency.** `backward-cpp` is added to Conan as
a new dependency. `libdw` is a system library on the Jetson and is linked via
`target_link_libraries(orion_app INTERFACE dw)`.

**Output target.** The stack trace is written to `stderr`. Services are expected
to run under a supervisor (systemd or a watchdog process) that captures stderr to
a log file. No custom output sink is provided.

## Consequences

- Every service gets symbolized crash traces with no per-service code changes
  beyond adding `CrashHandler crash;` after `ShutdownLatch`.
- `libdw-dev` must be present on the Jetson and in the cross-compilation sysroot.
- Release builds stripped of debug symbols will produce address-only traces.
  A symbol map or unstripped binary must be retained for release crash analysis.
- `CrashHandler` is not safe to construct more than once per process. Constructing
  two instances produces double-registration of signal actions; the second
  instance's destructor will restore the first instance's handlers, leaving the
  first instance's destructor restoring the original (pre-`CrashHandler`) handler.
  Enforce single-instance via a process-level assertion in the constructor.
