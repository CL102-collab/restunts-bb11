# Custom-car adhesion resources

Updated Restunts reads adhesion settings independently for the player and
opponent from their `CAR*.RES` files on every ported platform. They are extra
resources in the normal DSI container; the stock 776-byte `simd` stays unchanged.
Original Stunts ignores these resources. Use identical car resources when
recording and playing back a replay.

## Enable flag: `fand`

The `fand` resource must contain exactly one byte, `01`, to enable adhesion.
Missing, zero, other values or a different length keep the original physics.
There is no startup switch. This is the existing four-character resource ID
introduced for fan adhesion. Its original history does not spell out an
expansion; "fan downforce" is an interpretation of the name.

## Settings: `adhs`

The optional `adhs` (adhesion settings) resource contains exactly six bytes.
Multi-byte values are unsigned and little-endian. Version 1 is:

| Byte offset | Size | Field | Valid values | Default |
| --- | --- | --- | --- | --- |
| 0 | 1 | Version | `1` | `1` |
| 1 | 2 | Maximum attachment distance, world units | `0` through `511` | `12` |
| 3 | 2 | Minimum alignment cosine, Q14 | `0` through `16384` | `8192` (60 degrees) |
| 5 | 1 | Allowed surface mask | Bits 0 through 3 only | `0x0F` |

The default payload is `01 0C 00 00 20 0F`, equivalent to Python
`struct.pack("<BHHB", 1, 12, 8192, 0x0F)`.

An enabled car without `adhs` retains all three defaults, so existing cars keep
their behavior. `adhs` by itself does not enable adhesion. A present resource
with an unsupported version, incorrect length, out-of-range field or unknown
material bit disables adhesion for that car. A zero mask is valid and permits
no surfaces. The 511-unit distance limit keeps the correction within the
engine's signed fixed-point coordinate range.

### Distance

Maximum attachment distance is the inclusive gap from each tyre contact point
to its selected driving surface plane, measured along that plane's normal.
A zero setting requires contact already on the plane. Contacts behind the
plane do not acquire adhesion. Acquisition and retention use the same limit.

The ordinary track collision lookup chooses the surface first. Raising the
distance does not search for arbitrary nearby walls or extend the finite
footprint of a track piece. Contact correction still closes the entire gap;
this setting does not control suction strength or the rate of movement.

### Alignment

Alignment compares the car's up direction with the selected surface normal.
The normalized dot product must be at least the stored Q14 value divided by
16384. This limits their relative angle, allowing vertical and inverted driving
while rejecting contacts where the car faces away from the surface.

For a maximum angle `a` from 0 through 90 degrees, store
`round(cos(a * pi / 180) * 16384)`. For example, 0 degrees is `16384`,
60 degrees is `8192`, and 90 degrees is `0`. The runtime uses this integer
directly. Angle values in degrees belong in authoring tools and documentation,
not in the resource payload.

### Materials

| Bit | Value | Driving surface |
| --- | --- | --- |
| 0 | `0x01` | Paved |
| 1 | `0x02` | Dirt |
| 2 | `0x04` | Ice |
| 3 | `0x08` | Grass |

Combine values with bitwise OR: `0x01` permits paved only; `0x03` permits paved
and dirt; `0x0F` permits all four. The material is the selected driving surface,
not a model's paint or rendering material. Water is never eligible.

## Tesla Roadster generation

`tools/scripts/tesla_roadster_physics.py` declares the Tesla's distance,
maximum angle and surface-mask constants and generates `adhs` separately from
`simd`. Its default angle is converted to Q14 while generating the resource.
`TERR.json` records both degrees and the stored cosine, and the car generator
validates the version, length, bounds and encoded tuning before writing files.

```sh
python3 tools/scripts/generate-tesla-roadster.py --install stunts
python3 tools/scripts/generate-tesla-roadster.py --check --install stunts
```
