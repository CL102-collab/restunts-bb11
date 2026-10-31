#include "frame_render_private.h"

#if !defined(RESTUNTS_SDL3)
legacy_u8 frame_racing_line_enabled = 1;
#endif

#if !defined(RESTUNTS_SDL3)
legacy_u8 frame_racing_line_submitted;
#endif

void frame_add_roadside_sign(const struct FRAME_TILE *tile, const struct FRAME_CAMERA *camera,
							 legacy_s8 redraw_transform_flags)
{
	legacy_u8 breakable_object_index =
		roadside_sign_indices_by_tile[tile->east + trackrows[tile->south]];
	struct TRACKOBJECT *track_object;
	if (breakable_object_index != FRAME_CHECKPOINT_NONE) {
		if (frame_state->game_object_destroyed[breakable_object_index] == 0) {
			track_object = &trkObjectList[FRAME_CHECKPOINT_TRACK_OBJECT_BASE +
										  roadside_sign_shape_indices[breakable_object_index]];
			curtransshape_ptr->pos.x = LEGACY_S16_WRAP_SUB(
				roadside_sign_positions[breakable_object_index].x, camera->position.x);
			curtransshape_ptr->pos.y = LEGACY_S16_WRAP_SUB(
				roadside_sign_positions[breakable_object_index].y, camera->position.y);
			curtransshape_ptr->pos.z = LEGACY_S16_WRAP_SUB(
				roadside_sign_positions[breakable_object_index].z, camera->position.z);
#if defined(RESTUNTS_SDL3)
			curtransshape_ptr->shapeptr = frame_hires_terrain_shape(track_object, tile->detail);
#else
			curtransshape_ptr->shapeptr = track_object->ss_shapePtr;
#endif
			curtransshape_ptr->rectptr = &frame_sorted_shapes_rect;
			curtransshape_ptr->ts_flags = redraw_transform_flags | FRAME_TRANSFORM_FLAGS_DEFAULT;
			curtransshape_ptr->rotvec.x = 0;
			curtransshape_ptr->rotvec.y = 0;
			curtransshape_ptr->rotvec.z = roadside_sign_headings[breakable_object_index];
			curtransshape_ptr->culling_distance = FRAME_CHECKPOINT_TRANSFORM_DISTANCE;
			curtransshape_ptr->material = 0;
			transformed_shape_add_for_sort(0, 0);
		} else if (frame_state->game_particles_active != 0
#if defined(RESTUNTS_SDL3)
				   && frame_hires_attached_particles()
#endif
		) {
			for (legacy_s16 particle_index = 0; particle_index < FRAME_DEBRIS_SLOT_COUNT;
				 particle_index++) {
				if (frame_state->game_particle_forward_speed[particle_index] != 0 &&
					breakable_object_index + FRAME_CHECKPOINT_OWNER_OFFSET ==
						frame_state->game_particle_owner[particle_index]) {
					track_object =
						&particle_scene_objects[frame_state
													->game_particle_shape_index[particle_index]];
					curtransshape_ptr->pos.x = frame_relative_track_position(
						frame_state->game_particle_x[particle_index],
						roadside_sign_positions[breakable_object_index].x, camera->position.x);
					curtransshape_ptr->pos.y = frame_relative_track_position(
						frame_state->game_particle_y[particle_index],
						roadside_sign_positions[breakable_object_index].y, camera->position.y);
					curtransshape_ptr->pos.z = frame_relative_track_position(
						frame_state->game_particle_z[particle_index],
						roadside_sign_positions[breakable_object_index].z, camera->position.z);
					frame_add_dynamic_shape(track_object, particle_index,
											redraw_transform_flags |
												FRAME_TRANSFORM_FLAGS_NO_DEPTH_SORT,
											0, 0, &camera->position);
				}
			}
		}
	}
}

