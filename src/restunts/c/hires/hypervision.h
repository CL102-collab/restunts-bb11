#ifndef RESTUNTS_HYPERVISION_H
#define RESTUNTS_HYPERVISION_H

#include "hires.h"

#define HIRES_BACKEND_MAX_VERTICES 64U
#define HIRES_BACKEND_BACKGROUND 1U
#define HIRES_BACKEND_DECAL 2U
#define HIRES_BACKEND_GROUND_CLIP 4U

/* The scene adapter submits clipped screen-space geometry. No game resources,
 * mutable simulation state, or legacy polygon records cross this boundary. */
struct HIRES_BACKEND_VERTEX {
	legacy_f64 x, y, inverse_z;
};
struct HIRES_BACKEND_MATERIAL {
	legacy_u32 family;
	legacy_u16 color, alternate, pattern;
	legacy_u8 mode, flags;
};

/* One immutable command stream per joined frame. Polygons use even-odd coverage;
 * inverse depth varies linearly along each scanline, including concave models. */
void hires_backend_begin(const struct HIRES_DEPTH_PLANE *ground);
void hires_backend_polygon(const struct HIRES_BACKEND_VERTEX *vertices, legacy_u32 count,
						   const struct HIRES_BACKEND_MATERIAL *material);
/* Build immutable command groups in parallel, then join in group order. The
 * callback writes only its supplied buffer; NULL selects the serial frame. */
struct HIRES_BACKEND_BUFFER;
void hires_backend_polygon_to(struct HIRES_BACKEND_BUFFER *buffer,
							  const struct HIRES_BACKEND_VERTEX *vertices, legacy_u32 count,
							  const struct HIRES_BACKEND_MATERIAL *material);
void hires_backend_build(legacy_s32 count,
						 void (*job)(void *, legacy_s32, struct HIRES_BACKEND_BUFFER *),
						 void *context);
/* Preserve depth is for incremental callers only. Complete scenes resolve solid
 * visibility in spans, then sample cutout materials against those visible spans. */
legacy_s32 hires_backend_end(const struct HIRES_RASTER_TARGET *target, legacy_s32 preserve_depth);
void hires_backend_shutdown(void);

#endif
