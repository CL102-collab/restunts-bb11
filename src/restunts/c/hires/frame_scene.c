#if defined(RESTUNTS_SDL3)
#include "../frame/frame_render_private.h"

void frame_hires_extend_lookahead(struct FRAME_TILE_SELECTION *tiles,
								  const struct FRAME_CAMERA *camera)
{
	hires_extended_frame_extend_lookahead(tiles, camera, frame_hires_adaptive_active);
}

void frame_hires_reset(void)
{
	frame_hires_adaptive_restart(&frame_hires_adaptive);
	frame_hires_adaptive_active = 0;
}

void frame_hires_draw_selected(struct FRAME_TILE_SELECTION *tiles, struct FRAME_CAMERA *camera,
							   struct FRAME_CAR_RENDER *cars, legacy_s8 buffer_index,
							   legacy_s8 redraw_transform_flags, legacy_s8 animated_material)
{
	(void)buffer_index;
	/* Full quality bypasses the adaptive policy; reductions apply only to
	 * isolated presentation snapshots and never to authoritative rendering. */
	tiles->detail_threshold = FRAME_TILE_DETAIL_FULL + 1;
	frame_place_cars(tiles, cars);
	if (frame_draw_tiles(tiles, camera, cars, redraw_transform_flags, animated_material) == 0 &&
		frame_hires_adaptive_active != 0) {
		frame_hires_draw_adaptive_particles(camera, cars, redraw_transform_flags);
	}
}

void frame_hires_prepare_tiles(struct FRAME_TILE_SELECTION *tiles,
							   const struct FRAME_CAMERA *camera)
{
	if (frame_hires_adaptive_active != 0) {
		frame_hires_adaptive_prepare(&frame_hires_adaptive, tiles->camera_east, tiles->camera_south,
									 cos_fast(camera->view_heading),
									 sin_fast(camera->view_heading));
	}
	if (hires_active != 0) {
		frame_hires_extend_lookahead(tiles, camera);
	}
}

#endif
