#include "frame_render_private.h"

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
/* Geometry decisions use one cached byte per tile. Intersecting the masks for
 * an object's complete footprint keeps it whenever any covered tile needs it. */
legacy_u8 frame_adaptive_tile_flags(const struct FRAME_TILE *tile)
{
	legacy_u8 flags = frame_adaptive_flags(&frame_adaptive, tile->east, tile->south);
	if (tile->element != 0) {
		legacy_u8 footprint = trkObjectList[tile->element].ss_multiTileFlag;
		legacy_s16 last_east = tile->east + ((footprint & FRAME_MULTITILE_COLUMN) != 0);
		legacy_s16 last_south = tile->south + ((footprint & FRAME_MULTITILE_ROW) != 0);
		for (legacy_s16 south = tile->south; south <= last_south; south++) {
			for (legacy_s16 east = tile->east; east <= last_east; east++) {
				flags &= frame_adaptive_flags(&frame_adaptive, east, south);
			}
		}
	}
	return flags;
}
#endif

#if defined(RESTUNTS_SDL3)
legacy_s32 frame_has_complete_tile_lookup(const struct FRAME_TILE_SELECTION *tiles)
{
	return extended_frame_has_complete_tile_lookup(tiles);
}
#endif

#if defined(RESTUNTS_SDL3)
legacy_s16 frame_lookup_world_tile(const struct FRAME_TILE_SELECTION *tiles, legacy_s16 east,
								   legacy_s16 south)
{
	return extended_frame_lookup_world_tile(tiles, east, south);
}
#endif

void frame_resolve_track_tile(struct FRAME_TILE *tile)
{
	if (tile->element != 0) {
		if (tile->terrain >= FRAME_HILL_ROAD_TERRAIN_FIRST &&
			tile->terrain < FRAME_HILL_ROAD_TERRAIN_END) {
			tile->element = subst_hillroad_track(tile->terrain, tile->element);
			tile->terrain = 0;
		}

		// Found a filler tile (non-main tile of a multitile component)
		// Process the main tile of the component instead (the NW one)
		if (tile->element == TRACK_TILE_CONTINUATION_SOUTHEAST) {
			tile->east = LEGACY_S8_WRAP_SUB(tile->east, 1);
			tile->south = LEGACY_S8_WRAP_SUB(tile->south, 1);
			tile->element = track_element_map[tile->east + trackrows[tile->south]];
			tile->terrain = track_terrain_map[tile->east + terrainrows[tile->south]];
		} else if (tile->element == TRACK_TILE_CONTINUATION_SOUTH) {
			tile->south = LEGACY_S8_WRAP_SUB(tile->south, 1);
			tile->element = track_element_map[tile->east + trackrows[tile->south]];
			tile->terrain = track_terrain_map[tile->east + terrainrows[tile->south]];
		} else if (tile->element == TRACK_TILE_CONTINUATION_EAST) {
			tile->east = LEGACY_S8_WRAP_SUB(tile->east, 1);
			tile->element = track_element_map[tile->east + trackrows[tile->south]];
			tile->terrain = track_terrain_map[tile->east + terrainrows[tile->south]];
		}
	}
}

legacy_s16 frame_lookahead_is_covered(const struct FRAME_LOOKAHEAD_TILE *lookahead,
									  legacy_s16 multitile_flag, legacy_s8 east_offset,
									  legacy_s8 south_offset)
{
	if (multitile_flag == FRAME_MULTITILE_ROW) {
		return lookahead->east == east_offset &&
			   (lookahead->south == south_offset || lookahead->south == south_offset + 1);
	}
	if (multitile_flag == FRAME_MULTITILE_COLUMN) {
		return lookahead->south == south_offset &&
			   (lookahead->east == east_offset || lookahead->east == east_offset + 1);
	}
	if (multitile_flag == FRAME_MULTITILE_BOTH) {
		return (lookahead->east == east_offset || lookahead->east == east_offset + 1) &&
			   (lookahead->south == south_offset || lookahead->south == south_offset + 1);
	}
	return 0;
}

void frame_mark_covered_tiles(struct FRAME_TILE_SELECTION *tiles, const struct FRAME_TILE *tile,
							  legacy_s16 tile_index)
{
#if defined(RESTUNTS_SDL3)
	if (extended_frame_mark_covered_tiles(tiles, tile, tile_index)) {
		return;
	}
#endif
	if (tile->element != 0) {
		legacy_s16 multitile_flag = trkObjectList[tile->element].ss_multiTileFlag;
		if (multitile_flag != FRAME_MULTITILE_NONE) {

			/* Recalculate after resolving filler tiles. Lower indices are visited later. */
			legacy_s8 east_offset = LEGACY_S8_WRAP_SUB(tile->east, tiles->camera_east);
			legacy_s8 south_offset = LEGACY_S8_WRAP_SUB(tile->south, tiles->camera_south);
			for (legacy_s16 covered_index = 0; covered_index < tile_index; covered_index++) {
				if (frame_lookahead_is_covered(&tiles->lookahead[covered_index], multitile_flag,
											   east_offset, south_offset)) {
					tiles->markers[covered_index] = FRAME_TILE_MULTITILE_COVERED_MARKER;
				}
			}
		}
	}
}

