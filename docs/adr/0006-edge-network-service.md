# ADR 0006: Edge Network Service as Multi-Protocol Gateway

## Status

Deferred - architecture decided, implementation not yet scheduled.

## Context

Orion vehicles need to communicate with entities outside the local Zenoh bus: ground control
stations (GCS), swarm peers, and off-board intelligence consumers. These external systems speak
a variety of wire formats - STANAG 4586, MAVLink, Cursor on Target (CoT/ATAK), STANAG 4676, and
potentially others. None of them speak Zenoh or protobuf natively.

Implementing per-format translation inside individual microservices (a `stanag_bridge`, a
`mavlink_bridge`, etc.) would multiply boundary-crossing services and distribute external protocol
logic across the codebase. A single service already owns the external network interface - the
natural consolidation point is there.

## Decision

A future **edge network service** will act as the multi-protocol gateway between the internal
Zenoh bus and all external communication endpoints. It is the only service that speaks any
external wire format. All other microservices are isolated from external protocol concerns.

The internal Zenoh/protobuf messaging architecture (ADR-0004) is unchanged. The edge service is
an ordinary Orion microservice from the bus's perspective: it uses the `Session` API to publish
and subscribe exactly like any other service.

```
[GCS CUCS]     ←── STANAG 4586 UDP ──→ ┌──────────────────────────────────────┐
[ATAK client]  ←── CoT / XML ────────→ │          edge network service        │
[MAVLink GCS]  ←── MAVLink v2 ───────→ │                                      │
                                        │       Protocol Handler Pool          │
                                        └──────────────────┬───────────────────┘
                                                           ↕ Zenoh bus (protobuf)
                                      [nav] [control] [perception] [system] services

[Swarm peer A] ←── Zenoh / WireGuard ──→ Zenoh Router (middleware service)
[Swarm peer B] ←── Zenoh / WireGuard ──→ Zenoh Router (middleware service)
```

### Protocol handler architecture

The edge service hosts a pool of **protocol handlers**, one per external format. Each handler is
responsible for:

1. **Inbound** - receiving external messages, parsing the wire format, and publishing the
   equivalent internal proto message on the Zenoh bus.
2. **Outbound** - subscribing to relevant Zenoh topics, and serialising + transmitting the
   internal proto message in the external wire format.

Handlers are self-contained translation units. Adding support for a new external format means
adding a new handler; existing handlers and all other microservices are unaffected.

### Planned protocol handlers

| Handler | External format | Primary use case |
|---------|----------------|-----------------|
| STANAG 4586 | Packed binary UDP (DLI) | NATO-interoperable GCS command and control |
| STANAG 4676 | ISR track reports | Off-board intelligence consumers, sensor fusion |
| MAVLink v2 | MAVLink binary UDP/serial | Commercial GCS, ArduPilot/PX4-ecosystem tools |
| CoT / ATAK | XML over UDP/TCP | ATAK-based situational awareness clients |

This list is not exhaustive. Additional handlers may be added without architectural changes.

### Bidirectional contract

Each handler must implement both directions. A handler that only consumes or only produces is
incomplete. This ensures that any external system that sends a command receives a conformant
response or acknowledgement in the same format it used.

### Internal proto field alignment

Proto message fields for the `nav`, `control`, and `system` domains must be semantically aligned
with the union of requirements across all planned handlers. Where standards disagree on units or
coordinate conventions, Orion's internal representation takes precedence and each handler is
responsible for converting at the boundary. Agreed internal conventions:

- Position: WGS-84 geodetic (latitude/longitude in radians, altitude in metres MSL).
- Velocity: NED frame, metres per second.
- Attitude: roll/pitch/yaw in radians, NED frame.
- Timestamps: nanoseconds since Unix epoch internally; each handler converts to its format's
  native time representation at the boundary.

### STANAG 4586 handler

**LOI target: 5** - full control including launch and recovery. LOI 5 is the ceiling; LOI 2
(telemetry only) will be implemented first as a vertical slice, with each subsequent LOI level
shipped incrementally.

