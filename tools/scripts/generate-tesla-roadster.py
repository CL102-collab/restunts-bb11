#!/usr/bin/env python3
"""Build the TERR custom car, with a hard decompressed DIA3 dashboard budget.

The four raw resources work in original Stunts and Restunts. No executable or
car registry changes are needed. Run --check to verify checked-in assets, or
--install DIRECTORY to also copy the generated car into a playable game folder.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct

from tesla_roadster_dashboard import (INSTRUMENT_RECT, WHEEL_TURN_DEGREES,
                                     build_dashboards, prepared_stda_size)
from tesla_roadster_model import (BODY_SCALE, SHOWROOM_SCALE, SHOWROOM_Y_OFFSET,
                                  WHEEL_RADIUS, WHEEL_SIZE_MULTIPLIER, build_models)
from tesla_roadster_physics import (ADHESION_MAX_ALIGNMENT_DEGREES,
                                    ADHESION_MAX_DISTANCE,
                                    ADHESION_MIN_ALIGNMENT_Q14,
                                    ADHESION_SURFACE_MASK, ADHESION_VERSION,
                                    build_car)


ROOT = Path(__file__).resolve().parents[2]
CAR_ID = "TERR"
REFERENCE_IMAGES = [
    {"file": "1000004851.jpg", "view": "Front, recessed fascia and trapezoid hood"},
    {"file": "1000004850.jpg", "view": "Rocket demonstration, profile and aero disc wheels"},
    {"file": "1000004849.jpg", "view": "Front three-quarter, grey facets and continuous canopy"},
    {"file": "1000004848.jpg", "view": "Rear three-quarter, light recess and four-nozzle diffuser"},
    {"file": "1000004856.jpg", "view": "Elevated front, curved nose and hood taper; doors are reference-only"},
    {"file": "1000004855.jpg", "view": "Elevated rear, narrow glass, broad haunches, spoiler and bumper layers"},
    {"file": "1000004854.jpg", "view": "Demonstration profile, door cutlines and dark aero wheels"},
]
SOURCES = [
    "https://www.tesla.com/roadster",
    "https://electrek.co/2026/10/15/tesla-unveils-cyberfied-new-roadster-kickflip/",
    "https://www.caranddriver.com/news/a74145531/tesla-roadster-revealed/",
    "https://www.topgear.com/car-news/electric/tesla-roadster-has-finally-landed-thrusters-0-60mph-059s-and-flight",
    "https://www.caranddriver.com/photos/a74152579/tesla-roadster-revealed-exterior-gallery/",
    "https://electrek.co/wp-content/uploads/sites/3/2026/10/roadster-03-carousel-slide-1-tesla-roadster-2026.jpg",
    "https://electrek.co/wp-content/uploads/sites/3/2026/10/roadster-02-pilot-tesla-roadster-2026.jpg",
]


def resources(data):
    """Read and validate a raw Stunts resource container."""
    length, count = struct.unpack_from("<IH", data)
    if length != len(data) or count == 0 or 6 + count * 8 > length:
        raise ValueError("Invalid resource container header")
    start = 6 + count * 8
    offsets = struct.unpack_from("<" + "I" * count, data, 6 + count * 4)
    if len(set(offsets)) != count or min(offsets) != 0:
        raise ValueError("Duplicate offsets or missing first resource")
    result = {}
    for index, offset in enumerate(offsets):
        name = data[6 + index * 4:10 + index * 4].decode("ascii")
        end = min([value for value in offsets if value > offset] + [length - start])
        if name in result or offset >= end or start + end > length:
            raise ValueError("Invalid resource directory")
        result[name] = data[start + offset:start + end]
    return result


def validate_bitmap(data):
    width, height = struct.unpack_from("<HH", data)
    if not width or not height or width * height > 65535:
        raise ValueError("Bitmap dimensions exceed the legacy shape limit")
    if len(data) != 16 + width * height:
        raise ValueError("Bitmap payload does not match its dimensions")
    # These assets are row-major, raw VGA pixels. No unflip workspace needed.
    if data[14] & 0xF0:
        raise ValueError("Unexpected bitmap plane transform")


def validate_model(data, name):
    vertices, primitives, paints, reserved = data[:4]
    if not vertices or not primitives or paints != 7 or reserved:
        raise ValueError(f"Invalid {name} shape header")
    points = [struct.unpack_from("<hhh", data, 4 + index * 6)
              for index in range(vertices)]
    cursor = 4 + vertices * 6 + primitives * 8
    wheels = []
    for _ in range(primitives):
        kind, flags = data[cursor:cursor + 2]
        count = kind if 1 <= kind <= 10 else {11: 2, 12: 6}.get(kind)
        if count is None or flags & ~3:
            raise ValueError(f"Unsupported primitive in {name}")
        material = data[cursor + 2:cursor + 2 + paints]
        indices = data[cursor + 2 + paints:cursor + 2 + paints + count]
        if len(indices) != count or max(indices) >= vertices or max(material) > 126:
            raise ValueError(f"Invalid indices/material in {name}")
        if kind == 12:
            wheels.append(list(indices))
        cursor += 2 + paints + count
    if len(data) - cursor < 1 or any(data[cursor:]):
        raise ValueError(f"Missing shape terminator in {name}")
    if name.startswith("car"):
        if wheels[:4] != [list(range(first, first + 6)) for first in (8, 14, 20, 26)]:
            raise ValueError(f"Wheel controls do not occupy fixed slots in {name}")
        # Native wheel primitives also draw the four stationary rear nozzle
        # discs. They must never alias the animated axle control slots.
        if len(wheels) != 8 or any(min(w) < 32 or len(set(w)) != 3 or
                                  w[0] != w[3] or w[1] != w[5] or w[2] != w[4]
                                  for w in wheels[4:]):
            raise ValueError(f"Invalid stationary nozzle disc controls in {name}")
        bounds = points[:8]
        for point in points[8:]:
            if any(point[axis] < min(p[axis] for p in bounds) or
                   point[axis] > max(p[axis] for p in bounds) for axis in range(3)):
                raise ValueError(f"Vertex outside bounding box in {name}")
    return {"vertices": vertices, "primitives": primitives, "paint_jobs": paints}


def validate(outputs, repository):
    car = resources(outputs["CARTERR.RES"])
    if len(car["simd"]) != 776 or car["simd"][0] != 1:
        raise ValueError("Expected a 776-byte single-speed SIMD")
    if car.get("fand") != b"\x01":
        raise ValueError("Missing fan surface adhesion capability")
    adhesion = car.get("adhs", b"")
    if len(adhesion) != 6:
        raise ValueError("Expected a six-byte adhesion settings resource")
    version, distance, alignment, surface_mask = struct.unpack("<BHHB", adhesion)
    if version != 1 or distance > 511 or alignment > 16384 or surface_mask & ~0x0F:
        raise ValueError("Invalid adhesion settings")
    if (version, distance, alignment, surface_mask) != (
            ADHESION_VERSION, ADHESION_MAX_DISTANCE,
            ADHESION_MIN_ALIGNMENT_Q14, ADHESION_SURFACE_MASK):
        raise ValueError("Adhesion resource disagrees with Tesla tuning")
    for name in ("gnam", "gsna", "edes"):
        if not car[name].endswith(b"\0"):
            raise ValueError("Unterminated car text")
    models = resources(outputs["STTERR.3SH"])
    if set(models) != {"car0", "car1", "car2", "exp0", "exp1", "exp2", "exp3"}:
        raise ValueError("Missing car or debris shape")
    model_metadata = {name: validate_model(data, name) for name, data in models.items()}
    wheel_points = struct.unpack_from("<12h", car["simd"], 0xD2)
    for name in ("car0", "car1", "car2"):
        scale = SHOWROOM_SCALE if name == "car0" else 1
        offset = (0, SHOWROOM_Y_OFFSET, 0) if name == "car0" else (0, 0, 0)
        for wheel in range(4):
            first = 8 + wheel * 6
            inner = struct.unpack_from("<3h", models[name], 4 + first * 6)
            outer = struct.unpack_from("<3h", models[name], 4 + (first + 3) * 6)
            if any((inner[axis] + outer[axis] - 2 * offset[axis]) * 32 !=
                   wheel_points[wheel * 3 + axis] * scale for axis in range(3)):
                raise ValueError(f"Visual and physical wheel centres disagree in {name}")
            radial = struct.unpack_from("<3h", models[name], 4 + (first + 1) * 6)
            if radial[1] - inner[1] != WHEEL_RADIUS * scale:
                raise ValueError(f"Incorrect wheel radius in {name}")
        points = [struct.unpack_from("<3h", models[name], 4 + index * 6)
                  for index in range(models[name][0])]
        floor = offset[1] - WHEEL_RADIUS * scale
        if any(point[1] < floor for point in points[32:]):
            raise ValueError(f"Bodywork protrudes below the tyre contact plane in {name}")
        model_metadata[name]["visible_dimensions"] = {
            "width": max(p[0] for p in points[8:]) - min(p[0] for p in points[8:]),
            "height_including_tyres": max(p[1] for p in points[8:]) - floor,
            "length": max(p[2] for p in points[8:]) - min(p[2] for p in points[8:]),
        }
    dashboards = [resources(outputs[f"STD{part}TERR.VSH"]) for part in "AB"]
    required = [set("dash whl1 whl2 whl3 ins1 ins2 ins3 inm1 inm3 gbox".split()),
                {"dot ", "dota", "dot1", "dot2", "gnob", "gnab"} |
                {f"dig{number}" for number in range(10)}]
    for dashboard, expected in zip(dashboards, required):
        if not expected <= dashboard.keys():
            raise ValueError("Missing dashboard instrument resources")
        for data in dashboard.values():
            validate_bitmap(data)
    budget = {}
    for part in "AB":
        reference = repository / f"tools/scripts/cars/STD{part}DIA3.VSH"
        limit = len(reference.read_bytes())
        actual = len(outputs[f"STD{part}TERR.VSH"])
        if actual > limit:
            raise ValueError(f"STD{part} exceeds DIA3's decompressed byte budget")
        budget[part] = {"TERR": actual, "DIA3": limit}
    prepared = {
        "TERR": prepared_stda_size(outputs["STDATERR.VSH"]) +
                (len(outputs["STDBTERR.VSH"]) + 15) // 16 * 16,
        "DIA3": prepared_stda_size(
            (repository / "tools/scripts/cars/STDADIA3.VSH").read_bytes()) +
                (len((repository / "tools/scripts/cars/STDBDIA3.VSH").read_bytes()) + 15) // 16 * 16,
    }
    if prepared["TERR"] > prepared["DIA3"]:
        raise ValueError("Prepared dashboard exceeds DIA3")
    for name in ("ins2", "gbox"):
        width, height = struct.unpack_from("<HH", dashboards[0][name])
        if width > 188 or height > 75:
            raise ValueError("Dashboard scratch sprite exceeds DIA3")
    return model_metadata, budget, prepared


def generate(repository):
    primary, secondary, patches = build_dashboards(repository)
    outputs = {"CARTERR.RES": build_car(repository, patches),
               "STTERR.3SH": build_models(),
               "STDATERR.VSH": primary, "STDBTERR.VSH": secondary}
    model_metadata, budget, prepared = validate(outputs, repository)
    adhesion_surfaces = [name for bit, name in enumerate(
        ("paved", "dirt", "ice", "grass"))
        if ADHESION_SURFACE_MASK & (1 << bit)]
    manifest = {
        "car_id": CAR_ID,
        "name": "Tesla Roadster Rocket",
        "reference_event": "2026-10-15 Tesla Roadster demonstration",
        "default_paint": "Metallic grey, black aero and all-black glazing",
        "geometry_reference": "Seven user-supplied photographs; black glazing follows the user's colour instruction.",
        "geometry_revision": "Stock-relative scale in showroom and racing, smaller wheels, and circular triangulated wheel arches in all detail levels.",
        "doors": "Closed in every game model; open-door photographs supply body and panel reference only.",
        "reference_images": REFERENCE_IMAGES,
        "announced_targets": {"zero_to_60_mph_seconds_under": 0.59,
                              "top_speed_mph_over": 230, "range_miles_over": 300},
        "simulation": "Single-speed EV with a launch torque pulse; no separate flight/thruster controls.",
        "capabilities": {"fand": "Automatic fan adhesion to contacted driving surfaces in updated Restunts; one-byte enable flag, with optional adhs settings."},
        "adhesion": {"resource": "adhs", "version": ADHESION_VERSION,
                     "max_distance_world_units": ADHESION_MAX_DISTANCE,
                     "max_alignment_degrees": ADHESION_MAX_ALIGNMENT_DEGREES,
                     "min_alignment_cosine_q14": ADHESION_MIN_ALIGNMENT_Q14,
                     "surface_mask": ADHESION_SURFACE_MASK,
                     "surfaces": adhesion_surfaces},
        "geometry_dimensions": "Calibrated against stock Corvette/NSX using reported 2026 width and wheelbase; length and height remain photo-based estimates. See TERR-sizing.md.",
        "sizing": {"reported_width_mm_approx": 2000, "reported_wheelbase_mm": 2825,
                   "confirmed_length_mm": None, "confirmed_height_mm": None,
                   "body_axis_multipliers": BODY_SCALE,
                   "wheel_size_multiplier_requested": WHEEL_SIZE_MULTIPLIER,
                   "wheel_radius_game_units": WHEEL_RADIUS,
                   "quantization": "25% smaller wheel target is rounded to whole game units; identical six-unit radii are used in both racing LODs and at 20x in the showroom.",
                   "sources": ["https://www.youtube.com/watch?v=JdkIOhFmbKI",
                               "https://electronmotion.com/2026/10/16/tesla-roadster-reviews/",
                               "https://evwire.com/p/tesla-roadster-deep-dive-videos-hagerty-top-gear-motortrend"]},
        "showroom": {"scale": SHOWROOM_SCALE, "vertical_offset": SHOWROOM_Y_OFFSET,
                     "reference": "Same calibrated car as racing, at the stock 20x display scale."},
        "dashboard_revision": "Larger photo-based rotating wheel with padded silver-edged grips, rollers, switches and paddles; contoured suede dashboard with masked steering-column recess, vents and carbon trim; live seven-segment speed and motor gauge on the rectangular centre screen.",
        "dashboard_reference_images": ["1000005072.jpg", "1000005071.jpg",
                                       "1000005951.jpg", "1000005952.jpg", "1000005953.jpg"],
        "dashboard_digit_reference": "https://en.wikipedia.org/wiki/Seven-segment_display#Characters",
        "dashboard_steering_degrees": [-WHEEL_TURN_DEGREES, 0, WHEEL_TURN_DEGREES],
        "dashboard_decompressed_bytes": budget,
        "dashboard_game_prepared_bytes": prepared,
        "models": model_metadata,
        "sources": SOURCES,
        "files": {name: {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
                  for name, data in outputs.items()},
    }
    outputs["TERR.json"] = (json.dumps(manifest, indent=2) + "\n").encode()
    outputs["READTERR.TXT"] = (f"""TESLA ROADSTER ROCKET - TERR

