# ADR 0004: Transport abstraction layer

## Status
Accepted

## Context
Zenoh was chosen as the IPC transport (ADR-0001). Microservices need a stable API to publish and
subscribe to typed messages without depending directly on Zenoh. Reasons to abstract:

- Zenoh is subject to change; a direct dependency in every service makes upgrades painful.
- Simulation requires an injectable clock; hardcoding `system_clock::now()` makes this
  impossible without source changes.
- Future transport protocols (DDS, custom UDP, etc.) should be substitutable without touching
  service code.

## Decision

Wrap Zenoh behind a three-type interface: `Session`, `Publisher<T>`, and `Subscriber<T>`.
All Zenoh includes are confined to `libs/transport/src/session_impl.cpp`. No Zenoh type leaks
into the public headers (`libs/transport/include/`).

Key design choices:

**Typed template API over raw bytes.** `Publisher<T>::publish(const T&)` and
`Subscriber<T>::Callback = std::function<void(const T&, const MessageHeader&)>`. This gives
compile-time type safety: it is impossible to publish the wrong message type on a topic.

**Envelope is transport-internal.** Each published message is wrapped in an `orion::Envelope`
(defined in `proto/orion/envelope.proto`) before transmission. The envelope carries a `Header`
(published_at_ns, source_id) and a type_url for runtime type validation. Service authors never
create or read envelopes; the transport stamps and validates them automatically.

**Injectable clock.** `Session::create` accepts a `std::shared_ptr<Clock>`. In production,
`WallClock` is used. In simulation, a `SimClock` (future work) reads from the sim time topic.
No service code ever calls `std::chrono::system_clock::now()` directly.

**Subscriber receives `MessageHeader`.** The callback signature exposes `MessageHeader` (a
plain struct with `published_at_ns` and `source_id`) so callers can read envelope metadata
without depending on protobuf types in their callback signatures.

**`orion_proto` is PRIVATE to `orion_transport`.** Consumers link against `orion_transport`
and include domain-specific proto headers directly; they do not see `Envelope` or `Header`.

## Consequences

- All inter-service communication goes through `Session::advertise<T>` / `Session::subscribe<T>`.
- Adding a new transport backend requires replacing `session_impl.cpp` only.
- Simulation support (SimClock, Docker Compose profile for sim-time publisher) is deferred to
  a follow-on phase; the Clock injection point is already in place.
- Request-reply (Zenoh queryables) is deferred; the abstraction is currently pub-sub only.
- Domain timestamps (e.g., camera capture time) belong in the proto message fields, not in the
  transport header. `published_at_ns` records when the transport published the message.
