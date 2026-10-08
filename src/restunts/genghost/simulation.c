#include <stdio.h>
#include <string.h>

#include "simulation.h"
#include "../c/externs.h"
#include "../c/restunts.h"
#include "../c/camera.h"
#include "../c/track_objects.h"
#include "../c/memmgr.h"
#include "../c/race_resources.h"
#include "../c/race_stats.h"
#include "../c/physics_internal.h"
#include "../c/wheel_transform.h"
#include "../c/crash_state.h"
#include "../c/trackdata_layout.h"

#undef memcpy
#undef strlen

#if !defined(RESTUNTS_SDL3) || !defined(RESTUNTS_HEADLESS)
#error genghost requires the SDL3 headless engine
#endif

extern struct MATRIX wheel_heading_rotation;
extern struct MATRIX plane_heading_rotation;
extern legacy_s16 cached_plane_heading;
extern legacy_s16 cached_wheel_heading;

static struct GENGHOST_SNAPSHOT initial_snapshot;
static unsigned char original_track[REPLAY_TRACK_SIZE];
static int initialized;

void genghost_sim_capture(struct GENGHOST_SNAPSHOT *saved)
{
	/* Zero structure padding as well: deterministic byte comparisons are useful
	 * when auditing restart and branching without serializing engine pointers. */
	memset(saved, 0, sizeof(*saved));
	saved->state = state;
	saved->residue = legacy_execution_residue;
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

void genghost_sim_restore(const struct GENGHOST_SNAPSHOT *saved)
{
	state = saved->state;
	legacy_execution_residue = saved->residue;
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

int genghost_sim_init(const unsigned char track[REPLAY_TRACK_SIZE], const char car[4],
					  int transmission, int argc, char **argv)
{
	if (initialized || track == NULL || car == NULL || argc < 0 || argc > (int)LEGACY_S16_MAX ||
		(transmission != 0 && transmission != 1)) {
		return -1;
	}
	/* Match repldump's allocation and initialization order. In particular,
	 * legacy penalty aliases depend on resource allocation before car setup. */
	init_main((legacy_s16)argc, (legacy_s8 **)argv);
	static const legacy_s8 state_chunk_name[12] = {'g', 'a', 'm', 'e', 's', 't',
												   'a', 't', 'e', 0,   0,	0};
	(void)mmgr_alloc_resbytes(state_chunk_name, GAMESTATE_SERIALIZED_SIZE);
	init_div0();
	init_row_tables();
	init_trackdata();
	reset_race_loop_state();
	init_kevinrandom((const legacy_s8 *)"kevin");

	memcpy(original_track, track, REPLAY_TRACK_SIZE);
	memcpy(track_element_map, track, REPLAY_TRACK_SIZE);
	memset(&gameconfig, 0, sizeof(gameconfig));
	memcpy(gameconfig.game_playercarid, car, REPLAY_CAR_ID_SIZE);
	gameconfig.game_playertransmission = (legacy_s8)transmission;
	gameconfig.game_opponentcarid[0] = -1;
	gameconfig.game_framespersec = GENGHOST_FRAME_RATE;
	gameconfigcopy = gameconfig;
	memcpy(track_and_directory_backup, track_element_map, TRACKDATA_LINK_TABLE_SIZE);
	memcpy(track_and_directory_backup + TRACKDATA_LINK_TABLE_SIZE, track_directory,
		   TRACKDATA_CHECKPOINT_DATA_SIZE);
	memcpy(track_and_directory_backup + TRACKDATA_CHECKPOINT_SECOND_OFFSET, replay_directory,
		   TRACKDATA_CHECKPOINT_DATA_SIZE);

	int setup_result = track_setup();
	if (setup_result != 0) {
		fprintf(stderr, "Cannot simulate track: track setup error %d.\n", setup_result);
		return setup_result;
	}
	cvxptr = mmgr_alloc_resbytes((const legacy_s8 *)"cvx",
								 sizeof(struct GAMESTATE) * GAMESTATE_CHECKPOINT_COUNT);
	init_game_state(-1);
	viewport_bottom_cache = -1;
	run_game_random = LEGACY_S16_SHL(get_kevinrandom(), 3);
	replaybar_toggle = 1;
	is_in_replay = 1;
	idle_expired = 0;
	cameramode = 0;
	game_replay_mode = REPLAY_MODE_PLAYBACK;
	if (setup_player_cars_without_dashboard() != 0) {
		return -1;
	}
	kbormouse = 0;
	replay_playback_speed = REPLAY_PLAYBACK_NORMAL;
	race_exit_request = 1;
	game_replay_mode_copy = LEGACY_U8_MAX;
	frame_buffer_index = 0;
	dashboard_buffer_index = 0;
	recording_limit_warning_requested = 0;
	dashb_toggle = 0;
	start_flag_animation = 500;
	framespersec = GENGHOST_FRAME_RATE;
	restore_gamestate(0);
	memset(replay_input_buffer, 0, GENGHOST_MAX_FRAMES);
	genghost_sim_capture(&initial_snapshot);
	initialized = 1;
	return 0;
}

void genghost_sim_reset(void)
{
	genghost_sim_restore(&initial_snapshot);
}

int genghost_sim_step(unsigned char input)
{
	if (!initialized || (unsigned int)(legacy_u16)state.game_frame >= GENGHOST_MAX_FRAMES) {
		return -1;
	}
	replay_input_buffer[(legacy_u16)state.game_frame] = (legacy_s8)(input & 63U);
	/* Silent keeps the original full tick, penalty checks, collision bugs and
	 * OWOOT enforcement. It only suppresses audio and checkpoint writes. */
	update_gamestate_silent();
	return state.playerstate.car_crashBmpFlag;
}

void genghost_sim_result(struct GENGHOST_RESULT *result)
{
	result->frames = (legacy_u16)state.game_frame;
	result->finish_frame = (legacy_u16)state.game_pEndFrame;
	result->total_frames = (legacy_u16)state.game_total_finish;
	result->finished = state.playerstate.car_crashBmpFlag == CRASH_EVENT_FINISH;
	/* Route tracking continues while a finished car brakes through the replay
	 * tail. Only the penalty latched into the native finish time counts. */
	result->penalty_frames =
		result->finished ? (legacy_u16)(result->total_frames - result->finish_frame - elapsed_time1)
						 : (legacy_u16)state.game_penalty;
	result->crashed = state.playerstate.car_crashBmpFlag != CRASH_EVENT_NONE && !result->finished;
}

int genghost_sim_evaluate(const unsigned char *inputs, unsigned int length,
						  struct GENGHOST_RESULT *result)
{
	if (!initialized || inputs == NULL || result == NULL || length > GENGHOST_MAX_FRAMES) {
		return -1;
	}
	genghost_sim_reset();
	for (unsigned int frame = 0; frame < length; frame++) {
		int event = genghost_sim_step(inputs[frame]);
		if (event != CRASH_EVENT_NONE && event != CRASH_EVENT_FINISH) {
			break;
		}
	}
	genghost_sim_result(result);
	return 0;
}

int genghost_sim_write_replay(const char *path, const char *track_name, const unsigned char *inputs,
							  unsigned int length)
{
	if (!initialized || path == NULL || track_name == NULL || inputs == NULL ||
		length > GENGHOST_MAX_FRAMES) {
		return -1;
	}
	struct GAMEINFO config = gameconfig;
	memset(config.game_trackname, 0, sizeof(config.game_trackname));
	size_t name_length = strlen(track_name);
	if (name_length >= sizeof(config.game_trackname)) {
		name_length = sizeof(config.game_trackname) - 1U;
	}
	memcpy(config.game_trackname, track_name, name_length);
	config.game_recordedframes = (legacy_u16)length;
	unsigned char header[REPLAY_GAMEINFO_SIZE];
	replay_gameinfo_encode(header, &config);
	FILE *output = fopen(path, "wb");
	if (output == NULL) {
		perror(path);
		return -1;
	}
	int failed =
		fwrite(header, 1, sizeof(header), output) != sizeof(header) ||
		fwrite(original_track, 1, sizeof(original_track), output) != sizeof(original_track) ||
		fwrite(inputs, 1, length, output) != length;
	if (fclose(output) != 0) {
		failed = 1;
	}
	if (failed) {
		fprintf(stderr, "Cannot write complete replay: %s\n", path);
		return -1;
	}
	return 0;
}