void frame_select_track_tile(struct FRAME_TILE_SELECTION *tiles, struct FRAME_TILE *tile,
							 legacy_s16 tile_index)
{
	tile->element = track_element_map[tile->east + trackrows[tile->south]];
	tile->terrain = track_terrain_map[tile->east + terrainrows[tile->south]];

#if defined(RESTUNTS_SDL3)
	if (extended_frame_uses_legacy_tiles())
#endif
		frame_resolve_track_tile(tile);
	tiles->terrain[tile_index] = tile->terrain;
	tiles->detail[tile_index] = tiles->lookahead[tile_index].detail;

	if (tile->element != 0 && detail_level != FRAME_DETAIL_FULL &&
		trkObjectList[tile->element].ss_physicalModel >= FRAME_SCENERY_PHYSICAL_MODEL_FIRST &&
		(tile->east != tiles->player_east || tile->south != tiles->player_south)) {
		tile->element = 0;
	}

	tiles->east[tile_index] = tile->east;
	tiles->south[tile_index] = tile->south;
#if defined(RESTUNTS_SDL3)
	tile->element = extended_frame_legacy_element(tile->element);
#endif
	tiles->elements[tile_index] = tile->element;

	frame_mark_covered_tiles(tiles, tile, tile_index);
}

#if defined(RESTUNTS_SDL3)
void frame_extend_lookahead(struct FRAME_TILE_SELECTION *tiles, const struct FRAME_CAMERA *camera)
{
	extended_frame_extend_lookahead(tiles, camera, frame_adaptive_active);
}
#endif

#if !(defined(RESTUNTS_SDL3))
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

void frame_select_lookahead_tile(struct FRAME_TILE_SELECTION *tiles, struct FRAME_TILE *tile,
								 legacy_s16 tile_index, legacy_s8 detail_threshold)
{
	/* Far tiles can be omitted at lower classic detail levels. */
	if (supersight_enabled == 0 && tiles->lookahead[tile_index].detail > detail_threshold) {
		tiles->markers[tile_index] = FRAME_TILE_UNAVAILABLE_MARKER;
		return;
	}
	tile->east = LEGACY_S8_WRAP_ADD(tiles->lookahead[tile_index].east, tiles->camera_east);
	tile->south = LEGACY_S8_WRAP_ADD(tiles->lookahead[tile_index].south, tiles->camera_south);
	if (tile->east < 0 || tile->east > FRAME_TRACK_LAST_X || tile->south < 0 ||
		tile->south > TRACK_GRID_LAST_COORDINATE) {
		tiles->markers[tile_index] = FRAME_TILE_UNAVAILABLE_MARKER;
		return;
	}
	frame_select_track_tile(tiles, tile, tile_index);
}

void frame_select_tiles(struct FRAME_TILE_SELECTION *tiles, const struct FRAME_CAMERA *camera)
{
#if defined(RESTUNTS_SDL3)
	tiles->complete_tile_lookup = 0;
#endif
	tiles->count = FRAME_LOOKAHEAD_TILE_COUNT;
	tiles->first = 0;
	tiles->camera_east =
		LEGACY_S8_FROM_BITS((legacy_u8)LEGACY_S16_SAR(camera->position.x, FRAME_CAMERA_TILE_SHIFT));
	tiles->camera_south = LEGACY_S8_WRAP_SUB(
		TRACK_GRID_LAST_COORDINATE, LEGACY_S16_SAR(camera->position.z, FRAME_CAMERA_TILE_SHIFT));
#if defined(RESTUNTS_SDL3)
	if (frame_adaptive_active != 0) {
		frame_adaptive_prepare(&frame_adaptive, tiles->camera_east, tiles->camera_south,
							   cos_fast(camera->view_heading), sin_fast(camera->view_heading));
	}
#endif
	if (supersight_enabled != 0) {
		frame_extend_lookahead(tiles, camera);
	}
#if defined(RESTUNTS_SDL3)
	extended_frame_position_tiles(tiles, camera, frame_adaptive_active);
#endif
	if (detail_level != FRAME_DETAIL_FULL) {
		tiles->player_east = frame_tile_from_world(frame_state->playerstate.car_position.lx);
		tiles->player_south = frame_south_tile_from_world(frame_state->playerstate.car_position.lz);
	}

	for (legacy_s16 tile_index = 0; tile_index < tiles->count; tile_index++) {
		tiles->markers[tile_index] = FRAME_TILE_DRAW_MARKER;
	}

	// Select the detail level (FULL if 1st or 2nd option in the graphics menu
	// were chosen, MEDIUM if the 3rd, FASTEST if 4th or 5th)
	legacy_s8 detail_threshold = detail_threshold_by_level[detail_level];

	/* Resolve each object's last submission tile first, suppressing earlier
	 * copies. The classic lookahead also retains its nearest-to-farthest order. */
	struct FRAME_TILE tile;
	for (legacy_s16 tile_index = tiles->count - 1; tile_index >= 0; tile_index--) {
		// Skip if a previous iteration determined this tile is not needed
		// (happens for multi-tile elements)
		if (tiles->markers[tile_index] != FRAME_TILE_DRAW_MARKER) {
			continue;
		}

		frame_select_lookahead_tile(tiles, &tile, tile_index, detail_threshold);
	}
}

