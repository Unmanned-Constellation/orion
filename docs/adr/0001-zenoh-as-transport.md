# ADR 0001: Zenoh as the inter-service transport

## Status
Accepted

## Context
Orion requires IPC between C++ microservices on the same Jetson board (zero-copy, low-latency) and a bridge to off-board consumers over WiFi/radio. Candidates evaluated:

- **iceoryx2** — zero-copy SHM, no daemon, C++ bindings via Rust core. On-board only; a separate bridge service would be needed for off-board routing.
- **Custom POSIX shared memory** — maximum control, but lock-free queue implementation on ARM64 is non-trivial engineering work separate from the project's goals.
- **Zenoh** — zero-copy SHM transport for local pub-sub; same API routes over UDP/WiFi to off-board consumers. Rust core with official C++ bindings (`zenoh-cpp`).

## Decision
Use Zenoh as the sole transport layer for all microservice communication, both on-board and off-board.

## Consequences
- The Zenoh C++ API (`zenoh-cpp`) is the only IPC interface microservices use. No direct POSIX SHM or socket calls in application code.
- A Zenoh Router process (configuration only, no application code) runs on the Jetson to bridge local SHM topics to the network.
- `zenoh-c` is consumed as a pre-built binary via a custom Conan recipe that downloads the official GitHub release archive. No Rust toolchain is required. `zenoh-cpp` is the header-only C++ wrapper, also managed by Conan.
- Off-board consumers subscribe to the same Zenoh topics as on-board services — no separate bridge microservice required.