Select Tesla Roadster Rocket in the game's car menu. The default paint is the
grey body shown in the supplied photographs. Other paints keep the same black
aero and all-black glazing. Original low polygon geometry and digital cockpit.

INSTALL
Copy CARTERR.RES, STTERR.3SH, STDATERR.VSH and STDBTERR.VSH to your Stunts game
data directory. They are also installed in this repository's stunts/ directory
by the creation command. No existing car is replaced.

Fan adhesion activates automatically in updated Restunts when this car is
selected. The one-byte fand=1 resource enables it; no startup option or launcher
control is needed. A separate version-{ADHESION_VERSION} adhs resource sets the maximum contact
distance to {ADHESION_MAX_DISTANCE} world units, maximum alignment angle to {ADHESION_MAX_ALIGNMENT_DEGREES} degrees (Q14 cosine
{ADHESION_MIN_ALIGNMENT_Q14}), and allowed material mask to 0x{ADHESION_SURFACE_MASK:02X} ({', '.join(adhesion_surfaces) or 'none'}).
Distance is measured from each tyre contact to its selected surface plane;
this does not search for arbitrary nearby surfaces. No water adhesion.
Cars without fand=1 keep the original physics. Missing adhs uses engine defaults
(12 units, 60 degrees, all four materials). Malformed or unsupported adhs
disables adhesion for that car.
Original Stunts ignores both extra resources. One fixed forward ratio
represents the EV drivetrain. No gear changes are necessary.
Surface adhesion holds the car against supported drivable surfaces, including
inverted and vertical contacts at a standstill. It is not a free-flight mode.
Use the same car resources when recording and playing back a replay.
See docs/car-adhesion.md in the repository for the settings format and bounds.

