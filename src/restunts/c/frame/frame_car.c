#include "frame_render_private.h"

legacy_s16 frame_car_z_adjust(const legacy_s8 *wheel_surfaces, struct MATRIX *rotation)
{
	if (wheel_surfaces[0] == CAR_SURFACE_GRASS && wheel_surfaces[1] == CAR_SURFACE_GRASS &&
		wheel_surfaces[2] == CAR_SURFACE_GRASS && wheel_surfaces[3] == CAR_SURFACE_GRASS) {
		return 0;
	}
	struct VECTOR offset_vector;
	offset_vector.x = 0;
	offset_vector.z = 0;
	offset_vector.y = FRAME_CAR_UP_VECTOR_LENGTH;
	struct VECTOR rotated_vector;
	mat_mul_vector(&offset_vector, rotation, &rotated_vector);
	mat_mul_vector(&rotated_vector, &mat_temp, &offset_vector);
	if (offset_vector.z <= 0) {
		return -FRAME_CAR_NEAR_SORT_ADJUSTMENT;
	}
	return FRAME_CAR_NEAR_SORT_ADJUSTMENT;
}

void frame_add_dynamic_shape(struct TRACKOBJECT *track_object, legacy_s16 state_index,
							 legacy_s16 flags, legacy_s16 material, legacy_s16 z_adjust,
							 const struct VECTOR *camera_position)
{
	struct SHAPE3D *shape = track_object->ss_shapePtr;
#if defined(RESTUNTS_SDL3)
	if (!frame_hires_dynamic_shape(track_object, camera_position, &shape)) {
		return;
	}
#else
	(void)camera_position;
#endif
	curtransshape_ptr->shapeptr = shape;
	curtransshape_ptr->rectptr = &frame_sorted_shapes_rect;
	curtransshape_ptr->ts_flags = flags;
	curtransshape_ptr->rotvec.x =
		LEGACY_S16_WRAP_NEGATE(frame_state->game_particle_rotation_x[state_index]);
	curtransshape_ptr->rotvec.y =
		LEGACY_S16_WRAP_NEGATE(frame_state->game_particle_rotation_y[state_index]);
	curtransshape_ptr->rotvec.z =
		LEGACY_S16_WRAP_NEGATE(frame_state->game_particle_heading[state_index]);
	curtransshape_ptr->culling_distance = FRAME_DEFAULT_TRANSFORM_DISTANCE;
	curtransshape_ptr->material = material;
	transformed_shape_add_for_sort(z_adjust, 0);
}

void frame_prepare_car_model(const struct TRACKOBJECT *track_object,
							 const struct CARSTATE *carstate, struct SHAPE3D *wheel_shape,
							 legacy_s16 *wheel_angles, struct VECTOR *wheel_vectors,
							 struct VECTOR *wheel_vector, legacy_s8 tile_detail)
{
	legacy_s16 low_detail =
		tile_detail != FRAME_TILE_DETAIL_FULL || detail_level >= FRAME_CAR_LOW_DETAIL_FIRST;
	frame_renderer_override_car_detail(track_object, tile_detail, &low_detail);
	if (low_detail) {
		curtransshape_ptr->shapeptr = track_object->ss_loShapePtr;
	} else {
		curtransshape_ptr->shapeptr = track_object->ss_shapePtr;
		shape3d_update_car_wheel_vertices(
			wheel_shape, FRAME_STEERED_WHEEL_FIRST_VERTEX, carstate->car_steeringAngle,
			carstate->car_suspension_deflection, wheel_angles, wheel_vectors, wheel_vector);
	}
}

void frame_prepare_car_clipping(const struct CARSTATE *carstate, struct RECTANGLE *slow_rect,
								struct RECTANGLE *crash_rect)
{
	if (slow_video_mgmt_copy != 0) {
		curtransshape_ptr->rectptr = slow_rect;
		curtransshape_ptr->ts_flags = FRAME_TRANSFORM_FLAGS_CLIPPED;
	} else if (carstate->car_crashBmpFlag != CRASH_EVENT_COLLISION) {
		curtransshape_ptr->ts_flags = FRAME_TRANSFORM_FLAGS_DEFAULT;
	} else {
		*crash_rect = empty_rect;
		curtransshape_ptr->rectptr = crash_rect;
		curtransshape_ptr->ts_flags = FRAME_TRANSFORM_FLAGS_CLIPPED;
	}
}

