#ifndef RESTUNTS_SKYBOX_HIRES_H
#define RESTUNTS_SKYBOX_HIRES_H

#include "../legacy.h"
#include "../skybox.h"

struct SPRITE;
struct SHAPE2D;
struct MATRIX;

/* Capture the original six-bit VGA palette, independently of later fades. */
void skybox_hires_set_palette(const legacy_u8 *palette);
void skybox_hires_unload(void);
void skybox_hires_draw(const struct SPRITE *target, legacy_s16 theme, legacy_s16 image,
					   legacy_s32 width, legacy_s32 height, legacy_s32 x, legacy_s32 y);

/* Draw a complete camera-oriented background. Returns nonzero when the full
 * target clip has been redrawn; legacy sprite bytes are left intact. */
legacy_s32 skybox_hires_render(const struct SPRITE *target, const struct SKYBOX *scenery,
							   struct SHAPE2D *const shapes[SKYBOX_IMAGE_COUNT], legacy_s16 theme,
							   const struct MATRIX *rotation, legacy_s16 direction,
							   legacy_s16 angle, legacy_s16 camera_y, legacy_s16 detail);

#endif
