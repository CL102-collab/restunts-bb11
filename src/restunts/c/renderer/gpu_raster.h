#ifndef RESTUNTS_GPU_RASTER_H
#define RESTUNTS_GPU_RASTER_H

#include "graphics_backend.h"
#include "../hires/hypervision.h"
#include "../shape2d/shape2d.h"

struct POINT2D;

#define GPU_RASTER_TILE_SIZE 16U
#define GPU_RASTER_HEADER_WORDS 20U
#define GPU_RASTER_COMMAND_WORDS 18U
#define GPU_RASTER_EDGE_WORDS 7U
#define GPU_RASTER_OUTPUT_WORDS 3U
#define GPU_RASTER_TOUCHED 256U
#define GPU_RASTER_LINE 8U
#define GPU_RASTER_LEGACY 16U
#define GPU_RASTER_CLASSIC 1U
#define GPU_RASTER_HIRES 0U

/* Screen-space edges and immutable material records are shared with the CPU
 * adapter. The wire format stores binary32 values, never native structures.
 * LEGACY edges instead contain raw line setup: x/y are integer start coordinates,
 * z/x_step are their unsigned fractions, z_step is the 16.16 increment, and
 * top/bottom are mode/inclusive sample count. Their explicit command bounds are
 * clipped by the common tile builder; the shader determines covered pixels. */
struct GPU_RASTER_EDGE {
	legacy_f64 x, y, z, x_step, z_step;
	legacy_s32 top, bottom;
};
struct GPU_RASTER_COMMAND {
	struct HIRES_BACKEND_MATERIAL material;
	legacy_u32 first_edge, hires_edge_count;
	legacy_s32 top, bottom;
	struct HIRES_DEPTH_PLANE plane;
	legacy_s32 planar;
	legacy_u32 bounds[4];
};

/* GPU output is synchronous. Untouched samples retain CPU artwork/background;
 * depth and family are available immediately for shadows and incremental work. */
legacy_s32 gpu_raster_hires(const struct HIRES_RASTER_TARGET *target,
							const struct GPU_RASTER_COMMAND *commands, size_t command_count,
							const struct GPU_RASTER_EDGE *edges, size_t edge_count,
							const struct HIRES_DEPTH_PLANE *ground, legacy_s32 preserve_depth);
legacy_s32 gpu_raster_submit(legacy_s32 width, legacy_s32 height, legacy_s32 left, legacy_s32 right,
							 legacy_s32 top, legacy_s32 bottom, legacy_u32 mode,
							 const struct GPU_RASTER_COMMAND *commands, size_t command_count,
							 const struct GPU_RASTER_EDGE *edges, size_t edge_count,
							 const struct HIRES_DEPTH_PLANE *ground, legacy_s32 preserve_depth,
							 legacy_u32 *output);

/* Only the classic 3D queue opens this scope. Shared menu/sprite callers remain
 * CPU operations outside it; logical DOS residue is still retained by callers. */
void gpu_raster_classic_begin(void);
void gpu_raster_classic_end(void);
void gpu_raster_classic_shutdown(void);
legacy_s32 gpu_raster_classic_active(void);
legacy_s32 gpu_raster_classic_polygon(legacy_u16 color, legacy_u16 alternate, legacy_u16 pattern,
									  legacy_u8 mode, legacy_u16 count,
									  const struct POINT2D *points, legacy_s32 patterned_line);
legacy_s32 gpu_raster_classic_line(legacy_s16 x0, legacy_s16 y0, legacy_s16 x1, legacy_s16 y1,
								   legacy_u16 color);
legacy_s32 gpu_raster_classic_span(legacy_s16 left, legacy_s16 right, legacy_s16 y,
								   legacy_u16 color);
void gpu_raster_shutdown(void);

#endif
