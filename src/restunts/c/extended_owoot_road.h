#ifndef RESTUNTS_EXTENDED_OWOOT_ROAD_H
#define RESTUNTS_EXTENDED_OWOOT_ROAD_H

#include "owoot_road.h"

/* Returns one when the active extended track owns the overlap result. */
#ifdef RESTUNTS_SDL3
legacy_s16 extended_owoot_road_overlap(const struct VECTOR *vertices, legacy_u16 ring_count,
									   legacy_s16 contact_tolerance, legacy_s16 *overlap);
#else
static inline legacy_s16 extended_owoot_road_overlap(const struct VECTOR *vertices,
													 legacy_u16 ring_count,
													 legacy_s16 contact_tolerance,
													 legacy_s16 *overlap)
{
	(void)vertices;
	(void)ring_count;
	(void)contact_tolerance;
	(void)overlap;
	return 0;
}
#endif

#endif
