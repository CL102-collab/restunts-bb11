"""Build the native Stunts parameters for the 2026 Tesla Roadster Rocket.

The announced Rocket targets are below 0.59 seconds to 60 mph and above
230 mph.  This is a one-speed approximation in Stunts' existing engine:
its low-speed torque peak stands in for cold-gas thrust.  The mass, gear
ratio, torque bytes and grip are game tuning values, not Tesla measurements.
In particular, ``MASS`` is deliberately a power of two so the legacy
unsigned-division power-gear bug does not manufacture extra acceleration.

Keep the stock Acura SIMD's reserved fields.  The dashboard builder supplies
only the instrument/gear-knob byte patches it needs via ``dashboard_patch``.
The optional one-byte ``fand=1`` resource automatically enables suction adhesion
in updated Restunts. The ``adhs`` resource supplies the distance, alignment and
material settings below. The original game ignores both resources.
"""

from pathlib import Path
import math
import struct

from tesla_roadster_model import BODY_SCALE, WHEEL_CENTERS

CAR_ID = "TERR"
CAR_NAME = "Tesla Roadster Rocket"
SCOREBOARD_NAME = CAR_NAME
SHORT_NAME = "TERR"
SIMD_SIZE = 776

# All of these values belong to the game, not to a production-car spec sheet.
MASS = 4
GEAR_RATIO = 12032  # 47 << 8; avoids rounding between stored/effective ratio.
IDLE_RPM = 768
MAX_RPM = 11264
AERO_RESISTANCE = 20
IDLE_TORQUE = 165
BRAKING = 512
GRIP = 800
WHEEL_POINTS = WHEEL_CENTERS
COLLISION_HALF_WIDTH = math.ceil(19 * BODY_SCALE[0])
COLLISION_HEIGHT = math.ceil(20 * BODY_SCALE[1])
COLLISION_HALF_LENGTH = math.ceil(39 * BODY_SCALE[2])
COLLISION_RADIUS = math.ceil(math.hypot(COLLISION_HALF_WIDTH, COLLISION_HALF_LENGTH))
# The camera subtracts the engine's fixed six-unit tyre contact height.
COCKPIT_HEIGHT = 6 + round((18 - 6) * BODY_SCALE[1])

# Per-car adhesion settings, kept outside the stock SIMD layout. The runtime
# reads the fixed-point cosine directly; degree conversion happens only here.
ADHESION_VERSION = 1
ADHESION_MAX_DISTANCE = 12  # World units from the tyre contact to its surface.
ADHESION_MAX_ALIGNMENT_DEGREES = 60
ADHESION_MIN_ALIGNMENT_Q14 = round(
    math.cos(math.radians(ADHESION_MAX_ALIGNMENT_DEGREES)) * 16384)
ADHESION_SURFACE_MASK = 0x0F  # Paved, dirt, ice and grass (bits 0 through 3).


def _adhesion_settings() -> bytes:
    """Encode the version-one six-byte ``adhs`` settings resource."""
    if (ADHESION_VERSION != 1 or not 0 <= ADHESION_MAX_DISTANCE <= 511 or
            not 0 <= ADHESION_MAX_ALIGNMENT_DEGREES <= 90 or
            not 0 <= ADHESION_MIN_ALIGNMENT_Q14 <= 16384 or
            ADHESION_SURFACE_MASK & ~0x0F):
        raise ValueError("Invalid adhesion settings")
    return struct.pack("<BHHB", ADHESION_VERSION, ADHESION_MAX_DISTANCE,
                       ADHESION_MIN_ALIGNMENT_Q14, ADHESION_SURFACE_MASK)


def _resources(data: bytes) -> dict[bytes, bytes]:
    """Read uncompressed DSI resources, including unsorted stock directories."""
    if len(data) < 6:
        raise ValueError("Truncated car resource header")
    length, count = struct.unpack_from("<IH", data)
    base = 6 + count * 8
    if length != len(data) or base > length:
        raise ValueError("Invalid car resource length")
    ids = [data[6 + i * 4:10 + i * 4] for i in range(count)]
    offsets = struct.unpack_from(f"<{count}I", data, 6 + count * 4)
    if len(set(ids)) != count or len(set(offsets)) != count:
        raise ValueError("Duplicate car resource entry")
    if any(offset >= length - base for offset in offsets):
        raise ValueError("Car resource offset outside payload")
    ends = {offset: following for offset, following in
            zip(sorted(offsets), sorted(offsets)[1:] + [length - base])}
    return {name: data[base + offset:base + ends[offset]]
            for name, offset in zip(ids, offsets)}


def _container(resources: dict[bytes, bytes]) -> bytes:
    count = len(resources)
    payload = bytearray()
    offsets = []
    for name, data in resources.items():
        if len(name) != 4:
            raise ValueError("Resource IDs must contain four bytes")
        offsets.append(len(payload))
        payload.extend(data)
    return (struct.pack("<IH", 6 + count * 8 + len(payload), count)
            + b"".join(resources)
            + struct.pack(f"<{count}I", *offsets)
            + payload)


def _torque_curve() -> bytes:
    """A launch pulse followed by a falling EV curve, sampled every 128 rpm."""
    anchors = ((0, IDLE_TORQUE), (2816, IDLE_TORQUE), (3328, 104),
               (4992, 86), (6400, 72), (8192, 58), (9728, 51),
               (11008, 47), (11264, 42), (13056, 0))
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


def build_car(repo: Path, dashboard_patch: dict[int, bytes]) -> bytes:
    """Return CARTERR.RES, accepting byte patches at SIMD-relative offsets."""
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
    # Neutral plus six ratio slots remain valid even though only D is used.
    struct.pack_into("<7H", simd, 0x0E, 0, *([GEAR_RATIO] * 6))
    simd[0x3A] = IDLE_TORQUE
    simd[0x3B:0xA2] = _torque_curve()
    struct.pack_into("<4h", simd, 0xB6, 256, 192, 64, 128)
    struct.pack_into("<4h", simd, 0xC8, COLLISION_HALF_WIDTH, COLLISION_HEIGHT,
                     COLLISION_HALF_LENGTH, COLLISION_RADIUS)
    for index, point in enumerate(WHEEL_POINTS):
        struct.pack_into("<3h", simd, 0xD2 + index * 6,
                         *(coordinate * 64 for coordinate in point))

    # Neutral and D share a position: no fictitious shift pause for this EV.
    knob = bytes(simd[0x1C:0x20])
    for index in range(1, 7):
        simd[0x1C + index * 4:0x20 + index * 4] = knob
    for offset, data in dashboard_patch.items():
        if not isinstance(offset, int) or not isinstance(data, bytes):
            raise TypeError("Dashboard patches must map integer offsets to bytes")
        if offset < 0 or offset + len(data) > SIMD_SIZE:
            raise ValueError("Dashboard patch outside SIMD")
        simd[offset:offset + len(data)] = data

    return _container({
        b"simd": bytes(simd),
        b"gnam": SCOREBOARD_NAME.encode("ascii") + b"\0",
        b"gsna": SHORT_NAME.encode("ascii") + b"\0",
        # The car menu draws a description line when it encounters ']'.
        b"edes": (CAR_NAME + "]]]Twin motor, RW Drive]Ten cold-gas thrusters]"
                  "1341 HP / 1000 kW]0-60 0.59 sec]Top Speed 230 MPH]"
                  "Price: Invitation only]").encode("ascii") + b"\0",
        b"fand": b"\x01",
        b"adhs": _adhesion_settings(),
    })
