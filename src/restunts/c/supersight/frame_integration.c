#if !defined(RESTUNTS_SDL3)
#include "../frame/frame_render_private.h"

void frame_supersight_begin(void)
{
	polyinfo_set_expanded(frame_supersight_enabled());
	if (supersight_enabled == 0) {
		frame_supersight_reset();
	}
}

void frame_supersight_prepare_ground(const struct FRAME_CAMERA *camera)
{
	(void)camera;
}

legacy_s16 frame_supersight_draw_scene(struct FRAME_TILE_SELECTION *tiles,
									   struct FRAME_CAMERA *camera, struct FRAME_CAR_RENDER *cars,
									   legacy_s8 buffer_index, legacy_s8 redraw_transform_flags,
									   legacy_s8 animated_material)
{
	if (supersight_enabled == 0) {
		return 0;
	}
	frame_supersight_draw_selected(tiles, camera, cars, buffer_index, redraw_transform_flags,
								   animated_material);
	return 1;
}

legacy_s16 frame_supersight_dashboard_top(const struct RECTANGLE *cliprect)
{
	if (supersight_enabled == 0 || replaybar_enabled == 0) {
		return 0;
	}
	/* Custom tops can extend below the replay toolbar boundary. */
	sprite_set_target_clip_bounds(0, FRAME_SCREEN_WIDTH, 0, height_above_replaybar);
	shape2d_rle_mask_position_clipped(dasmshapeptr);
	shape2d_rle_or_position_clipped(
		(struct SHAPE2D far *)dos_memory_make_pointer(dastseg, dastbmp_y2));
	sprite_set_target_clip_bounds(0, FRAME_SCREEN_WIDTH, cliprect->top, cliprect->bottom);
	return 1;
}

#endif
