#ifndef RESTUNTS_VULKAN_SCENE_H
#define RESTUNTS_VULKAN_SCENE_H

#include "graphics_backend.h"

#define VULKAN_SCENE_TEXTURE_WIDTH 1280U
#define VULKAN_SCENE_TEXTURE_HEIGHT 800U
#define VULKAN_SCENE_PALETTE_COLORS 256U

/* Screen-space triangles. Material values are flat for each triangle. */
struct VULKAN_SCENE_VERTEX {
	legacy_f32 x, y, inverse_depth;
	legacy_u32 color, alternate, pattern, mode, flags;
};

/* The sampled image uses RGBA8 UNORM and remains owned by this backend.
 * SDL owns only its view; destroy that external SDL texture before shutdown. */
struct VULKAN_SCENE_HANDLES {
	void *instance, *physical_device, *device;
	legacy_u32 queue_family;
	legacy_u64 image;
	legacy_u32 image_layout, image_format, texture_width, texture_height;
	legacy_s32 presentable;
};

legacy_s32 vulkan_scene_get_handles(struct VULKAN_SCENE_HANDLES *handles, legacy_char *error,
									size_t error_capacity);
void vulkan_scene_set_active(legacy_s32 active);
legacy_s32 vulkan_scene_active(void);
void vulkan_scene_palette(const legacy_u32 *argb);
/* Repaint retained geometry only when the palette changes without a new scene,
 * as in intro fades. Ordinary presentation is an immediate no-op. */
legacy_s32 vulkan_scene_refresh_palette(legacy_char *error, size_t error_capacity);
legacy_s32 vulkan_scene_submit(const struct VULKAN_SCENE_VERTEX *vertices, size_t count,
							   legacy_s32 width, legacy_s32 height, legacy_s32 left,
							   legacy_s32 right, legacy_s32 top, legacy_s32 bottom,
							   legacy_s32 preserve, legacy_s32 classic, legacy_char *error,
							   size_t error_capacity);
/* Explicit screenshot/debug readback; interactive frames never call this.
 * Words contain RGBA bytes in memory, not native-endian ARGB palette values. */
legacy_s32 vulkan_scene_readback(legacy_u32 *rgba, legacy_s32 width, legacy_s32 height,
								 legacy_char *error, size_t error_capacity);
/* Returns zero on a bounded completion timeout, retaining live resources. */
legacy_s32 vulkan_scene_shutdown(void);

#endif
