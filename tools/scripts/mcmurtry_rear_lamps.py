"""Brake lamps authored after HyperVision body simplification.

The source's sub-unit lenses disappear at chase-camera distances, and QEM
removes the lower pair entirely. Keep the four annuli and central kite as
explicit final details on the showroom and near racing meshes.
"""

import math


TRIANGLE_VERTICES = 3
LAMP_X = 14.5
LAMP_HEIGHTS = (2.25, 8.5)
LAMP_FACE_Z = -41.0
LAMP_OUTER_RADIUS = .625
LAMP_INNER_RADIUS = .3125
LAMP_SEGMENTS = 12
# Place the widest corners above mid-height: the upper pair of sides is
# shorter than the lower pair, while the overall lamp bounds stay fixed.
KITE_SHOULDER_Y = 3.25
KITE_HALF_WIDTH = 1.5
KITE_UPPER_HEIGHT = 1.5
KITE_LOWER_HEIGHT = 3.0
KITE_INNER_RATIO = .55
REAR_SOURCE_LIMIT_Z = -30.0
# Two perimeter loops per lamp; the kite shares its centreline vertices.
DETAIL_VERTEX_ALLOWANCE = len(LAMP_HEIGHTS) * 2 * 2 * LAMP_SEGMENTS + 8


def apply(shape, model, *, showroom=False):
    """Replace rear light materials and add visible, non-overlapping rims.

    Body panels and original lens geometry remain intact. Their old red
    surfaces become black housings behind the new lamps. Author in racing
    units and quantize only after applying the showroom scale and offset.
    """
    scale = model.SHOWROOM_SCALE if showroom else 1
    shift = model.SHOWROOM_Y_OFFSET if showroom else 0
    factor = 1 << shape.coordinate_fraction_bits
    shape.primitives = [
        (kind, flags, (model.BLACK,) * model.PAINT_COUNT, indices)
        if materials == (model.BRAKE_LIGHT,) * model.PAINT_COUNT and
        all(shape.vertices[i][2] / scale < REAR_SOURCE_LIMIT_Z for i in indices)
        else (kind, flags, materials, indices)
        for kind, flags, materials, indices in shape.primitives]
    shape._lookup = {point: index for index, point in enumerate(shape.vertices)
                     if index >= model.WHEEL_CONTROL_START + len(model.WHEEL_CENTERS) * model.WHEEL_CONTROL_COUNT}

    def triangle(points, material, *, reflect=False):
        if reflect:
            points = [(-x, y, z) for x, y, z in reversed(points)]
        encoded = [(round(x * scale * factor) / factor,
                    round((y * scale + shift) * factor) / factor,
                    round(z * scale * factor) / factor) for x, y, z in points]
        shape.primitives.append((TRIANGLE_VERTICES, model.TWO_SIDED_FLAG,
                                 (material,) * model.PAINT_COUNT,
                                 tuple(shape.vertex(point) for point in encoded)))

    def annulus(outer, inner, *, reflect=False):
        for index in range(len(outer)):
            following = (index + 1) % len(outer)
            triangle((outer[index], outer[following], inner[following]),
                     model.BRAKE_LIGHT, reflect=reflect)
            triangle((outer[index], inner[following], inner[index]),
                     model.BRAKE_LIGHT, reflect=reflect)
        # A black disk fills only the hole, never overlaps the red rim.
        for index in range(1, len(inner) - 1):
            triangle((inner[0], inner[index], inner[index + 1]),
                     model.BLACK, reflect=reflect)

    for height in LAMP_HEIGHTS:
        loops = [[(LAMP_X + radius * math.cos(math.tau * i / LAMP_SEGMENTS),
                   height + radius * math.sin(math.tau * i / LAMP_SEGMENTS),
                   LAMP_FACE_Z) for i in range(LAMP_SEGMENTS)]
                 for radius in (LAMP_OUTER_RADIUS, LAMP_INNER_RADIUS)]
        for reflect in (False, True):
            annulus(*loops, reflect=reflect)

    # Mirror the positive half explicitly to preserve exact encoded symmetry.
    outer = [(0., KITE_SHOULDER_Y + KITE_UPPER_HEIGHT, LAMP_FACE_Z),
             (KITE_HALF_WIDTH, KITE_SHOULDER_Y, LAMP_FACE_Z),
             (0., KITE_SHOULDER_Y - KITE_LOWER_HEIGHT, LAMP_FACE_Z)]
    inner = [(x * KITE_INNER_RATIO,
              KITE_SHOULDER_Y + (y - KITE_SHOULDER_Y) * KITE_INNER_RATIO, z)
             for x, y, z in outer]
    for reflect in (False, True):
        for index in range(len(outer) - 1):
            following = index + 1
            triangle((outer[index], outer[following], inner[following]),
                     model.BRAKE_LIGHT, reflect=reflect)
            triangle((outer[index], inner[following], inner[index]),
                     model.BRAKE_LIGHT, reflect=reflect)
        triangle(inner, model.BLACK, reflect=reflect)
