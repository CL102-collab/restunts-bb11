#if !defined(RESTUNTS_SDL3)
#include "../frame/frame_render_private.h"

legacy_s16 frame_supersight_draw_water(const struct FRAME_TILE *tile,
									   const struct FRAME_CAMERA *camera,
									   legacy_s8 redraw_transform_flags)
{
	if (tile->element == 0 ||
		(tile->element >= FRAME_ELEVATED_CORNER_FIRST &&
		 tile->element <= FRAME_ELEVATED_CORNER_LAST && tile->terrain != TERRAIN_RAISED_TILE)) {
		/* Elevated corners already draw every cell's terrain. */
		return 0;
	}
	legacy_s16 footprint = trkObjectList[tile->element].ss_multiTileFlag;
	if (footprint < FRAME_MULTITILE_ROW || footprint > FRAME_MULTITILE_BOTH) {
		return 0;
	}
	legacy_s16 last_east = tile->east + (footprint != FRAME_MULTITILE_ROW);
	legacy_s16 last_south = tile->south + (footprint != FRAME_MULTITILE_COLUMN);
	for (legacy_s16 south = tile->south; south <= last_south && south < TRACK_GRID_SIZE; south++) {
		for (legacy_s16 east = tile->east; east <= last_east && east < TRACK_GRID_SIZE; east++) {
			if (east == tile->east && south == tile->south) {
				continue;
			}
			legacy_u8 terrain = track_terrain_map[east + terrainrows[south]];
			if (terrain < FRAME_WATER_TERRAIN_FIRST || terrain > FRAME_WATER_TERRAIN_LAST) {
				continue;
			}
			struct TRACKOBJECT *object = &terrain_scene_objects[terrain];
			currenttransshape->shapeptr = frame_terrain_shape(object, tile->detail);
			frame_prepare_flat_track_shape(
				currenttransshape, east, south, &camera->position,
				(legacy_s16)(redraw_transform_flags | FRAME_TRANSFORM_FLAGS_NO_DEPTH_SORT),
				object->ss_rotY);
			legacy_s16 transform_result = shape3d_transform_and_queue(currenttransshape);
			if (transform_result > 0) {
				return 1;
			}
		}
	}
	return 0;
}

legacy_s16 frame_supersight_draw_covered_water(const struct FRAME_TILE *tile,
											   const struct FRAME_CAMERA *camera,
											   legacy_s8 redraw_transform_flags)
{
	return supersight_enabled != 0 &&
		   frame_supersight_draw_water(tile, camera, redraw_transform_flags) != 0;
}

#endif
