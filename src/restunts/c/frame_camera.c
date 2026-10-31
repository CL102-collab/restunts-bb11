#include "frame_render_private.h"

/* The camera looks out of the car, so it uses the car's rotation inverted. */
struct MATRIX *frame_car_rotation(legacy_s16 rot_x, legacy_s16 rot_y, legacy_s16 rot_z)
{
	return mat_rot_zxy(LEGACY_S16_WRAP_NEGATE(rot_z), LEGACY_S16_WRAP_NEGATE(rot_y),
					   LEGACY_S16_WRAP_NEGATE(rot_x), MATRIX_ROTATION_ORDER_ZXY);
}

/* Ghosts share the opponent drawing and camera slot without adding a
 * simulated car, collision body or sound source. */
const struct CARSTATE *frame_second_car_state(void)
{
	return gameconfig.game_opponenttype != 0
			   ? &frame_state->opponentstate
			   : (frame_uses_snapshot != 0 ? frame_ghost : ghost_car_state());
}

const struct CARSTATE *frame_viewed_car_state(void)
{
	const struct CARSTATE *second_car = followOpponentFlag != 0 ? frame_second_car_state() : 0;
	return second_car != 0 ? second_car : &frame_state->playerstate;
}

const struct GHOST_CAMERA_STATE *frame_viewed_ghost_camera(void)
{
	return followOpponentFlag != 0 && gameconfig.game_opponenttype == 0
			   ? (frame_uses_snapshot != 0 ? frame_ghost_camera : ghost_camera_state())
			   : 0;
}

legacy_s16 frame_position_camera(struct FRAME_CAMERA *camera, const struct VECTOR *car_pos,
								 legacy_s16 car_rot_x, legacy_s16 car_rot_y, legacy_s16 car_rot_z,
								 const struct GHOST_CAMERA_STATE *ghost_camera)
{
	// Set camera position, based on the car position and the camera mode
	struct MATRIX *car_rot_matrix;
	struct VECTOR car_to_cam_rotated;
	legacy_s16 camera_roll = 0;
	struct VECTOR offset_vector;
	legacy_u8 car_index =
		followOpponentFlag != 0 && (gameconfig.game_opponenttype != 0 || ghost_camera != 0)
			? OPPONENT_CAR_INDEX
			: PLAYER_CAR_INDEX;
	if (cameramode == CAMERA_MODE_COCKPIT) {
		camera->yaw = car_rot_x & ANGLE_MASK;
		camera->pitch = car_rot_y & ANGLE_MASK;
		camera_roll = car_rot_z & ANGLE_MASK;
		car_rot_matrix = frame_car_rotation(car_rot_x, car_rot_y, car_rot_z);
		offset_vector.x = 0;
		offset_vector.z = 0;
		offset_vector.y = LEGACY_S16_WRAP_SUB(ghost_camera != 0 ? ghost_car_simd()->car_height
																: simd_player.car_height,
											  FRAME_COCKPIT_HEIGHT_CLEARANCE);

		mat_mul_vector(&offset_vector, car_rot_matrix, &car_to_cam_rotated);
		camera->position.x = LEGACY_S16_WRAP_ADD(car_pos->x, car_to_cam_rotated.x);
		camera->position.y = LEGACY_S16_WRAP_ADD(car_pos->y, car_to_cam_rotated.y);
		camera->position.z = LEGACY_S16_WRAP_ADD(car_pos->z, car_to_cam_rotated.z);
	} else if (cameramode == CAMERA_MODE_FOLLOW) {
		camera->position = ghost_camera != 0 ? ghost_camera->follow_position
											 : frame_state->game_follow_camera_position[car_index];
	} else if (cameramode == CAMERA_MODE_CUSTOM) {
		offset_vector.x = 0;
		offset_vector.y = 0;
		offset_vector.z = FRAME_CAMERA_DIRECTION_LENGTH;
		car_rot_matrix = frame_car_rotation(car_rot_x, car_rot_y, car_rot_z);
		mat_mul_vector(&offset_vector, car_rot_matrix, &car_to_cam_rotated);

		offset_vector.x = 0;
		offset_vector.y = 0;
		offset_vector.z = custom_camera.distance;
		car_rot_matrix =
			mat_rot_zxy(0, LEGACY_S16_WRAP_NEGATE(custom_camera.elevation_angle),
						LEGACY_S16_WRAP_SUB(polarAngle(car_to_cam_rotated.x, car_to_cam_rotated.z),
											custom_camera.azimuth_angle),
						MATRIX_ROTATION_ORDER_ZXY);

		mat_mul_vector(&offset_vector, car_rot_matrix, &car_to_cam_rotated);
		camera->position.x = LEGACY_S16_WRAP_ADD(car_pos->x, car_to_cam_rotated.x);
		camera->position.y = LEGACY_S16_WRAP_ADD(car_pos->y, car_to_cam_rotated.y);
		camera->position.z = LEGACY_S16_WRAP_ADD(car_pos->z, car_to_cam_rotated.z);
	} else if (cameramode == CAMERA_MODE_TRACKSIDE) {
		legacy_s16 track_index = ghost_camera != 0
									 ? ghost_camera->trackside_index
									 : frame_state->game_trackside_camera_index[car_index];
		camera->position.x = trackside_camera_positions[track_index].x;
		camera->position.y =
			LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_ADD(trackside_camera_positions[track_index].y,
													camera_track_height_offset),
								FRAME_TRACK_CAMERA_HEIGHT_OFFSET);
		camera->position.z = trackside_camera_positions[track_index].z;
	}

	return camera_roll;
}

