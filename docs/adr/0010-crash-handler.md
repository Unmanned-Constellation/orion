# ADR 0010: CrashHandler for symbolized stack traces

## Status
Accepted

## Context
When a service crashes on the Jetson (SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS)
the default behaviour is a core dump or silent termination. Debugging a crash on
an embedded target without a symbolized stack trace requires reproducing the
fault on a development machine, which is often not possible for hardware-induced
or timing-dependent faults.

A crash handler that prints a symbolized stack trace to stderr before the process
exits gives the information needed to diagnose the fault from a log alone.

`ShutdownLatch` already manages SIGINT and SIGTERM via `pthread_sigmask` +
`sigwait`. Crash signals must not be handled via `sigwait` - they are
synchronous faults that must be delivered to the faulting thread as a signal
action, not consumed by a watcher thread.

## Decision

Use **backward-cpp** (header-only, MIT licence) to print a symbolized stack
trace on crash signals. `CrashHandler` is a RAII type in `namespace orion::app`
that registers signal actions at construction and restores the previous handlers
at destruction.

```cpp
int main()
{
    orion::app::ShutdownLatch latch;   // blocks SIGINT/SIGTERM - must be first
    orion::app::CrashHandler  crash;   // registers crash signal actions
    // ...
}
```

### Signal handler setup

`CrashHandler` calls `sigaction` directly for each crash signal rather than
using `backward::SignalHandling`. This gives full control over signal flags.
Each action is registered with all three flags:

- **`SA_SIGINFO`** - handler receives `siginfo_t` (faulting address via
  `si_addr`) and `ucontext_t` (CPU register state). backward-cpp uses the
  instruction pointer from `ucontext_t` to include the faulting frame in the
  trace; without it the frame that actually crashed is absent.
- **`SA_ONSTACK`** - signal is delivered on an alternate stack allocated at
  construction via `sigaltstack`. Required for stack-overflow crashes (SIGSEGV
  on stack exhaust): the default stack is already full and the signal cannot be
  delivered without an alternate stack, resulting in silent hang or re-fault.
  The alternate stack buffer is allocated with `mmap(MAP_ANONYMOUS | MAP_PRIVATE)`
  rather than `malloc`. Heap corruption is a common crash scenario; a
  malloc'd buffer may itself be corrupt at crash time. `mmap` bypasses the heap
  allocator entirely. A `PROT_NONE` guard page is mapped immediately after the
  64 KB buffer so that overflow inside the handler (e.g. deep DWARF unwinding)
  faults immediately rather than scribbling on adjacent memory. The constructor
  stores the `mmap` base and total size; the destructor calls `munmap`.
- **`SA_RESETHAND`** - resets the disposition to `SIG_DFL` after the first
  delivery. If the handler itself faults (corrupted stack, bad pointer), the
  second signal hits the kernel default (core dump) rather than re-entering the
  handler. On an autonomy platform a locked-up crash handler is worse than no
  crash handler.

The constructor saves the previous `sigaction` for each signal (via the
`oldact` out-parameter). The destructor restores them, making `CrashHandler`
composable in test harnesses and correctly scoped as a RAII object.

### Registered signals

`SIGSEGV`, `SIGABRT`, `SIGFPE`, `SIGILL`, `SIGBUS`.

`SIGABRT` is included because `std::terminate` and `assert()` raise it -
those are crashes in production, not intentional calls.

### Handler body

The handler runs on the alternate stack with `SA_SIGINFO` context available:

1. Writes a single header line to `STDERR_FILENO` via `write()` (async-signal-safe):
   ```
   [CrashHandler] caught SIGSEGV - stack trace:
   ```
2. Captures a `backward::StackTrace`, skipping signal handler frames so the
   first printed frame is the fault site.
3. Prints via `backward::Printer` to stderr.
4. Re-raises the signal via `raise(sig)`. Because `SA_RESETHAND` has reset the
   disposition to `SIG_DFL`, the re-raise delivers the default action - core
   dump if enabled, otherwise signal-exit. The process exit status reflects the
   signal, which supervisors and `systemd` can distinguish from a clean exit.

`write()` is used for the header rather than `fprintf` or `std::cerr` because
those are not async-signal-safe. The backward-cpp symbolization calls
(`backtrace()`, `dladdr()`) are also not strictly async-signal-safe, but this
is acceptable: the handler runs in a process that is already dying.

### Single-instance enforcement

`CrashHandler` must not be constructed more than once per process. Two
instances produce double-registration; the second destructor restores the first
instance's handlers and the first destructor restores the pre-`CrashHandler`
handlers, leaving the process unprotected. A `static std::atomic<bool>` flag is
set in the constructor and checked first - a second construction asserts false
with a clear message.

### Symbol resolution

backward-cpp supports three backends: `libdw` (DWARF), `libdwarf`, and
`libbfd`. `libdw` is available on the Jetson via `libdw-dev` and produces the
best output (file names, line numbers, inlined frames). Debug builds are already
compiled with `-g`. Release builds stripped of debug symbols produce
address-only traces; an unstripped binary or symbol map must be retained for
release crash analysis.

### Output

The stack trace is written to `stderr`. Services run under a supervisor
(systemd or a watchdog) that captures stderr to a persistent log. The header
line makes crash traces identifiable and grep-able in aggregated logs from
multiple concurrent services.

### CMake wiring

`backward-cpp` is added to `conanfile.py`. `libdw` is a system library linked
via:

```cmake
target_link_libraries(orion_app INTERFACE dw)
```

### Symbol management for release builds

Deploying unstripped binaries to the Jetson is impractical. The build pipeline
splits debug symbols before stripping using `objcopy`:

```bash
objcopy --only-keep-debug <binary> <binary>.dbg   # extract symbols
objcopy --strip-debug <binary>                     # strip deployable binary
objcopy --add-gnu-debuglink=<binary>.dbg <binary>  # link the two
```

The `.dbg` file is archived alongside the release artifact. To symbolize a
crash trace from a stripped binary, copy the `.dbg` file alongside the binary
and run `addr2line` or point a debugger at the `.dbg` file. CMake's
`CMAKE_BUILD_TYPE=Release` configuration will drive this via a post-build
step on the `install` target.

## Consequences

- Every service gets symbolized crash traces with no per-service code changes
  beyond adding `CrashHandler crash;` after `ShutdownLatch`.
- `libdw-dev` must be present on the Jetson and in the cross-compilation sysroot.
- Release builds are stripped before deployment. Debug symbols are split into a
  `.dbg` file via `objcopy --only-keep-debug` and archived alongside the release
  artifact. Crash traces from stripped binaries are symbolized offline using the
  `.dbg` file and `addr2line`.
- `CrashHandler` must not be constructed more than once per process; the
  single-instance check enforces this at runtime.
- `SA_RESETHAND` means the handler fires at most once per signal per process
  lifetime - a second fault of the same type after a partial trace hits
  `SIG_DFL` directly.
- Stack-overflow faults produce a full trace because the handler runs on the
  alternate stack; without `SA_ONSTACK` they would hang silently.
