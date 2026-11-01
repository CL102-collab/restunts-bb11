#include "frame_render_private.h"

const legacy_s8 fence_by_edge[FRAME_FENCE_EDGE_CLASS_COUNT][FRAME_FENCE_EDGE_CLASS_COUNT] = {
	{7, 5, 6}, {1, 3, 2}, {0, 4, FRAME_FENCE_NONE}};

legacy_s16 frame_border_index(legacy_s8 offset)
{
	if (offset == 0) {
		return FRAME_FENCE_EDGE_LOW;
	}
	if (offset == TRACK_GRID_LAST_COORDINATE) {
		return FRAME_FENCE_EDGE_HIGH;
	}
	return FRAME_FENCE_EDGE_INTERIOR;
}

/* Report exhaustion to SuperSight; the legacy caller stops only this pass. */
legacy_s16 frame_draw_fences(const struct FRAME_TILE *tile,
							 const struct FRAME_TILE_SELECTION *tiles,
							 const struct FRAME_CAMERA *camera, legacy_s8 redraw_transform_flags)
{
	legacy_s8 *fence_tile_offsets;
	legacy_s16 fence_position_count;
	struct TRACKOBJECT *track_object;
	if (tile->element == 0) {
		fence_position_count = 1;
		fence_tile_offsets = fence_tile_offsets_column;
	} else {
		track_object = &trkObjectList[tile->element];
		if (track_object->ss_multiTileFlag == FRAME_MULTITILE_NONE) {
			fence_position_count = FRAME_FENCE_POSITION_COUNT_SINGLE;
			fence_tile_offsets = fence_tile_offsets_single;
		} else if (track_object->ss_multiTileFlag == FRAME_MULTITILE_ROW) {
			fence_position_count = FRAME_FENCE_POSITION_COUNT_ROW;
			fence_tile_offsets = fence_tile_offsets_row;
		} else if (track_object->ss_multiTileFlag == FRAME_MULTITILE_COLUMN) {
			fence_position_count = FRAME_FENCE_POSITION_COUNT_COLUMN;
			fence_tile_offsets = fence_tile_offsets_column;
		} else if (track_object->ss_multiTileFlag == FRAME_MULTITILE_BOTH) {
			fence_position_count = FRAME_FENCE_POSITION_COUNT_BOTH;
			fence_tile_offsets = fence_tile_offsets_both;
		} else {
			return 0;
		}
	}

	// Draw the fence
	struct TRACKOBJECT *fence_object;
	for (legacy_s16 position_index = 0; position_index < fence_position_count; position_index++) {
		legacy_s8 tile_to_draw_east_offset = LEGACY_S8_WRAP_ADD(
			fence_tile_offsets[position_index * FRAME_FENCE_POSITION_STRIDE], tile->east);
		legacy_s8 tile_to_draw_south_offset =
			LEGACY_S8_WRAP_ADD(fence_tile_offsets[position_index * FRAME_FENCE_POSITION_STRIDE +
												  FRAME_FENCE_SECOND_COORDINATE],
							   tile->south);

		if (detail_level == FRAME_DETAIL_FULL ||
			(tile_to_draw_east_offset == tiles->player_east &&
			 tile_to_draw_south_offset == tiles->player_south)) {
			legacy_s16 fence_index = fence_by_edge[frame_border_index(tile_to_draw_east_offset)]
												  [frame_border_index(tile_to_draw_south_offset)];

			if (fence_index != FRAME_FENCE_NONE) {
#if defined(RESTUNTS_SDL3)
				if (frame_hires_cell_hidden(tile_to_draw_east_offset, tile_to_draw_south_offset)) {
					continue;
				}
#endif
				fence_object = frame_track_object_from_legacy_index(fence_TrkObjCodes[fence_index]);
#if defined(RESTUNTS_SDL3)
				currenttransshape->shapeptr = frame_track_shape(fence_object, tile->detail);
#else
				if (tile->detail == FRAME_TILE_DETAIL_FULL) {
					currenttransshape->shapeptr = fence_object->ss_shapePtr;
				} else {
					currenttransshape->shapeptr = fence_object->ss_loShapePtr;
				}
#endif

				frame_prepare_flat_track_shape(
					currenttransshape, tile_to_draw_east_offset, tile_to_draw_south_offset,
					&camera->position,
					(legacy_s16)(redraw_transform_flags | FRAME_TRANSFORM_FLAGS_NO_DEPTH_SORT),
					fence_rotations[fence_index]);
				legacy_s16 transform_result = shape3d_transform_and_queue(&currenttransshape[0]);
				if (transform_result > 0) {
					// if the return value is > 0, we are out of memory
					// for the polygons, so the rendering is interrupted.
					// Note that (since we start from afar) this means that
					// if the scene is too complex only the far objects
					// will be drawn, while our car and its immediate
					// surroundings will be invisible. Luckily, it does not
					// happen often
					return 1;
				}
			}
		}
	}
	return 0;
}