/* The player and the opponent are drawn identically: first the debris
 * attached to that car, then the car body itself with its wheels, clip
 * rectangle and rotation. Only the shapes, buffers and material differ. */
void frame_add_car(const struct CARSTATE *carstate, legacy_s8 debris_owner, legacy_u16 car_object,
				   struct SHAPE3D *wheel_shape, legacy_s16 *wheel_angles,
				   struct VECTOR *wheel_vectors, struct VECTOR *wheel_vector,
				   struct RECTANGLE *slow_rect, struct RECTANGLE *crash_rect,
				   const struct VECTOR *camera_position, legacy_s8 tile_detail, legacy_s8 flags,
				   legacy_s16 material, legacy_s16 z_adjust)
{
#if defined(RESTUNTS_SDL3)
	struct VECTOR render_offset = {0, 0, 0};
	if (!frame_hires_prepare_car(carstate, debris_owner, wheel_shape, &tile_detail, &flags,
								 &render_offset)) {
		return;
	}
#endif
	struct TRACKOBJECT *track_object;
	if (frame_state->game_particles_active != 0 && (flags & SHAPE3D_GHOST_FLAG) == 0U
#if defined(RESTUNTS_SDL3)
		&& !frame_renderer_adaptive_enabled()
#endif
	) {
		for (legacy_s16 index = 0; index < FRAME_DEBRIS_SLOT_COUNT; index++) {
			if (frame_state->game_particle_forward_speed[index] != 0 &&
				frame_state->game_particle_owner[index] == debris_owner) {
				track_object =
					&particle_scene_objects[frame_state->game_particle_shape_index[index]];
				curtransshape_ptr->pos.x =
					frame_relative_position_sum(frame_state->game_particle_x[index],
												carstate->car_position.lx, camera_position->x);
				curtransshape_ptr->pos.y =
					frame_relative_position_sum(frame_state->game_particle_y[index],
												carstate->car_position.ly, camera_position->y);
				curtransshape_ptr->pos.z =
					frame_relative_position_sum(frame_state->game_particle_z[index],
												carstate->car_position.lz, camera_position->z);
#if defined(RESTUNTS_SDL3)
				frame_hires_offset_car_shape(&render_offset);
#endif
				frame_add_dynamic_shape(track_object, index,
										flags | FRAME_TRANSFORM_FLAGS_NO_DEPTH_SORT, material,
										z_adjust, camera_position);
			}
		}
	}

	track_object = &trkObjectList[car_object];
	curtransshape_ptr->pos.x =
		frame_relative_position(carstate->car_position.lx, camera_position->x);
	curtransshape_ptr->pos.y =
		frame_relative_position(carstate->car_position.ly, camera_position->y);
	curtransshape_ptr->pos.z =
		frame_relative_position(carstate->car_position.lz, camera_position->z);
#if defined(RESTUNTS_SDL3)
	frame_hires_offset_car_shape(&render_offset);
#endif

	frame_prepare_car_model(track_object, carstate, wheel_shape, wheel_angles, wheel_vectors,
							wheel_vector, tile_detail);
	frame_prepare_car_clipping(carstate, slow_rect, crash_rect);

	curtransshape_ptr->ts_flags |= flags & SHAPE3D_GHOST_FLAG;
#if defined(RESTUNTS_SDL3)
	frame_hires_car_flags(flags);
#endif
	curtransshape_ptr->rotvec.x = LEGACY_S16_WRAP_NEGATE(carstate->car_rotate.z);
	curtransshape_ptr->rotvec.y = LEGACY_S16_WRAP_NEGATE(carstate->car_rotate.y);
	curtransshape_ptr->rotvec.z = LEGACY_S16_WRAP_NEGATE(carstate->car_rotate.x);
	curtransshape_ptr->culling_distance = FRAME_CAR_TRANSFORM_DISTANCE;
	curtransshape_ptr->material = material;
	/* The sort slot carries the same id as the track object: 2 for the
	   player, 3 for the opponent. */
	transformed_shape_add_for_sort(z_adjust, (legacy_s16)car_object);
}

