#ifndef RESTUNTS_GENGHOST_SIMULATION_H
#define RESTUNTS_GENGHOST_SIMULATION_H

#include "../c/gamestate.h"
#include "../c/replay.h"
#include "../c/residue.h"
#include "../c/track_collision.h"

#define GENGHOST_MAX_FRAMES 12000U
#define GENGHOST_FRAME_RATE 20U

/* The engine is process-global. Snapshots belong to the one initialized track,
 * car and rule set; workers must use separate processes, not concurrent ticks.
 * GAMESTATE alone omits random and legacy stack state that affects collisions. */
struct GENGHOST_SNAPSHOT {
	struct GAMESTATE state;
	struct LEGACY_EXECUTION_RESIDUE residue;
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

struct GENGHOST_RESULT {
	unsigned int frames;
	unsigned int finish_frame;
	unsigned int penalty_frames;
	unsigned int total_frames;
	int finished;
	int crashed;
};

/* Read game resources from cwd, like repldump. Call once per process. Returns
 * zero on success, or a track setup/resource error. Car is exactly four bytes;
 * transmission is zero for manual and one for automatic. No rendering occurs. */
int genghost_sim_init(const unsigned char track[REPLAY_TRACK_SIZE], const char car[4],
					  int transmission, int argc, char **argv);
void genghost_sim_capture(struct GENGHOST_SNAPSHOT *snapshot);
void genghost_sim_restore(const struct GENGHOST_SNAPSHOT *snapshot);
void genghost_sim_reset(void);
/* Consume one original 20 Hz input tick. Returns the game's crash/finish event,
 * or -1 at the replay length limit. Read engine `state` for route/speed data. */
int genghost_sim_step(unsigned char input);
void genghost_sim_result(struct GENGHOST_RESULT *result);
/* Always replays from the canonical initial state; stops on a crash, but retains
 * any post-finish input frames. Returns zero for valid input length. */
int genghost_sim_evaluate(const unsigned char *inputs, unsigned int length,
						  struct GENGHOST_RESULT *result);
/* Validate the source lap in its initialized transmission and materialize native
 * automatic shifts as manual input buttons. On success, inputs and the canonical
 * simulation/configuration are manual, with the original finish and score. */
int genghost_sim_manual_inputs(unsigned char *inputs, unsigned int length,
							   struct GENGHOST_RESULT *baseline);
/* Save original track bytes and standard v1.1 header. Caller must validate the
 * complete input sequence first. Output errors return nonzero. */
int genghost_sim_write_replay(const char *path, const char *track_name, const unsigned char *inputs,
							  unsigned int length);

#endif
