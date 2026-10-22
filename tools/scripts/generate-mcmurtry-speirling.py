#!/usr/bin/env python3
"""Build and validate the MCMU Stunts car from original photo-based geometry.

Run with --install stunts to install the four game resources, or --check to
verify that the checked-in files match their deterministic authoring sources.
No third-party model download or graphics package is needed to rebuild them.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

import mcmurtry_dashboard as dashboard
import mcmurtry_model as model
import mcmurtry_physics as physics


ROOT = Path(__file__).resolve().parents[2]
CAR_ID = "MCMU"
GAME_FILES = ("CARMCMU.RES", "STMCMU.3SH", "STDAMCMU.VSH", "STDBMCMU.VSH")
SPEC_SOURCE = physics.SPECIFICATION_SOURCE
SOURCES = list(dict.fromkeys(physics.REAL_SPECIFICATIONS["sources"] + [
    "https://www.zr1netregistry.com/Information/C4ZR-1GeneralInfo/Dimensions.aspx",
    "https://acuranews.com/en-US/releases/release-ff6633ebcc698302413a3d004c34ca74-1991-acura-nsx-specifications",
]))
REFERENCE_IMAGES = [
    {"view": "2026 production front", "url": "https://mcmurtry.com/wp-content/uploads/2026/07/McMurtry-Speirling-PURE-front-2048x1280.jpg"},
    {"view": "2026 production front three-quarter", "url": "https://mcmurtry.com/wp-content/uploads/2026/07/McMurtry-Speirling-PURE-front-3_4-2048x1279.jpg"},
    {"view": "2026 production front detail", "url": "https://mcmurtry.com/wp-content/uploads/2026/07/McMurtry-Speirling-PURE-front-detail-2048x1280.jpg"},
    {"view": "2026 production rear", "url": "https://mcmurtry.com/wp-content/uploads/2026/07/McMurtry-Speirling-PURE-rear-2048x1280.jpg"},
    {"view": "2026 production rear three-quarter", "url": "https://mcmurtry.com/wp-content/uploads/2026/07/McMurtry-Speirling-PURE-rear-3_4-2048x1280.jpg"},
    {"view": "2026 production rear studio detail", "url": "https://mcmurtry.com/wp-content/uploads/2026/06/rear-FINAL-2048x1365.jpg"},
    {"view": "2026 production rear track detail", "url": "https://mcmurtry.com/wp-content/uploads/2026/06/TRACK-REAR-1-2048x1366.jpg"},
    {"view": "2026 production overhead", "url": "https://mcmurtry.com/wp-content/uploads/2026/06/TRACK-OVERHEAD-1-2048x1365.jpg"},
]
INITIAL_REFERENCE_IMAGES = [
    {"view": "front", "url": "https://mcmurtry.com/wp-content/uploads/2024/06/McMurtry-Speirling-PURE-Front-View-1-1024x723-1.jpg"},
    {"view": "side", "url": "https://mcmurtry.com/wp-content/uploads/2024/06/McMurtry-Speirling-PURE-Side-View-1-1024x723-1.jpg"},
    {"view": "rear", "url": "https://mcmurtry.com/wp-content/uploads/2024/06/McMurtry-Speirling-PURE-Rear-3-4-1024x723-1.jpg"},
    {"view": "front three-quarter", "url": "https://mcmurtry.com/wp-content/uploads/2024/06/Speirling-PURE-Hero-Shot-with-logos-1024x723-1.jpg"},
]
DETAIL_REFERENCE_IMAGES = [
    {"file": "1000005898.jpg", "view": "Close-up of front wheel-crown aperture, rim, dark recess and cross rib."},
    {"file": "1000005901.jpg", "view": "Rear wheel-crown apertures beside the wing."},
    {"file": "1000005899.jpg", "view": "Front three-quarter view showing both front apertures."},
    {"file": "1000005900.jpg", "view": "Low front three-quarter view showing the aperture within the fender crown."},
]


def resources(data):
    """Validate a raw DSI container, including an unsorted stock directory."""
    if len(data) < 6:
        raise ValueError("Truncated DSI container")
    size, count = struct.unpack_from("<IH", data)
    base = 6 + 8 * count
    if size != len(data) or not count or base >= size:
        raise ValueError("Invalid DSI header")
    ids = [data[6 + 4 * i:10 + 4 * i].decode("ascii") for i in range(count)]
    offsets = list(struct.unpack_from(f"<{count}I", data, 6 + 4 * count))
    if len(set(ids)) != count or len(set(offsets)) != count or min(offsets) != 0:
        raise ValueError("Duplicate DSI entry or missing first payload")
    ordered = sorted(offsets) + [size - base]
    ends = dict(zip(ordered, ordered[1:]))
    if any(offset >= size - base for offset in offsets):
        raise ValueError("DSI offset outside container")
    return {name: data[base + offset:base + ends[offset]]
            for name, offset in zip(ids, offsets)}


def decode_model(data, name):
    if len(data) < 4:
        raise ValueError(f"Truncated {name}")
    vertices, primitives, paints, reserved = data[:4]
    if not vertices or not primitives or paints != 7 or reserved:
        raise ValueError(f"Invalid shape header in {name}")
    cursor = 4 + vertices * 6 + primitives * 8
    if cursor > len(data):
        raise ValueError(f"Truncated shape tables in {name}")
    points = [struct.unpack_from("<3h", data, 4 + i * 6) for i in range(vertices)]
    wheels = []
    triangles = []
    for _ in range(primitives):
        if cursor + 2 + paints > len(data):
            raise ValueError(f"Truncated primitive in {name}")
        kind, flags = data[cursor:cursor + 2]
        count = kind if 1 <= kind <= 10 else {11: 2, 12: 6}.get(kind)
        if count is None or flags & ~3:
            raise ValueError(f"Invalid primitive type or flags in {name}")
        materials = data[cursor + 2:cursor + 2 + paints]
        indices = data[cursor + 2 + paints:cursor + 2 + paints + count]
        if len(indices) != count or max(indices) >= vertices or max(materials) > 126:
            raise ValueError(f"Invalid primitive index or material in {name}")
        if kind == 12:
            wheels.append(tuple(indices))
        if 3 <= kind <= 10:
            a, b, c = [points[i] for i in indices[:3]]
            u, v = [tuple(p[k] - a[k] for k in range(3)) for p in (b, c)]
            normal = (u[1] * v[2] - u[2] * v[1],
                      u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
            if normal == (0, 0, 0):
                raise ValueError(f"Degenerate first triangle in {name}")
            triangles.extend(tuple(points[i] for i in (indices[0], indices[j], indices[j + 1]))
                             for j in range(1, len(indices) - 1))
        cursor += 2 + paints + count
    if cursor == len(data) or any(data[cursor:]):
        raise ValueError(f"Invalid shape terminator in {name}")
    if name.startswith("car"):
        if wheels[:4] != [tuple(range(i, i + 6)) for i in (8, 14, 20, 26)]:
            raise ValueError(f"Animated wheel slots disagree with Stunts in {name}")
        for p in points[8:]:
            if any(not min(b[k] for b in points[:8]) <= p[k] <=
                   max(b[k] for b in points[:8]) for k in range(3)):
                raise ValueError(f"Vertex outside shape bounds in {name}")
    return points, triangles, {"vertices": vertices, "primitives": primitives, "paint_jobs": paints}


def wheel_cover_groups(data, points):
    """Collect roof faces which the native painter draws after each wheel."""
    vertices, primitives, paints, _ = data[:4]
    cursor = 4 + vertices * 6 + primitives * 8
    groups = []
    parent = None
    for _ in range(primitives):
        kind, flags = data[cursor:cursor + 2]
        count = kind if 1 <= kind <= 10 else {11: 2, 12: 6}[kind]
        indices = data[cursor + 2 + paints:cursor + 2 + paints + count]
        if kind == 12:
            groups.append([])
            parent = len(groups) - 1
        elif not flags & 2:
            parent = None
        elif parent is not None and 3 <= kind <= 10:
            groups[parent].extend(tuple(points[i] for i in (indices[0], indices[j], indices[j + 1]))
                                  for j in range(1, len(indices) - 1))
        cursor += 2 + paints + count
    return groups


def validate_wheel_covers(points, triangles, scale, shift, name, *, wheel_groups=None):
    """Ray-test the encoded fenders against the entire upper tyre envelope.

    Side-view arches can look correct while their sloping top panels cut
    through the inner tread. Test the quantized mesh at quarter-unit intervals
    along each tyre and half-unit intervals across its width, in game units.
    When wheel_groups is supplied, only cover triangles attached to their own
    wheel count: an independently sorted face can be physically above a tyre
    yet still paint behind its rim. Native raster comparisons additionally
    check the actual occlusion in angled views.
    """
    def normalize(point):
        return point[0] / scale, (point[1] - shift) / scale, point[2] / scale

    def project(faces):
        result = []
        for triangle in faces:
            a, b, c = map(normalize, triangle)
            ux, uz, vx, vz = b[0] - a[0], b[2] - a[2], c[0] - a[0], c[2] - a[2]
            determinant = ux * vz - uz * vx
            if abs(determinant) > 1e-9:
                result.append((a, b, c, ux, uz, vx, vz, determinant))
        return result

    if wheel_groups is not None and len(wheel_groups) != 4:
        raise ValueError(f"Expected four wheel cover groups in {name}")
    projected_groups = ([project(group) for group in wheel_groups]
                        if wheel_groups is not None else [project(triangles)] * 4)
    minimum = math.inf
    for wheel in range(4):
        projected = projected_groups[wheel]
        inner, radial, outer = [normalize(points[8 + 6 * wheel + j]) for j in (0, 1, 3)]
        cx, cy, cz = [(inner[a] + outer[a]) / 2 for a in range(3)]
        radius = radial[1] - inner[1]
        half_width = abs(outer[0] - inner[0]) / 2
        across = [0] + [-half_width + .25 + i * .5
                        for i in range(round(half_width * 4))]
        for dx in across:
            for step in range(-round(radius * 4) + 1, round(radius * 4)):
                dz = step / 4
                x, z = cx + dx, cz + dz
                tyre_top = cy + math.sqrt(radius * radius - dz * dz)
                heights = []
                for a, b, c, ux, uz, vx, vz, determinant in projected:
                    px, pz = x - a[0], z - a[2]
                    u = (px * vz - pz * vx) / determinant
                    v = (ux * pz - uz * px) / determinant
                    if min(u, v, 1 - u - v) >= -1e-8:
                        heights.append(a[1] + u * (b[1] - a[1]) + v * (c[1] - a[1]))
                clearance = max(heights, default=-math.inf) - tyre_top
                if clearance < .1:
                    raise ValueError(f"Tyre protrudes through {name} wheel {wheel + 1} "
                                     f"at X={x:g}, Z={z:g}: clearance {clearance:.3f}")
                minimum = min(minimum, clearance)
    return round(minimum, 3)


def dashboard_ram_usage(primary, secondary):
    """Classic-mode arena payloads, matching dashboard.c and sprite_make_wnd.

    STDA remains RLE-encoded after loading; STDB stays extracted. Instrument
    and gearbox panels share scratch storage when either contains the other.
    The sprite definitions/row tables live in an already allocated global
    table, rather than additional per-dashboard arena chunks.
    """
    bitmaps = resources(primary)
    instrument = struct.unpack_from("<HH", bitmaps["ins2"])
    gearbox = struct.unpack_from("<HH", bitmaps["gbox"])
    contains = lambda outer, inner: all(a >= b for a, b in zip(outer, inner))
    scratch = gearbox if contains(gearbox, instrument) else instrument
    panels = [scratch]
    if not contains(scratch, gearbox):
        panels.append(gearbox)
    # sprite_make_wnd includes the 16-byte SHAPE2D header, then reserves one
    # extra paragraph after truncating to paragraphs (even for exact multiples).
    scratch_bytes = sum(((width * height + 16) // 16 + 1) * 16
                        for width, height in panels)
    prepared_bytes = dashboard.prepared_stda_size(primary) + (len(secondary) + 15) // 16 * 16
    return {
        "extracted_resources_bytes": len(primary) + len(secondary),
        "prepared_resources_bytes": prepared_bytes,
        "scratch_bitmap_bytes": scratch_bytes,
        "scratch_dimensions": [list(panel) for panel in panels],
        "resident_arena_bytes": prepared_bytes + scratch_bytes,
    }


def validate(outputs, repository):
    car = resources(outputs["CARMCMU.RES"])
    simd = car["simd"]
    if len(simd) != 776 or simd[0] != 1:
        raise ValueError("Expected a 776-byte single-speed EV SIMD")
    if car.get("fand") != b"\x01" or car.get("adhs") != struct.pack("<BHHB", 1, 12, 8192, 15):
        raise ValueError("Expected enabled fan adhesion with explicit default settings")
    if struct.unpack_from("<H", simd, 0xAE)[0] != dashboard.RED | (dashboard.RED << 8):
        raise ValueError("Expected red dashboard instrument needles")
    for key in ("gnam", "gsna", "edes"):
        if not car[key].endswith(b"\0"):
            raise ValueError(f"Unterminated car text: {key}")
    shapes = resources(outputs["STMCMU.3SH"])
    if set(shapes) != {"car0", "car1", "car2", "exp0", "exp1", "exp2", "exp3"}:
        raise ValueError("Missing showroom, racing, LOD, or crash shape")
    metadata = {}
    contacts = [struct.unpack_from("<3h", simd, 0xD2 + i * 6) for i in range(4)]
    for name, shape in shapes.items():
        points, triangles, metadata[name] = decode_model(shape, name)
        if not name.startswith("car"):
            continue
        scale = model.SHOWROOM_SCALE if name == "car0" else 1
        shift = model.SHOWROOM_Y_OFFSET if name == "car0" else 0
        floor = shift - 6 * scale
        for wheel, contact in enumerate(contacts):
            inner, radial, outer = [points[8 + 6 * wheel + j] for j in (0, 1, 3)]
            for axis in range(3):
                centre = inner[axis] + outer[axis] - (2 * shift if axis == 1 else 0)
                if centre * 32 != contact[axis] * scale:
                    raise ValueError(f"Physical and visual wheel centres disagree in {name}")
            radius = radial[1] - inner[1]
            if inner[1] - radius != floor:
                raise ValueError(f"Wheel does not meet engine contact plane in {name}")
        if min(p[1] for p in points[32:]) < floor:
            raise ValueError(f"Body below tyre contact plane in {name}")
        dimensions = {
            "length": (max(p[2] for p in points[8:]) - min(p[2] for p in points[8:])) / scale,
            "width": (max(p[0] for p in points[8:]) - min(p[0] for p in points[8:])) / scale,
            "height": (max(p[1] for p in points[8:]) - floor) / scale,
            "wheelbase": (contacts[0][2] - contacts[3][2]) / 64,
        }
        for axis in ("length", "width", "height", "wheelbase"):
            mm = physics.REAL_SPECIFICATIONS[f"{axis}_mm"]
            if abs(dimensions[axis] - mm / model.MM_PER_UNIT) > 1.1:
                raise ValueError(f"Incorrect real-world scale for {axis} in {name}: {dimensions}")
        metadata[name]["dimensions_game_units"] = dimensions
        wheel_groups = wheel_cover_groups(shape, points)
        metadata[name]["minimum_upper_tyre_clearance_game_units"] = validate_wheel_covers(
            points, triangles, scale, shift, name, wheel_groups=wheel_groups)
        metadata[name]["wheel_cover_triangles_attached_to_each_wheel"] = [len(group) for group in wheel_groups]
    budgets = {}
    for part, required in (("A", set("dash dast dasm whl1 whl2 whl3 ins1 ins2 ins3 inm1 inm3 gbox".split())),
                           ("B", {"dot ", "dota", "dot1", "dot2", "gnob", "gnab"} |
                            {f"dig{i}" for i in range(10)})):
        payload = outputs[f"STD{part}MCMU.VSH"]
        bitmaps = resources(payload)
        if not required <= bitmaps.keys():
            raise ValueError(f"Missing dashboard {part} resources")
        for name, bitmap in bitmaps.items():
            if len(bitmap) < 16:
                raise ValueError(f"Truncated dashboard bitmap {name}")
            width, height = struct.unpack_from("<HH", bitmap)
            if not width or not height or len(bitmap) != 16 + width * height or bitmap[14] & 0xF0:
                raise ValueError(f"Invalid dashboard bitmap {name}")
        if part == "A":
            top, mask = bitmaps["dast"], bitmaps["dasm"]
            if top[:16] != mask[:16]:
                raise ValueError("Windshield fender artwork and mask are misaligned")
            width, height = struct.unpack_from("<HH", top)
            x, y = struct.unpack_from("<HH", top, 8)
            if (width != 320 or x != 0 or not 0 < y < dashboard.DASH_TOP or
                    y + height != dashboard.DASH_TOP):
                raise ValueError("Windshield fender overlay must end at the dashboard edge")
            if set(mask[16:]) != {0, 255}:
                raise ValueError("Windshield fender mask must contain opaque and clear areas")
            if any(pixel and clear == 255 for pixel, clear in zip(top[16:], mask[16:])):
                raise ValueError("Windshield fender artwork would alter the clear road view")
        limit = len((repository / f"tools/scripts/cars/STD{part}DIA3.VSH").read_bytes())
        if len(payload) >= limit:
            raise ValueError(f"Dashboard {part} must use less extracted memory than DIA3")
        budgets[part] = {"MCMU": len(payload), "DIA3": limit}
    ram = {}
    for car_id, a, b in (("MCMU", outputs["STDAMCMU.VSH"], outputs["STDBMCMU.VSH"]),
                         ("DIA3", (repository / "tools/scripts/cars/STDADIA3.VSH").read_bytes(),
                          (repository / "tools/scripts/cars/STDBDIA3.VSH").read_bytes())):
        ram[car_id] = dashboard_ram_usage(a, b)
    for stage in ("extracted_resources_bytes", "prepared_resources_bytes", "resident_arena_bytes"):
        if ram["MCMU"][stage] >= ram["DIA3"][stage]:
            raise ValueError(f"Dashboard must use less RAM than DIA3: {stage}")
    return metadata, budgets, ram


def generate(repository):
    specs = physics.REAL_SPECIFICATIONS
    a, b, patches = dashboard.build_dashboards(repository)
    outputs = {"CARMCMU.RES": physics.build_car(repository, patches),
               "STMCMU.3SH": model.build_models(), "STDAMCMU.VSH": a, "STDBMCMU.VSH": b}
    models, budgets, ram = validate(outputs, repository)
    performance = [physics.simulate_performance(fps) for fps in (20, 10)]
    for result in performance:
        if (abs(result["zero_to_60_mph_with_one_foot_rollout_seconds"] -
                specs["zero_to_60_mph_seconds"]) > 1 / result["fps"] + .01 or
                abs(result["peak_speed_mph"] - specs["top_speed_mph"]) > 1):
            raise ValueError("Straight-line tuning no longer matches the car's performance targets")
    outputs["MCMU.obj"] = model.export_obj().encode("ascii")
    outputs["MCMU.mtl"] = model.export_mtl().encode("ascii")
    manifest = {
        "car_id": CAR_ID, "name": "McMurtry Spéirling PURE", "variant": specs["variant"],
        "model_year": specs["model_year"], "specifications_verified_date": specs["verified_date"],
        "game_name": "McMurtry Speirling PURE", "geometry": "Original low-polygon reconstruction from manufacturer photographs/renders, with front and rear bodywork rebuilt from the 2026 production-car photographs; no downloaded third-party mesh.",
        "geometry_revision": "Production-car front and rear reconstructed with divided nose intakes, inset headlights, recessed rear ducts, four small tail lamps and a separate lower diffuser. Raised, triangulated fender shoulders clear the upper tyre envelope in all three model detail levels. Cover faces and bounded recessed wheel-arch vents follow their matching wheel with native flag 2, preserving their drawing order in angled views.",
        "model_search": [{"url": "https://sketchfab.com/3d-models/mcmurtry-speirling-8217dad4511a441baadffac685ba4a62",
                          "license": "CC BY-NC-SA 4.0", "result": "Official download API requires authentication (HTTP 401); not used."},
                         {"url": "https://www.turbosquid.com/3d-models/3d-mcmurtry-speirling-model-2411624", "result": "Paid, editorial license; not used."}],
        "reference_images": REFERENCE_IMAGES,
        "initial_reconstruction_reference_images": INITIAL_REFERENCE_IMAGES,
        "user_supplied_detail_references": DETAIL_REFERENCE_IMAGES,
        "wheel_arch_vents": {"count": len(model.VENTS), "locations": model.VENTS,
                             "width_game_units": model.VENT_WIDTH,
                             "length_game_units": model.VENT_LENGTH,
                             "recess_depth_game_units": model.VENT_DEPTH,
                             "representation": "Chamfered painted rims, dark recessed bowls and a cross rib; a low-polygon approximation of the photographed wheel-crown openings."},
        "reference_image_usage": "Reference only; manufacturer images are not redistributed in the car package.",
        "specifications": specs,
        "performance_note": "The 2026 production-car claim of 1.55 s to 60 mph includes one-foot rollout. Both standing-start and rollout-adjusted game timings are recorded separately; native tuning is an approximation of the production figures.",
        "game_straight_line_performance": performance,
        "performance_measurement": "Full-throttle level-ground integer drivetrain update with all four tyres grounded; checked against native statecar.c. This isolates gearing and torque from track and steering effects. The native redline brake produces a small limiter sawtooth.",
        "sizing": {"millimetres_per_game_unit": model.MM_PER_UNIT, "showroom_scale": model.SHOWROOM_SCALE,
                   "showroom_y_offset": model.SHOWROOM_Y_OFFSET, "wheel_radius_game_units": model.WHEEL_RADIUS,
                   "wheel_centres_game_units": model.WHEEL_CENTERS,
                   "reference": "Stock Corvette body 103 units / 4506 mm and NSX 100 units / 4405 mm; stock cars are stylized, so exact ratios to every stock car are impossible.",
                   "rounding": "2026 production dimensions set the nominal calibration. Symmetric racing coordinates round to whole game units; the showroom retains sub-unit precision at 20x."},
        "paint_jobs": list(model.PAINT_NAMES),
        "adhesion": {"fand": 1, "resource": "adhs", "version": 1, "max_distance_world_units": 12,
                     "min_alignment_cosine_q14": 8192, "max_alignment_degrees": 60, "surface_mask": 15,
                     "surfaces": ["paved", "dirt", "ice", "grass"],
                     "note": "Restunts surface attachment is a game approximation of fan downforce. No water adhesion. Original Stunts ignores these extra resources."},
        "dashboard_note": "Original indexed artwork based on the supplied cockpit photographs and 2024 references: upright LCD, lamp row, a continuous shaped steering rim with textured side grips and contrasting carbon top and bottom bridges, five matching unlabelled fluted knobs, turquoise vent pods, silver circular vents and detailed carbon side panels. Every rim section uses the same rotation about the hub, preserving the upper bridge's shape while steering. The instrument needle is red. A gently bowed carbon cowl and shaded upper lip curve around the wheel. Native masked upper artwork shows that curved edge, turquoise front fender crowns and their top openings while preserving the live road above them. Live instruments remain legible in all three steering positions; no copied photograph pixels.",
        "dashboard_reference": "https://www.topgear.com/car-reviews/speirling/first-drive",
        "dashboard_reference_images": dashboard.DASHBOARD_REFERENCES,
        "dashboard_decompressed_bytes": budgets,
        "dashboard_game_prepared_bytes": {car_id: usage["prepared_resources_bytes"]
                                          for car_id, usage in ram.items()},
        "dashboard_ram_bytes": ram,
        "dashboard_ram_note": "Classic 320x200 native arena payloads including 16-byte allocation rounding and shared instrument/gearbox scratch bitmaps. Sprite definitions and row tables use the common preallocated window table. Generation requires strictly less extracted, prepared and resident dashboard RAM than DIA3.",
        "models": models, "sources": SOURCES,
        "files": {name: {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
                  for name, data in outputs.items()},
    }
    outputs["MCMU.json"] = (json.dumps(manifest, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    outputs["READMCMU.TXT"] = (f"""MCMURTRY SPEIRLING PURE ({specs['model_year']} PRODUCTION) - MCMU