void frame_animate_start_flag(void)
{
	legacy_s16 flag_x = multiply_and_scale(cos_fast(start_flag_animation), FRAME_START_FLAG_RADIUS);
	legacy_s16 flag_z = LEGACY_S16_WRAP_ADD(
		multiply_and_scale(sin_fast(start_flag_animation), FRAME_START_FLAG_RADIUS),
		FRAME_START_FLAG_CENTER_Z);

	struct VECTOR start_flag_vertices[FRAME_START_FLAG_VERTEX_COUNT];
	for (legacy_u16 vertex_index = 0; vertex_index < FRAME_START_FLAG_VERTEX_COUNT;
		 vertex_index++) {
		shape3d_vertex_read(&game3dshapes[FRAME_START_FLAG_SHAPE_INDEX],
							LEGACY_U16_WRAP_ADD(FRAME_START_FLAG_FIRST_VERTEX, vertex_index),
							&start_flag_vertices[vertex_index]);
	}
	start_flag_vertices[FRAME_START_FLAG_VERTEX_LEFT_NEAR].x =
		LEGACY_S16_WRAP_SUB(flag_x, FRAME_START_FLAG_RADIUS);
	start_flag_vertices[FRAME_START_FLAG_VERTEX_LEFT_FAR].x =
		LEGACY_S16_WRAP_SUB(flag_x, FRAME_START_FLAG_RADIUS);
	start_flag_vertices[FRAME_START_FLAG_VERTEX_RIGHT_NEAR].x =
		LEGACY_S16_WRAP_SUB(FRAME_START_FLAG_RADIUS, flag_x);
	start_flag_vertices[FRAME_START_FLAG_VERTEX_RIGHT_FAR].x =
		LEGACY_S16_WRAP_SUB(FRAME_START_FLAG_RADIUS, flag_x);

	start_flag_vertices[FRAME_START_FLAG_VERTEX_LEFT_NEAR].z = flag_z;
	start_flag_vertices[FRAME_START_FLAG_VERTEX_LEFT_FAR].z = flag_z;
	start_flag_vertices[FRAME_START_FLAG_VERTEX_RIGHT_NEAR].z = flag_z;
	start_flag_vertices[FRAME_START_FLAG_VERTEX_RIGHT_FAR].z = flag_z;
	for (legacy_u16 vertex_index = 0; vertex_index < FRAME_START_FLAG_VERTEX_COUNT;
		 vertex_index++) {
		shape3d_vertex_write(&game3dshapes[FRAME_START_FLAG_SHAPE_INDEX],
							 LEGACY_U16_WRAP_ADD(FRAME_START_FLAG_FIRST_VERTEX, vertex_index),
							 &start_flag_vertices[vertex_index]);
	}
}

void frame_add_start_flag(const struct FRAME_TILE *tile, const struct FRAME_CAMERA *camera,
						  legacy_s8 redraw_transform_flags)
{
	if (frame_state->game_inputmode == GAME_INPUT_MODE_WAITING) {
		if ((tile->east == start_finish_column || tile->last_east == start_finish_column) &&
			(tile->south == start_finish_row || tile->last_south == start_finish_row)) {
			frame_animate_start_flag();
#if defined(RESTUNTS_SDL3)
			if (extended_frame_add_start_flag(camera, redraw_transform_flags)) {
				return;
			}
#endif

			curtransshape_ptr->pos.x = LEGACY_S16_WRAP_SUB(
				LEGACY_S16_WRAP_ADD(
					LEGACY_S16_WRAP_ADD(multiply_and_scale(sin_fast(LEGACY_S16_WRAP_ADD(
															   track_angle, ANGLE_QUARTER_TURN)),
														   FRAME_START_FLAG_RADIUS),
										multiply_and_scale(sin_fast(LEGACY_S16_WRAP_ADD(
															   track_angle, ANGLE_HALF_TURN)),
														   FRAME_START_FLAG_FAR_OFFSET)),
					track_column_centers[start_finish_column]),
				camera->position.x);
			curtransshape_ptr->pos.y =
				LEGACY_S16_WRAP_SUB(hillHeightConsts[hillFlag], camera->position.y);
			curtransshape_ptr->pos.z = LEGACY_S16_WRAP_SUB(
				LEGACY_S16_WRAP_ADD(
					LEGACY_S16_WRAP_ADD(multiply_and_scale(cos_fast(LEGACY_S16_WRAP_ADD(
															   track_angle, ANGLE_QUARTER_TURN)),
														   FRAME_START_FLAG_RADIUS),
										multiply_and_scale(cos_fast(LEGACY_S16_WRAP_ADD(
															   track_angle, ANGLE_HALF_TURN)),
														   FRAME_START_FLAG_FAR_OFFSET)),
					track_row_centers[start_finish_row]),
				camera->position.z);

			curtransshape_ptr->shapeptr = &game3dshapes[FRAME_START_FLAG_SHAPE_INDEX];
			curtransshape_ptr->rectptr = &frame_sorted_shapes_rect;
			curtransshape_ptr->ts_flags = redraw_transform_flags | FRAME_TRANSFORM_FLAGS_DEFAULT;
			curtransshape_ptr->rotvec.x = 0;
			curtransshape_ptr->rotvec.y = 0;
			curtransshape_ptr->rotvec.z = track_angle;
			curtransshape_ptr->culling_distance = FRAME_DEFAULT_TRANSFORM_DISTANCE;
			legacy_s16 flag_material =
				LEGACY_S16_SAR(start_flag_animation, FRAME_START_FLAG_ANIMATION_SHIFT);
			if (flag_material > FRAME_START_FLAG_MAX_MATERIAL) {
				flag_material = FRAME_START_FLAG_MAX_MATERIAL;
			}

			curtransshape_ptr->material = flag_material;
			transformed_shape_add_for_sort(tile->depth_mask & -FRAME_SINGLE_TILE_TRANSFORM_DISTANCE,
										   0);
		}
	}
}

