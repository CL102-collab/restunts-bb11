#if defined(RESTUNTS_SDL3)
#include "../frame/frame_render_private.h"

void frame_hires_begin(void)
{
	polyinfo_set_expanded(frame_hires_enabled() || ghost_is_active());
	if (hires_active == 0) {
		frame_hires_reset();
	}
	frame_hires_adaptive_active =
		hires_active != 0 && frame_hires_snapshot_active() && frame_hires_adaptive.quality != 0;
	hires_extended_frame_adaptive_policy(&frame_hires_adaptive_active);
}

void frame_hires_prepare_ground(const struct FRAME_CAMERA *camera)
{
	if (hires_active != 0) {
		shape3d_hires_ground_begin(&camera->position);
		shape3d_hires_shadows_begin(&camera->position);
		frame_hires_add_car_shadow(&frame_state->playerstate, &simd_player, &camera->position,
								   &game3dshapes[PLAYER_CAR_WHEEL_SHAPE]);
		if (gameconfig.game_opponenttype != 0) {
			frame_hires_add_car_shadow(&frame_state->opponentstate, &simd_opponent,
									   &camera->position, &game3dshapes[OPPONENT_CAR_WHEEL_SHAPE]);
		}
	}
}

legacy_s16 frame_hires_draw_scene(struct FRAME_TILE_SELECTION *tiles, struct FRAME_CAMERA *camera,
								  struct FRAME_CAR_RENDER *cars, legacy_s8 buffer_index,
								  legacy_s8 redraw_transform_flags, legacy_s8 animated_material)
{
	if (hires_active == 0) {
		return 0;
	}
	frame_hires_draw_selected(tiles, camera, cars, buffer_index, redraw_transform_flags,
							  animated_material);
	return 1;
}

legacy_s16 frame_hires_dashboard_top(const struct RECTANGLE *cliprect)
{
	if (hires_active == 0 || replaybar_enabled == 0) {
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
