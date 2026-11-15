#ifndef RESTUNTS_UNIT_SHORTCUT_H
#define RESTUNTS_UNIT_SHORTCUT_H

#include "unit_planner.h"

/* Shared with the planner: changing corridor expansion requires changing the
 * geometric proof, rather than silently making a wider line eligible. */
#define UNIT_SHORTCUT_EXTRA_WIDTH_FRACTION 0.15
#define UNIT_SHORTCUT_MAX_LANE_FRACTION 0.80
#define UNIT_SHORTCUT_ROAD_CLEARANCE 12.0

/* Main-thread only, after the complete route and car limits are available.
 * Clears every flag, including when OWOOT is enabled. A marked point permits
 * a wider line only within its existing flat, obstacle-free route footprint;
 * it never permits omitting a route piece or checkpoint. The expanded line,
 * adjoining line segments and swept car radius retain one native physics
 * tick of clearance from the footprint boundary. Native penalty detection
 * remains authoritative for the actual driven trajectory. */
void unit_shortcut_mark_route(struct UNIT_ROUTE_POINT *points, legacy_u32 count,
							  const struct UNIT_CAR_LIMITS *limits, legacy_u8 owoot);

#endif
