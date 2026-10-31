#ifndef RESTUNTS_EXTENDED_STATEPLY_H
#define RESTUNTS_EXTENDED_STATEPLY_H

#ifdef RESTUNTS_SDL3
#include "gamestate.h"

#define EXTENDED_PLAYER_SUSPENSION_WORDS 4U

struct EXTENDED_PLAYER_SUSPENSION_STATE {
	legacy_s16 words[EXTENDED_PLAYER_SUSPENSION_WORDS];
	legacy_u8 retain;
};

/* Native player updates delegate format-specific contact and roll behavior. */
void extended_player_begin(struct CARSTATE *carstate, const struct GAMESTATE *motion_state,
						   legacy_u32 motion_fraction);
void extended_player_finish(struct CARSTATE *carstate, const struct GAMESTATE *motion_state,
							legacy_u32 motion_fraction);
void extended_player_clear_motion(struct CARSTATE *carstate);
void extended_player_clear_crashed_motion(struct CARSTATE *carstate);
void extended_player_capture_suspension(const struct CARSTATE *carstate,
										struct EXTENDED_PLAYER_SUSPENSION_STATE *saved);
void extended_player_restore_suspension(struct CARSTATE *carstate,
										const struct EXTENDED_PLAYER_SUSPENSION_STATE *saved);
legacy_s16 extended_player_measure_plane(const struct VECTORLONG *position, legacy_s16 input_mode);
void extended_player_contact_distance(const struct VECTORLONG *position, legacy_s16 input_mode,
									  legacy_s16 *distance);
legacy_s16 extended_player_wheel_gravity(const struct CARSTATE *carstate, legacy_s16 gravity);
legacy_s16 extended_player_use_ground_plane(const struct VECTORLONG *position);
legacy_s16 extended_player_correct_wheel_contact(struct VECTORLONG *current,
												 const struct VECTORLONG *previous,
												 legacy_s16 input_mode);
void extended_player_finish_wheel_contact(struct VECTORLONG *position, legacy_s16 input_mode,
										  legacy_s16 resolved);
legacy_s16 extended_player_clamp_world_pose(legacy_s32 *x, legacy_s32 *z);
legacy_s16 extended_player_body_height_only(legacy_s16 height_only);
legacy_s16 extended_player_other_car_distant(const struct CARSTATE *other_carstate);
void extended_player_start_finish_pole(struct VECTOR *obstacle_pose, legacy_s16 angle,
									   legacy_s16 offset);
void extended_player_scenery_tile(const struct VECTOR *position, legacy_s16 *column,
								  legacy_s16 *row, legacy_s16 *valid);
#endif

#endif
