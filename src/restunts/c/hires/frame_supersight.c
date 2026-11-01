#include "../frame/frame_render_private.h"

#if !defined(RESTUNTS_SDL3)
const struct FRAME_SUPERSIGHT_TILE supersight_tiles[FRAME_SUPERSIGHT_TILE_COUNT] = {
	{-6, 4, 26}, {0, 10, 20}, {5, 6, 27},  {-5, 6, 27}, {6, 3, 24},	 {-6, 3, 24}, {2, 9, 24},
	{-2, 9, 24}, {6, 2, 22},  {-6, 2, 22}, {4, 7, 26},	{-4, 7, 26}, {3, 8, 25},  {-3, 8, 25},
	{5, 5, 25},	 {-5, 5, 25}, {1, 9, 21},  {-1, 9, 21}, {6, 1, 20},	 {-6, 1, 20}, {0, 9, 18},
	{5, 4, 23},	 {-5, 4, 23}, {4, 6, 24},  {-4, 6, 24}, {2, 8, 22},	 {-2, 8, 22}, {3, 7, 23},
	{-3, 7, 23}, {5, 3, 21},  {-5, 3, 21}, {1, 8, 19},	{-1, 8, 19}, {0, 8, 16},  {4, 5, 22},
	{-4, 5, 22}, {5, 2, 19},  {-5, 2, 19}, {2, 7, 20},	{-2, 7, 20}, {5, 1, 17},  {-5, 1, 17},
	{3, 6, 21},	 {-3, 6, 21}, {4, 4, 20},  {-4, 4, 20}, {1, 7, 17},	 {-1, 7, 17}, {0, 7, 14},
	{3, 5, 19},	 {-3, 5, 19}, {4, 3, 18},  {-4, 3, 18}, {2, 6, 18},	 {-2, 6, 18}, {4, 2, 16},
	{-4, 2, 16}, {1, 6, 15},  {-1, 6, 15}, {3, 4, 17},	{-3, 4, 17}, {4, 1, 14},  {-4, 1, 14},
	{0, 6, 12},	 {4, 0, 12},  {-4, 0, 12}, {2, 5, 16},	{-2, 5, 16}, {3, 3, 15},  {-3, 3, 15},
	{1, 5, 13},	 {-1, 5, 13}, {2, 4, 14},  {-2, 4, 14}, {0, 5, 10},	 {3, 2, 13},  {-3, 2, 13},
	{3, 1, 11},	 {-3, 1, 11}, {3, 0, 9},   {-3, 0, 9},	{1, 4, 11},	 {-1, 4, 11}, {2, 3, 12},
	{-2, 3, 12}, {0, 4, 8},	  {2, 2, 10},  {-2, 2, 10}, {1, 3, 9},	 {-1, 3, 9},  {2, 1, 8},
	{-2, 1, 8},	 {0, 3, 6},	  {2, -1, 8},  {-2, -1, 8}, {2, 0, 6},	 {-2, 0, 6},  {1, 2, 7},
	{-1, 2, 7},	 {0, 2, 4},	  {1, 1, 5},   {-1, 1, 5},	{1, -1, 5},	 {-1, -1, 5}, {1, 0, 3},
	{-1, 0, 3},	 {0, -2, 4},  {0, 1, 2},   {0, -1, 2},	{0, 0, 0},
};
#endif

#if !defined(RESTUNTS_SDL3)
const legacy_s8 supersight_detail_thresholds[] = {99, 20, 18, 16, 14, 12, 10};
#endif

#if !defined(RESTUNTS_SDL3)
legacy_s16 supersight_attempt_hint;
#endif

#if !defined(RESTUNTS_SDL3)
legacy_u8 supersight_probe_frames;
#endif

#if !defined(RESTUNTS_SDL3)
legacy_u8 supersight_view_valid;
#endif

#if !defined(RESTUNTS_SDL3)
legacy_s16 supersight_previous_view[FRAME_SUPERSIGHT_VIEW_KEY_COUNT];
#endif

#if defined(RESTUNTS_SDL3)
void frame_extend_lookahead(struct FRAME_TILE_SELECTION *tiles, const struct FRAME_CAMERA *camera)
{
	extended_frame_extend_lookahead(tiles, camera, frame_adaptive_active);
}
#endif

