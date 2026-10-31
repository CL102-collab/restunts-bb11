#ifndef RESTUNTS_OWOOT_ROAD_INTERNAL_H
#define RESTUNTS_OWOOT_ROAD_INTERNAL_H

#include "owoot_road.h"

#define OWOOT_HILL_ROAD_MODEL 36U
#define OWOOT_ROAD_TILE_OVERHANG 8

struct OWOOT_ROAD_TRIANGLE {
	struct VECTOR vertex[OWOOT_ROAD_TRIANGLE_VERTEX_COUNT];
};

/* Generic exact tire/triangle overlap shared by both track formats. */
legacy_s16 owoot_road_triangle_overlaps_wheel(const struct OWOOT_ROAD_TRIANGLE far *triangle,
											  const struct VECTOR *vertices, legacy_u16 ring_count,
											  legacy_s16 contact_tolerance);

#endif