legacy_s16 frame_draw_tiles(const struct FRAME_TILE_SELECTION *tiles,
							const struct FRAME_CAMERA *camera, struct FRAME_CAR_RENDER *cars,
							legacy_s8 redraw_transform_flags, legacy_s8 animated_material)
{
	/* A deferred overlay can carry over until a depth-sorted track shape. */
	legacy_s8 overlay_needs_depth_sort = 0;
	/* Multi-tile road overlays are submitted at their anchor. HyperVision
	 * finishes flat extended ground first so later ground cannot cover them.
	 * Hills and classic rendering retain their authored overlap order. */
	legacy_s16 first_pass = FRAME_TILE_OBJECT_PASS;
#if defined(RESTUNTS_SDL3)
	first_pass = extended_frame_first_tile_pass();
#endif
	for (legacy_s16 pass = first_pass; pass <= FRAME_TILE_OBJECT_PASS; pass++) {
		struct FRAME_TILE tile;
		for (legacy_s16 tile_index = tiles->first; tile_index < tiles->count; tile_index++) {
			if (tiles->markers[tile_index] != FRAME_TILE_DRAW_MARKER) {
				continue;
			}
			if (!frame_prepare_draw_tile(&tile, tiles, tile_index)) {
				continue;
			}
			if (frame_draw_tile_background(&tile, tiles, camera, redraw_transform_flags, first_pass,
										   pass) != 0) {
				return 1;
			}
			if (pass == FRAME_TILE_GROUND_PASS) {
				continue;
			}
			if (frame_draw_tile_objects(&tile, tiles, camera, cars, redraw_transform_flags,
										animated_material, &overlay_needs_depth_sort) != 0) {
				return 1;
			}
		}
	}
	return 0;
}

#if defined(RESTUNTS_SDL3)
void frame_supersight_reset(void)
{
	frame_adaptive_restart(&frame_adaptive);
	frame_adaptive_active = 0;
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

#if !(defined(RESTUNTS_SDL3))
void frame_draw_classic(const struct FRAME_TILE_SELECTION *tiles, struct FRAME_CAMERA *camera,
						struct FRAME_CAR_RENDER *cars, legacy_s8 buffer_index,
						legacy_s8 redraw_transform_flags, legacy_s8 animated_material)
{
	legacy_s8 initial_brake_paint = backlights_paint_override;
	frame_racing_line_enabled = 1;
	frame_racing_line_submitted = 0;
	frame_place_cars(tiles, cars);
	(void)frame_draw_tiles(tiles, camera, cars, redraw_transform_flags, animated_material);
	if (frame_retry_without_racing_line() != 0) {
		polyinfo_reset();
		backlights_paint_override = initial_brake_paint;
		frame_begin(buffer_index);
		frame_draw_clouds(camera, redraw_transform_flags);
		cars[PLAYER_CAR_INDEX].explosion_visible = 0;
		cars[OPPONENT_CAR_INDEX].explosion_visible = 0;
		frame_place_cars(tiles, cars);
		(void)frame_draw_tiles(tiles, camera, cars, redraw_transform_flags, animated_material);
	}
	frame_racing_line_enabled = 1;
}
#endif

#if !(defined(RESTUNTS_SDL3))
void frame_supersight_reset(void)
{
	supersight_attempt_hint = 0;
	supersight_probe_frames = 0;
	supersight_view_valid = 0;
}
#endif

#if !(defined(RESTUNTS_SDL3))
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

#if !(defined(RESTUNTS_SDL3))
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
