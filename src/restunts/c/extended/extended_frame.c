#include "extended_frame_internal.h"

legacy_s16 frame_extended_border_index(legacy_s16 coordinate, legacy_u16 dimension)
{
	if (coordinate == 0) {
		return FRAME_FENCE_EDGE_LOW;
	}
	if (coordinate == dimension - 1) {
		return FRAME_FENCE_EDGE_HIGH;
	}
	return FRAME_FENCE_EDGE_INTERIOR;
}

/* The original fence posts sit on tile edges. Interpolate shared terrain
 * corners so adjacent spans meet and each post keeps its original height. */
legacy_s16 frame_extended_fence_height(const struct CSTRK_TRACK *track,
									   const struct FRAME_TILE *tile, const struct VECTOR *point)
{
	legacy_u32 stride = track->width + 1U;
	legacy_u32 corner = (legacy_u32)tile->south * stride + tile->east;
	const legacy_u8 *heights = &track->corner_heights[corner];
	legacy_s32 u = (legacy_s32)point->x + FRAME_EXTENDED_TERRAIN_HALF_TILE;
	legacy_s32 v = FRAME_EXTENDED_TERRAIN_HALF_TILE - (legacy_s32)point->z;
	legacy_s32 north = heights[0] * (CSTRK_TILE_WORLD_SIZE - u) + heights[1] * u;
	legacy_s32 south = heights[stride] * (CSTRK_TILE_WORLD_SIZE - u) + heights[stride + 1U] * u;
	legacy_s64 height = ((legacy_s64)north * (CSTRK_TILE_WORLD_SIZE - v) + (legacy_s64)south * v) *
						CSTRK_LEVEL_WORLD_HEIGHT;
	return (legacy_s16)(height / ((legacy_s64)CSTRK_TILE_WORLD_SIZE * CSTRK_TILE_WORLD_SIZE));
}

legacy_s16 frame_draw_extended_fence_span(const struct FRAME_TILE *tile,
										  const struct FRAME_CAMERA *camera,
										  legacy_s8 redraw_transform_flags, legacy_u16 fence_index)
{
	const struct TRACKOBJECT *object =
		frame_track_object_from_legacy_index(fence_TrkObjCodes[fence_index]);
	const struct SHAPE3D *source = frame_track_shape(object, tile->detail);
	struct SHAPE3D mesh;
	if (!extended_geometry_transform_shape(source, &mesh, fence_rotations[fence_index],
										   CSTRK_ROAD_PAVED, FRAME_SCENERY_PHYSICAL_MODEL_FIRST)) {
		return 0;
	}
	const struct CSTRK_TRACK *track = extended_track_model();
	for (legacy_u16 index = 0; index < mesh.shape3d_numverts; index++) {
		struct VECTOR point;
		shape3d_vertex_read(&mesh, index, &point);
		point.y = LEGACY_S16_WRAP_ADD(point.y, frame_extended_fence_height(track, tile, &point));
		shape3d_vertex_write(&mesh, index, &point);
	}
	struct TRANSFORMEDSHAPE3D instance = {0};
	frame_prepare_flat_track_shape(&instance, tile->east, tile->south, &camera->position,
								   redraw_transform_flags | FRAME_TRANSFORM_FLAGS_NO_DEPTH_SORT, 0);
	instance.shapeptr = &mesh;
	legacy_s16 exhausted = (legacy_s16)shape3d_transform_and_queue(&instance) > 0;
	extended_geometry_free_shape(&mesh);
	return exhausted;
}

