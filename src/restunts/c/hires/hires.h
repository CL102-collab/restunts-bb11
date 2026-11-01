#ifndef RESTUNTS_HIRES_H
#define RESTUNTS_HIRES_H

#include "../legacy.h"
#include "../video/video_framebuffer.h"

/* Maximum raster size and authored artwork scale. Active dimensions may be smaller. */
#define HIRES_INITIAL_PRIMITIVE_CAPACITY 592U
#define HIRES_SCALE 4
#define HIRES_MEDIUM_SCALE 2
#define HIRES_MINIMUM_SCALE 1
#define HIRES_WIDTH 1280
#define HIRES_HEIGHT 800
#define HIRES_SAMPLE_CENTER_OFFSET 0.5
#define HIRES_DEPTH_FAMILY_NONE 0U
/* Legacy paint patterns contain two byte-wide rows, most significant bit first. */
#define HIRES_PATTERN_WIDTH LEGACY_BYTE_BITS
#define HIRES_PATTERN_HEIGHT LEGACY_WORD_BYTES
#define HIRES_PATTERN_COLUMN_MASK ((legacy_s32)HIRES_PATTERN_WIDTH - 1)
#define HIRES_PATTERN_ROW_MASK ((legacy_s32)HIRES_PATTERN_HEIGHT - 1)
enum HIRES_PAINT_MODE { HIRES_PAINT_SOLID = 0, HIRES_PAINT_PATTERN = 1, HIRES_PAINT_ALTERNATE = 2 };

struct SPRITE;
struct HIRES_SURFACE;

/* Inverse depth at pixel centers: x_step*x + y_step*y + origin. */
struct HIRES_DEPTH_PLANE {
	legacy_f64 x_step, y_step, origin;
};

/* Immutable for one joined raster pass. Screen rows must map to disjoint
 * legacy cells; preparation rejects targets that cannot be split safely. */
struct HIRES_RASTER_TARGET {
	struct HIRES_SURFACE *surface;
	legacy_u16 rows[HIRES_HEIGHT / HIRES_SCALE];
	legacy_s32 left, right, top, bottom;
	legacy_s32 scale, scale_shift, scale_mask, cell_pixels, cell_shift;
	legacy_s32 width, height;
	legacy_f32 *inverse_depth;
	legacy_u32 *depth_family;
	legacy_s32 depth_left, depth_right, depth_top, depth_bottom;
};

/* Each job owns complete legacy rows: top/bottom are multiples of target->scale.
 * Contexts must not overlap, and their counters start at zero. */
struct HIRES_RASTER_CONTEXT {
	const struct HIRES_RASTER_TARGET *target;
	legacy_s32 top, bottom;
	legacy_u32 cleared_argb_cells;
};

/* SDL3-only companion pixels. Legacy sprite offsets and resources stay 16-bit. */
void hires_set_enabled(legacy_s32 enabled);
legacy_s32 hires_enabled(void);
/* Select 4, 2 or 1 samples per axis between joined frames. Changing scale discards
 * cached companion pixels, so the next frame must redraw its scene. Invalid values
 * and changes inside hires_begin/end are ignored. Enable transitions reset to 4.
 * Getters describe the enhanced raster even when HyperVision is disabled. */
void hires_set_render_scale(legacy_s32 scale);
legacy_s32 hires_render_scale(void);
legacy_s32 hires_render_width(void);
legacy_s32 hires_render_height(void);
legacy_s32 hires_begin(const struct SPRITE *target);
void hires_end(void);
enum HIRES_DEPTH_MODE { HIRES_DEPTH_SURFACE, HIRES_DEPTH_ATTACHED, HIRES_DEPTH_ORDERED };

/* Bounds use high-resolution pixels with exclusive right/bottom edges.
 * Family zero is reserved. Attached decals retain their parent's depth;
 * ordered shapes share a family and retain their nearest supporting depth. */
void hires_depth_begin(legacy_s32 left, legacy_s32 right, legacy_s32 top, legacy_s32 bottom);
legacy_s32 hires_depth_test(legacy_s32 x, legacy_s32 y, legacy_f64 inverse_z, legacy_u32 family,
							legacy_s32 mode);
/* Prepare after hires_begin/hires_depth_begin. Keep the target immutable and
 * its buffers alive; no other drawing may overlap these jobs. After joining,
 * finish once on the main thread with all job counters summed, before hires_end. */
legacy_s32 hires_raster_prepare(struct HIRES_RASTER_TARGET *target);
legacy_s32 hires_raster_depth_test(struct HIRES_RASTER_CONTEXT *context, legacy_s32 x, legacy_s32 y,
								   legacy_f64 inverse_z, legacy_u32 family, legacy_s32 mode);
