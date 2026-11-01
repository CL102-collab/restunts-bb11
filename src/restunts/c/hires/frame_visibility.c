#include "../frame/frame_render_private.h"

#if defined(RESTUNTS_SDL3)
legacy_u8 frame_adaptive_active;
#endif

#if defined(RESTUNTS_SDL3)
legacy_s32 frame_adaptive_world_tile(legacy_s32 position, legacy_s16 offset)
{
	/* Positions and SIMD wheel offsets both have six fractional bits. Add
	 * before converting to tiles, retaining fractional boundary crossings.
	 * Widen the sum and floor negative values to keep off-map tiles signed. */
	legacy_s64 combined = (legacy_s64)position + offset;
	const legacy_s64 tile_size = (legacy_s64)1 << TRACK_WORLD_TILE_SHIFT;
	if (combined < 0) {
		combined -= tile_size - 1;
	}
	return (legacy_s32)(combined / tile_size);
}
#endif

#if defined(RESTUNTS_SDL3)
legacy_u8 frame_adaptive_position_flags(const struct VECTOR *relative, const struct VECTOR *camera)
{
	if (frame_adaptive_active == 0) {
		return FRAME_ADAPTIVE_FULL;
	}
	legacy_s16 east = ((legacy_s32)relative->x + camera->x) >> FRAME_CAMERA_TILE_SHIFT;
	legacy_s16 south = TRACK_GRID_LAST_COORDINATE -
					   (((legacy_s32)relative->z + camera->z) >> FRAME_CAMERA_TILE_SHIFT);
	return frame_adaptive_flags(&frame_adaptive, east, south);
}
#endif

#if defined(RESTUNTS_SDL3)
legacy_u8 frame_adaptive_car_flags(const struct CARSTATE *carstate, const struct SIMD *simd)
{
	if (frame_adaptive_active == 0) {
		return FRAME_ADAPTIVE_FULL;
	}
	legacy_s32 first_east = frame_adaptive_world_tile(carstate->car_position.lx, 0);
	legacy_s32 last_east = first_east;
	legacy_s32 first_south =
		TRACK_GRID_LAST_COORDINATE - frame_adaptive_world_tile(carstate->car_position.lz, 0);
	legacy_s32 last_south = first_south;
	struct MATRIX rotation =
		*mat_rot_zxy(LEGACY_S16_WRAP_NEGATE(carstate->car_rotate.z),
					 LEGACY_S16_WRAP_NEGATE(carstate->car_rotate.y),
					 LEGACY_S16_WRAP_NEGATE(carstate->car_rotate.x), MATRIX_ROTATION_ORDER_ZXY);
	for (legacy_s16 wheel = 0; wheel < FRAME_CAR_WHEEL_COUNT; wheel++) {
		struct VECTOR offset;
		struct VECTOR wheel_position = simd->wheel_coords[wheel];
		mat_mul_vector(&wheel_position, &rotation, &offset);
		legacy_s32 east = frame_adaptive_world_tile(carstate->car_position.lx, offset.x);
		legacy_s32 south = TRACK_GRID_LAST_COORDINATE -
						   frame_adaptive_world_tile(carstate->car_position.lz, offset.z);
		if (east < first_east) {
			first_east = east;
		}
		if (east > last_east) {
			last_east = east;
		}
		if (south < first_south) {
			first_south = south;
		}
		if (south > last_south) {
			last_south = south;
		}
	}
	/* Off-map cells contribute HIDE to the footprint intersection. Clipping
	 * the bounds also keeps malformed or far-outside positions inexpensive. */
	if (first_east < 0) {
		first_east = 0;
	}
	if (first_south < 0) {
		first_south = 0;
	}
	if (last_east > FRAME_TRACK_LAST_X) {
		last_east = FRAME_TRACK_LAST_X;
	}
	if (last_south > TRACK_GRID_LAST_COORDINATE) {
		last_south = TRACK_GRID_LAST_COORDINATE;
	}
	legacy_u8 flags = FRAME_ADAPTIVE_HIDE;
	for (legacy_s32 south = first_south; south <= last_south; south++) {
		for (legacy_s32 east = first_east; east <= last_east; east++) {
			flags &= frame_adaptive_flags(&frame_adaptive, east, south);
		}
	}
	return flags;
}
#endif

