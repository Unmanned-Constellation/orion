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
  allocator entirely.

  The stack is 128 KB. 64 KB is the POSIX minimum but backward-cpp's DWARF
  unwinding via libdw interacts with ELF structures and string parsing and can
  exhaust a smaller buffer on deeply nested call stacks. Anonymous pages are not
  backed by physical RAM until accessed, so the extra 64 KB costs nothing in
  practice.

  The total `mmap` allocation is `128 KB + PAGE_SIZE` where `PAGE_SIZE` is
  obtained via `sysconf(_SC_PAGESIZE)`. The constructor asserts that the stack
  size is a multiple of the page size. A `PROT_NONE` guard page is mapped
  immediately after the 128 KB buffer via `mprotect` so that overflow inside the
  handler faults immediately rather than scribbling on adjacent memory:

  ```
  ┌─────────────────────────────────┬──────────────┐
  │  Alternate Stack (128 KB)       │  Guard Page  │
  │  PROT_READ | PROT_WRITE         │  PROT_NONE   │
  └─────────────────────────────────┴──────────────┘
  ```

  The constructor stores the `mmap` base and total size; the destructor calls
  `munmap`.
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
2. Captures a `backward::StackTrace` via `load_here(depth, HANDLER_FRAME_SKIP)`
   where `constexpr size_t HANDLER_FRAME_SKIP = 3` skips the signal trampoline,
   handler dispatch, and `load_here` itself. The `ucontext_t*` obtained by
   `reinterpret_cast<ucontext_t*>` from the third `SA_SIGINFO` parameter is
   passed so the instruction pointer of the faulting frame is included.
   The exact skip count is verified by inspection on first implementation; if
   compiler optimization collapses frames and `CrashHandler::handleSignal`
   appears at the top of the trace, that is harmless — the fault site is visible
   immediately below.
3. Prints via `backward::Printer` to stderr. The printer is stack-allocated with
   colorization and source snippet printing disabled:
   ```cpp
   auto printer = backward::Printer{};
   printer.color_mode = backward::ColorMode::never;
   printer.snippet    = false;
   ```
   Color escape codes are noise in aggregated systemd logs. Source snippets
   require file I/O inside the signal handler, which is not async-signal-safe.
4. Re-raises the signal via `raise(sig)`. Because `SA_RESETHAND` has reset the
   disposition to `SIG_DFL`, the re-raise delivers the default action - core
   dump if enabled, otherwise signal-exit. The process exit status reflects the
   signal, which supervisors and `systemd` can distinguish from a clean exit.
   `_exit(1)` follows `raise(sig)` as a fallback in case `raise` returns — this
   guards against pathological signal masking and ensures the process never
   returns from the handler.

`write()` is used for the header rather than `fprintf` or `std::cerr` because
those are not async-signal-safe. The backward-cpp symbolization calls
(`backtrace()`, `dladdr()`) are also not strictly async-signal-safe, but this
is acceptable: the handler runs in a process that is already dying.

### Constructor precondition: crash signals must not be blocked

The constructor calls `pthread_sigmask(SIG_BLOCK, nullptr, &current_mask)` and
`sigismember` for each registered signal. If any crash signal is currently
blocked, `assert(false)` fires with a clear message. Blocking a synchronous
fault signal produces undefined behaviour (silent hang or lost signal). This
catches misconfigured signal masks immediately in development.

### Saved handler storage

The previous `sigaction` for each signal is saved in parallel arrays:

```cpp
static constexpr std::array<int, 5> SIGNALS = {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS};
std::array<struct sigaction, 5> old_actions_;
```

The destructor loops over both arrays to restore previous handlers.

### `libdw` CMake integration

`libdw` is a system library found via `pkg_check_modules`:

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(libdw REQUIRED libdw)
target_link_libraries(orion_app INTERFACE ${libdw_LIBRARIES})
target_include_directories(orion_app INTERFACE ${libdw_INCLUDE_DIRS})
```

This produces a configure-time error if `libdw-dev` is absent rather than a
cryptic linker failure. `libdw-dev` must be added to the devcontainer Dockerfile
and to the Jetson deployment sysroot.

### `objcopy` debug symbol splitting

A CMake helper function `orion_split_debug_symbols(target)` defined in
`cmake/DebugSymbols.cmake` wraps the three `objcopy` steps and is guarded by
`CMAKE_BUILD_TYPE STREQUAL "Release"`. Each executable calls it once:

```cmake
orion_split_debug_symbols(my_service)
```

### Testing strategy

Three test categories:

1. **Registration/restoration** - after construction, `sigaction(SIGSEGV, nullptr, &act)`
   verifies the handler is installed. After destruction, the previous handler is
   restored. In-process, no signal delivery required.
2. **Single-instance death test** - `EXPECT_DEATH` constructs a second
   `CrashHandler` and verifies the assert fires.
3. **Crash trace output** - `EXPECT_DEATH` raises a signal and checks stderr
   matches `"\\[CrashHandler\\] caught SIG"`. Spawns a subprocess via GoogleTest's
   death test mechanism; no in-process signal delivery needed.

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
