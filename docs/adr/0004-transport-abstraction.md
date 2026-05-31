# ADR 0004: Transport abstraction layer

## Status
Accepted (amended - see timestamp ownership revision below)

## Context
Zenoh was chosen as the IPC transport (ADR-0001). Microservices need a stable API to publish and
subscribe to typed messages without depending directly on Zenoh. Reasons to abstract:

- Zenoh is subject to change; a direct dependency in every service makes upgrades painful.
- Future transport protocols (DDS, custom UDP, etc.) should be substitutable without touching
  service code.

## Decision

Wrap Zenoh behind a three-type interface: `Session`, `Publisher<T>`, and `Subscriber<T>`.
All Zenoh includes are confined to `libs/transport/src/session_impl.cpp`. No Zenoh type leaks
into the public headers (`libs/transport/include/`).

Key design choices:

**Typed template API over raw bytes.** `Publisher<T>::publish(const T&, uint64_t captured_at_ns)`
and `Subscriber<T>::Callback = std::function<void(const T&, const MessageHeader&)>`. This gives
compile-time type safety: it is impossible to publish the wrong message type on a topic.

**Envelope is transport-internal.** Each published message is wrapped in an `orion::v1::Envelope`
(defined in `proto/orion/v1/envelope.proto`) before transmission. The envelope carries a
`orion::v1::Header` (captured_at_ns, source_id) and a type_url for runtime type validation.
Service authors never create or read envelopes directly.

**Timestamp ownership belongs to services.** `Publisher<T>::publish` accepts `captured_at_ns`
explicitly - the time the underlying data was captured, supplied by the calling service.
Services hold their own `orion::clock::Clock` reference and call `clock->nowNs()` at the point
of hardware or data capture, not at publish time. This ensures timestamps reflect data age
rather than transport latency.

`orion_transport` has no dependency on `orion_clock`. The two libraries are fully independent;
services link both and wire them at startup.

**Subscriber receives `MessageHeader`.** The callback signature exposes `MessageHeader` (a
plain struct with `captured_at_ns` and `source_id`) so callers can read envelope metadata
without depending on protobuf types in their callback signatures.

**`orion_proto` is PRIVATE to `orion_transport`.** Consumers link against `orion_transport`
and include domain-specific proto headers directly; they do not see `Envelope` or `Header`.

## Consequences

- All inter-service communication goes through `Session::advertise<T>` / `Session::subscribe<T>`.
- `Session::create(SessionConfig)` takes no Clock parameter - the transport is time-agnostic.
- Services are responsible for capturing and passing timestamps at the right moment.
- Adding a new transport backend requires replacing `session_impl.cpp` only.
- `orion_clock` and `orion_transport` have no dependency on each other - circular dependency
  is structurally impossible.
- Request-reply (Zenoh queryables) is deferred; the abstraction is currently pub-sub only.