INSTALL
Copy CARMCMU.RES, STMCMU.3SH, STDAMCMU.VSH and STDBMCMU.VSH into your
Stunts game-data folder and select McMurtry Speirling PURE in the car menu.
Paints: Turquoise (default), Purple, Orange, Yellow, Blue, White, Light gray.
Single-speed electric drive. No stock car is replaced.

Fan adhesion is enabled by fand=1 and adhs=01 0C 00 00 20 0F. Updated
Restunts attaches tyre contacts to supported driving surfaces automatically,
including vertical/inverted surfaces, with a 12-unit gap and 60-degree
alignment limit on paved, dirt, ice and grass. No water adhesion. This is
a game approximation, not a physical aerodynamic simulation. The original
DOS game ignores these extra resources. Use identical car resources when
recording and playing back a replay.

REAL CAR AND SCALE
This is the {specs['model_year']} production PURE: {specs['length_mm']} x {specs['width_mm']} x {specs['height_mm']} mm,
{specs['wheelbase_mm']} mm wheelbase, approximately {specs['weight_kg']} kg (subject to options),
{specs['peak_power_bhp']} bhp ({specs['peak_power_kw']} kW), {specs['top_speed_mph']} mph, {specs['battery_kwh']} kWh, twin-motor rear-wheel drive.
One fixed forward ratio is represented. The production car was unveiled
in July 2026 and publicly debuted at Monterey Car Week in August 2026.
The manufacturer's {specs['zero_to_60_mph_seconds']:.2f}-second 0-60 mph claim uses one-foot rollout.
Normal 20 Hz game tuning gives {performance[0]['zero_to_60_mph_seconds']:.2f} s from rest, approximately
{performance[0]['zero_to_60_mph_with_one_foot_rollout_seconds']:.2f} s after excluding the first foot of travel.
Mass, grip, gearing and torque bytes are Stunts tuning, not SI measurements.