legacy_s16 extended_frame_draw_fences(const struct FRAME_TILE *tile,
									  const struct FRAME_TILE_SELECTION *tiles,
									  const struct FRAME_CAMERA *camera,
									  legacy_s8 redraw_transform_flags)
{
	if (detail_level != FRAME_DETAIL_FULL &&
		(tile->east != tiles->player_east || tile->south != tiles->player_south)) {
		return 0;
	}
	const struct CSTRK_TRACK *track = extended_track_model();
	if (track->width > 1U && track->height > 1U) {
		legacy_s16 east = frame_extended_border_index(tile->east, track->width);
		legacy_s16 south = frame_extended_border_index(tile->south, track->height);
		legacy_s16 fence_index = fence_by_edge[east][south];
		return fence_index == FRAME_FENCE_NONE
				   ? 0
				   : frame_draw_extended_fence_span(tile, camera, redraw_transform_flags,
													(legacy_u16)fence_index);
	}
	/* A one-cell-wide map needs opposite sides in the same cell, which the
	 * original corner lookup cannot represent. Queue each boundary once. */
	legacy_u8 sides[FRAME_EXTENDED_FENCE_SIDE_COUNT];
	sides[FRAME_EXTENDED_FENCE_NORTH] = tile->south == 0;
	sides[FRAME_EXTENDED_FENCE_EAST] = tile->east == track->width - 1;
	sides[FRAME_EXTENDED_FENCE_SOUTH] = tile->south == track->height - 1;
	sides[FRAME_EXTENDED_FENCE_WEST] = tile->east == 0;
	for (legacy_u16 side = 0; side < FRAME_EXTENDED_FENCE_SIDE_COUNT; side++) {
		if (sides[side] &&
			frame_draw_extended_fence_span(tile, camera, redraw_transform_flags,
										   side * FRAME_EXTENDED_FENCE_STRAIGHT_INDEX_STEP)) {
			return 1;
		}
	}
	return 0;
}

legacy_s16 extended_frame_add_start_flag(const struct FRAME_CAMERA *camera,
										 legacy_s8 redraw_transform_flags)
{
	if (!extended_track_active()) {
		return 0;
	}

	const struct CSTRK_OBJECT *object =
		extended_track_object_at(start_finish_column, start_finish_row);
	if (!object) {
		return 1;
	}
	struct VECTOR origin, offset;
	extended_geometry_object_origin(extended_track_model(), object, &origin);
	offset = (struct VECTOR){
		LEGACY_S16_WRAP_ADD(
			multiply_and_scale(sin_fast(track_angle + ANGLE_QUARTER_TURN), FRAME_START_FLAG_RADIUS),
			multiply_and_scale(sin_fast(track_angle + ANGLE_HALF_TURN),
							   FRAME_START_FLAG_FAR_OFFSET)),
		0,
		LEGACY_S16_WRAP_ADD(
			multiply_and_scale(cos_fast(track_angle + ANGLE_QUARTER_TURN), FRAME_START_FLAG_RADIUS),
			multiply_and_scale(cos_fast(track_angle + ANGLE_HALF_TURN),
							   FRAME_START_FLAG_FAR_OFFSET))};
	legacy_u16 paint = (legacy_u16)start_flag_animation >> FRAME_START_FLAG_ANIMATION_SHIFT;
	if (paint > FRAME_START_FLAG_MAX_MATERIAL) {
		paint = FRAME_START_FLAG_MAX_MATERIAL;
	}
	struct SHAPE3D mesh;
	if (extended_geometry_transform_shape(&game3dshapes[FRAME_START_FLAG_SHAPE_INDEX], &mesh,
										  track_angle, paint, -1)) {
		struct TRANSFORMEDSHAPE3D instance = {0};
		instance.pos = (struct VECTOR){
			LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_ADD(origin.x, offset.x), camera->position.x),
			LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_ADD(origin.y, offset.y), camera->position.y),
			LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_ADD(origin.z, offset.z), camera->position.z)};
		instance.shapeptr = &mesh;
		instance.rectptr = &frame_sorted_shapes_rect;
		instance.culling_distance = FRAME_DEFAULT_TRANSFORM_DISTANCE;
		instance.ts_flags = redraw_transform_flags | FRAME_TRANSFORM_FLAGS_DEFAULT;
		shape3d_transform_and_queue(&instance);
		extended_geometry_free_shape(&mesh);
	}
	return 1;
}

