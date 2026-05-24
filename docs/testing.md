# Testing

Orion enforces correctness at every layer of the build: static analysis at compile time,
memory and thread safety at runtime, and behaviour correctness through fuzzing. This document
explains the philosophy behind each tool, how to use it locally, and what CI enforces.

## Philosophy

The goal is a codebase where defects are caught as early as possible. The order of preference is:

1. **Compile-time** — clang-tidy rules out entire classes of error before the binary exists
2. **Sanitizer runs** — ASan/UBSan/TSan catch memory and concurrency bugs at test time
3. **Fuzzing** — libFuzzer finds crash-inducing inputs that hand-written tests miss
4. **Coverage** — the 60% floor is a floor, not a target; it catches test suite regressions

None of these tools replaces the others. All four run in CI on every PR.

---

## Unit tests

Tests live in `tests/` and use [GoogleTest](https://google.github.io/googletest/). Each test
file covers a single library target. CTest discovers and runs all tests via
`gtest_discover_tests`.

### Writing a test

```cpp
#include <gtest/gtest.h>

TEST(TransportTest, PublishesOnConfiguredTopic) {
    // arrange
    // act
    // assert
    EXPECT_EQ(actual, expected);
}
```

Place the file in `tests/`, add it to `tests/CMakeLists.txt`:

```cmake
add_executable(my_tests my_test.cpp)
target_link_libraries(my_tests PRIVATE GTest::gmock_main orion_transport)
target_compile_features(my_tests PRIVATE cxx_std_20)
gtest_discover_tests(my_tests DISCOVERY_MODE PRE_TEST)
```

`DISCOVERY_MODE PRE_TEST` defers test discovery to CTest time rather than CMake build time.
This is required because running the test binary during the build would crash under
ThreadSanitizer in Docker (see [ThreadSanitizer limitations](#tsan-docker-limitation)).

### Running unit tests

```bash
cmake --build --preset debug
ctest --preset debug
```

Or via VS Code: **CMake: Run Tests**.

Output is suppressed on success and printed in full on failure.

---

## AddressSanitizer + UndefinedBehaviorSanitizer

ASan catches heap/stack buffer overflows, use-after-free, and memory leaks. UBSan catches
signed integer overflow, null pointer dereference, misaligned access, and other C++ undefined
behaviour. Both are compile-time instrumentation — the sanitized binary detects errors at the
exact line they occur and terminates with a detailed report.

### When to run

Run the sanitize preset before merging any change that:
- allocates memory or manages resource lifetimes
- casts between types
- performs arithmetic on pointers or indices
- modifies shared state

CI runs it on every PR — this section is for running it locally.

### Running

```bash
cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize
```

### Interpreting output

A clean run produces no output. A violation looks like:

```
==12345==ERROR: AddressSanitizer: heap-use-after-free on address 0x...
READ of size 8 at 0x... thread T0
    #0 0x... in orion::transport::... transport.cpp:42
```

Fix the issue, then re-run. Do not merge with open ASan findings.

UBSan violations look like:

```
transport.cpp:17:5: runtime error: signed integer overflow: 2147483647 + 1 cannot be represented
```

The flags `-fno-sanitize-recover=all` ensure that both ASan and UBSan abort on the first
violation rather than continuing. This makes CI failures unambiguous.

---

## ThreadSanitizer

(tsan-docker-limitation)=

TSan detects data races between threads: two concurrent accesses to the same memory where at
least one is a write and there is no synchronisation between them. It instruments the binary
to track all memory accesses and lock operations at runtime.

### When to run

Run TSan whenever you add or modify code that:
- creates or joins threads
- reads or writes shared state from a callback or subscriber
- uses any `std::atomic`, mutex, or condition variable

### Running

```bash
cmake --preset tsan
cmake --build --preset tsan
ctest --preset tsan
```

### Interpreting output

```
WARNING: ThreadSanitizer: data race (pid=...)
  Write of size 8 at 0x... by thread T2:
    #0 orion::transport::... transport.cpp:88
  Previous read of size 8 at 0x... by thread T1:
    #0 orion::transport::... transport.cpp:63
```

TSan reports the two conflicting accesses, the threads they belong to, and their stack traces.
The fix is to add appropriate synchronisation (mutex, `std::atomic`, or a lock-free structure).

---

## Coverage

Coverage instrumentation records which lines of code were executed during a test run. The
report answers "which code paths does the test suite actually exercise?" and surfaces untested
branches.

### Running locally

```bash
cmake --preset coverage
cmake --build --preset coverage
ctest --preset coverage
cmake --build --preset coverage --target coverage-report
```

The `coverage-report` target merges `build/Coverage/cov-*.profraw` files and prints a summary.

### Reading the report

```
Filename                         Regions  Missed Regions  Cover  Functions  ...  Lines  Missed Lines  Cover
tests/transport_test.cpp               1               0  100%           1  ...      1             0  100%
TOTAL                                  1               0  100%           1  ...      1             0  100%
```

The three coverage percentages are:
- **Region%** — percentage of distinct code regions (conditionals, loops) executed
- **Function%** — percentage of functions called at least once
- **Line%** — percentage of executable lines reached

Line coverage is the primary metric enforced by CI.

### The 60% floor

CI fails if total line coverage falls below **60%**. This floor exists to detect test suite
regressions — a sharp drop signals that new code shipped without tests. It is not a quality
target. As the test suite matures, raise it in `.github/workflows/ci.yml`:

```yaml
- name: Enforce minimum coverage
  run: |
    ...
    if awk "BEGIN { exit !(${COVERAGE}+0 < 80) }"; then   # raise 60 → 80
```

Aim for high branch coverage on safety-critical paths (message parsing, state machines),
not just high line coverage overall.

---

## Fuzzing

Fuzzing automatically generates large volumes of randomised inputs and feeds them to a target
function, looking for crashes, assertion failures, and sanitizer violations. It finds the
inputs that hand-written tests never think to try.

Orion uses [libFuzzer](https://llvm.org/docs/LibFuzzer.html), which is built into clang.
Fuzz targets live in `tests/fuzz/` and are compiled with `-fsanitize=address` so that any
memory error discovered during fuzzing also triggers an ASan report.

### When to add a fuzz target

Add a fuzz target for any code that:
- parses bytes from an external source (Zenoh messages, protobuf, topic strings)
- deserialises binary data into a structured type
- validates or canonicalises user-supplied input

Rule of thumb: if the function takes a `std::span<const std::byte>`, `std::string_view`, or
a raw byte pointer, it should have a fuzz target.

### Building and running

```bash
cmake --preset fuzz
cmake --build --preset fuzz

# Short smoke run (use this to verify the target builds and does not immediately crash)
./build/Fuzz/tests/fuzz/fuzz_topic -runs=10000 -max_len=4096 -timeout=10

# Longer campaign (store discovered interesting inputs in a corpus directory)
mkdir -p corpus/fuzz_topic
./build/Fuzz/tests/fuzz/fuzz_topic corpus/fuzz_topic/ -max_len=4096
```

The longer campaign runs indefinitely, saving new interesting inputs to `corpus/fuzz_topic/`.
Press Ctrl-C to stop. On subsequent runs, pass the corpus directory to resume from known
interesting inputs rather than starting from scratch.

### Writing a new fuzz target

Create a file in `tests/fuzz/`:

```cpp
#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, const size_t size) {
    // Feed data into the function under test.
    // Do NOT allocate memory that is not freed — ASan will catch it.
    // Return 0 always. Non-zero return values have special meaning to libFuzzer.
    return 0;
}
```

Rules:
- The function signature must match exactly — libFuzzer provides the entry point
- Keep the target deterministic: no random numbers, no time-dependent behaviour
- If the function throws, catch the exception rather than letting it propagate
- Avoid calling functions with external side effects (network, filesystem) inside the target

Register the target in `tests/fuzz/CMakeLists.txt`:

```cmake
add_executable(fuzz_my_parser fuzz_my_parser.cpp)
target_link_libraries(fuzz_my_parser PRIVATE orion_transport)  # or whichever lib
target_compile_features(fuzz_my_parser PRIVATE cxx_std_20)
target_link_options(fuzz_my_parser PRIVATE -fsanitize=fuzzer)
```

The `-fsanitize=fuzzer` link option provides the libFuzzer main function. The ASan
instrumentation is inherited from the `fuzz` CMake preset's global `add_link_options`.

### Interpreting output

A clean run:

```
#10000  DONE   corp: 5/42b lim: 98 exec/s: 50000 rss: 36Mb
Done 10000 runs in 0 second(s)
```

A crash:

```
==12345==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x...
SUMMARY: AddressSanitizer: heap-buffer-overflow ...
MS: 3 ChangeByte-EraseBytes-InsertByte-; base unit: ...
artifact_prefix='./'; Test unit written to ./crash-<hash>
```

When a crash is found, libFuzzer writes the minimal reproducing input to `crash-<hash>`. To
reproduce and debug:

```bash
./build/Fuzz/tests/fuzz/fuzz_topic crash-<hash>
```

Fix the bug, rebuild, and verify the crash file no longer triggers it. Add the crash file to
`corpus/` so it is tested on every future run.

### The "no interesting inputs" warning

```
WARNING: no interesting inputs were found so far. Is the code instrumented for coverage?
```

This is normal for a stub target that has no branches. It means libFuzzer cannot find any
input that takes a new code path, so it is not generating a corpus. This warning disappears
once real logic is added to the function under test.

---

## CI summary

| Job | Preset | What it catches |
|---|---|---|
| Build and lint (debug) | `debug` | Compile errors, clang-tidy violations |
| Build and test (release) | `release` | Release-mode UB exposed by NDEBUG, optimisation bugs |
| Sanitize (ASan + UBSan) | `sanitize` | Memory errors, undefined behaviour |
| Thread Sanitizer | `tsan` | Data races |
| Coverage | `coverage` | Test suite regressions (< 60% line coverage fails) |
| Fuzz (smoke test) | `fuzz` | Immediate crashes in fuzz targets (10 000 iterations) |

All six jobs must pass before a PR can be merged. See [ci-cd.md](ci-cd.md) for job details.
