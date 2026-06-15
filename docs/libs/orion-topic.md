# orion_topic - Canonical Topic Name Construction

Type-safe helpers for building Zenoh topic strings that follow the
`orion/{vehicle_id}/{domain}/{name}` hierarchy defined in ADR-0005.

## Overview

`orion_topic` is a **STATIC** library with no dependencies beyond the C++ standard
library. Every microservice that constructs a publish or subscribe topic should use
these helpers rather than building raw strings inline, so that any future topic
renames propagate from a single location.

Each free function validates `vehicle_id` and throws `std::invalid_argument` if it
is empty or contains a `/`. Wildcard functions (`all*`) return `constexpr` string
views and do no validation.

See [ADR-0005](../adr/0005-topic-naming-scheme.md) for the topic naming rationale.

---

## `orion::topic::sensing`

```cpp
#include "orion/topic/topic.hpp"
namespace orion::topic::sensing
```

| Function | Returns | Topic string |
|---|---|---|
| `detections(vehicle_id)` | `std::string` | `orion/{vehicle_id}/sensing/detections` |
| `allDetections()` | `constexpr std::string_view` | `orion/*/sensing/detections` |

```cpp
// publish
auto topic = orion::topic::sensing::detections(ctx.vehicle_id);
auto pub   = session.advertise<orion::v1::DetectionFrame>(topic);

// subscribe to all vehicles
auto sub = session.subscribe<orion::v1::DetectionFrame>(
    orion::topic::sensing::allDetections(), callback);
```

---

## `orion::topic::clock`

```cpp
#include "orion/topic/topic.hpp"
namespace orion::topic::clock
```

| Function | Returns | Topic string |
|---|---|---|
| `simTime(vehicle_id)` | `std::string` | `orion/{vehicle_id}/clock/sim_time` |

```cpp
auto topic = orion::topic::clock::simTime(vehicle_id);
auto pub   = session.advertise<orion::v1::SimTimeUpdate>(topic);
```

---

## Error handling

Both `detections` and `simTime` throw `std::invalid_argument` when `vehicle_id` is
empty or contains `/`. The invariant is enforced by a shared internal `validate()`
helper; all topic builders in this library call it.

```cpp
// throws: orion::topic: vehicle_id must not be empty
orion::topic::sensing::detections("");

// throws: orion::topic: vehicle_id must not contain '/'
orion::topic::sensing::detections("alpha/bravo");
```

---

## CMake integration

```cmake
target_link_libraries(my_service PRIVATE orion_topic)
```

`orion_topic` is a STATIC library with no transitive dependencies.