/* The legacy caller ignores corner exhaustion and continues ordinary terrain. */
legacy_s16 frame_draw_elevated_corners(struct FRAME_TILE *tile, const struct FRAME_CAMERA *camera,
									   legacy_s8 redraw_transform_flags)
{
	struct TRACKOBJECT *track_object;

	for (legacy_s16 corner_index = 0; corner_index < FRAME_ELEVATED_CORNER_COUNT; corner_index++) {
		if (corner_index == FRAME_CORNER_NORTHWEST) {
			tile->last_east = tile->east;
			tile->last_south = tile->south;
		} else if (corner_index == FRAME_CORNER_NORTHEAST) {
			tile->last_east = LEGACY_S8_WRAP_ADD(tile->east, 1);
			tile->last_south = tile->south;
		} else if (corner_index == FRAME_CORNER_SOUTHWEST) {
			tile->last_east = tile->east;
			tile->last_south = LEGACY_S8_WRAP_ADD(tile->south, 1);
		} else if (corner_index == FRAME_CORNER_SOUTHEAST) {
			tile->last_east = LEGACY_S8_WRAP_ADD(tile->east, 1);
			tile->last_south = LEGACY_S8_WRAP_ADD(tile->south, 1);
		}
		tile->terrain = track_terrain_map[tile->last_east + terrainrows[tile->last_south]];
#if defined(RESTUNTS_SDL3)
		if (frame_hires_cell_hidden(tile->last_east, tile->last_south)) {
			continue;
		}
#endif
		if (tile->terrain != 0) {
			track_object = &terrain_scene_objects[tile->terrain];
#if defined(RESTUNTS_SDL3)
			currenttransshape->shapeptr = frame_terrain_shape(track_object, tile->detail);
#else
			currenttransshape->shapeptr = track_object->ss_shapePtr;
#endif
			frame_prepare_flat_track_shape(
				currenttransshape, tile->last_east, tile->last_south, &camera->position,
				(legacy_s16)(redraw_transform_flags | FRAME_TRANSFORM_FLAGS_NO_DEPTH_SORT),
				track_object->ss_rotY);
			legacy_s16 transform_result = shape3d_transform_and_queue(&currenttransshape[0]);
			if (transform_result > 0) {
				return 1;
			}
		}
	}
	return 0;
}

/* Multi-tile selection suppresses continuation cells, but physics still samples
 * their own terrain. Restore water beneath the complete visible track object. */