void frame_aim_external_camera(struct FRAME_CAMERA *camera, const struct VECTOR *car_pos)
{
	build_track_object(&camera->position, &camera->position);
	if (camera->position.y < terrainHeight) {
		camera->position.y = terrainHeight;
	}

	if (track_wall_collision_enabled != 0) {
		legacy_s16 plane_distance = plane_signed_distance(planindex, camera->position.x,
														  camera->position.y, camera->position.z);
		if (plane_distance < FRAME_PLANE_CLEARANCE) {
			wheel_forward_travel.x = 0;
			wheel_forward_travel.y = LEGACY_S16_WRAP_SUB(FRAME_PLANE_CLEARANCE, plane_distance);
			wheel_forward_travel.z = 0;
			planindex_copy = planindex;
			wheel_heading_offset = 0;
			car_initial_pitch = 0;
			car_initial_roll = 0;
			car_initial_yaw = 0;
			transform_wheel_travel_to_world();
			camera->position.x = LEGACY_S16_WRAP_ADD(camera->position.x, wheel_world_travel.x);
			camera->position.y = LEGACY_S16_WRAP_ADD(camera->position.y, wheel_world_travel.y);
			camera->position.z = LEGACY_S16_WRAP_ADD(camera->position.z, wheel_world_travel.z);
		}
	}

	camera->yaw = LEGACY_S16_FROM_BITS((legacy_u16)LEGACY_S16_WRAP_NEGATE(polarAngle(
										   LEGACY_S16_WRAP_SUB(car_pos->x, camera->position.x),
										   LEGACY_S16_WRAP_SUB(car_pos->z, camera->position.z))) &
									   ANGLE_MASK);
	legacy_s16 camera_horizontal_distance =
		polarRadius2D(LEGACY_S16_WRAP_SUB(car_pos->x, camera->position.x),
					  LEGACY_S16_WRAP_SUB(car_pos->z, camera->position.z));
	camera->pitch = LEGACY_S16_FROM_BITS(
		(legacy_u16)polarAngle(
			LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_SUB(car_pos->y, camera->position.y),
								FRAME_CAMERA_TARGET_HEIGHT_OFFSET),
			camera_horizontal_distance) &
		ANGLE_MASK);
}

void frame_setup_camera(struct FRAME_CAMERA *camera)
{
	const struct CARSTATE *viewed_car = frame_viewed_car_state();
	const struct GHOST_CAMERA_STATE *ghost_camera = frame_viewed_ghost_camera();
	struct VECTOR car_pos;
	car_pos.x = position_to_word(viewed_car->car_position.lx);
	car_pos.y = position_to_word(viewed_car->car_position.ly);
	car_pos.z = position_to_word(viewed_car->car_position.lz);
	camera->yaw = -1;
	legacy_s16 camera_roll =
		frame_position_camera(camera, &car_pos, viewed_car->car_rotate.x, viewed_car->car_rotate.y,
							  viewed_car->car_rotate.z, ghost_camera);

	// Keep external cameras above the track and aim them at the followed car.
	if (camera->yaw == -1) {
		frame_aim_external_camera(camera, &car_pos);
	}

	if (camera_roll > 1 && (legacy_u16)camera_roll < ANGLE_MASK) {
		camera->roll = camera_roll;
	} else {
		camera->roll = 0;
	}
}

