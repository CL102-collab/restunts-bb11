#ifndef RESTUNTS_EXTENDED_PHYSICS_SNAPSHOT_H
#define RESTUNTS_EXTENDED_PHYSICS_SNAPSHOT_H

#include "../gamestate.h"
#include "../residue.h"
#include "../track/track_collision.h"
#include "extended_replay_runtime.h"

/* Full mutable physics context for the current initialized track and car set.
 * Capture, simulate and restore only on the game thread, between render jobs.
 * This does not own replay buffers or presentation/audio state; preview callers
 * must suppress those side effects and restore their separate UI envelope. */
struct EXTENDED_PHYSICS_SNAPSHOT {
	struct GAMESTATE state;
	struct LEGACY_EXECUTION_RESIDUE residue;
	struct EXTENDED_REPLAY_CLOCKS clocks;
	legacy_s32 context_x, context_z;
	struct TRACK_COLLISION_SNAPSHOT collision;
	struct VECTORLONG position;
	struct VECTOR rotation, initial_rotation, forward, world;
	struct MATRIX car_rotation, plane_rotation, wheel_rotation;
	legacy_s8 random_seed[GAMESTATE_RANDOM_SEED_SIZE];
	legacy_s16 plane, next_position, heading, plane_heading, wheel_heading;
	legacy_s16 render_headings, flag_animation, penalty;
	legacy_u16 elapsed1, elapsed2, closed_hihat;
	legacy_u8 exit_request, start_sequence, frame_countdown, slow_countdown;
	legacy_s8 penalty_counter;
};

void extended_physics_capture(struct EXTENDED_PHYSICS_SNAPSHOT *snapshot);
void extended_physics_restore(const struct EXTENDED_PHYSICS_SNAPSHOT *snapshot);

#endif
