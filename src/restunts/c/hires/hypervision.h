#ifndef RESTUNTS_HYPERVISION_H
#define RESTUNTS_HYPERVISION_H

#include "hires.h"

#define HYPERVISION_MAX_VERTICES 64U
#define HYPERVISION_BACKGROUND 1U
#define HYPERVISION_DECAL 2U
#define HYPERVISION_GROUND_CLIP 4U

/* The scene adapter submits clipped screen-space geometry. No game resources,
 * mutable simulation state, or legacy polygon records cross this boundary. */
struct HYPERVISION_VERTEX {
	legacy_f64 x, y, inverse_z;
};
struct HYPERVISION_MATERIAL {
	legacy_u32 family;
	legacy_u16 color, alternate, pattern;
	legacy_u8 mode, flags;
};

/* One immutable command stream per joined frame. Polygons use even-odd coverage;
 * inverse depth varies linearly along each scanline, including concave models. */
void hypervision_begin(const struct HIRES_DEPTH_PLANE *ground);
void hypervision_polygon(const struct HYPERVISION_VERTEX *vertices, legacy_u32 count,
						 const struct HYPERVISION_MATERIAL *material);
/* Preserve depth is for incremental callers only. Complete scenes resolve solid
 * visibility in spans, then sample cutout materials against those visible spans. */
legacy_s32 hypervision_end(const struct HIRES_RASTER_TARGET *target, legacy_s32 preserve_depth);
void hypervision_shutdown(void);

#endif