legacy_s16 frame_draw_terrain(struct FRAME_TILE *tile, const struct FRAME_CAMERA *camera,
							  legacy_s8 redraw_transform_flags)
{
#if defined(RESTUNTS_SDL3)
	legacy_s16 result;
	if (extended_frame_try_draw_terrain(tile, camera, redraw_transform_flags, &result)) {
		return result;
	}
#endif
	if (frame_renderer_draw_covered_water(tile, camera, redraw_transform_flags)) {
		return 1;
	}

	// Elevated terrain is a flat piece of land at an elevated level.
	if (tile->terrain != TERRAIN_RAISED_TILE) {
		tile->height = 0;

		// Special treatment of elevated corners
		if (tile->element >= FRAME_ELEVATED_CORNER_FIRST &&
			tile->element <= FRAME_ELEVATED_CORNER_LAST) {
			if (frame_draw_elevated_corners(tile, camera, redraw_transform_flags) != 0 &&
				frame_renderer_enabled()) {
				return 1;
			}

			tile->terrain = 0;
		}
	} else {
		tile->height = hillHeightConsts[TERRAIN_RAISED_HEIGHT_INDEX];
		if (tile->element != 0) {
			tile->terrain = 0;
		}
	}

	// The rest of the rendering loop still needs to be analyzed in detail.
	// Anyway, the gist is that every tile is associated with various shape,
	// each of which is rendered via a call to `shape3d_transform_and_queue`. The
	// result of such fn is checked each time, since a return value of 1
	// means we ran out of memory

	struct TRACKOBJECT *track_object;
	if (tile->terrain != 0) {
		track_object = &terrain_scene_objects[tile->terrain];
#if defined(RESTUNTS_SDL3)
		currenttransshape->shapeptr = frame_terrain_shape(track_object, tile->detail);
#else
		currenttransshape->shapeptr = track_object->ss_shapePtr;
#endif
		currenttransshape->pos.x =
			LEGACY_S16_WRAP_SUB(track_column_centers[tile->east], camera->position.x);
		currenttransshape->pos.y = LEGACY_S16_WRAP_SUB(tile->height, camera->position.y);
		currenttransshape->pos.z =
			LEGACY_S16_WRAP_SUB(track_row_centers[tile->south], camera->position.z);
		if (tile->height == 0) {
			currenttransshape->rectptr = &frame_unsorted_shapes_rect;
		} else {
			currenttransshape->rectptr = &frame_sorted_shapes_rect;
		}

		currenttransshape->ts_flags = redraw_transform_flags | FRAME_TRANSFORM_FLAGS_NO_DEPTH_SORT;
		currenttransshape->rotvec.x = 0;
		currenttransshape->rotvec.y = 0;
		currenttransshape->rotvec.z = track_object->ss_rotY;
		currenttransshape->culling_distance = FRAME_DEFAULT_TRANSFORM_DISTANCE;
		currenttransshape->material = 0;
		legacy_s16 transform_result = shape3d_transform_and_queue(&currenttransshape[0]);
		if (transform_result > 0) {
			return 1;
		}
	}

	return 0;
}

/* Hill-fill exhaustion stops this pass; the track element still follows. */
legacy_s16 frame_draw_hill_fill(const struct FRAME_TILE *tile,
								const struct TRACKOBJECT *track_object,
								legacy_s8 redraw_transform_flags)
{
	legacy_s16 fill_count;
	legacy_s16 *hill_fill_offsets;
	if (tile->height != 0) {
		if (track_object->ss_multiTileFlag == FRAME_MULTITILE_NONE) {
			fill_count = FRAME_HILL_FILL_COUNT_SINGLE;
			hill_fill_offsets = hill_fill_offsets_single;
		} else if (track_object->ss_multiTileFlag == FRAME_MULTITILE_ROW) {
			fill_count = FRAME_HILL_FILL_COUNT_ROW;
			hill_fill_offsets = hill_fill_offsets_row;
		} else if (track_object->ss_multiTileFlag == FRAME_MULTITILE_COLUMN) {
			fill_count = FRAME_HILL_FILL_COUNT_COLUMN;
			hill_fill_offsets = hill_fill_offsets_column;
		} else if (track_object->ss_multiTileFlag == FRAME_MULTITILE_BOTH) {
			fill_count = FRAME_HILL_FILL_COUNT_BOTH;
			hill_fill_offsets = hill_fill_offsets_both;
		} else {
			return 0;
		}

		for (legacy_s16 fill_index = 0; fill_index < fill_count; fill_index++) {
			currenttransshape->pos.x = LEGACY_S16_WRAP_ADD(*hill_fill_offsets, tile->position.x);
			hill_fill_offsets++;
			currenttransshape->pos.y = tile->position.y;
			currenttransshape->pos.z = LEGACY_S16_WRAP_ADD(*hill_fill_offsets, tile->position.z);
			hill_fill_offsets++;
			currenttransshape->shapeptr = &game3dshapes[FRAME_HILL_FILL_SHAPE_INDEX];
			currenttransshape->rectptr = &frame_sorted_shapes_rect;
			currenttransshape->ts_flags =
				redraw_transform_flags | FRAME_TRANSFORM_FLAGS_NO_DEPTH_SORT;
			currenttransshape->rotvec.x = 0;
			currenttransshape->rotvec.y = 0;
			currenttransshape->rotvec.z = 0;
			currenttransshape->culling_distance = FRAME_SINGLE_TILE_TRANSFORM_DISTANCE;
			currenttransshape->material = 0;
			legacy_s16 transform_result = shape3d_transform_and_queue(&currenttransshape[0]);
			if (transform_result > 0) {
				return 1;
			}
		}
	}
	return 0;
}