/* Additional line/stroke/decal coverage must not show through uncovered ground.
 * A null context uses the active target; ground must point to the scene plane.
 * Existing surface ownership and attached-detail ordering take precedence. */
void hires_coverage_pixel(struct HIRES_RASTER_CONTEXT *context, legacy_s32 x, legacy_s32 y,
						  legacy_f64 inverse_z, legacy_u32 family, legacy_s32 mode, legacy_u8 color,
						  const struct HIRES_DEPTH_PLANE *ground);
void hires_raster_pixel(struct HIRES_RASTER_CONTEXT *context, legacy_s32 x, legacy_s32 y,
						legacy_u8 color);
/* Draw an exclusive-right scanline with the same sequential depth interpolation
 * and paint ordering as the per-pixel APIs. The context owns complete cells. */
void hires_raster_span(struct HIRES_RASTER_CONTEXT *context, legacy_s32 left, legacy_s32 right,
					   legacy_s32 y, legacy_f64 inverse_z, legacy_f64 depth_step, legacy_u32 family,
					   legacy_s32 depth_mode, legacy_u16 color, legacy_u16 alternate,
					   legacy_u16 pattern, legacy_s32 paint_mode, legacy_s32 depth_test);
/* HyperVision has already resolved this opaque span. Write each visible sample
 * once, without repeating visibility tests or touching legacy simulation data. */
void hires_raster_resolved_span(struct HIRES_RASTER_CONTEXT *context, legacy_s32 left,
								legacy_s32 right, legacy_s32 y, legacy_f64 inverse_z,
								legacy_f64 depth_step, legacy_u32 family, legacy_u16 color,
								legacy_u16 alternate, legacy_u16 pattern, legacy_s32 paint_mode);
/* Replace a complete logical row of the prepared target. Samples are contiguous
 * cell-major blocks of scale*scale bytes, starting at target->left / scale.
 * Jobs own whole logical rows; ARGB retirement is counted until the join. */
void hires_raster_fill_row(struct HIRES_RASTER_CONTEXT *context, legacy_s32 y, legacy_u8 color);
void hires_raster_write_row(struct HIRES_RASTER_CONTEXT *context, legacy_s32 y,
							const legacy_u8 *samples);
void hires_raster_finish(const struct HIRES_RASTER_TARGET *target, legacy_u32 cleared_argb_cells);
/* Optional full-color artwork uses the same clipping and sprite-copy lifetime.
 * Allocation failure leaves the indexed fallback intact. */
legacy_s32 hires_begin_argb(const struct SPRITE *target);
void hires_argb_pixel(legacy_s32 x, legacy_s32 y, legacy_u32 color);
/* Darken existing samples without changing their indexed color or scene depth. */
legacy_s32 hires_shadow_begin(void);
void hires_shadow_pixel(legacy_s32 x, legacy_s32 y, legacy_u8 opacity);
const legacy_u32 *hires_framebuffer_argb(const legacy_u8 *legacy, const legacy_u32 *palette);
/* Compose the enhanced output directly into caller-owned rows.
 * Disabled rendering leaves the destination untouched.
 * Pitch is in bytes; each row must be aligned for legacy_u32 and hold a full row. */
void hires_copy_framebuffer_argb(const legacy_u8 *legacy, const legacy_u32 *palette,
								 legacy_u32 *destination, legacy_s32 pitch);
void hires_pixel(legacy_s32 x, legacy_s32 y, legacy_u8 color);
/* Fill all companion samples at logical 320x200 coordinates without changing the legacy byte. */
void hires_fill_pixel(legacy_s32 x, legacy_s32 y, legacy_u8 color);
/* Copy scale*scale row-major samples into one logical cell, retiring its ARGB overlay. */
void hires_write_pixel(legacy_s32 x, legacy_s32 y, const legacy_u8 *samples);
void hires_write(const legacy_u8 *base, legacy_u16 offset, legacy_u8 color);
void hires_raster(const legacy_u8 *destination, legacy_u16 destination_offset,
				  const legacy_u8 *source, legacy_u16 source_offset, legacy_u16 count,
				  legacy_s16 operation, const legacy_u8 *palette);
void hires_forget(const void *base);
void hires_forget_range(const void *base, legacy_u32 size);
/* Return enhanced samples, or NULL without changing dimensions when disabled. */
const legacy_u8 *hires_framebuffer(const legacy_u8 *legacy, legacy_s32 *width, legacy_s32 *height);
legacy_u32 hires_generation(void);
void hires_shutdown(void);

#endif
