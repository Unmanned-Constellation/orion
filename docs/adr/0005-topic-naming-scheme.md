# ADR 0005: Topic naming scheme

## Status
Accepted

## Context
All inter-service messages are published on Zenoh topics. A consistent naming scheme is required
before any service publishes its first message; retrofitting it later would require changing every
topic in every service and any off-board consumer.

Design goals:
- Swarm-ready from day one (multiple vehicles on the same network must be distinguishable).
- Data-centric, not service-centric (subscribers care about the data, not who produces it).
- Wildcard-friendly (off-board consumers should be able to subscribe to useful subsets).

## Decision

**Per-vehicle topics:**
```
orion/{vehicle_id}/{domain}/{topic}
```

**Swarm-wide topics:**
```
orion/swarm/{topic}
```

### Vehicle ID
A human-readable name (`alpha`, `bravo`, `uav-01`) configured at deployment via the
`ORION_VEHICLE_ID` environment variable. Names are assigned by the operator; collision avoidance
is the operator's responsibility, the same as hostname assignment on a network.

A UUID for global uniqueness lives in `MessageHeader::source_id` (stamped per-message by the
transport) but does not appear in the topic path where it would harm readability.

### Data-centric, not service-centric
Topics do not encode which service publishes them. If a service is renamed or split, topics are
unchanged. The `MessageHeader::source_id` field (service name from `SessionConfig`) already
records the publisher identity for debugging purposes.

### Domain vocabulary
Domains group related topics and enable useful wildcard subscriptions
(`orion/*/sensing/**` → all sensing data from all vehicles).

| Domain   | What lives here                              |
|----------|----------------------------------------------|
| `sensing` | Perception outputs - detections, tracked objects |
| `control` | Actuator commands, flight mode               |
| `nav`     | Navigation state - position, velocity, attitude |
| `system`  | Health, diagnostics, service status          |

New domains are added by amending this ADR. Existing domain names must not be renamed (breaking
change for all subscribers of that domain).

### Swarm-wide namespace
Topics that span vehicles (e.g., swarm-level coordination, cross-vehicle rendezvous)
use `orion/swarm/{topic}`. This leaves room for future swarm coordination topics.

Note: the simulation clock is published per-vehicle at
`orion/{vehicle_id}/clock/sim_time` (see ADR-0009), not under `orion/swarm/`. Each
vehicle's `clock_service` drives its own `CoordinatedClock` subscribers independently,
which allows vehicles in a swarm simulation to run at different speeds or phases.

## Consequences

- All `Session::advertise` and `Session::subscribe` call sites must use this scheme.
- Topic names are not validated at runtime; enforcement is by convention and code review.
- Wildcards work as expected: `orion/alpha/**` (all alpha data), `orion/*/sensing/detections`
  (detections from all vehicles), `orion/swarm/**` (all swarm-wide topics).