#if !defined(RESTUNTS_SDL3)
void frame_extend_lookahead(struct FRAME_TILE_SELECTION *tiles, const struct FRAME_CAMERA *camera)
{
	(void)camera;
	legacy_s8 east = tiles->lookahead[0].east;
	legacy_s8 south = tiles->lookahead[0].south;
	legacy_s8 depth_east = east == FRAME_LOOKAHEAD_DEPTH_TILES	  ? 1
						   : east == -FRAME_LOOKAHEAD_DEPTH_TILES ? -1
																  : 0;
	legacy_s8 depth_south = south == FRAME_LOOKAHEAD_DEPTH_TILES	? 1
							: south == -FRAME_LOOKAHEAD_DEPTH_TILES ? -1
																	: 0;
	legacy_s8 width_east = east == FRAME_LOOKAHEAD_WIDTH_TILES	  ? 1
						   : east == -FRAME_LOOKAHEAD_WIDTH_TILES ? -1
																  : 0;
	legacy_s8 width_south = south == FRAME_LOOKAHEAD_WIDTH_TILES	? 1
							: south == -FRAME_LOOKAHEAD_WIDTH_TILES ? -1
																	: 0;
	for (legacy_s16 index = 0; index < FRAME_SUPERSIGHT_TILE_COUNT; index++) {
		const struct FRAME_SUPERSIGHT_TILE *source = &supersight_tiles[index];
		struct FRAME_LOOKAHEAD_TILE *target = &tiles->extended_lookahead[index];
		target->east = source->depth * depth_east + source->width * width_east;
		target->south = source->depth * depth_south + source->width * width_south;
		target->detail = source->priority;
	}
	tiles->lookahead = tiles->extended_lookahead;
	tiles->count = FRAME_SUPERSIGHT_TILE_COUNT;
}
#endif

#if defined(RESTUNTS_SDL3)
void frame_supersight_reset(void)
{
	frame_adaptive_restart(&frame_adaptive);
	frame_adaptive_active = 0;
}
#endif

#if !defined(RESTUNTS_SDL3)
void frame_supersight_reset(void)
{
	supersight_attempt_hint = 0;
	supersight_probe_frames = 0;
	supersight_view_valid = 0;
}
#endif

#if defined(RESTUNTS_SDL3)
void frame_draw_supersight(struct FRAME_TILE_SELECTION *tiles, struct FRAME_CAMERA *camera,
						   struct FRAME_CAR_RENDER *cars, legacy_s8 buffer_index,
						   legacy_s8 redraw_transform_flags, legacy_s8 animated_material)
{
	(void)buffer_index;
	/* Full quality bypasses the adaptive policy; reductions apply only to
	 * isolated presentation snapshots and never to authoritative rendering. */
	tiles->detail_threshold = FRAME_TILE_DETAIL_FULL + 1;
	frame_place_cars(tiles, cars);
	if (frame_draw_tiles(tiles, camera, cars, redraw_transform_flags, animated_material) == 0 &&
		frame_adaptive_active != 0) {
		frame_draw_adaptive_particles(camera, cars, redraw_transform_flags);
	}
}
#endif

#if !defined(RESTUNTS_SDL3)
void frame_draw_supersight(struct FRAME_TILE_SELECTION *tiles, struct FRAME_CAMERA *camera,
						   struct FRAME_CAR_RENDER *cars, legacy_s8 buffer_index,
						   legacy_s8 redraw_transform_flags, legacy_s8 animated_material)
{
	/* Failed attempts stop immediately. Reuse tile lookup and car placement,
	 * and avoid repeating known-overfull quality levels on nearby frames. */
	struct FRAME_CAR_RENDER placed_cars[FRAME_EXPLOSION_CAR_COUNT] = {{0}};
	frame_place_cars(tiles, placed_cars);
	legacy_s16 attempt = frame_supersight_first_attempt(tiles, camera);
	tiles->first = attempt > 1 ? (attempt - 1) * FRAME_SUPERSIGHT_DISCARD_BATCH : 0;
	if (tiles->first > FRAME_SUPERSIGHT_TILE_COUNT - FRAME_SUPERSIGHT_MINIMUM_TILES) {
		tiles->first = FRAME_SUPERSIGHT_TILE_COUNT - FRAME_SUPERSIGHT_MINIMUM_TILES;
	}
	legacy_s8 initial_brake_paint = backlights_paint_override;
	frame_racing_line_enabled = 1;
	frame_racing_line_submitted = 0;
	for (;;) {
		legacy_s16 threshold_index = attempt;
		if (threshold_index >= (legacy_s16)sizeof(supersight_detail_thresholds)) {
			threshold_index = sizeof(supersight_detail_thresholds) - 1;
		}
		tiles->detail_threshold = supersight_detail_thresholds[threshold_index];
		cars[PLAYER_CAR_INDEX] = placed_cars[PLAYER_CAR_INDEX];
		cars[OPPONENT_CAR_INDEX] = placed_cars[OPPONENT_CAR_INDEX];
		legacy_s16 exhausted =
			frame_draw_tiles(tiles, camera, cars, redraw_transform_flags, animated_material);
		if (frame_retry_without_racing_line() == 0) {
			if (exhausted == 0 ||
				tiles->first == FRAME_SUPERSIGHT_TILE_COUNT - FRAME_SUPERSIGHT_MINIMUM_TILES) {
				supersight_attempt_hint = attempt;
				frame_racing_line_enabled = 1;
				return;
			}
			attempt++;
			if (attempt > 1) {
				tiles->first += FRAME_SUPERSIGHT_DISCARD_BATCH;
				if (tiles->first > FRAME_SUPERSIGHT_TILE_COUNT - FRAME_SUPERSIGHT_MINIMUM_TILES) {
					tiles->first = FRAME_SUPERSIGHT_TILE_COUNT - FRAME_SUPERSIGHT_MINIMUM_TILES;
				}
			}
		}
		polyinfo_reset();
		backlights_paint_override = initial_brake_paint;
		frame_begin(buffer_index);
		frame_draw_clouds(camera, redraw_transform_flags);
	}
}
#endif