Wikipedia was checked alongside the current McMurtry production sources.
Its obsolete under-1000/900 kg weight and rounded 1050 mm height conflict
with the factory's current approximately 1350 kg and 1056 mm values.
MCMU.json records the factory figures. The in-game description uses the
user's supplied display text.

Geometry uses 44 mm per racing-model unit, calibrated to the stock Corvette
and NSX. Both race LODs have the same real-world size; the showroom uses
the same proportions at the stock 20x scale. Wheels and physical contacts
agree, with tyre bottoms on the engine's six-unit contact plane.

MODEL SOURCES
A free Sketchfab model was found, but its official download endpoint
requires authentication; other candidates were paid or lacked clear reuse
permission. This package therefore uses original photo-based low-polygon
geometry and cockpit artwork, as authorized by the request. No third-party
mesh or manufacturer photograph is redistributed. MCMU.json records the
model search, photo URLs, specifications, dimensions and resource hashes.
The front and rear are reconstructed from the manufacturer's 2026 production
photographs, including the sculpted rear ducts and divided nose intakes.
MCMU.obj and MCMU.mtl are editable Wavefront source exports in metres,
Y-up with tyre bottoms at ground level; keep these two files together.
The raised fender shoulders cover the upper tyre envelope in all model
detail levels, checked against the quantized geometry during generation.
Each wheel's cover triangles follow it with native primitive flag 2, so
the painter draws the fender after its wheel even at shallow angles.
The four real wheel-crown openings are represented by bounded recessed
vents with painted rims, dark recessed bowls and cross ribs, based on
the user's 1000005898/5901/5899/5900.jpg detail photographs.
The cockpit combines the supplied 1000005914/5913/5912.jpg photographs with
the earlier rectangular LCD wheel reference. A continuous shaped steering
rim joins textured side grips with contrasting carbon top and bottom bridges.
Every rim section rotates about the same hub, including the upper bridge.
It has five matching blank fluted knobs, a red instrument needle, turquoise
vent pods, silver circular vents, stitched carbon trim and release hardware. Native
dast/dasm masks show the gently bowed carbon cowl, raised turquoise front
fenders and top apertures while preserving the live road above them. The earlier
wheel reference remains available at:
https://www.topgear.com/car-reviews/speirling/first-drive

