# Gimbal Payload Hardware

Hardware specifications for the 3-axis gimbal vision payload. See ADR-0012 through
ADR-0015 for the architectural decisions that derive from these specs.

---

## Camera - Arducam xISP Swift (AR0234)

**Product:** Arducam xISP Swift, Pre-tuned ISP, Global Shutter 2.3MP MIPI Camera
for NVIDIA Jetson Orin NX / Orin Nano

### Image Sensor

| Parameter | Value |
|---|---|
| Sensor | AR0234 |
| Still resolution | 2.3 MP |
| Color filter | Color |
| Shutter type | Global shutter |
| Optical format | 1/2.6" |
| Output format | UYVY |
| Pixel size | 3 µm × 3 µm |
| Active area | 1920(H) × 1200(V) |
| Frame rate | 1920×1200 @ 20 fps / 960×600 @ 80 fps |

### Lens (Default - Wide Angle)

| Parameter | Value |
|---|---|
| Focus type | Manual |
| FOV | 98°(D) / 85°(H) / 69°(V) |
| Lens mount | M12 |
| F-number | F/2.5 |
| EFL | 3.56 mm |
| Default focus range | 1 m – ∞ |
| IR sensitivity | Integral IR-cut filter (visible light only) |

### Electrical & Mechanical

| Parameter | Value |
|---|---|
| Operating voltage | 3.3 V |
| Power (max) | 3.3 V × 350 mA |
| Operating temperature | 0 °C – 70 °C |
| Board size | 34 × 34 mm |

### Software Compatibility

| Parameter | Value |
|---|---|
| Supported OS | JetPack 6.1 (L4T 36.4.0) |
| Supported platform | Jetson Orin NX / Orin Nano |
| Camera controls | Demosaic, gamma, lens shading correction (optional), DPC, AWB, AE, AGC, RGB→YUV, BLC, CCM |
| Driver | V4L2 Linux driver; GStreamer 1.0 |

---

## Lenses - M12 Swappable

The camera board uses an M12 lens mount. Two lenses are in use; the telephoto is
the **governing design case** for all gimbal accuracy and stabilization requirements
(see ADR-0012).

### Wide Angle (default, ships with camera)

| Parameter | Value |
|---|---|
| EFL | 3.56 mm |
| FOV on AR0234 (1/2.6") | 85°(H) / 69°(V) |
| F-number | F/2.5 |
| Mount | M12 |

### Telephoto

| Parameter | Value |
|---|---|
| EFL | 16 mm |
| 35 mm equivalent | 173 mm |
| Optical format | 1/2.5" |
| FOV on 1/2.5" sensor | 29°(D) / 23°(H) / 17°(V) |
| FOV on 1/2.7" sensor | 25°(D) / 22°(H) / 12°(V) |
| FOV on AR0234 (1/2.6") | ~22°(H) / ~15°(V) (interpolated) |
| F-number | F/2.0 |
| Mount | M12 (13 mm holder height) |
| IR sensitivity | Visible light, 650 nm IR filter |

---

## Gimbal Controller - Storm32 BGC

**Role:** Inner-loop FOC stabilization and MAVLink Gimbal Device (see ADR-0013).

> Full datasheet and wiring diagram to be added.

---

## Motors

> Specifications to be added once motors are selected.

---

## Notes

- All FOV values are measured at the sensor's active area. The AR0234 is 1/2.6";
  lens datasheets that specify FOV at 1/2.5" or 1/2.7" require interpolation.
- The M12 mount accepts standard M12 lenses with a 0.5 mm thread pitch. Lens
  holder height must be verified against the physical gimbal clearance envelope
  when new lenses are evaluated.
