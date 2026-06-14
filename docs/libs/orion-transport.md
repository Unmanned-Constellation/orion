# orion_transport

`orion_transport` is the message-passing layer for Orion microservices. It wraps the
underlying pub/sub backend (currently Zenoh) behind a backend-agnostic C++ API so that
service code never imports backend headers directly.

## Component relationships

```{mermaid}
classDiagram
    class Session {
        +create(SessionConfig) Session$
        +advertise~T~(topic) Publisher~T~
        +subscribe~T~(topic, callback) Subscriber~T~
    }
    class Publisher~T~ {
        +publish(msg, captured_at_ns) void
    }
    class Subscriber~T~ {
        +~Subscriber()
    }
    class PublisherBackend {
        <<interface>>
        +send(bytes) void
    }
    class SubscriberBackend {
        <<interface>>
    }
    class SessionImpl {
        <<pimpl>>
    }
    Session --> Publisher~T~ : advertise
    Session --> Subscriber~T~ : subscribe
    Session *-- SessionImpl
    Publisher~T~ *-- PublisherBackend
    Subscriber~T~ *-- SubscriberBackend
```

## Publish flow

```{mermaid}
sequenceDiagram
    participant Service
    participant Clock as TimeSource
    participant Pub as Publisher&lt;T&gt;
    participant Backend as ZenohBackend

    Service->>Clock: nowNs()
    Clock-->>Service: captured_at_ns

    Service->>Pub: publish(msg, captured_at_ns)
    Pub->>Pub: serialize msg → payload bytes
    Pub->>Backend: send(Envelope{header, type_url, payload})
    Backend->>Backend: transmit over Zenoh
```

## Subscribe flow

```{mermaid}
sequenceDiagram
    participant Backend as ZenohBackend
    participant Sub as Subscriber&lt;T&gt;
    participant Service

    Backend->>Sub: raw bytes received
    Sub->>Sub: deserialize Envelope
    Sub->>Sub: check type_url matches T
    Sub->>Service: callback(msg, MessageHeader)
```

## Responsibilities

- Open and manage a transport session (one per microservice process).
- Provide typed `Publisher<T>` and `Subscriber<T>` handles for protobuf messages.
- Wrap every published message in an `Envelope` with a `MessageHeader` containing the
  capture timestamp and source identifier.
- Deserialize incoming `Envelope` bytes, type-check against `type_url`, and dispatch to
  the typed callback.

## Core types

### `Session`

Entry point. Created once at startup via `Session::create(SessionConfig)`.

| Method | Description |
|--------|-------------|
| `Session::create(SessionConfig)` | Opens a transport session. Throws `std::runtime_error` on failure. |
| `advertise<T>(topic)` | Returns a `Publisher<T>` bound to `topic`. |
| `subscribe<T>(topic, callback)` | Returns a `Subscriber<T>` whose lifetime controls the subscription. |

```cpp
auto session = orion::transport::Session::create({
    .vehicle_id   = "alpha",
    .service_name = "nav",
});
```

### `SessionConfig`

| Field | Type | Description |
|-------|------|-------------|
| `vehicle_id` | `std::string` | Vehicle identifier - second segment of every per-vehicle topic. |
| `service_name` | `std::string` | Stamped as `MessageHeader::source_id` on every outbound message. |
| `config_path` | `std::optional<std::string>` | Path to a backend-specific config file. Uses built-in defaults when absent. |

### `Publisher<T>`

Obtained from `Session::advertise<T>()`. Non-copyable; movable.

| Method | Description |
|--------|-------------|
| `publish(msg, captured_at_ns)` | Wraps `msg` in an `Envelope` and transmits it. `captured_at_ns` is the time the underlying data was captured - supplied by the calling service, not the transport layer. |

```cpp
auto pub = session.advertise<orion::v1::NavState>("orion/alpha/nav/state");

// In the sensor loop, after reading hardware:
const auto now_ns = clock->nowNs();
pub.publish(nav_state, now_ns);
```

### `Subscriber<T>`

Obtained from `Session::subscribe<T>()`. Non-copyable; movable. Destroying the object
cancels the subscription.

The callback signature is:

```cpp
void(const T& msg, const orion::transport::MessageHeader& hdr)
```

```cpp
auto sub = session.subscribe<orion::v1::NavState>(
    "orion/alpha/nav/state",
    [](const orion::v1::NavState& msg, const orion::transport::MessageHeader& hdr) {
        // hdr.captured_at_ns - when the data was captured
        // hdr.source_id      - which service published it
    });
```

### `MessageHeader`

Decoded from the `Envelope` and passed to every subscriber callback.

| Field | Type | Description |
|-------|------|-------------|
| `captured_at_ns` | `uint64_t` | Time the underlying data was captured, nanoseconds since the Unix epoch. |
| `source_id` | `std::string` | `service_name` of the publishing service. |

## Envelope format

Every published message is wrapped in an `orion.v1.Envelope` protobuf:

```
Envelope {
  header   { captured_at_ns, source_id }
  type_url  (e.g. "orion.v1.NavState")
  payload   (serialized T bytes)
}
```

Subscribers silently drop messages whose `type_url` does not match the expected type.

## Design notes

- The transport library has **no dependency on `orion_clock`**. Services own their clock
  reference and supply `captured_at_ns` at point of hardware capture. The transport layer
  is fully time-agnostic.
- Backend types (Zenoh session, publisher, subscriber handles) never appear in public
  headers - the pimpl pattern keeps all backend includes confined to `zenoh_session.cpp`.
- `PublisherBackend` and `SubscriberBackend` are abstract interfaces; tests can substitute
  fake backends without any Zenoh dependency.