REBUILD
python3 tools/scripts/generate-mcmurtry-speirling.py --install stunts
python3 tools/scripts/generate-mcmurtry-speirling.py --check --install stunts

The generator validates all model, wheel, bitmap and adhesion resources
and requires dashboard memory use to stay strictly below DIA3:
Extracted A+B: {ram['MCMU']['extracted_resources_bytes']:,} vs {ram['DIA3']['extracted_resources_bytes']:,} bytes.
Prepared A+B: {ram['MCMU']['prepared_resources_bytes']:,} vs {ram['DIA3']['prepared_resources_bytes']:,} bytes.
With scratch: {ram['MCMU']['resident_arena_bytes']:,} vs {ram['DIA3']['resident_arena_bytes']:,} bytes.
These are classic-mode arena payloads; shared window tables are preallocated.
See docs/mcmurtry-speirling.md
for dimensional calibration, source details and validation.

SOURCES
""" + "\n".join(SOURCES) + "\n").replace("\n", "\r\n").encode("ascii")
    return outputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository-root", type=Path, default=ROOT)
    parser.add_argument("--output-directory", type=Path)
    parser.add_argument("--install", type=Path, metavar="GAME_DIRECTORY")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    outputs = generate(args.repository_root)
    output = args.output_directory or args.repository_root / "tools/scripts/cars"
    targets = [(output, outputs)]
    if args.install:
        targets.append((args.install, {name: outputs[name] for name in GAME_FILES}))
    for directory, files in targets:
        if not args.check:
            directory.mkdir(parents=True, exist_ok=True)
        for name, data in files.items():
            path = directory / name
            if args.check:
                if not path.exists() or path.read_bytes() != data:
                    raise ValueError(f"Missing or stale generated file: {path}")
            else:
                path.write_bytes(data)
    print(f"{'Verified' if args.check else 'Generated'} McMurtry Speirling PURE (MCMU)")
    for name in GAME_FILES:
        print(f"{name}: {len(outputs[name]):,} bytes")


if __name__ == "__main__":
    main()
