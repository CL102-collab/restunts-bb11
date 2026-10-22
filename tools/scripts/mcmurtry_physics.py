"""Stunts resources for the 2026 production McMurtry Speirling PURE.

REAL_SPECIFICATIONS is the canonical specification shared by the authoring
tools. McMurtry's current product page and production release take precedence
over Wikipedia's mixture of production dimensions and obsolete prototype
mass figures. SIMD values are game tuning units, not kilograms or physical
motor torque. The single-speed EV curve approximates the production car's
190 mph speed and 1.55-second 0-60 mph claim with one-foot rollout. A power-
of-two game mass avoids the original unsigned-division power-gear bug.
"""

import math
from pathlib import Path
import struct


CAR_ID = "MCMU"
CAR_NAME = "McMurtry Speirling PURE"
SIMD_SIZE = 776
SPECIFICATION_SOURCE = "https://mcmurtry.com/speirling-pure/"
PERFORMANCE_SOURCE = "https://mcmurtry.com/mcmurtry-speirling-production-form/"
PRODUCTION_STATUS_SOURCE = "https://mcmurtry.com/speirling-usa-debut/"
WIKIPEDIA_SOURCE = "https://en.wikipedia.org/wiki/McMurtry_Sp%C3%A9irling"
REAL_SPECIFICATIONS = {
    "variant": "2026 Speirling PURE production car",
    "model_year": 2026,
    "verified_date": "2026-10-21",
    "length_mm": 3815,
    "width_mm": 1795,
    "height_mm": 1056,
    "wheelbase_mm": 2200,
    "weight_kg": 1350,
    "weight_note": "Approximately 1350 kg, subject to options",
    "peak_power_bhp": 1000,
    "peak_power_kw": 745,
    "top_speed_mph": 190,
    "battery_kwh": 100,
    "drive": "Twin-motor rear-wheel drive with E-differential",
    "forward_gears": 1,
    "front_tyres": "30/68-18 Michelin Pilot Sport GT S8M slick",
    "rear_tyres": "31/71-18 Michelin Pilot Sport GT S8M slick",
    "downforce_kg_at_standstill": 2000,
    "cornering_g": 3,
    "braking_g": 3,
    "zero_to_60_mph_seconds": 1.55,
    "acceleration_rollout_feet": 1,
    "acceleration_reference": (
        "Manufacturer's production-car claim: 0-60 mph in 1.55 seconds "
        "with one-foot rollout. This excludes travel through the first "
        "0.3048 metres; it is not a standing-start timing measurement."
    ),
    "production_status": (
        "Production-ready car publicly debuted at Monterey Car Week 2026; "
        "first customer deliveries planned later in 2026."
    ),
    "source_reconciliation": (
        "Wikipedia currently combines 2026 dimensions and battery with "
        "obsolete under-1000/900 kg mass figures and a rounded 1050 mm "
        "height. The current manufacturer sources consistently specify "
        "approximately 1350 kg and 1056 mm; those values are used here."
    ),
    "sources": [
        SPECIFICATION_SOURCE,
        PERFORMANCE_SOURCE,
        PRODUCTION_STATUS_SOURCE,
        WIKIPEDIA_SOURCE,
        "https://www.carthrottle.com/news/finished-mcmurtry-speirling-pure-fan-car-here-1000bhp",
    ],
}

# These are Stunts tuning values, unrelated to the real car's mass in kg.
MASS = 16
GEAR_RATIO = 16384  # 64 << 8; no mismatch between stored/effective gearing.
IDLE_RPM = 768
MAX_RPM = 12160  # 12160 / 64 = 190 mph.
AERO_RESISTANCE = 35
IDLE_TORQUE = 143
BRAKING = 832
GRIP = 1200
COCKPIT_HEIGHT = 18

ADHESION_VERSION = 1
ADHESION_MAX_DISTANCE = 12
ADHESION_MAX_ALIGNMENT_DEGREES = 60
ADHESION_MIN_ALIGNMENT_Q14 = 8192
ADHESION_SURFACE_MASK = 0x0F