**Known STANAG 4586 compliant GCS software:**

| GCS | Type | Notes |
|-----|------|-------|
| Kutta Technologies UGCS | Commercial | Most cited STANAG 4586 certified solution; MOSA compliant |
| Neptus (LSTS) | Open-source | Only known open-source GCS with STANAG 4586 support |
| UAS Europe SkyView | Commercial | STANAG 4586 based, NATO-oriented |
| Asseco GCS | Commercial | MAVLink + STANAG 4586 hybrid, military sector |

QGroundControl, Mission Planner, and ATAK are **not** STANAG 4586 GCS implementations.
QGC and Mission Planner speak MAVLink; ATAK speaks CoT. They are covered by separate handlers.

**Additional prerequisites before implementation:**

- **Document access** - STANAG 4586 is NATO RESTRICTED. The canonical document is required for
  exact message field layouts; open-source implementations (Neptus, python-stanag-4586-EDA-v1)
  may inform development but must not substitute for the authoritative specification.
- **GCS target** - Neptus is the recommended starting point for protocol-level integration
  testing given it is open-source. Kutta UGCS should be the target for formal compliance
  validation; contact is required to evaluate licensing and export restrictions.
- **Test harness** - no public NATO conformance test tool exists. Protocol-level testing will
  use the Python STANAG 4586 library (`python-stanag-4586-EDA-v1`) and Neptus before
  progressing to a commercial GCS.

### Swarm peer communication

Swarm peers (other vehicles running Orion) do **not** communicate through the edge network
service. They are Zenoh peers.

The existing Zenoh Router in the middleware service already bridges the local shared-memory bus
to the network transport for off-board consumers. Swarm peers extend this naturally: each vehicle's
Zenoh Router forms a mesh with peer routers over the tactical network link.

**Zenoh Router endpoints between swarm peers must use UDP unicast.** TCP is not acceptable for
inter-vehicle links because:

- TCP interprets radio-induced packet loss as network congestion and throttles its send window,
  degrading throughput on links where loss is due to link quality rather than capacity.
- Head-of-line blocking stalls all in-flight messages behind a single lost packet. For real-time
  telemetry, a stale dropped frame is worthless - the next frame should arrive unimpeded.
- Many tactical radio systems implement their own link-layer reliability (ARQ, FEC). TCP
  retransmission on top of radio ARQ produces double retransmission and compounds latency.

Zenoh Router configuration for swarm peering:

```json
{
  "listen":  { "endpoints": ["udp/0.0.0.0:7447"] },
  "connect": { "endpoints": ["udp/<peer-wireguard-ip>:7447"] }
}
```

**Zenoh QoS** is set per publisher and applies independently of the UDP transport:

| Topic class | Reliability | Rationale |
|---|---|---|
| Telemetry (position, detections, health) | `BestEffort` | Stale frames are worthless; drop and move on |
| Commands, mission uploads | `Reliable` | Zenoh-level retransmission without TCP congestion control |

**WireGuard** provides network-layer encryption and mutual authentication. Each vehicle holds a
WireGuard keypair; only keypairs in the swarm's allowlist can join the mesh. WireGuard operates
transparently below Zenoh - the router sees a regular UDP socket over the WireGuard interface.

**WireGuard was chosen over alternatives (IPsec, custom TLS, application-layer encryption)
because:**
- Minimal attack surface (small, auditable codebase).
- Kernel-space implementation means negligible CPU overhead on the Jetson.
- Public-key per vehicle is a natural fit for swarm member identity.
- Works transparently over any IP-capable link - WiFi, commercial radio, or custom hardware.

**Radio hardware note:** Orion's swarm link layer is designed to run over a custom radio being
developed in parallel with the software stack. The radio will expose a standard IP interface;
Zenoh and WireGuard are agnostic to what is below the IP layer. No changes to the Zenoh
configuration or application code are required when the custom radio replaces interim hardware.

The existing topic scheme (ADR-0005) handles vehicle disambiguation with no additional mechanism:

- `orion/{vehicle_id}/...` - per-vehicle data, namespaced by vehicle ID.
- `orion/swarm/...` - swarm-wide coordination topics.

Any microservice on any vehicle subscribes to another vehicle's topics using the identical
`Session::subscribe` call it uses for local topics. The transport abstraction is unaware of
whether a publisher is local or remote.

### Swarm presence - neighbor discovery and minimal state

Every vehicle publishes a `Presence` message on `orion/swarm/presence` at a fixed interval.
Every vehicle subscribes to `orion/swarm/presence`. This single topic is the complete mechanism
for:

1. **Discovery** - a vehicle learns a neighbor exists when it first receives its `Presence`.
2. **Liveness** - a vehicle is considered lost after a defined number of consecutive missed
   heartbeats. No separate health check is needed.
3. **Minimal neighbor state** - `Presence` carries the smallest set of information each platform
   needs about its neighbors to make safe, coordinated decisions. Nothing more is relayed.

**`Presence` is the only swarm topic that is relayed across intermediate vehicles.** In a chain
topology (A - B - C), Router B relays `orion/swarm/presence` between A and C so that A knows
C exists and C knows A exists. High-bandwidth per-vehicle topics (`sensing`, `control`, full
`nav` streams) are never relayed - they remain local to direct peers. Zenoh's interest-based
routing ensures B only relays topics that the far side has actually subscribed to; as long as
no service subscribes to `orion/charlie/**` from Platform A, that traffic never crosses B.

**Candidate `Presence` fields** (exact proto definition deferred to implementation):

| Field | Rationale |
|---|---|
| `vehicle_id` | Identifies the publisher; required for all subsequent topic subscriptions |
| `captured_at_ns` | Heartbeat timestamp; drives liveness timeout logic |
| `latitude_rad`, `longitude_rad`, `altitude_m` | Position in WGS-84; minimum required for flight deconfliction |
| `velocity_ned_ms` | Current velocity vector; improves deconfliction prediction |
| `heading_rad` | Current heading |
| `vehicle_state` | Enum: GROUND, TAKEOFF, FLYING, LOITER, RTL, LANDING, FAILED |
| `health` | Enum: NOMINAL, DEGRADED, CRITICAL |

The field list above is a starting point. The canonical definition must be agreed before any
swarm-capable service is implemented; adding fields later is non-breaking but removing or
renaming them is a breaking change for all swarm members.

**Heartbeat rate and liveness timeout:** specific values are deferred. As a baseline, 1 Hz
publish rate with a 3-missed-heartbeat timeout (3 s) is a reasonable starting point subject
to radio link characterisation.

**Testing:** Prior to custom radio hardware availability, inter-vehicle link conditions
(packet loss, latency, jitter) should be simulated using Linux `tc netem` between two Zenoh
Router instances. UDP vs TCP performance under realistic loss rates (1–10%) must be characterised
before any radio integration milestone.

## Consequences

- The edge network service does not yet exist. It requires its own design phase and ADR covering
  its internal structure, deployment topology (one per vehicle, one per swarm?), and failure
  modes before any handler is implemented.
- All external wire format logic (non-Orion systems) is confined to the edge service. No other
  microservice may import or parse an external protocol format.
- Swarm peer communication bypasses the edge service entirely. WireGuard configuration and
  Zenoh Router peering are operational concerns, not application code.
- Proto message authors for `nav`, `control`, and `system` domains must apply the internal
  conventions above before finalising field definitions, even before the edge service exists.
  Retrofitting coordinate frames or unit conventions is a breaking change for all subscribers.
- Request-reply patterns (Zenoh queryables, deferred in ADR-0004) may be required for
  command/acknowledgement exchanges in some handlers. That dependency is re-evaluated when each
  handler is scoped.
- LOI 5 for STANAG 4586 is the target ceiling. It must be reached incrementally (LOI 2 → 3 → 4
  → 5); attempting to implement LOI 5 in one phase is not viable given the scope of the message
  set.