/* Exhaustion stops the current tile's sorted shapes, then advances the tile. */
void frame_select_brake_paint(legacy_s16 shape_index)
{
	if (transformed_shape_sort_types[shape_index] == FRAME_PLAYER_SORT_ID) {
		if (frame_state->playerstate.car_is_braking != 0) {
			backlights_paint_override = BACKLIGHT_PAINT_BRAKING;
		} else {
			backlights_paint_override = BACKLIGHT_PAINT_NORMAL;
		}
	} else if (transformed_shape_sort_types[shape_index] == FRAME_OPPONENT_SORT_ID) {
		if (frame_state->opponentstate.car_is_braking == 0) {
			backlights_paint_override = BACKLIGHT_PAINT_NORMAL;
		} else {
			backlights_paint_override = BACKLIGHT_PAINT_BRAKING;
		}
	}
}

#if !defined(RESTUNTS_SDL3)
legacy_s16 frame_retry_without_racing_line(void)
{
	if (frame_racing_line_enabled != 0 && frame_racing_line_submitted != 0 &&
		polygon_buffer_full != 0) {
		/* Guidance must never spend the fixed queue budget needed by the world.
		 * Retry the complete scene, including batches queued before exhaustion. */
		frame_racing_line_enabled = 0;
		return 1;
	}
	return 0;
}
#endif

legacy_s16 frame_has_racing_line(const struct FRAME_TILE *tile)
{
#if !defined(RESTUNTS_SDL3)
	if (frame_racing_line_enabled == 0) {
		return 0;
	}
#endif
	if (tile == 0 || !ghost_is_active()) {
		return 0;
	}
	for (legacy_s16 south = tile->south; south <= tile->last_south; south++) {
		for (legacy_s16 east = tile->east; east <= tile->last_east; east++) {
			if (ghost_racing_line_first(east, south) != 0) {
				return 1;
			}
		}
	}
	return 0;
}

struct VECTOR frame_racing_line_origin(const struct FRAME_TILE *tile,
									   const struct FRAME_CAMERA *camera)
{
	if (tile->element != 0) {
		return tile->position;
	}
	struct VECTOR origin;
	origin.x = LEGACY_S16_WRAP_SUB(track_column_centers[tile->east], camera->position.x);
	origin.y = LEGACY_S16_WRAP_SUB(tile->height, camera->position.y);
	origin.z = LEGACY_S16_WRAP_SUB(track_row_centers[tile->south], camera->position.z);
	return origin;
}