#if defined(RESTUNTS_SDL3)
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

legacy_s16 frame_hires_enabled(void)
{
	return supersight_enabled != 0;
}

legacy_s16 frame_hires_uses_screen_depth(void)
{
#if defined(RESTUNTS_SDL3)
	return supersight_enabled != 0;
#else
	return 0;
#endif
}

legacy_s16 frame_hires_adaptive_enabled(void)
{
#if defined(RESTUNTS_SDL3)
	return frame_adaptive_active != 0;
#else
	return 0;
#endif
}

#if defined(RESTUNTS_SDL3)
legacy_s16 frame_hires_cell_hidden(legacy_s16 east, legacy_s16 south)
{
	return frame_adaptive_active != 0 &&
		   frame_adaptive_flags(&frame_adaptive, east, south) == FRAME_ADAPTIVE_HIDE;
}

void frame_hires_override_track_shape(const struct TRACKOBJECT *object, struct SHAPE3D **shape)
{
	if (supersight_enabled != 0 && *shape == 0) {
		/* Enhanced detail reductions must not remove a mesh with no alternative. */
		*shape = object->ss_shapePtr;
	}
}

void frame_hires_override_terrain_shape(const struct TRACKOBJECT *object, legacy_s8 detail,
										struct SHAPE3D **shape)
{
	if (supersight_enabled != 0) {
		*shape = frame_track_shape(object, detail);
	}
}

legacy_s16 frame_hires_dynamic_shape(const struct TRACKOBJECT *track_object,
									 const struct VECTOR *camera_position, struct SHAPE3D **shape)
{
	legacy_u8 adaptive_flags =
		frame_adaptive_position_flags(&curtransshape_ptr->pos, camera_position);
	if (adaptive_flags == FRAME_ADAPTIVE_HIDE) {
		return 0;
	}
	if (adaptive_flags == FRAME_ADAPTIVE_LOW_GEOMETRY && track_object->ss_loShapePtr != 0) {
		*shape = track_object->ss_loShapePtr;
	}
	return 1;
}

legacy_s16 frame_hires_wheel_tile(const struct CARSTATE *carstate, const struct VECTOR *rotated,
								  legacy_s8 *tile_east, legacy_s8 *tile_south, legacy_s16 *visible)
{
	if (frame_adaptive_active == 0) {
		return 0;
	}
	legacy_s32 east = frame_adaptive_world_tile(carstate->car_position.lx, rotated->x);
	legacy_s32 south = TRACK_GRID_LAST_COORDINATE -
					   frame_adaptive_world_tile(carstate->car_position.lz, rotated->z);
	*visible = east >= 0 && east <= FRAME_TRACK_LAST_X && south >= 0 &&
			   south <= TRACK_GRID_LAST_COORDINATE;
	if (*visible) {
		*tile_east = east;
		*tile_south = south;
	}
	return 1;
}
#endif

legacy_s16 frame_hires_tile_detail(const struct FRAME_TILE_SELECTION *tiles, legacy_s16 tile_index,
								   legacy_s8 *detail)
{
	if (supersight_enabled == 0) {
		return 0;
	}
	*detail = tiles->lookahead[tile_index].detail >= tiles->detail_threshold;
	return 1;
}

legacy_s16 frame_hires_prepare_tile(struct FRAME_TILE *tile)
{
#if defined(RESTUNTS_SDL3)
	tile->adaptive_flags = 0;
	if (frame_adaptive_active != 0) {
		tile->adaptive_flags = frame_adaptive_tile_flags(tile);
	}
	if (tile->adaptive_flags == FRAME_ADAPTIVE_HIDE) {
		return 0;
	}
	if ((tile->adaptive_flags & FRAME_ADAPTIVE_LOW_GEOMETRY) != 0) {
		tile->detail = FRAME_TILE_DETAIL_FULL + 1;
	}
#else
	(void)tile;
#endif
	return 1;
}

void frame_hires_racing_line_flags(legacy_u8 *flags)
{
	*flags |= SHAPE3D_NO_SHADOW_RECEIVE_FLAG;
}