def _adhesion_settings() -> bytes:
    return struct.pack("<BHHB", ADHESION_VERSION, ADHESION_MAX_DISTANCE,
                       ADHESION_MIN_ALIGNMENT_Q14, ADHESION_SURFACE_MASK)


def _resources(data: bytes) -> dict[bytes, bytes]:
    """Read an uncompressed DSI container, including unsorted directories."""
    if len(data) < 6:
        raise ValueError("Truncated resource header")
    length, count = struct.unpack_from("<IH", data)
    base = 6 + count * 8
    if length != len(data) or not count or base > length:
        raise ValueError("Invalid resource header")
    names = [data[6 + index * 4:10 + index * 4] for index in range(count)]
    offsets = struct.unpack_from(f"<{count}I", data, 6 + count * 4)
    if len(set(names)) != count or len(set(offsets)) != count or min(offsets) != 0:
        raise ValueError("Duplicate resources or missing first payload")
    if max(offsets) >= length - base:
        raise ValueError("Resource offset outside payload")
    ends = dict(zip(sorted(offsets), sorted(offsets)[1:] + [length - base]))
    return {name: data[base + offset:base + ends[offset]]
            for name, offset in zip(names, offsets)}


def _container(resources: dict[bytes, bytes]) -> bytes:
    payload = bytearray()
    offsets = []
    for name, data in resources.items():
        if len(name) != 4:
            raise ValueError("Resource IDs must contain four bytes")
        offsets.append(len(payload))
        payload.extend(data)
    count = len(resources)
    return (struct.pack("<IH", 6 + count * 8 + len(payload), count)
            + b"".join(resources)
            + struct.pack(f"<{count}I", *offsets)
            + payload)


def _torque_curve() -> bytes:
    """An EV's low-speed torque plateau and high-speed power-limited falloff."""
    anchors = ((0, IDLE_TORQUE), (4096, IDLE_TORQUE), (5120, 128),
               (6400, 103), (7680, 86), (8960, 74), (10240, 65),
               (11264, 59), (12160, 55), (13056, 0))
    result = []
    for index in range(103):
        rpm = index * 128
        for (low_rpm, low), (high_rpm, high) in zip(anchors, anchors[1:]):
            if low_rpm <= rpm <= high_rpm:
                result.append(round(low + (high - low) * (rpm - low_rpm)
                                    / (high_rpm - low_rpm)))
                break
        else:
            raise ValueError("Torque curve does not cover the SIMD range")
    return bytes(result)


def simulate_performance(fps: int = 20) -> dict[str, float | int | None]:
    """Measure a grounded, straight, full-throttle run in game tuning units.

    This is the integer update in statecar.c for this single-speed car, with
    zero slope, no opponent slowdown and all four tyres in contact. It includes
    drag-table quantization, the engine torque limiter and the redline brake.
    It has been checked against the native update_car_speed implementation;
    it does not model the rest of the track/contact physics. Rollout distance
    integrates adjacent measured speeds by the trapezoidal rule in metres;
    it is an authoring measurement, not a change to native track positioning.
    """
    if fps not in (10, 20):
        raise ValueError("Stunts car physics supports 10 or 20 Hz")
    curve = _torque_curve()
    speed = 0
    rpm = IDLE_RPM
    limiter = 0
    peak = 0
    first60 = first100 = None
    rollout_time = None
    distance_metres = 0.0
    for tick in range(1, 600 * fps + 1):
        if limiter:
            limiter -= 1
        drag = AERO_RESISTANCE * (speed >> 10) ** 2 >> 9
        if rpm > MAX_RPM:
            rpm = MAX_RPM - 1
            delta = -drag - BRAKING
        else:
            torque = IDLE_TORQUE if rpm < 2600 else curve[rpm >> 7]
            if limiter and rpm < 5000:
                torque = (IDLE_TORQUE + torque) >> 1
            force = -drag + ((GEAR_RATIO >> 8) * torque >> 4)
            # The power-of-two mass gives the same low signed word after
            # original unsigned division and corrected signed division.
            scaled = ((force * 25) & 0xFFFFFFFF) // MASS
            signed_word = ((scaled + 32768) & 0xFFFF) - 32768
            delta = signed_word >> 1
            if delta > 296:
                limiter = 5
        if fps == 10:
            delta *= 2
        previous_speed = speed
        speed = max(0, speed + delta)
        interval_metres = (previous_speed + speed) * 0.44704 / (512 * fps)
        if rollout_time is None and distance_metres + interval_metres >= 0.3048:
            fraction = (0.3048 - distance_metres) / interval_metres
            rollout_time = (tick - 1 + fraction) / fps
        distance_metres += interval_metres
        rpm = max((speed * GEAR_RATIO) >> 16, IDLE_RPM)
        peak = max(peak, speed)
        if first60 is None and speed >= 60 * 256:
            first60 = tick / fps
        if first100 is None and speed >= 100 * 256:
            first100 = tick / fps
    return {"fps": fps, "duration_seconds": 600,
            "zero_to_60_mph_seconds": first60,
            "zero_to_100_mph_seconds": first100,
            "one_foot_rollout_seconds": rollout_time,
            "zero_to_60_mph_with_one_foot_rollout_seconds": (
                first60 - rollout_time if first60 is not None and
                rollout_time is not None else None),
            "peak_speed_mph": peak / 256,
            "settled_speed_mph": speed / 256}