legacy_s16 extended_frame_car_visible(const struct CARSTATE *carstate)
{
	if (!extended_track_active()) {
		return 1;
	}
	legacy_s32 reference_x, reference_z;
	extended_geometry_get_context(&reference_x, &reference_z);
	legacy_s32 dx =
		LEGACY_S32_SAR(carstate->car_position.lx, FRAME_EXTENDED_POSITION_SHIFT) - reference_x;
	legacy_s32 dz =
		LEGACY_S32_SAR(carstate->car_position.lz, FRAME_EXTENDED_POSITION_SHIFT) - reference_z;
	return dx >= -FRAME_EXTENDED_GEOMETRY_MAX_DISTANCE &&
		   dx <= FRAME_EXTENDED_GEOMETRY_MAX_DISTANCE &&
		   dz >= -FRAME_EXTENDED_GEOMETRY_MAX_DISTANCE &&
		   dz <= FRAME_EXTENDED_GEOMETRY_MAX_DISTANCE;
}

legacy_s16 extended_frame_background_height(legacy_s16 height)
{
	return extended_track_active() ? 0 : height;
}

void extended_frame_position_camera_tiles(struct FRAME_TILE_SELECTION *tiles,
										  const struct FRAME_CAMERA *camera)
{
	legacy_s32 x, z;
	extended_geometry_world_position(&camera->position, &x, &z);
	tiles->camera_east = (legacy_s8)LEGACY_S32_SAR(x, FRAME_CAMERA_TILE_SHIFT);
	tiles->camera_south =
		(legacy_s8)(extended_track_height() - 1 - LEGACY_S32_SAR(z, FRAME_CAMERA_TILE_SHIFT));
}

void extended_frame_set_context(const struct CARSTATE *viewed)
{
	if (extended_track_active()) {
		extended_geometry_set_context(viewed->car_position.lx, viewed->car_position.lz);
	}
}

legacy_u8 extended_frame_should_draw_terrain(const struct FRAME_TILE *tile, legacy_s16 first_pass,
											 legacy_s16 pass)
{
	struct CSTRK_TERRAIN_PROFILE ground_profile;
	legacy_u8 ground_first = first_pass == FRAME_TILE_GROUND_PASS &&
							 cstrk_terrain_profile(extended_track_model(), (legacy_u16)tile->east,
												   (legacy_u16)tile->south, &ground_profile) &&
							 ground_profile.terrain_id < CSTRK_TERRAIN_PROFILE_NORTH;
	return (pass == FRAME_TILE_GROUND_PASS) == ground_first;
}

#if !defined(RESTUNTS_HEADLESS)
legacy_s16 extended_frame_format_elapsed_time(legacy_s8 *out)
{
	if (!extended_replay_is_active()) {
		return 0;
	}
	extended_replay_format_runtime_time(out, extended_replay_clocks.input_frame, 0);
	return 1;
}
#endif

legacy_s16 extended_frame_uses_legacy_tiles(void)
{
	return !extended_track_active();
}

legacy_u8 extended_frame_legacy_element(legacy_u8 element)
{
	return extended_track_active() && element >= FRAME_EXTENDED_LEGACY_ELEMENT_LIMIT ? 0 : element;
}

legacy_s16 extended_frame_try_draw_terrain(const struct FRAME_TILE *tile,
										   const struct FRAME_CAMERA *camera, legacy_s8 flags,
										   legacy_s16 *result)
{
	if (!extended_track_active()) {
		return 0;
	}
	*result = extended_frame_draw_terrain(tile, camera, flags);
	return 1;
}

legacy_s16 extended_frame_try_draw_objects(struct FRAME_TILE *tile,
										   const struct FRAME_CAMERA *camera, legacy_s8 flags,
										   legacy_s16 *result)
{
	if (!extended_track_active()) {
		return 0;
	}
	*result = extended_frame_draw_objects(tile, camera, flags);
	return 1;
}

legacy_s16 extended_frame_last_x(void)
{
	return extended_track_width() - 1;
}

legacy_s16 extended_frame_last_y(void)
{
	return extended_track_height() - 1;
}
