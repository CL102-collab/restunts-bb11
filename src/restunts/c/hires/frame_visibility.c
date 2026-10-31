#include "../frame_render_private.h"

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

#if defined(RESTUNTS_SDL3)
struct SHAPE3D *frame_track_shape(const struct TRACKOBJECT *object, legacy_s8 detail)
{
	/* A missing alternative must never turn a detail reduction into removal. */
	return detail != FRAME_TILE_DETAIL_FULL &&
				   (supersight_enabled == 0 || object->ss_loShapePtr != 0)
			   ? object->ss_loShapePtr
			   : object->ss_shapePtr;
}
#endif

legacy_s16 frame_hires_enabled(void)
{
	return supersight_enabled != 0;
}

legacy_s16 frame_hires_sort_depth(void)
{
#if defined(RESTUNTS_SDL3)
	return supersight_enabled == 0;
#else
	return 1;
#endif
}

legacy_s16 frame_hires_attached_particles(void)
{
#if defined(RESTUNTS_SDL3)
	return frame_adaptive_active == 0;
#else
	return 1;
#endif
}

#if defined(RESTUNTS_SDL3)
legacy_s16 frame_hires_cell_hidden(legacy_s16 east, legacy_s16 south)
{
	return frame_adaptive_active != 0 &&
		   frame_adaptive_flags(&frame_adaptive, east, south) == FRAME_ADAPTIVE_HIDE;
}

struct SHAPE3D *frame_hires_terrain_shape(const struct TRACKOBJECT *object, legacy_s8 detail)
{
	return supersight_enabled != 0 ? frame_track_shape(object, detail) : object->ss_shapePtr;
}

legacy_s16 frame_hires_dynamic_shape(const struct TRACKOBJECT *track_object,
									 const struct VECTOR *camera_position)
{
	legacy_u8 adaptive_flags =
		frame_adaptive_position_flags(&curtransshape_ptr->pos, camera_position);
	if (adaptive_flags == FRAME_ADAPTIVE_HIDE) {
		return 0;
	}
	curtransshape_ptr->shapeptr = track_object->ss_shapePtr;
	if (adaptive_flags == FRAME_ADAPTIVE_LOW_GEOMETRY && track_object->ss_loShapePtr != 0) {
		curtransshape_ptr->shapeptr = track_object->ss_loShapePtr;
	}
	return 1;
}

legacy_s16 frame_hires_wheel_tile(const struct CARSTATE *carstate, const struct VECTOR *rotated,
								  legacy_s8 *tile_east, legacy_s8 *tile_south)
{
	if (frame_adaptive_active != 0) {
		legacy_s32 east = frame_adaptive_world_tile(carstate->car_position.lx, rotated->x);
		legacy_s32 south = TRACK_GRID_LAST_COORDINATE -
						   frame_adaptive_world_tile(carstate->car_position.lz, rotated->z);
		if (east < 0 || east > FRAME_TRACK_LAST_X || south < 0 ||
			south > TRACK_GRID_LAST_COORDINATE) {
			return 0;
		}
		*tile_east = east;
		*tile_south = south;
	} else {
		*tile_east = frame_tile_from_world_offset(carstate->car_position.lx, rotated->x);
		*tile_south = frame_south_tile_from_world_offset(carstate->car_position.lz, rotated->z);
	}
	return 1;
}
#endif

legacy_s16 frame_hires_prepare_tile(struct FRAME_TILE *tile,
									const struct FRAME_TILE_SELECTION *tiles, legacy_s16 tile_index)
{
	tile->detail = supersight_enabled != 0
					   ? tiles->lookahead[tile_index].detail >= tiles->detail_threshold
					   : tiles->detail[tile_index];
#if defined(RESTUNTS_SDL3)
	tile->adaptive_flags = frame_adaptive_active != 0 ? frame_adaptive_tile_flags(tile) : 0;
	if (tile->adaptive_flags == FRAME_ADAPTIVE_HIDE) {
		return 0;
	}
	if ((tile->adaptive_flags & FRAME_ADAPTIVE_LOW_GEOMETRY) != 0) {
		tile->detail = FRAME_TILE_DETAIL_FULL + 1;
	}
#endif
	return 1;
}

legacy_s16 frame_hires_exhausted(legacy_s16 result)
{
	return result != 0 && supersight_enabled != 0;
}

legacy_s16 frame_hires_racing_line_flags(legacy_s8 redraw_transform_flags)
{
	return redraw_transform_flags | FRAME_TRANSFORM_FLAGS_DEFAULT | SHAPE3D_NO_SHADOW_RECEIVE_FLAG;
}