void frame_build_racing_line_segment(struct SHAPE3D *shape,
									 const struct GHOST_RACING_LINE_SEGMENT *segment,
									 legacy_u16 count, const struct VECTOR *origin,
									 struct VECTOR *minimum, struct VECTOR *maximum)
{
	static const legacy_u8 paints[] = {12, 14, 10}; /* Bright red, yellow, green. */
	const struct VECTORLONG *corners[FRAME_RACING_LINE_SEGMENT_VERTICES];
	corners[0] = &segment->start_left;
	corners[1] = &segment->start_right;
	corners[2] = &segment->end_right;
	corners[3] = &segment->end_left;
	legacy_u16 first =
		FRAME_RACING_LINE_BOUNDS_VERTICES + count * FRAME_RACING_LINE_SEGMENT_VERTICES;
	legacy_u8 *primitive = shape->shape3d_primitives + count * FRAME_RACING_LINE_PRIMITIVE_SIZE;
	primitive[0] = FRAME_RACING_LINE_SEGMENT_VERTICES;
	/* A ribbon is visible from either side, including inside a loop.
	 * Its surface lift supplies depth separation from road artwork. */
	primitive[1] = 1;
	primitive[2] =
		paints[ghost_racing_line_color(frame_state->playerstate.car_actual_speed, segment->speed)];
	for (legacy_u16 corner = 0; corner < FRAME_RACING_LINE_SEGMENT_VERTICES; corner++) {
		struct VECTOR vertex;
		vertex.x = LEGACY_S16_WRAP_SUB(position_to_word(corners[corner]->lx), origin->x);
		vertex.y = LEGACY_S16_WRAP_SUB(position_to_word(corners[corner]->ly), origin->y);
		vertex.z = LEGACY_S16_WRAP_SUB(position_to_word(corners[corner]->lz), origin->z);
		shape3d_vertex_write(shape, first + corner, &vertex);
		primitive[3 + corner] = (legacy_u8)(first + corner);
		if (vertex.x < minimum->x) {
			minimum->x = vertex.x;
		}
		if (vertex.y < minimum->y) {
			minimum->y = vertex.y;
		}
		if (vertex.z < minimum->z) {
			minimum->z = vertex.z;
		}
		if (vertex.x > maximum->x) {
			maximum->x = vertex.x;
		}
		if (vertex.y > maximum->y) {
			maximum->y = vertex.y;
		}
		if (vertex.z > maximum->z) {
			maximum->z = vertex.z;
		}
	}
}

