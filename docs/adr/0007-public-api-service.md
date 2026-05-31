# ADR 0007: Public API Service for Customer and Operator Integration

## Status

Deferred - architecture decided, implementation not yet scheduled.

## Context

Orion needs a well-defined integration surface for three distinct external audiences:

- **Commercial partners** - third-party software vendors building payload control, mission
  planning, or analytics products on top of Orion. They need a stable, versioned, documented API
  with broad language support and low integration friction.
- **Military / government integrators** - program offices and defence contractors integrating
  Orion into a larger system of systems. They need strong typing, auditability, and clear
  versioning guarantees.
- **Internal team** - the team building the operator-facing ground interface (GCS / ground UI),
  treating Orion as a backend. The UI may be browser-based, native desktop, or both.

The edge network service (ADR-0006) already handles communication with traditional GCS software
using established wire formats (STANAG 4586, MAVLink, CoT/ATAK). That service is for
interoperability with existing tooling. This ADR covers the first-party API Orion exposes for
custom integrations and custom operator interfaces.

### Industry survey

Research into how major UAV platforms expose customer APIs reveals a consistent pattern:

| Platform | Commands / management | Real-time telemetry |
|---|---|---|
| DJI Cloud API | HTTPS REST | MQTT / WebSocket |
| Autel | HTTPS REST | MQTT / WebSocket |
| Parrot | HTTP REST | WebSocket |
| Skydio | REST + webhooks | - |
| MAVSDK (PX4) | gRPC (internally) | gRPC streaming |

The commercial mainstream - the platforms customers already integrate with - uses **REST for
discrete operations and WebSocket for continuous telemetry push**. gRPC appears in
developer-focused robotics tooling (MAVSDK) but not in customer-facing commercial APIs.
Adopting gRPC at the customer boundary would impose tooling and conceptual overhead that the
industry has not required of customers elsewhere.

## Decision

The API service (`orion_api`) exposes two complementary interfaces:

1. **REST (HTTPS + JSON)** for all discrete operations: commands, queries, configuration,
   authentication, and mission management. This is the primary integration surface.
2. **WebSocket (JSON)** for real-time push: continuous telemetry (position, attitude, velocity),
   detection events, system health, and any other high-frequency vehicle state.

```
[Browser GCS / web UI] ──── HTTPS REST + WebSocket ──→ ┌─────────────────┐
[Native desktop app]   ──── HTTPS REST + WebSocket ──→ │   orion_api     │
[Commercial SDK]       ──── HTTPS REST + WebSocket ──→ │   (API service) │
[Gov't integrator]     ──── HTTPS REST + WebSocket ──→ └────────┬────────┘
                                                                 ↕ Zenoh bus (protobuf)
                                              [nav] [control] [perception] [system] services
```

`orion_api` is an ordinary Orion microservice internally - it uses the `Session` API to publish
and subscribe on the Zenoh bus. Externally it is a standard HTTPS server with WebSocket upgrade
support.

### REST surface

- **Protocol**: HTTPS, JSON body.
- **Style**: resource-oriented REST. Resources map to vehicle concepts (`/vehicle/state`,
  `/vehicle/mission`, `/vehicle/payload`, etc.), not to internal microservice boundaries.
- **API documentation**: OpenAPI 3.x spec is the canonical contract. Client SDKs in common
  languages (Python, TypeScript, Go) are generated from the OpenAPI spec.
- **Versioning**: URL-prefixed (`/v1/...`). Breaking changes require a new version prefix.
  A breaking change is: removing or renaming a field, changing a field's type or unit, removing
  an endpoint, or changing an endpoint's behaviour in a way that silently produces wrong results.

### WebSocket surface

- **Protocol**: WebSocket over HTTPS (WSS), JSON messages.
- **Pattern**: client connects to a well-known endpoint (`/v1/stream`), sends a subscription
  message specifying which topics it wants (position, detections, health, etc.), and receives a
  continuous stream of JSON-serialised events.
- **Schema**: each WebSocket message carries a `type` field identifying the event kind, so
  clients can multiplex multiple streams on a single connection.
- **Backpressure**: if a client cannot keep up, the server drops oldest frames rather than
  blocking the Zenoh subscriber. Dropped frames are signalled by a sequence number gap in the
  message schema.

### Separate `api/` schema namespace

JSON schemas and OpenAPI definitions for the public API live in a distinct `schema/api/`
directory, separate from the internal `proto/orion/` definitions. This separation is
load-bearing:

- `schema/api/` is the **public contract** - versioned, stable, subject to a deprecation policy.
- `proto/orion/` is the **internal contract** - free to evolve without affecting external consumers.

The API service translates between the two. No external consumer ever sees an internal
`orion/` field name or type.

### Unified surface for operators and integrators

The same REST + WebSocket interface serves both the operator-facing GCS/UI and programmatic
integrators. There is no separate operator channel. The distinction between human operators and
programmatic clients is enforced by **authentication and authorisation scopes**, not by separate
endpoints.

### Authentication and authorisation

Deferred to the implementation phase. Minimum requirements to establish before implementation:

- TLS for all connections (no plaintext).
- API key or JWT bearer token for machine-to-machine integrators.
- Session-based auth for the web UI.
- Role-based scopes: at minimum `read` (telemetry, state queries) and `command` (sending control
  inputs). Whether `command` requires additional per-operation authorisation (e.g. arming) is
  an operational decision.

### Relationship to the edge network service

| Concern | Service |
|---|---|
| Interoperability with existing GCS tooling (STANAG 4586, MAVLink, CoT) | Edge network service (ADR-0006) |
| First-party API for custom integrations and custom operator UIs | API service (this ADR) |

These services are independent. An operator may use both simultaneously.

### gRPC as a future option

gRPC is not adopted at launch. If a specific integrator class (e.g. high-rate sensor fusion
consumers) demonstrates that REST + WebSocket cannot meet their throughput requirements,
a gRPC endpoint can be added to `orion_api` as a secondary interface without changing the
REST or WebSocket surfaces. The internal protobuf definitions already make this straightforward
to add incrementally.

## Consequences

- `schema/api/` becomes a public, versioned, stable artifact. Changes require the same
  discipline as a public library release.
- OpenAPI spec generation and SDK generation must be part of the build and CI pipeline before
  `v1` is published to any customer.
- The API service is the only permitted path for external access to the Zenoh bus. No external
  consumer gets direct Zenoh access.
- WebSocket connection management (reconnection, subscription state, backpressure) must be
  designed explicitly. Clients that drop and reconnect must be able to re-subscribe without
  missing state.
- The API service will need load testing under realistic telemetry rates and concurrent
  connection counts before any customer commitment.