#if !defined(RESTUNTS_SDL3)
legacy_s16 frame_supersight_first_attempt(const struct FRAME_TILE_SELECTION *tiles,
										  const struct FRAME_CAMERA *camera)
{
	/* Reuse nearby views, but immediately probe full quality after a change
	 * of camera, graphics settings, viewport, tile, or heading sector. Explicit
	 * race/replay seek hooks invalidate the hint even when the pose is similar. */
	legacy_s16 view[FRAME_SUPERSIGHT_VIEW_KEY_COUNT];
	view[0] = tiles->camera_east;
	view[1] = tiles->camera_south;
	view[2] = cameramode;
	view[3] = followOpponentFlag;
	view[4] = detail_level;
	view[5] = slow_video_mgmt_copy;
	view[6] = LEGACY_S16_SAR(camera->yaw, FRAME_LOOKAHEAD_HEADING_SHIFT);
	view[7] = LEGACY_S16_SAR(camera->pitch, FRAME_LOOKAHEAD_HEADING_SHIFT);
	view[8] = LEGACY_S16_SAR(camera->roll, FRAME_LOOKAHEAD_HEADING_SHIFT);
	view[9] = LEGACY_S16_SAR(camera->position.y, FRAME_CAMERA_TILE_SHIFT);
	view[10] = select_rect_rc.left;
	view[11] = select_rect_rc.right;
	view[12] = select_rect_rc.top;
	view[13] = select_rect_rc.bottom;
	view[14] = cameramode == CAMERA_MODE_CUSTOM ? custom_camera.distance : 0;
	view[15] = cameramode == CAMERA_MODE_CUSTOM ? custom_camera.elevation_angle : 0;
	view[16] = cameramode == CAMERA_MODE_CUSTOM ? custom_camera.azimuth_angle : 0;
	view[17] = cameramode == CAMERA_MODE_TRACKSIDE ? camera->position.x : 0;
	view[18] = cameramode == CAMERA_MODE_TRACKSIDE ? camera->position.y : 0;
	view[19] = cameramode == CAMERA_MODE_TRACKSIDE ? camera->position.z : 0;

	for (legacy_s16 index = 0; index < FRAME_SUPERSIGHT_VIEW_KEY_COUNT; index++) {
		if (view[index] != supersight_previous_view[index]) {
			supersight_view_valid = 0;
		}
		supersight_previous_view[index] = view[index];
	}
	if (supersight_view_valid == 0 || supersight_probe_frames >= FRAME_SUPERSIGHT_PROBE_INTERVAL) {
		supersight_attempt_hint = 0;
		supersight_probe_frames = 0;
		supersight_view_valid = 1;
	}
	supersight_probe_frames++;
	return supersight_attempt_hint;
}
#endif

void frame_hires_prepare_tiles(struct FRAME_TILE_SELECTION *tiles,
							   const struct FRAME_CAMERA *camera)
{
#if defined(RESTUNTS_SDL3)
	if (frame_adaptive_active != 0) {
		frame_adaptive_prepare(&frame_adaptive, tiles->camera_east, tiles->camera_south,
							   cos_fast(camera->view_heading), sin_fast(camera->view_heading));
	}
#endif
	if (supersight_enabled != 0) {
		frame_extend_lookahead(tiles, camera);
	}
}
