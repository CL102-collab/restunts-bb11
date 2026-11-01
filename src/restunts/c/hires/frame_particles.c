#include "../frame/frame_render_private.h"

#if defined(RESTUNTS_SDL3)
legacy_s16 frame_hires_draw_adaptive_particles(const struct FRAME_CAMERA *camera,
											   struct FRAME_CAR_RENDER *cars,
											   legacy_s8 redraw_transform_flags)
{
	if (frame_hires_adaptive_active == 0 || frame_state->game_particles_active == 0) {
		return 0;
	}
	/* Debris can cross the mask boundary independently of its hidden owner.
	 * Gather the active slots once; their complete group fits the existing
	 * transformed-shape array and uses the normal depth-sorted submission. */
	struct VECTOR car_offsets[GAME_CAR_COUNT];
	legacy_u8 offset_ready[GAME_CAR_COUNT] = {0};
	transformedshape_counter = 0;
	curtransshape_ptr = currenttransshape;
	for (legacy_s16 index = 0; index < FRAME_DEBRIS_SLOT_COUNT; index++) {
		if (frame_state->game_particle_forward_speed[index] == 0) {
			continue;
		}
		legacy_s16 owner = frame_state->game_particle_owner[index];
		legacy_s16 flags = redraw_transform_flags | FRAME_TRANSFORM_FLAGS_NO_DEPTH_SORT;
		legacy_s16 material = 0;
		if (owner == PLAYER_CAR_INDEX || owner == OPPONENT_CAR_INDEX) {
			/* Ghosts have no physical debris and do not use opponent particles. */
			if (owner == OPPONENT_CAR_INDEX && gameconfig.game_opponenttype == 0) {
				continue;
			}
			const struct CARSTATE *carstate =
				owner == PLAYER_CAR_INDEX ? &frame_state->playerstate : &frame_state->opponentstate;
			curtransshape_ptr->pos.x = frame_relative_position_sum(
				frame_state->game_particle_x[index], carstate->car_position.lx, camera->position.x);
			curtransshape_ptr->pos.y = frame_relative_position_sum(
				frame_state->game_particle_y[index], carstate->car_position.ly, camera->position.y);
			curtransshape_ptr->pos.z = frame_relative_position_sum(
				frame_state->game_particle_z[index], carstate->car_position.lz, camera->position.z);
			if (offset_ready[owner] == 0) {
				const struct SHAPE3D *wheel_shape =
					&game3dshapes[owner == PLAYER_CAR_INDEX ? PLAYER_CAR_WHEEL_SHAPE
															: OPPONENT_CAR_WHEEL_SHAPE];
				car_offsets[owner] = frame_hires_car_render_offset(carstate, wheel_shape);
				offset_ready[owner] = 1;
			}
			frame_hires_offset_car_shape(&car_offsets[owner]);
			flags |= SHAPE3D_NO_SHADOW_RECEIVE_FLAG;
			material = owner == PLAYER_CAR_INDEX ? gameconfig.game_playermaterial
												 : gameconfig.game_opponentmaterial;
		} else {
			legacy_s16 sign = owner - FRAME_CHECKPOINT_OWNER_OFFSET;
			if (sign < 0 || sign >= (legacy_s16)GAMESTATE_BREAKABLE_OBJECT_COUNT ||
				frame_state->game_object_destroyed[sign] == 0) {
				continue;
			}
			curtransshape_ptr->pos.x =
				frame_relative_track_position(frame_state->game_particle_x[index],
											  roadside_sign_positions[sign].x, camera->position.x);
			curtransshape_ptr->pos.y =
				frame_relative_track_position(frame_state->game_particle_y[index],
											  roadside_sign_positions[sign].y, camera->position.y);
			curtransshape_ptr->pos.z =
				frame_relative_track_position(frame_state->game_particle_z[index],
											  roadside_sign_positions[sign].z, camera->position.z);
		}
		struct TRACKOBJECT *object =
			&particle_scene_objects[frame_state->game_particle_shape_index[index]];
		frame_add_dynamic_shape(object, index, flags, material, 0, &camera->position);
	}
	return frame_draw_sorted_shapes(cars, 0, 0, 0);
}
#endif