def build_car(repo: Path, dashboard_patch: dict[int, bytes]) -> bytes:
    """Build CARMCMU.RES from stock reserved fields and authored tuning."""
    from mcmurtry_model import HEIGHT, LENGTH, WIDTH, WHEEL_CENTERS

    resources = _resources((repo / "stunts" / "CARANSX.RES").read_bytes())
    simd = bytearray(resources[b"simd"])
    if len(simd) != SIMD_SIZE:
        raise ValueError("Expected the stock 776-byte Acura SIMD")
    simd[0] = 1
    for offset, value in ((0x02, MASS), (0x04, BRAKING), (0x06, IDLE_RPM),
                          (0x08, 0), (0x0A, MAX_RPM), (0x0C, MAX_RPM),
                          (0x38, AERO_RESISTANCE), (0xA4, GRIP),
                          (0xB4, 0), (0xD0, COCKPIT_HEIGHT)):
        struct.pack_into("<h", simd, offset, value)
    struct.pack_into("<7H", simd, 0x0E, 0, *([GEAR_RATIO] * 6))
    simd[0x3A] = IDLE_TORQUE
    simd[0x3B:0xA2] = _torque_curve()
    struct.pack_into("<4h", simd, 0xB6, 256, 192, 64, 128)
    half_width = math.ceil(WIDTH / 2)
    half_length = math.ceil(LENGTH / 2)
    struct.pack_into("<4h", simd, 0xC8, half_width, math.ceil(HEIGHT - 6),
                     half_length, math.ceil(math.hypot(half_width, half_length)))
    for index, point in enumerate(WHEEL_CENTERS):
        struct.pack_into("<3h", simd, 0xD2 + index * 6,
                         *(round(coordinate * 64) for coordinate in point))

    # Neutral and Drive share their selector position, avoiding a fictitious
    # shift pause for the real car's fixed-ratio electric transmission.
    selector = bytes(simd[0x1C:0x20])
    for index in range(1, 7):
        simd[0x1C + index * 4:0x20 + index * 4] = selector
    for offset, data in dashboard_patch.items():
        if not isinstance(offset, int) or not isinstance(data, bytes):
            raise TypeError("Dashboard patches must map integer offsets to bytes")
        if offset < 0 or offset + len(data) > SIMD_SIZE:
            raise ValueError("Dashboard patch outside SIMD")
        simd[offset:offset + len(data)] = data

    description = (
        "McMurtry Speirling PURE]"
        "2026 production]]"
        "Twin Motor, RW Drive]"
        "1000 kWh Molicel]"
        "1000 HP / 745 kW]"
        "0-60 1.55 sec]"
        "Top Speed 190 MPH]"
        "Price $1,350,000]"
    )
    return _container({
        b"simd": bytes(simd),
        b"gnam": CAR_NAME.encode("ascii") + b"\0",
        b"gsna": CAR_ID.encode("ascii") + b"\0",
        b"edes": description.encode("ascii") + b"\0",
        b"fand": b"\x01",
        b"adhs": _adhesion_settings(),
    })
