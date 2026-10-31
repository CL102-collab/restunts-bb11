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

void frame_add_dynamic_shape(struct TRACKOBJECT *track_object, legacy_s16 state_index,
							 legacy_s16 flags, legacy_s16 material, legacy_s16 z_adjust,
							 const struct VECTOR *camera_position)
{
#if defined(RESTUNTS_SDL3)
	legacy_u8 adaptive_flags =
		frame_adaptive_position_flags(&curtransshape_ptr->pos, camera_position);
	if (adaptive_flags == FRAME_ADAPTIVE_HIDE) {
		return;
	}
#else
	(void)camera_position;
#endif
	curtransshape_ptr->shapeptr = track_object->ss_shapePtr;
#if defined(RESTUNTS_SDL3)
	if (adaptive_flags == FRAME_ADAPTIVE_LOW_GEOMETRY && track_object->ss_loShapePtr != 0) {
		curtransshape_ptr->shapeptr = track_object->ss_loShapePtr;
	}
#endif
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

#if defined(RESTUNTS_SDL3)
struct VECTOR frame_car_render_offset(const struct CARSTATE *carstate,
									  const struct SHAPE3D *wheel_shape)
{
	struct VECTOR local = {0, -shape3d_car_ground_offset(wheel_shape), 0};
	struct VECTOR offset;
	struct MATRIX *rotation =
		mat_rot_zxy(LEGACY_S16_WRAP_NEGATE(carstate->car_rotate.z),
					LEGACY_S16_WRAP_NEGATE(carstate->car_rotate.y),
					LEGACY_S16_WRAP_NEGATE(carstate->car_rotate.x), MATRIX_ROTATION_ORDER_ZXY);
	mat_mul_vector(&local, rotation, &offset);
	/* Collision surfaces are raised two world units above the road artwork.
	 * Grass uses a zero/one-unit height variation instead. Remove the lowest
	 * supported-wheel bias to avoid sinking tires at mixed surface edges.
	 * Keep the road allowance in flight, without snapping to a surface. */
	legacy_s16 surface_bias = FRAME_NON_GRASS_HEIGHT_OFFSET;
	for (legacy_s16 wheel = 0; wheel < FRAME_CAR_WHEEL_COUNT; wheel++) {
		if (carstate->car_surfaceWhl[wheel] == CAR_SURFACE_GRASS) {
			const struct VECTOR *contact = &carstate->car_wheel_contact_positions[wheel];
			legacy_s16 grass_bias =
				((legacy_u16)(contact->x ^ contact->z) >> FRAME_GRASS_HEIGHT_HASH_SHIFT) &
				FRAME_GRASS_HEIGHT_VARIATION_MASK;
			if (grass_bias < surface_bias) {
				surface_bias = grass_bias;
			}
		}
	}
	offset.y = LEGACY_S16_WRAP_SUB(offset.y, surface_bias);
	return offset;
}
#endif

#if defined(RESTUNTS_SDL3)
void frame_offset_car_shape(const struct VECTOR *offset)
{
	curtransshape_ptr->pos.x = LEGACY_S16_WRAP_ADD(curtransshape_ptr->pos.x, offset->x);
	curtransshape_ptr->pos.y = LEGACY_S16_WRAP_ADD(curtransshape_ptr->pos.y, offset->y);
	curtransshape_ptr->pos.z = LEGACY_S16_WRAP_ADD(curtransshape_ptr->pos.z, offset->z);
}
#endif

void frame_prepare_car_model(const struct TRACKOBJECT *track_object,
							 const struct CARSTATE *carstate, struct SHAPE3D *wheel_shape,
							 legacy_s16 *wheel_angles, struct VECTOR *wheel_vectors,
							 struct VECTOR *wheel_vector, legacy_s8 tile_detail)
{
	if ((tile_detail != FRAME_TILE_DETAIL_FULL ||
		 (supersight_enabled == 0 && detail_level >= FRAME_CAR_LOW_DETAIL_FIRST))
#if defined(RESTUNTS_SDL3)
		&& (supersight_enabled == 0 || track_object->ss_loShapePtr != 0)
#endif
	) {
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
	if (frame_adaptive_active != 0) {
		const struct SIMD *simd =
			debris_owner == PLAYER_CAR_INDEX
				? &simd_player
				: (gameconfig.game_opponenttype != 0 ? &simd_opponent : ghost_car_simd());
		legacy_u8 adaptive_flags = frame_adaptive_car_flags(carstate, simd);
		if (adaptive_flags == FRAME_ADAPTIVE_HIDE) {
			return;
		}
		tile_detail = adaptive_flags == FRAME_ADAPTIVE_LOW_GEOMETRY;
	}
	struct VECTOR render_offset = {0, 0, 0};
	if (supersight_enabled != 0) {
		flags |= SHAPE3D_NO_SHADOW_RECEIVE_FLAG;
		render_offset = frame_car_render_offset(carstate, wheel_shape);
	}
#endif
	struct TRACKOBJECT *track_object;
	if (frame_state->game_particles_active != 0 && (flags & SHAPE3D_GHOST_FLAG) == 0U
#if defined(RESTUNTS_SDL3)
		&& frame_adaptive_active == 0
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
				frame_offset_car_shape(&render_offset);
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
	frame_offset_car_shape(&render_offset);
#endif

	frame_prepare_car_model(track_object, carstate, wheel_shape, wheel_angles, wheel_vectors,
							wheel_vector, tile_detail);
	frame_prepare_car_clipping(carstate, slow_rect, crash_rect);

	curtransshape_ptr->ts_flags |= flags & SHAPE3D_GHOST_FLAG;
#if defined(RESTUNTS_SDL3)
	curtransshape_ptr->ts_flags |= flags & SHAPE3D_NO_SHADOW_RECEIVE_FLAG;
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
		if (frame_adaptive_active != 0) {
			legacy_s32 east =
				frame_adaptive_world_tile(carstate->car_position.lx, rotated_vector.x);
			legacy_s32 south =
				TRACK_GRID_LAST_COORDINATE -
				frame_adaptive_world_tile(carstate->car_position.lz, rotated_vector.z);
			if (east < 0 || east > FRAME_TRACK_LAST_X || south < 0 ||
				south > TRACK_GRID_LAST_COORDINATE) {
				continue;
			}
			tile_east = east;
			tile_south = south;
		} else
#endif
		{
			tile_east = frame_tile_from_world_offset(carstate->car_position.lx, rotated_vector.x);
			tile_south =
				frame_south_tile_from_world_offset(carstate->car_position.lz, rotated_vector.z);
		}
#if defined(RESTUNTS_SDL3)
		if (frame_has_complete_tile_lookup(tiles)) {
			legacy_s16 tile_index = frame_lookup_world_tile(tiles, tile_east, tile_south);
			if (tile_index > best_tile_index &&
				tiles->markers[tile_index] != FRAME_TILE_UNAVAILABLE_MARKER) {
				*result_tile_east = tile_east;
				*result_tile_south = tile_south;
				best_tile_index = tile_index;
				matched_wheel = wheel;
			}
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

#if defined(RESTUNTS_SDL3)
void frame_add_car_shadow(const struct CARSTATE *carstate, const struct SIMD *simd,
						  const struct VECTOR *camera_position, const struct SHAPE3D *shape)
{
	if (!extended_frame_car_visible(carstate)) {
		return;
	}

	if (frame_adaptive_car_flags(carstate, simd) == FRAME_ADAPTIVE_HIDE ||
		carstate->car_crashBmpFlag == CRASH_EVENT_WATER ||
		(cameramode == CAMERA_MODE_COCKPIT && carstate == frame_viewed_car_state())) {
		return;
	}
	struct VECTOR relative_position;
	relative_position.x = frame_relative_position(carstate->car_position.lx, camera_position->x);
	relative_position.y = frame_relative_position(carstate->car_position.ly, camera_position->y);
	relative_position.z = frame_relative_position(carstate->car_position.lz, camera_position->z);
	/* The collision box stores half-width and half-length in renderer units. */
	shape3d_hires_shadow_car(&relative_position, LEGACY_S16_WRAP_NEGATE(carstate->car_rotate.x),
							 simd->collide_points[0].px, simd->collide_points[1].px);
	shape3d_hires_shadow_model(shape);
}
#endif

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
