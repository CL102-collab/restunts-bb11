#include "extended_physics_snapshot.h"

#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
#include <string.h>
#include "extended_track.h"
#include "extended_geometry.h"
#include "../externs.h"
#include "../physics/physics_internal.h"
#include "../wheel_transform.h"
#include "../race/race_stats.h"

#define EXTENDED_SNAPSHOT_POSITION_SCALE 64

extern struct MATRIX wheel_heading_rotation;
extern struct MATRIX plane_heading_rotation;
extern legacy_s16 cached_plane_heading;
extern legacy_s16 cached_wheel_heading;

void extended_physics_capture(struct EXTENDED_PHYSICS_SNAPSHOT *saved)
{
	/* Zero structure padding as well: deterministic byte comparisons are useful
	 * when auditing restart and branching without serializing engine pointers. */
	memset(saved, 0, sizeof(*saved));
	saved->state = state;
	saved->residue = legacy_execution_residue;
	saved->clocks = extended_replay_clocks;
	if (extended_track_active()) {
		extended_geometry_get_context(&saved->context_x, &saved->context_z);
	}
	track_collision_capture(&saved->collision);
	get_kevinrandom_seed(saved->random_seed);
	saved->position = (struct VECTORLONG){car_working_x, car_working_y, car_working_z};
	saved->rotation = (struct VECTOR){car_working_yaw, car_working_pitch, car_working_roll};
	saved->initial_rotation = (struct VECTOR){car_initial_yaw, car_initial_pitch, car_initial_roll};
	saved->forward = wheel_forward_travel;
	saved->world = wheel_world_travel;
	saved->car_rotation = car_to_world_rotation;
	saved->plane_rotation = plane_heading_rotation;
	saved->wheel_rotation = wheel_heading_rotation;
	saved->plane = planindex_copy;
	saved->next_position = nextPosAndNormalIP;
	saved->heading = wheel_heading_offset;
	saved->plane_heading = cached_plane_heading;
	saved->wheel_heading = cached_wheel_heading;
	saved->render_headings = legacy_render_player_headings_active;
	saved->flag_animation = start_flag_animation;
	saved->penalty = penalty_time;
	saved->elapsed1 = elapsed_time1;
	saved->elapsed2 = elapsed_time2;
	saved->closed_hihat = legacy_closed_hihat_offset;
	saved->exit_request = race_exit_request;
	saved->start_sequence = race_start_sequence_state;
	saved->frame_countdown = frame_callback_countdown;
	saved->slow_countdown = slow_replay_countdown;
	saved->penalty_counter = show_penalty_counter;
}

void extended_physics_restore(const struct EXTENDED_PHYSICS_SNAPSHOT *saved)
{
	state = saved->state;
	legacy_execution_residue = saved->residue;
	extended_replay_clocks = saved->clocks;
	if (extended_track_active()) {
		extended_geometry_set_context(saved->context_x * EXTENDED_SNAPSHOT_POSITION_SCALE,
									  saved->context_z * EXTENDED_SNAPSHOT_POSITION_SCALE);
	}
	track_collision_restore(&saved->collision);
	init_kevinrandom(saved->random_seed);
	car_working_x = saved->position.lx;
	car_working_y = saved->position.ly;
	car_working_z = saved->position.lz;
	car_working_yaw = saved->rotation.x;
	car_working_pitch = saved->rotation.y;
	car_working_roll = saved->rotation.z;
	car_initial_yaw = saved->initial_rotation.x;
	car_initial_pitch = saved->initial_rotation.y;
	car_initial_roll = saved->initial_rotation.z;
	wheel_forward_travel = saved->forward;
	wheel_world_travel = saved->world;
	car_to_world_rotation = saved->car_rotation;
	plane_heading_rotation = saved->plane_rotation;
	wheel_heading_rotation = saved->wheel_rotation;
	planindex_copy = saved->plane;
	nextPosAndNormalIP = saved->next_position;
	wheel_heading_offset = saved->heading;
	cached_plane_heading = saved->plane_heading;
	cached_wheel_heading = saved->wheel_heading;
	legacy_render_player_headings_active = saved->render_headings;
	start_flag_animation = saved->flag_animation;
	penalty_time = saved->penalty;
	elapsed_time1 = saved->elapsed1;
	elapsed_time2 = saved->elapsed2;
	legacy_closed_hihat_offset = saved->closed_hihat;
	race_exit_request = saved->exit_request;
	race_start_sequence_state = saved->start_sequence;
	frame_callback_countdown = saved->frame_countdown;
	slow_replay_countdown = saved->slow_countdown;
	show_penalty_counter = saved->penalty_counter;
}

#endif