legacy_s16 frame_draw_racing_line(const struct FRAME_TILE *tile, const struct FRAME_CAMERA *camera,
								  legacy_s8 redraw_transform_flags)
{
	legacy_u8 vertices[(FRAME_RACING_LINE_BOUNDS_VERTICES +
						FRAME_RACING_LINE_BATCH_SIZE * FRAME_RACING_LINE_SEGMENT_VERTICES) *
					   SHAPE3D_VERTEX_SIZE];
	legacy_u8 primitives[FRAME_RACING_LINE_BATCH_SIZE * FRAME_RACING_LINE_PRIMITIVE_SIZE +
						 SHAPE3D_PRIMITIVE_HEADER_SIZE];
	legacy_u8 visibility[FRAME_RACING_LINE_BATCH_SIZE * SHAPE3D_VISIBILITY_MASK_SIZE];
	struct SHAPE3D shape = {0};
	struct TRANSFORMEDSHAPE3D instance = {0};
	shape.shape3d_vertex_bytes = vertices;
	shape.shape3d_primitives = primitives;
	shape.shape3d_numpaints = 1;
	shape.shape3d_visibility_masks = visibility;
	shape.shape3d_front_facing_masks = visibility;
	for (legacy_u16 byte = 0; byte < sizeof(visibility); byte++) {
		visibility[byte] = 255;
	}
	instance.shapeptr = &shape;
	instance.rectptr = &frame_sorted_shapes_rect;
	instance.pos = frame_racing_line_origin(tile, camera);
	instance.ts_flags = frame_hires_racing_line_flags(redraw_transform_flags);
	instance.culling_distance = FRAME_DEFAULT_TRANSFORM_DISTANCE;
	struct VECTOR origin;
	origin.x = LEGACY_S16_WRAP_ADD(instance.pos.x, camera->position.x);
	origin.y = LEGACY_S16_WRAP_ADD(instance.pos.y, camera->position.y);
	origin.z = LEGACY_S16_WRAP_ADD(instance.pos.z, camera->position.z);

	for (legacy_s16 south = tile->south; south <= tile->last_south; south++) {
		for (legacy_s16 east = tile->east; east <= tile->last_east; east++) {
			const struct GHOST_RACING_LINE_SEGMENT *segment = ghost_racing_line_first(east, south);
			while (segment != 0) {
				struct VECTOR minimum = {LEGACY_S16_MAX, LEGACY_S16_MAX, LEGACY_S16_MAX};
				struct VECTOR maximum = {FRAME_RACING_LINE_MINIMUM_COORDINATE,
										 FRAME_RACING_LINE_MINIMUM_COORDINATE,
										 FRAME_RACING_LINE_MINIMUM_COORDINATE};
				legacy_u16 count = 0;
				while (segment != 0 && count < FRAME_RACING_LINE_BATCH_SIZE) {
					frame_build_racing_line_segment(&shape, segment, count, &origin, &minimum,
													&maximum);
					count++;
					segment = ghost_racing_line_next(segment);
				}
				for (legacy_u16 corner = 0; corner < FRAME_RACING_LINE_BOUNDS_VERTICES; corner++) {
					struct VECTOR vertex;
					vertex.x = (corner & 1U) != 0 ? maximum.x : minimum.x;
					vertex.y = (corner & 4U) != 0 ? maximum.y : minimum.y;
					vertex.z = (corner & 2U) != 0 ? maximum.z : minimum.z;
					shape3d_vertex_write(&shape, corner, &vertex);
				}
				shape.shape3d_numverts =
					FRAME_RACING_LINE_BOUNDS_VERTICES + count * FRAME_RACING_LINE_SEGMENT_VERTICES;
				shape.shape3d_numprimitives = count;
				primitives[count * FRAME_RACING_LINE_PRIMITIVE_SIZE] = SHAPE3D_PRIMITIVE_EMPTY;
				/* Classic clipping inspects the next primitive's flags before its
				 * type, including when the last polygon in a full batch is hidden. */
				primitives[count * FRAME_RACING_LINE_PRIMITIVE_SIZE + 1] = 0;
#if !defined(RESTUNTS_SDL3)
				frame_racing_line_submitted = 1;
#endif
				if ((legacy_s16)shape3d_transform_and_queue(&instance) > 0) {
					return 1;
				}
			}
		}
	}
	return 0;
}

void frame_mark_collision_explosion(struct FRAME_CAR_RENDER *cars, legacy_s16 shape_index)
{
	if (transformed_shape_sort_types[shape_index] == FRAME_PLAYER_SORT_ID) {
		if (frame_state->playerstate.car_crashBmpFlag == CRASH_EVENT_COLLISION) {
			cars[PLAYER_CAR_INDEX].explosion_visible = 1;
		}
	} else if (transformed_shape_sort_types[shape_index] == FRAME_OPPONENT_SORT_ID) {
		if ((currenttransshape[shape_index].ts_flags & SHAPE3D_GHOST_FLAG) == 0U &&
			frame_state->opponentstate.car_crashBmpFlag == CRASH_EVENT_COLLISION) {
			cars[OPPONENT_CAR_INDEX].explosion_visible = 1;
		}
	}
}

