# ADR 0012: Gimbal Signal Routing - FPC Through Hollow-Shaft Motors, Software Yaw Limit

## Status
Accepted

## Context
The vision payload is an Arducam AR0234 global-shutter MIPI CSI camera (1920×1200, 2.3 MP)
mounted on a custom 3-axis direct-drive gimbal. The camera uses a MIPI CSI interface to
the Jetson Orin Nano. An M12 lens mount allows swapping between a wide-angle lens
(85° HFOV) and a telephoto lens (22° HFOV); the telephoto case is the governing design
requirement.

MIPI CSI is a high-speed differential serial bus. Its signal integrity degrades sharply
through sliding-contact slip rings due to contact resistance variation, impedance
discontinuities, and mechanical noise introduced by the rotating interface. High-bandwidth
USB 3.0, which some camera configurations use instead of CSI, has the same limitation.
Slip rings that are rated for these signal classes are expensive, bulky, and have limited
service lives - unsuitable for a custom UAV payload.

The alternative to a slip ring is continuous rotation with a managed cable. Unmanaged
cables snagged by unrestricted rotation will sever.

## Decision

Route all payload cables - MIPI CSI flex and power - through the hollow bore of each
BLDC motor shaft as flexible printed circuit (FPC) cables. Constrain the Yaw axis to
**±270° of travel in software** to prevent cable wind-up and severing.

### Cable routing

Each motor shaft is hollow. The FPC cables are routed axially through the center of the
Yaw, Roll, and Pitch shafts before exiting to the payload. The cables are selected for
repeated flexure life (rated flex-cycle FPC, not standard ribbon cable). The routing path
is concentric with each rotation axis so cable flex is minimized and predictable.

### Yaw software limit

The gimbal controller firmware enforces a ±270° Yaw travel limit. Commanded positions
beyond this range are clamped and the requesting system is notified. The limit is set
conservatively below the mechanical wind-up limit to provide a safety margin against
encoder drift or commanded overshoot at slew rate.

The ±270° limit covers the full operational envelope of a fixed-wing or VTOL platform.
Full nadir surveillance does not require continuous yaw rotation: the aircraft provides
gross heading changes while the gimbal provides fine stabilization and pointing within its
travel range.

### Lens swappability

The M12 mount is preserved as an external interface. Swapping between the wide-angle
(85° HFOV) and telephoto (22° HFOV) lenses does not require re-routing cables or
modifying the gimbal structure. The telephoto configuration (22° HFOV) is the governing
case for all pointing accuracy and stabilization requirements in subsequent ADRs.

## Consequences

- Infinite continuous yaw rotation is not supported. Mission profiles that require
  sustained yaw travel beyond ±270° (e.g. persistent orbit tracking without heading
  change) must account for this limit and command aircraft heading changes to unwrap
  the gimbal.
- Yaw position must be tracked continuously by the gimbal controller. A power cycle
  that loses encoder position must re-home before accepting pointing commands.
- FPC selection and routing is a mechanical design constraint: the flex cables must be
  rated for the expected cycle count at the bend radii imposed by the shaft geometry.
- No slip ring is required, eliminating a cost, weight, and reliability concern from
  the BOM.
- The same routing architecture applies if the camera interface is later changed
  (e.g. USB 3.0 or GMSL), provided the replacement cable can be sourced in FPC form
  factor at the required bandwidth.
