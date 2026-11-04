#ifndef RESTUNTS_GPU_SCENE_H
#define RESTUNTS_GPU_SCENE_H

#include "../hires/hypervision.h"

struct SPRITE;
struct POINT2D;

/* The direct scene path is available only after the native presenter has
 * adopted the GPU image. Headless tools retain the compute/readback backend. */
legacy_s32 gpu_scene_active(void);
/* Tiny presenter-backed previews use the existing CPU raster paths. Headless
 * Vulkan and other native backends retain their compute fallback. */
legacy_s32 gpu_scene_software_preview(void);
void gpu_scene_begin(legacy_s32 classic);
/* width/height describe the logical320x200 framebuffer, not the inset clip. */
legacy_s32 gpu_scene_target_eligible(const struct SPRITE *sprite, legacy_s32 width,
									 legacy_s32 height);
void gpu_scene_classic_begin(const struct SPRITE *sprite, legacy_s32 width, legacy_s32 height);
void gpu_scene_polygon(const struct HIRES_BACKEND_VERTEX *vertices, legacy_u32 count,
					   const struct HIRES_BACKEND_MATERIAL *material);
void gpu_scene_line(legacy_f32 x0, legacy_f32 y0, legacy_f32 x1, legacy_f32 y1,
					const struct HIRES_BACKEND_MATERIAL *material);
legacy_s32 gpu_scene_flush(const struct HIRES_RASTER_TARGET *target, legacy_s32 preserve_depth);
legacy_s32 gpu_scene_classic_flush(const struct SPRITE *sprite, legacy_s32 width,
								   legacy_s32 height);
void gpu_scene_shutdown(void);

#endif