legacy_s16 frame_draw_sorted_shapes(struct FRAME_CAR_RENDER *cars, const struct FRAME_TILE *tile,
									const struct FRAME_CAMERA *camera,
									legacy_s8 redraw_transform_flags)
{
	legacy_s16 racing_line_pending = frame_has_racing_line(tile);
	legacy_s16 racing_line_depth = 0;
	legacy_s16 depth_sort_racing_line = frame_hires_sort_depth();
	if (racing_line_pending != 0 && depth_sort_racing_line != 0) {
		struct VECTOR origin = frame_racing_line_origin(tile, camera);
		struct VECTOR view;
		mat_mul_vector(&origin, &mat_temp, &view);
		/* Follow the road's own tile sort point, immediately above its paint.
		 * Merge a virtual entry instead of using a slot needed by car/debris shapes. */
		racing_line_depth = LEGACY_S16_WRAP_SUB(view.z, 1);
	}
	if (transformedshape_counter != 0) {
		if (transformedshape_counter >= FRAME_SORT_MINIMUM_SHAPE_COUNT &&
			frame_hires_sort_depth()) {
			heapsort_by_order(transformedshape_counter, transformedshape_zarray,
							  transformedshape_indices);
		}

		// Draw red overlights on the brake lights on own and opponent's car
		for (legacy_s16 sort_index = 0; sort_index < transformedshape_counter; sort_index++) {
			legacy_s16 shape_index = transformedshape_indices[sort_index];
			if (racing_line_pending != 0 &&
				(depth_sort_racing_line == 0 ||
				 racing_line_depth > transformedshape_zarray[shape_index])) {
				racing_line_pending = 0;
				/* A full optional line never suppresses the remaining world shapes. */
				(void)frame_draw_racing_line(tile, camera, redraw_transform_flags);
			}
			frame_select_brake_paint(shape_index);

			legacy_s16 transform_result =
				shape3d_transform_and_queue(&currenttransshape[shape_index]);
			if (transform_result > 0) {
				return 1;
			}

			if (transform_result == 0) {
				frame_mark_collision_explosion(cars, shape_index);
			}
		}
	}
	if (racing_line_pending != 0) {
		(void)frame_draw_racing_line(tile, camera, redraw_transform_flags);
	}
	return 0;
}

void frame_position_track_element(struct FRAME_TILE *tile, const struct FRAME_CAMERA *camera,
								  const struct TRACKOBJECT *track_object)
{
	legacy_s16 track_object_world_z;
	if ((track_object->ss_multiTileFlag & FRAME_MULTITILE_ROW) != 0) {
		track_object_world_z = track_row_position((legacy_u16)tile->south);
		tile->last_south = LEGACY_S8_WRAP_ADD(tile->south, 1);
	} else {
		track_object_world_z = track_row_centers[tile->south];
		tile->last_south = tile->south;
	}

	legacy_s16 track_object_world_x;
	if ((track_object->ss_multiTileFlag & FRAME_MULTITILE_COLUMN) != 0) {
		track_object_world_x = track_column_position((legacy_u16)LEGACY_S8_WRAP_ADD(tile->east, 1));
		tile->last_east = LEGACY_S8_WRAP_ADD(tile->east, 1);
	} else {
		track_object_world_x = track_column_centers[tile->east];
		tile->last_east = tile->east;
	}

	tile->position.x = LEGACY_S16_WRAP_SUB(track_object_world_x, camera->position.x);
	tile->position.y = LEGACY_S16_WRAP_SUB(tile->height, camera->position.y);
	tile->position.z = LEGACY_S16_WRAP_SUB(track_object_world_z, camera->position.z);
}

void frame_track_sort_state(struct FRAME_TILE *tile, struct FRAME_CAR_RENDER *cars,
							legacy_s8 sorted_overlay)
{
	if (sorted_overlay != 0) {
		if (cars[PLAYER_CAR_INDEX].depth_adjustment != 0) {
			cars[PLAYER_CAR_INDEX].depth_adjustment = -FRAME_WHEEL_SORT_ADJUSTMENT;
		}
		if (cars[OPPONENT_CAR_INDEX].depth_adjustment != 0) {
			cars[OPPONENT_CAR_INDEX].depth_adjustment = LEGACY_S16_WRAP_SUB(
				cars[OPPONENT_CAR_INDEX].depth_adjustment, FRAME_WHEEL_SORT_ADJUSTMENT);
		}
	}
	tile->depth_mask =
		tile->east == start_finish_column && tile->south == start_finish_row ? 0 : -1;
}