legacy_s16 frame_prepare_overlay(const struct FRAME_TILE *tile,
								 const struct TRACKOBJECT *track_object,
								 legacy_s8 redraw_transform_flags, legacy_s8 animated_material,
								 legacy_s8 *overlay_needs_depth_sort)
{
	struct TRACKOBJECT *overlay_track_object;

	if (track_object->ss_ssOvelay != 0) {
		overlay_track_object = frame_track_object_from_legacy_index(track_object->ss_ssOvelay);
#if defined(RESTUNTS_SDL3)
		currenttransshape[1].shapeptr = frame_track_shape(overlay_track_object, tile->detail);
#else
		if (tile->detail != FRAME_TILE_DETAIL_FULL) {
			currenttransshape[1].shapeptr = overlay_track_object->ss_loShapePtr;
		} else {
			currenttransshape[1].shapeptr = overlay_track_object->ss_shapePtr;
		}
#endif

		if (currenttransshape[1].shapeptr != 0) {
			currenttransshape[1].pos = tile->position;
			currenttransshape[1].rotvec.x = 0;
			currenttransshape[1].rotvec.y = 0;
			currenttransshape[1].rotvec.z = overlay_track_object->ss_rotY;
			if (overlay_track_object->ss_multiTileFlag != FRAME_MULTITILE_NONE) {
				currenttransshape[1].culling_distance = FRAME_DEFAULT_TRANSFORM_DISTANCE;
			} else {
				currenttransshape[1].culling_distance = FRAME_SINGLE_TILE_TRANSFORM_DISTANCE;
			}

			if (overlay_track_object->ss_surfaceType >= 0) {
				currenttransshape[1].material = overlay_track_object->ss_surfaceType;
			} else {
				currenttransshape[1].material = animated_material;
			}

			currenttransshape[1].ts_flags = overlay_track_object->ss_ignoreZBias |
											redraw_transform_flags | FRAME_TRANSFORM_FLAGS_DEFAULT;
			if ((currenttransshape[1].ts_flags & FRAME_NO_DEPTH_SORT_FLAG) != 0) {
				currenttransshape[1].rectptr = &frame_unsorted_shapes_rect;
				legacy_s16 transform_result = shape3d_transform_and_queue(&currenttransshape[1]);
				if (transform_result > 0) {
					return 1;
				}
			} else {
				currenttransshape[1].rectptr = &frame_sorted_shapes_rect;
				*overlay_needs_depth_sort = 1;
			}
		}
	}

	return 0;
}

#if defined(RESTUNTS_SDL3)
struct SHAPE3D *frame_track_shape(const struct TRACKOBJECT *object, legacy_s8 detail)
{
	struct SHAPE3D *shape =
		detail != FRAME_TILE_DETAIL_FULL ? object->ss_loShapePtr : object->ss_shapePtr;
	frame_hires_override_track_shape(object, &shape);
	return shape;
}
#endif

struct SHAPE3D *frame_terrain_shape(const struct TRACKOBJECT *object, legacy_s8 detail)
{
	struct SHAPE3D *shape = object->ss_shapePtr;
#if defined(RESTUNTS_SDL3)
	frame_hires_override_terrain_shape(object, detail, &shape);
#else
	(void)detail;
#endif
	return shape;
}
