#include "skybox_scene.h"

#ifdef RESTUNTS_SDL3
#include "skybox_hires.h"
#include "../frame_internal.h"
#include "../shape2d/shape2d_internal.h"
#include "../externs.h"

legacy_s16 skybox_hires_render_scene(struct RECTANGLE *clip, const struct MATRIX *rotation,
									 legacy_s16 direction, legacy_s16 angle, legacy_s16 camera_y,
									 legacy_s16 full_redraw)
{
	struct SPRITE target = drawing_sprite;
	target.sprite_raster_left = clip->left;
	target.sprite_raster_right = clip->right;
	target.sprite_top = clip->top;
	target.sprite_bottom = clip->bottom;
	if (skybox_hires_render(&target, &skybox, skyboxes, loaded_skybox_index, rotation, direction,
							angle, camera_y, detail_level)) {
		/* Presentation must copy all companion pixels, including transitions
		 * back from inverted views to the legacy dirty-rectangle path. */
		rect_skybox = *clip;
		return 1;
	}
	return full_redraw;
}
#endif
