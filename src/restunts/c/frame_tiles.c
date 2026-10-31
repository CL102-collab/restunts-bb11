#include "frame_render_private.h"

#if defined(RESTUNTS_SDL3)
/* Geometry decisions use one cached byte per tile. Intersecting the masks for
 * an object's complete footprint keeps it whenever any covered tile needs it. */

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

void frame_select_lookahead_tile(struct FRAME_TILE_SELECTION *tiles, struct FRAME_TILE *tile,
								 legacy_s16 tile_index, legacy_s8 detail_threshold)
{
	/* Far tiles can be omitted at lower classic detail levels. */
	if (!frame_hires_enabled() && tiles->lookahead[tile_index].detail > detail_threshold) {
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
	frame_hires_prepare_tiles(tiles, camera);
#if defined(RESTUNTS_SDL3)
	extended_frame_position_tiles(tiles, camera, frame_hires_adaptive_enabled());
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
	extended_frame_first_tile_pass(&first_pass);
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