legacy_s16 frame_add_track_element(struct FRAME_TILE *tile, const struct FRAME_CAMERA *camera,
								   struct FRAME_CAR_RENDER *cars, legacy_s8 redraw_transform_flags,
								   legacy_s8 animated_material, legacy_s8 *overlay_needs_depth_sort)
{
	struct TRACKOBJECT *track_object;
#if defined(RESTUNTS_SDL3)
	legacy_s16 result;
	if (extended_frame_try_draw_objects(tile, camera, redraw_transform_flags, &result)) {
		return result;
	}
#endif

	if (tile->element == 0) {
		tile->last_east = tile->east;
		tile->last_south = tile->south;
	} else {
		track_object = &trkObjectList[tile->element];
		frame_position_track_element(tile, camera, track_object);
		if (frame_hires_exhausted(
				frame_draw_hill_fill(tile, track_object, redraw_transform_flags))) {
			return 1;
		}

		if (frame_prepare_overlay(tile, track_object, redraw_transform_flags, animated_material,
								  overlay_needs_depth_sort) != 0) {
			return 1;
		}

#if defined(RESTUNTS_SDL3)
		currenttransshape->shapeptr = frame_track_shape(track_object, tile->detail);
#else
		if (tile->detail != FRAME_TILE_DETAIL_FULL) {
			currenttransshape->shapeptr = track_object->ss_loShapePtr;
		} else {
			currenttransshape->shapeptr = track_object->ss_shapePtr;
		}
#endif

		currenttransshape->pos = tile->position;
		currenttransshape->rotvec.x = 0;
		currenttransshape->rotvec.y = 0;
		currenttransshape->rotvec.z = track_object->ss_rotY;
		if (track_object->ss_multiTileFlag != FRAME_MULTITILE_NONE) {
			currenttransshape->culling_distance = FRAME_DEFAULT_TRANSFORM_DISTANCE;
		} else {
			currenttransshape->culling_distance = FRAME_SINGLE_TILE_TRANSFORM_DISTANCE;
		}

		currenttransshape->ts_flags =
			track_object->ss_ignoreZBias | redraw_transform_flags | FRAME_TRANSFORM_FLAGS_DEFAULT;
		if (track_object->ss_surfaceType >= 0) {
			currenttransshape->material = track_object->ss_surfaceType;
		} else {
			currenttransshape->material = animated_material;
		}

		if ((track_object->ss_ignoreZBias & FRAME_NO_DEPTH_SORT_FLAG) != 0) {
			currenttransshape->rectptr = &frame_unsorted_shapes_rect;
			legacy_s16 transform_result = shape3d_transform_and_queue(&currenttransshape[0]);
			if (transform_result > 0) {
				return 1;
			}
		} else {
			currenttransshape->rectptr = &frame_sorted_shapes_rect;
			transformed_shape_add_for_sort(0, 0);
			legacy_s8 had_sorted_overlay = *overlay_needs_depth_sort;
			if (*overlay_needs_depth_sort != 0) {
				*overlay_needs_depth_sort = 0;
				transformed_shape_add_for_sort(-FRAME_SINGLE_TILE_TRANSFORM_DISTANCE, 0);
			}
			frame_track_sort_state(tile, cars, had_sorted_overlay);
		}

		frame_add_roadside_sign(tile, camera, redraw_transform_flags);
	}

	return 0;
}

void frame_add_tile_cars(const struct FRAME_TILE *tile, const struct FRAME_CAMERA *camera,
						 struct FRAME_CAR_RENDER *cars, legacy_s8 redraw_transform_flags)
{
	if ((cars[PLAYER_CAR_INDEX].east == tile->east ||
		 cars[PLAYER_CAR_INDEX].east == tile->last_east) &&
		(cars[PLAYER_CAR_INDEX].south == tile->south ||
		 cars[PLAYER_CAR_INDEX].south == tile->last_south)) {
		frame_add_car(&frame_state->playerstate, PLAYER_CAR_INDEX, FRAME_PLAYER_SORT_ID,
					  &game3dshapes[PLAYER_CAR_WHEEL_SHAPE], player_wheel_vertex_state,
					  player_base_wheel_vertices, player_front_wheel_centers,
					  &frame_player_car_rect, &cars[PLAYER_CAR_INDEX].crash_rect, &camera->position,
					  tile->detail, redraw_transform_flags, gameconfig.game_playermaterial,
					  cars[PLAYER_CAR_INDEX].depth_adjustment & tile->depth_mask);
	}