PERFORMANCE
Reported Rocket targets: under 0.59 s to 60 mph, above 230 mph, above 300 miles
range; rear twin-motor drive, reported 1000 kW. These are announced targets.
The demonstration's roughly 0.81-second run is distinct from that claim.
Native game straight-line measurement: 0-60 mph in 0.55 s at 20 Hz, maximum
236.97 mph. The low-speed torque peak approximates compressed-air launch thrust.
Mass, gearing, grip, torque bytes and dimensions are game tuning estimates.
Battery capacity and exact Rocket mass were not confirmed; no old 2017 specs
are substituted. Range, gas capacity, flight and active aero are not simulated.

DASHBOARD MEMORY
Raw/extracted: STDA {budget['A']['TERR']:,} + STDB {budget['B']['TERR']:,} = {sum(part['TERR'] for part in budget.values()):,} bytes.
DIA3 reference: {budget['A']['DIA3']:,} + {budget['B']['DIA3']:,} = {sum(part['DIA3'] for part in budget.values()):,} bytes.
Game-prepared resources: TERR {prepared['TERR']:,} vs DIA3 {prepared['DIA3']:,} bytes.
TERR instrument scratch is {INSTRUMENT_RECT[2]}x{INSTRUMENT_RECT[3]}; DIA3 is 188x75. Both dashboards share their
instrument/selector scratch. These are expanded/loaded comparisons, not ZIP
or compressed PVS sizes. The generator rejects either resource over its cap.
The larger steering wheel follows supplied cockpit photos 1000005072.jpg,
1000005071.jpg and 1000005951.jpg through 1000005953.jpg: padded silver-edged
grips, open spokes, paddles, scroll rollers, switches and a broad dark central
pad. Every part rotates around a fixed hub using the original left/centre/right
frames. All three frames restore the same background rectangle. Live speed and
motor readings sit on the centre screen, clear of the larger wheel.
The suede dashboard has a shaped upper silhouette and recessed steering column,
with vent slats, seams, red door trim and a carbon-fibre console. The optional
dast/dasm artwork and mask leave scenery visible above the contoured edge.
The speedometer uses conventional seven-segment numerals: three horizontal
bars of equal thickness and four vertical bars, with a single centre bar.
Exposed vertical ends on 1, 4 and 7 reach the full digit height.
Reference: https://en.wikipedia.org/wiki/Seven-segment_display#Characters
The freestanding computer screen and bezel remain rectangular, with metallic
rails framing the lower centre console.
When updating this cockpit, replace CARTERR.RES, STDATERR.VSH and STDBTERR.VSH
together: the CAR resource also stores the new digit and gauge coordinates.

