# Orion — Claude Code Rules

## Build & Test Commands

```bash
# Build (debug is the default dev preset)
cmake --build --preset debug

# Run tests
ctest --preset debug

# Run with sanitizers (ASan + UBSan)
cmake --build --preset sanitize && ctest --preset sanitize

# Run with thread sanitizer
cmake --build --preset tsan && ctest --preset tsan

# Lint (requires compile_commands.json from a configure step)
cmake --build --preset debug --target tidy

# Format C++ sources
cmake --build --preset debug --target format-clang
```

## C++ Style

### Almost Always Auto (AAA)
Prefer `auto` for local variable declarations. State the type explicitly only when it is not deducible or when the deduced type would be misleading.

```cpp
// good
auto cfg  = SessionConfig{};
auto err  = zenoh::ZResult{};
auto raw  = RawCallback{[...](...) { ... }};

// avoid
SessionConfig cfg{};
zenoh::ZResult err{};
```

### Trailing Return Types
Use trailing return types on all function and method declarations and definitions, **except** `void`-returning functions. `-> void` adds no information and breaks Doxygen.

```cpp
// good — non-void return
auto create(SessionConfig config) -> Session;
auto makePublisherBackend(std::string_view topic) -> std::unique_ptr<PublisherBackend>;

// good — void stays conventional
void publish(const T& msg);
virtual void send(std::string_view bytes) = 0;

// avoid
Session create(SessionConfig config);
std::unique_ptr<PublisherBackend> makePublisherBackend(std::string_view topic);
auto publish(const T& msg) -> void;
```

### Naming Conventions
Enforced by `readability-identifier-naming` in `.clang-tidy` — violations are build errors.

| Construct | Convention | Example |
|---|---|---|
| Classes / structs | `PascalCase` | `SessionImpl`, `MessageHeader` |
| Functions / methods | `camelBack` | `makePublisherBackend`, `nowNs` |
| Variables / parameters | `snake_case` | `source_id`, `expected_type` |
| Private members | `snake_case_` (trailing `_`) | `impl_`, `clock_` |
| Global constants | `UPPER_CASE` | `MAX_RETRIES` |
| Namespaces | `snake_case` | `orion::transport` |
| Type aliases | `PascalCase` | `RawCallback` |
| Enum types | `PascalCase` | `TransportState` |
| Enum constants | `UPPER_CASE` | `CONNECTED`, `DISCONNECTED` |

### NOLINT Suppressions
Always name the specific check — bare `// NOLINT` is not allowed.

```cpp
zenoh::ZResult err{}; // NOLINT(misc-const-correctness)

// avoid
zenoh::ZResult err{}; // NOLINT
```

### Function Complexity Limits
Enforced by clang-tidy — write functions to stay within these bounds from the start.

| Limit | Threshold |
|---|---|
| Lines | 50 |
| Statements | 30 |
| Branches | 10 |
| Nesting depth | 4 |
| Parameters | 4 |
| Cognitive complexity | 15 |

### Include Ordering
clang-format enforces four groups, separated by blank lines, in this order:

1. C++ standard library (`<vector>`, `<memory>`, …)
2. POSIX / system headers (`<unistd.h>`, `<sys/socket.h>`, …)
3. Third-party headers (`<zenoh/…>`, `<gtest/gtest.h>`, …)
4. Project headers (`"orion/transport/session.hpp"`, …)

## Documentation

When making changes, update documentation alongside the code — never leave them out of sync.

| Change type | What to update |
|---|---|
| New public API (class, method, type) | Doxygen doc-comments on the declaration; `docs/libs/` page for the owning library if one exists |
| Architectural decision | New ADR in `docs/adr/` following the existing numbering |
| New library or major feature | Entry in `docs/index.md`; dedicated page under `docs/libs/` |
| Behaviour that affects operators / integrators | `README.md` and/or the relevant `docs/` page |
| Change to build, test, or CI workflow | `docs/ci-cd.md`, `docs/development-environment.md`, or `docs/testing.md` as appropriate |
| Change to dependency management | `docs/dependency-management.md` |

`CONTEXT.md` at the repo root captures high-level domain decisions — update it when the architecture shifts, not for routine code changes.

## Testing

- Framework: **GoogleTest**
- Tests live in `tests/`; one `orion_tests` binary covers all units
- Prefer **fake objects** over mocks — see `FakeClock` in `transport_test.cpp` as the pattern
- Write tests before implementation (TDD)