	if ((cars[OPPONENT_CAR_INDEX].east == tile->east) ||
		(cars[OPPONENT_CAR_INDEX].east == tile->last_east)) {
		if ((cars[OPPONENT_CAR_INDEX].south == tile->south) ||
			(cars[OPPONENT_CAR_INDEX].south == tile->last_south)) {
			const struct CARSTATE *second_car = frame_second_car_state();
			if (second_car == 0) {
				return;
			}
			legacy_s8 ghost_flag = gameconfig.game_opponenttype == 0 ? SHAPE3D_GHOST_FLAG : 0;
			frame_add_car(second_car, OPPONENT_CAR_INDEX, FRAME_OPPONENT_SORT_ID,
						  &game3dshapes[OPPONENT_CAR_WHEEL_SHAPE], opponent_wheel_vertex_state,
						  opponent_base_wheel_vertices, opponent_front_wheel_centers,
						  &frame_opponent_car_rect, &cars[OPPONENT_CAR_INDEX].crash_rect,
						  &camera->position, tile->detail, redraw_transform_flags | ghost_flag,
						  ghost_flag != 0 ? ghost_car_material() : gameconfig.game_opponentmaterial,
						  cars[OPPONENT_CAR_INDEX].depth_adjustment & tile->depth_mask);
		}
	}
}

legacy_s16 frame_prepare_draw_tile(struct FRAME_TILE *tile,
								   const struct FRAME_TILE_SELECTION *tiles, legacy_s16 tile_index)
{
	tile->east = tiles->east[tile_index];
	tile->south = tiles->south[tile_index];
	tile->element = tiles->elements[tile_index];
	tile->terrain = tiles->terrain[tile_index];
	tile->depth_mask = 0;
	return frame_hires_prepare_tile(tile, tiles, tile_index);
}

legacy_s16 frame_draw_tile_background(struct FRAME_TILE *tile,
									  const struct FRAME_TILE_SELECTION *tiles,
									  const struct FRAME_CAMERA *camera,
									  legacy_s8 redraw_transform_flags, legacy_s16 first_pass,
									  legacy_s16 pass)
{
	if (
#if defined(RESTUNTS_SDL3)
		extended_frame_uses_legacy_tiles() &&
#endif
		frame_hires_exhausted(frame_draw_fences(tile, tiles, camera, redraw_transform_flags))) {
		return 1;
	}
#if defined(RESTUNTS_SDL3)
	if (!extended_frame_should_draw_terrain(tile, first_pass, pass)) {
		return 0;
	}
#else
	(void)first_pass;
	(void)pass;
#endif
	return frame_draw_terrain(tile, camera, redraw_transform_flags);
}

legacy_s16 frame_draw_tile_objects(struct FRAME_TILE *tile,
								   const struct FRAME_TILE_SELECTION *tiles,
								   const struct FRAME_CAMERA *camera, struct FRAME_CAR_RENDER *cars,
								   legacy_s8 redraw_transform_flags, legacy_s8 animated_material,
								   legacy_s8 *overlay_needs_depth_sort)
{
#if defined(RESTUNTS_SDL3)
	/* Covered extended cells remain selected; submit their boundary once,
	 * after the terrain that supports it. */
	if (extended_frame_front_fences(tile, tiles, camera, redraw_transform_flags)) {
		return 1;
	}
#else
	(void)tiles;
#endif
	transformedshape_counter = 0;
	curtransshape_ptr = currenttransshape;
	if (frame_add_track_element(tile, camera, cars, redraw_transform_flags, animated_material,
								overlay_needs_depth_sort) != 0) {
		return 1;
	}
	frame_add_tile_cars(tile, camera, cars, redraw_transform_flags);
	frame_add_start_flag(tile, camera, redraw_transform_flags);
	return frame_hires_exhausted(
		frame_draw_sorted_shapes(cars, tile, camera, redraw_transform_flags));
}
