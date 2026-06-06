# Stallion Drone

The Stallion is a fixed-wing FPV drone with a V-tail configuration. It is the primary airframe
for Orion flight testing. The full build manual is available at
[STALLION-MANUALV2.pdf](STALLION-MANUALV2.pdf).

---

## Bill of Materials

### Structural

| Item | Qty |
|------|-----|
| 10×800mm Carbon Tube (main spar) | 1 |
| 8×600mm Carbon Tube (secondary spar) | 1 |
| 6×435mm Carbon Tube (wing spar) | 2 |
| 16×430mm Carbon Tube (tail boom) | 1 |
| 4×260mm Carbon Tube (V-tail spar) | 2 |
| 4×250mm Carbon Tube (V-tail spar) | 2 |
| CA Polyester hinge 25×20mm | 14 |
| Small torsion spring with pin | 4 |
| M3 threaded insert (outer Ø5mm, height 5mm) | 12 |
| M3 screw | 12 |

### Consumables & Adhesives

| Item | Qty |
|------|-----|
| Thick + thin CA glue | 20g tube |
| CA activator (optional) | 1 |
| Epoxy glue | 1 |

### Printed & Fabricated Parts

| Item | Qty | Notes |
|------|-----|-------|
| LW-PLA filament | 1 roll | Primary airframe prints — Polymaker Polylite LW-PLA (prefoamed), 0.4mm nozzle |
| PETG-CF | small amount | Avionics bay and high-stress brackets — Sunlu PETG-CF, 0.6mm hardened steel nozzle |
| TPU | small amount | Vibration isolation standoffs between avionics bay and airframe — Sunlu TPU |
| PLA | as needed | Ground jigs, assembly fixtures, non-flight tooling only — Sunlu PLA, 0.4mm nozzle |

### Hardware & Wiring

| Item | Qty |
|------|-----|
| Velcro strap | 2 |
| Servo extension cable | 4 |
| Control horn | 1 |
| Pushrod | 1 |
| 2812 ARM LED nav lights (optional) | 3 |

---

### Electronics

| Component | Part | Notes |
|-----------|------|-------|
| Motors | T-Motor F60 1750KV or T-Motor F90 1300KV | One per wing |
| Propellers | 7×4 / 7×5 / 7×6 — one CW, one CCW | Match to motor KV |
| ESC | 2× BlHeliS 30–40A | One per motor |
| Flight controller | Speedybee F405 Wing or any MAVLink-capable FC | |
| GPS | Matek M10Q or similar with compass | |
| Servos | 4× EMAX ES08 MAII metal gear or equivalent | |
| Battery | 4S max 4S6P 21Ah Li-Ion, or 3S pack | |
| Receiver | Matek R24-D ELRS or similar | |
| FPV camera + VTX | Walksnail Avatar or any digital/analog | |
| FPV goggles | Walksnail Goggles X or matching VTX | |
| Gimbal | Caddx GM3 or equivalent twin model | |

---

### Required Additional Equipment

| Item | Notes |
|------|-------|
| 3D printer | Bambu Lab X1C or other with min. 220×220×200mm build volume |
| RC transmitter | Radiomaster TX16S or other modern RC transmitter |
| Transmitter module | HappyModel ES24TX or other ELRS TX module |

---

## Print Settings

The Stallion is designed for LW-PLA as the primary airframe material, with rigid materials for
high-stress brackets and the avionics bay. All parts are solid bodies filled by the slicer —
no manual hollowing required.

**Printer:** Bambu Lab P1S (enclosed chamber)

**Filament stock and nozzle assignment:**

| Material | Brand | Stock | Nozzle | Use |
|----------|-------|-------|--------|-----|
| LW-PLA (prefoamed) | Polymaker Polylite | 1.6 kg | 0.4 mm brass | Airframe — fuselage, wings, tail |
| PETG-CF | Sunlu | 1 kg | 0.6 mm hardened steel | Avionics bay, high-stress brackets |
| TPU | Sunlu | 1 kg | 0.4 mm brass | Vibration isolation standoffs |
| PLA | Sunlu | 3 kg | 0.4 mm brass | Ground jigs and assembly fixtures only — not flight parts |

**General infill rules:**

| Part type | Pattern | Density |
|-----------|---------|---------|
| Fuselage | Gyroid | 3–6% |
| Wings | Cubic Subdivision or 2D Lattice | 3–6% |

Parts printed from PETG-CF use standard PETG-CF settings for that material; only the LW-PLA
parts require the specialised settings below.

### LW-PLA Filament Types

There are two fundamentally different classes of LW-PLA and they require very different settings:

**Active foaming** (e.g. eSUN ePLA-LW) — the foaming agent activates in the nozzle at high
temperature. Print at ~235 °C with 60% flow to compensate for the volume expansion. No cooling
required — airflow will cause warping before the layer sets.