REBUILD / VERIFY (from repository root, Python 3.10+)
python3 tools/scripts/generate-tesla-roadster.py --install stunts
python3 tools/scripts/generate-tesla-roadster.py --check --install stunts

MODEL REFERENCE PHOTOGRAPHS (supplied by the user)
1000004851.jpg - Front: trapezoid hood, thin light strip and deep black fascia.
1000004850.jpg - Rocket demonstration: profile and smooth aero disc wheels.
1000004849.jpg - Front three-quarter: grey body facets and continuous canopy.
1000004848.jpg - Rear three-quarter: recessed red light bar and lower diffuser.
1000004856.jpg - Elevated front: bowed nose, hood taper and curved windshield.
1000004855.jpg - Elevated rear: narrow glass, broad haunches and layered bumper.
1000004854.jpg - Demonstration profile: door boundaries and dark aero wheels.
Doors remain CLOSED in all game models. Open-door photos are reference only.
The model follows these actual images: grey body, swept black canopy and side
windows, low tapered nose, sculpted wheel haunches and inward-sloping sills.
The hood widens toward its bowed leading edge and is framed by crowned
fenders. Slim grey roof rails surround black glass; the rear screen narrows
between broad grey haunches. Closed-door seams, side markers, a shallow aero
lip, layered rear bumper and four round recessed nozzles add exterior detail.
The dark aero wheels use the original renderer's wheel proportions.
Showroom and racing share a stock-relative size calibration: reported 2026
width about 2,000 mm and wheelbase 2,825 mm, referenced against Corvette/NSX.
Showroom geometry uses the stock 20x display scale. Exact Tesla length and
height remain unconfirmed; stock car dimensions are not uniformly scaled.
See TERR-sizing.md in the car package for the sources and estimates.
Wheels target a 25% reduction relative to the resized body, rounded to the
game's integer coordinates. Circular wheel openings extend below each axle
and use finer, symmetric arc segments in both showroom and racing models.
All glazing is black as requested. The body is interpreted within the game's
255-vertex and 255-primitive limits. Wheel contacts, collision bounds and eye
height follow the larger racing model. Gearing and torque are retained.
Car dimensions affect handling; use the same car resources for
recording and playback. Replace both CARTERR.RES and STTERR.3SH together.

