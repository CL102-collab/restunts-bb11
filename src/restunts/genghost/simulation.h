#ifndef RESTUNTS_GENGHOST_SIMULATION_H
#define RESTUNTS_GENGHOST_SIMULATION_H

#include "../c/gamestate.h"
#include "../c/replay.h"
#include "../c/residue.h"
#include "../c/track_collision.h"
#include "../c/extended_replay_runtime.h"

#define GENGHOST_LEGACY_MAX_FRAMES 12000U
#define GENGHOST_MAX_FRAMES 120000U
#define GENGHOST_FRAME_RATE 20U

/* The engine is process-global. Snapshots belong to the one initialized track,
 * car and rule set; workers must use separate processes, not concurrent ticks.
 * GAMESTATE alone omits random and legacy stack state that affects collisions. */
struct GENGHOST_SNAPSHOT {
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

struct GENGHOST_RESULT {
	legacy_uint frames;
	legacy_uint finish_frame;
	legacy_uint penalty_frames;
	legacy_uint total_frames;
	legacy_int finished;
	legacy_int crashed;
};

/* Read game resources from cwd, like repldump. Call once per process. Returns
 * zero on success, or a track setup/resource error. Car is exactly four bytes;
 * transmission is zero for manual and one for automatic. No rendering occurs. */
legacy_int genghost_sim_init(const legacy_u8 track[REPLAY_TRACK_SIZE],
							 const legacy_char car[REPLAY_CAR_ID_SIZE], legacy_int transmission,
							 legacy_int argc, legacy_char **argv);
/* Size-aware initialization admits complete legacy or CSTRK tracks. Configure
 * an embedded replay's rate and metadata before initialization. */
legacy_int genghost_sim_init_track(const legacy_u8 *track, legacy_uint track_size,
								   const legacy_char car[REPLAY_CAR_ID_SIZE],
								   legacy_int transmission, legacy_int argc, legacy_char **argv);
void genghost_sim_set_frame_rate(legacy_u16 rate);
void genghost_sim_set_replay_metadata(const struct CSRPL_METADATA *metadata);
legacy_uint genghost_sim_frame(void);
legacy_uint genghost_sim_frame_limit(void);
legacy_u16 genghost_sim_frame_rate(void);
void genghost_sim_capture(struct GENGHOST_SNAPSHOT *snapshot);
void genghost_sim_restore(const struct GENGHOST_SNAPSHOT *snapshot);
void genghost_sim_reset(void);
/* Consume one original input tick at the configured frame rate. Returns the
 * game's crash/finish event,
 * or -1 at the replay length limit. Read engine `state` for route/speed data. */
legacy_int genghost_sim_step(legacy_u8 input);
void genghost_sim_result(struct GENGHOST_RESULT *result);
/* Always replays from the canonical initial state; stops on a crash, but retains
 * any post-finish input frames. Returns zero for valid input length. */
legacy_int genghost_sim_evaluate(const legacy_u8 *inputs, legacy_uint length,
								 struct GENGHOST_RESULT *result);
/* Validate the source lap in its initialized transmission and materialize native
 * automatic shifts as manual input buttons. On success, inputs and the canonical
 * simulation/configuration are manual, with the original finish and score. */
legacy_int genghost_sim_manual_inputs(legacy_u8 *inputs, legacy_uint length,
									  struct GENGHOST_RESULT *baseline);
/* Save a legacy RPL or a CSRPL with the complete extended track and rules. Caller must validate the
 * complete input sequence first. Output errors return nonzero. */
legacy_int genghost_sim_write_replay(const legacy_char *path, const legacy_char *track_name,
									 const legacy_u8 *inputs, legacy_uint length);

#endif