legacy_s16 frame_find_car_wheel(const struct CARSTATE *carstate, const struct SIMD *simd,
								const struct FRAME_TILE_SELECTION *tiles,
								legacy_s8 *result_tile_east, legacy_s8 *result_tile_south)
{
	struct MATRIX *rotation =
		mat_rot_zxy(LEGACY_S16_WRAP_NEGATE(carstate->car_rotate.z),
					LEGACY_S16_WRAP_NEGATE(carstate->car_rotate.y),
					LEGACY_S16_WRAP_NEGATE(carstate->car_rotate.x), MATRIX_ROTATION_ORDER_ZXY);
	struct VECTOR rotated_vector;
	legacy_s16 best_tile_index = -1;
	legacy_s16 matched_wheel = -1;
	struct VECTOR offset_vector;
	for (legacy_s16 wheel = 0; wheel < FRAME_CAR_WHEEL_COUNT; wheel++) {
		offset_vector = simd->wheel_coords[wheel];
		mat_mul_vector(&offset_vector, rotation, &rotated_vector);
		legacy_s8 tile_east, tile_south;
#if defined(RESTUNTS_SDL3)
		legacy_s16 visible;
		if (frame_hires_wheel_tile(carstate, &rotated_vector, &tile_east, &tile_south, &visible)) {
			if (!visible) {
				continue;
			}
		} else
#endif
		{
			tile_east = frame_tile_from_world_offset(carstate->car_position.lx, rotated_vector.x);
			tile_south =
				frame_south_tile_from_world_offset(carstate->car_position.lz, rotated_vector.z);
		}
#if defined(RESTUNTS_SDL3)
		if (hires_extended_frame_find_indexed_wheel(tiles, tile_east, tile_south, wheel,
													&best_tile_index, &matched_wheel,
													result_tile_east, result_tile_south)) {
			continue;
		}
#endif
		for (legacy_s16 tile_index = tiles->count - 1; tile_index > best_tile_index; tile_index--) {
			if (tiles->markers[tile_index] != FRAME_TILE_UNAVAILABLE_MARKER &&
				tiles->lookahead[tile_index].east + tiles->camera_east == tile_east &&
				tiles->lookahead[tile_index].south + tiles->camera_south == tile_south) {
				*result_tile_east = tile_east;
				*result_tile_south = tile_south;
				best_tile_index = tile_index;
				matched_wheel = wheel;
			}
		}
	}
	if (matched_wheel != -1) {
		return frame_car_z_adjust(carstate->car_surfaceWhl, rotation);
	}
	return 0;
}

void frame_place_cars(const struct FRAME_TILE_SELECTION *tiles, struct FRAME_CAR_RENDER *cars)
{
	// Locate the player in the visible tile list using its wheels.
	cars[PLAYER_CAR_INDEX].east = -1;
	cars[PLAYER_CAR_INDEX].depth_adjustment = 0;
	if (cameramode != CAMERA_MODE_COCKPIT || followOpponentFlag != 0) {
		if (frame_state->playerstate.car_crashBmpFlag != CRASH_EVENT_WATER) {
			cars[PLAYER_CAR_INDEX].depth_adjustment =
				frame_find_car_wheel(&frame_state->playerstate, &simd_player, tiles,
									 &cars[PLAYER_CAR_INDEX].east, &cars[PLAYER_CAR_INDEX].south);
		}
	}

	// Locate the opponent in the same draw order.
	cars[OPPONENT_CAR_INDEX].east = -1;
	cars[OPPONENT_CAR_INDEX].depth_adjustment = 0;
	const struct CARSTATE *second_car = frame_second_car_state();
	if (second_car != 0) {
		if (cameramode != CAMERA_MODE_COCKPIT || followOpponentFlag == 0) {
			if (second_car->car_crashBmpFlag != CRASH_EVENT_WATER) {
				const struct SIMD *second_simd =
					gameconfig.game_opponenttype != 0 ? &simd_opponent : ghost_car_simd();
				cars[OPPONENT_CAR_INDEX].depth_adjustment = frame_find_car_wheel(
					second_car, second_simd, tiles, &cars[OPPONENT_CAR_INDEX].east,
					&cars[OPPONENT_CAR_INDEX].south);
			}
		}
	}
}