const struct FRAME_LOOKAHEAD_TILE *frame_setup_projection(struct FRAME_CAMERA *camera,
														  struct RECTANGLE *cliprect)
{
	// Select the vector specifying the 23 tiles to draw. The vector contains
	// 24 elements, each 3 bytes long, in format (east_offset, south_offset,
	// detail threshold). A tile is drawn only if its detail threshold is lower
	// enough (0 = draw always, 1 = only if graphic detail is MEDIUM or FULL,
	// 2 = only if graphic detail is FULL).
	// There are 8 possible vectors, but they are all rotations/reflections of a
	// basic schema. Which is chosen depends on the heading of the car. For a
	// car heading north ($), the schema is the following:
	//
	// OOOOO
	// OOOOO
	// OOOOO
	// OOOOO
	//  O$O
	//
	// Also, note that the tiles appear in the vector in drawing order
	// (farthest tiles first). If a car is heading north but slightly west, the
	// algo will draw the NW tile before the NE, and vice-versa

	legacy_s16 heading =
		select_cliprect_rotate(camera->roll, camera->pitch, camera->yaw, cliprect, 0);
	camera->view_heading = heading;
	const struct FRAME_LOOKAHEAD_TILE *lookahead_tiles = (const struct FRAME_LOOKAHEAD_TILE *)
		lookahead_tiles_tables[(heading & ANGLE_MASK) >> FRAME_LOOKAHEAD_HEADING_SHIFT];

	camera->pitch_roll_rotation =
		*mat_rot_zxy(camera->roll, camera->pitch, 0, MATRIX_ROTATION_ORDER_YXZ);
	struct VECTOR offset_vector;
	offset_vector.x = 0;
	offset_vector.y = 0;
	offset_vector.z = FRAME_SKYBOX_TEST_DISTANCE;
	struct VECTOR shape_relative_position;
	mat_mul_vector(&offset_vector, &camera->pitch_roll_rotation, &shape_relative_position);
	if (shape_relative_position.z > 0) {
		camera->skybox_parameter = 1;
	} else {
		camera->skybox_parameter = -1;
	}

	return lookahead_tiles;
}

/* Extended scenery can extend beyond the original finite background shell.
 * Keep the horizon and clouds at infinity while world geometry retains the
 * real camera height. Legacy tracks preserve their original altitude shift. */
legacy_s16 frame_background_camera_height(const struct FRAME_CAMERA *camera)
{
#ifdef RESTUNTS_SDL3
	return extended_frame_background_height(camera->position.y);
#else
	return camera->position.y;
#endif
}

void frame_draw_clouds(struct FRAME_CAMERA *camera, legacy_s8 redraw_transform_flags)
{
	// Draw the eight cloud shapes at full detail.
	struct VECTOR rotated_position;
	struct MATRIX cloud_heading_rotation;
	struct VECTOR offset_vector;
	if (detail_level == FRAME_DETAIL_FULL) {
		currenttransshape->rectptr = &frame_cloud_rect;
		currenttransshape->ts_flags = redraw_transform_flags | FRAME_DISTANT_SHAPE_FLAGS;
		currenttransshape->rotvec.x = 0;
		currenttransshape->rotvec.y = 0;
		currenttransshape->culling_distance = FRAME_DEFAULT_TRANSFORM_DISTANCE;
		currenttransshape->material = 0;

		for (legacy_s16 cloud_index = 0; cloud_index < FRAME_DISTANT_SHAPE_COUNT; cloud_index++) {
			legacy_s16 cloud_angle = LEGACY_S16_FROM_BITS(
				(legacy_u16)LEGACY_S16_WRAP_ADD(
					LEGACY_S16_WRAP_ADD(cloud_heading_offsets[cloud_index], camera->yaw),
					run_game_random) &
				ANGLE_MASK);
			if (cloud_angle < FRAME_DISTANT_SHAPE_MIN_ANGLE ||
				cloud_angle > FRAME_DISTANT_SHAPE_MAX_ANGLE) {
				mat_rot_y(&cloud_heading_rotation, cloud_angle);
				offset_vector.x = 0;
				offset_vector.y = LEGACY_S16_WRAP_SUB(FRAME_DISTANT_SHAPE_HEIGHT,
													  frame_background_camera_height(camera));
				offset_vector.z = FRAME_DISTANT_SHAPE_DISTANCE;
				mat_mul_vector(&offset_vector, &cloud_heading_rotation, &rotated_position);
				rotated_position.z = FRAME_DISTANT_SHAPE_DISTANCE;
				mat_mul_vector(&rotated_position, &camera->pitch_roll_rotation,
							   &currenttransshape->pos);
				if (currenttransshape->pos.z > FRAME_DISTANT_SHAPE_MIN_DEPTH) {
					currenttransshape->shapeptr = cloud_shapes[cloud_index];
					currenttransshape->rotvec.z = LEGACY_S16_WRAP_NEGATE(camera->yaw);
					legacy_s16 transform_result =
						shape3d_transform_and_queue(&currenttransshape[0]);
					// we cannot be out of memory as we are just starting to process
					(void)transform_result;
				}
			}
		}
	}
}