NEWS / ORIGINAL REVEAL LINKS (accessed 2026-10-17)
The following news sources support the performance claims above.

""" + "\n".join(SOURCES) + "\n").replace("\n", "\r\n").encode("ascii")
    return outputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository-root", type=Path, default=ROOT)
    parser.add_argument("--output-directory", type=Path)
    parser.add_argument("--install", type=Path, metavar="GAME_DIRECTORY")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    directory = args.output_directory or args.repository_root / "tools/scripts/cars"
    outputs = generate(args.repository_root)
    for target in [directory] + ([args.install] if args.install else []):
        if not args.check:
            target.mkdir(parents=True, exist_ok=True)
        for name, data in outputs.items():
            path = target / name
            if args.check:
                if path.read_bytes() != data:
                    raise ValueError(f"Generated resource differs: {path}")
            else:
                path.write_bytes(data)
    print(f"{'Verified' if args.check else 'Generated'} Tesla Roadster Rocket (TERR)")
    for part in "AB":
        size = len(outputs[f"STD{part}TERR.VSH"])
        limit = len((args.repository_root / f"tools/scripts/cars/STD{part}DIA3.VSH").read_bytes())
        print(f"Dashboard {part}: {size:,} / {limit:,} decompressed bytes")


if __name__ == "__main__":
    main()
