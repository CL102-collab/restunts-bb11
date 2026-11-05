#ifndef RESTUNTS_HYPERVISION_INTERNAL_H
#define RESTUNTS_HYPERVISION_INTERNAL_H

#include "hypervision.h"
#include <stddef.h>

/* Planar commands need only screen crossings. Keep their common edge stream
 * compact and retain exact depth interpolation separately for warped polygons. */
struct HIRES_VISIBILITY_EDGE {
	legacy_f64 x, y, x_step;
	legacy_s32 top, bottom;
};
struct HIRES_VISIBILITY_EDGE_DEPTH {
	legacy_f64 z, z_step;
};
struct HIRES_VISIBILITY_COMMAND {
	struct HIRES_BACKEND_MATERIAL material;
	legacy_u32 first_edge;
	legacy_u16 hires_edge_count;
	legacy_s16 top, bottom;
	legacy_u16 planar;
	union {
		struct HIRES_DEPTH_PLANE plane;
		legacy_u32 first_depth;
	} depth;
};

enum {
	HIRES_VISIBILITY_SOLID,
	HIRES_VISIBILITY_DETAIL,
	HIRES_VISIBILITY_PATTERN,
	HIRES_VISIBILITY_CLASS_COUNT
};
struct HIRES_VISIBILITY_LIST {
	legacy_u32 *commands;
	size_t count, capacity;
};

/* Private records for scanline crossings and the ordered visibility partition. */
struct HIRES_VISIBILITY_SPAN {
	legacy_s32 left, right;
	legacy_f64 origin, step;
	legacy_u32 command;
};
struct HIRES_VISIBILITY_CROSSING {
	legacy_f64 x, z;
};

#endif