**Prefoamed** (Polymaker Polylite LW-PLA — this build) — the filament is already expanded at
the factory. Print at ~210 °C with 100% flow and full cooling, using retraction to control
stringing. See the Cura settings below.

### LW-PLA Alternatives

| Material | Use case | Requirement |
|----------|----------|-------------|
| LW-ASA | Outdoor / UV-exposed airframes | Enclosed chamber required |
| LW-PLA HT | Parts near motors, ESCs, or battery | Tune temp/cooling; slower speeds |

Both alternatives are compatible with Stallion geometry. Print settings will need tuning —
especially temperature, flow, and cooling.

### Bambu Lab Presets (PLA Aero / ASA Aero)

Optimised JSON presets for Bambu Studio and Orca Slicer are available from the Flightory
project page. Load both the printer config and the filament config. Presets target the X1
Carbon but adapt easily to other Bambu Lab models.

---

### Cura Settings — Active Foaming LW-PLA

Reference filament: eSUN ePLA-LW. Use as a baseline and tune temperature and flow for other
brands.

#### Quality

| Setting | Value |
|---------|-------|
| Layer height | 0.25 mm |
| Initial layer height | 0.25 mm |
| Line width (all) | 0.4 mm |
| Initial layer line width | 100% |

#### Walls

| Setting | Value |
|---------|-------|
| Wall line count | 1 |
| Wall thickness | 0.4 mm |
| Optimize wall printing order | ✅ |
| Wall ordering | Inside to Outside |
| Print thin walls | ✅ |
| Z seam alignment | Sharpest Corner |
| Seam corner preference | Smart Hiding |

#### Top / Bottom

| Setting | Value |
|---------|-------|
| Top/bottom thickness | 0.75 mm (3 layers) |
| Top/bottom pattern | Lines |
| Extra skin wall count | 1 |
| Skin overlap | 10% |
| Enable ironing | ⬜ |

#### Infill — Gyroid (fuselage)

| Setting | Value |
|---------|-------|
| Infill density | 3% |
| Infill line distance | 13.333 mm |
| Infill pattern | Gyroid |
| Infill overlap | 10% |
| Infill layer thickness | 0.25 mm |

#### Infill — Cubic Subdivision (wings)

| Setting | Value |
|---------|-------|
| Infill density | 3% |
| Infill line distance | 40.0 mm |
| Infill pattern | Cubic Subdivision |
| Cubic subdivision shell | 0.4 mm |
| Infill overlap | 10% |
| Infill layer thickness | 0.25 mm |

#### Material

| Setting | Value |
|---------|-------|
| Print temperature | 235 °C |
| Build plate temperature | 60 °C |
| Flow | **60%** |
| Initial layer flow | 80% |

#### Speed

| Setting | Value |
|---------|-------|
| Print speed | 60 mm/s |
| Wall speed | 30 mm/s |
| Top/bottom speed | 30 mm/s |
| Travel speed | 120 mm/s |
| Initial layer speed | 30 mm/s |

#### Travel & Retraction

| Setting | Value |
|---------|-------|
| Enable retraction | ✅ |
| Retraction distance | **0.0 mm** |
| Retraction speed | 35 mm/s |
| Retraction extra prime | 0.3 mm |
| Retraction minimum travel | 1.5 mm |
| Combing mode | All |

#### Cooling

| Setting | Value |
|---------|-------|
| Enable print cooling | **⬜** |

#### Bed Adhesion

| Setting | Value |
|---------|-------|
| Adhesion type | Brim |
| Brim width | 8.0 mm (20 lines) |
| Brim only on outside | ✅ |

---

### Cura Settings — Prefoamed LW-PLA

Reference filament: Polymaker Polylite LW-PLA. Quality, walls, top/bottom, infill, speed, and
bed adhesion settings are identical to the active foaming profile above. Only the differences
are listed here.

#### Material

| Setting | Value |
|---------|-------|
| Print temperature | **210 °C** |
| Build plate temperature | 60 °C |
| Flow | **100%** |

#### Travel & Retraction

| Setting | Value |
|---------|-------|
| Enable retraction | ✅ |
| Retraction distance | **6.0 mm** |
| Retraction speed | 50 mm/s |
| Retraction extra prime | 2.0 mm |
| Retraction minimum travel | 1.5 mm |
| Maximum retraction count | 100 |
| Combing mode | All |

#### Cooling

| Setting | Value |
|---------|-------|
| Enable print cooling | **✅** |
| Fan speed | 100% |
| Regular fan speed at layer | 10 (0.9 mm height) |
| Minimum layer time | 5.0 s |
| Maximum speed | 15 mm/s |
