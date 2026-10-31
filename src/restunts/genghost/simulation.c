#include <stdio.h>
#include <stdlib.h>
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
#include "../c/game_input.h"
#include "../c/extended_track.h"
#include "../c/extended_geometry.h"
#include "../c/driving_rules.h"

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
static legacy_u8 original_track[REPLAY_TRACK_SIZE];
static legacy_int initialized;
static legacy_u16 selected_frame_rate = GENGHOST_FRAME_RATE;
static struct CSRPL_METADATA replay_metadata;
static legacy_int has_replay_metadata;
static legacy_u8 *original_extended_track;
static size_t original_extended_track_size;

#define GENGHOST_ERROR_CAPACITY 256U
#define GENGHOST_WORLD_POSITION_SCALE 64L
#define GENGHOST_INPUT_MASK CSRPL_INPUT_MASK

void genghost_sim_set_frame_rate(legacy_u16 rate)
{
	if (!initialized && (rate == GAME_FRAME_RATE_LOW || rate == GAME_FRAME_RATE_NORMAL)) {
		selected_frame_rate = rate;
	}
}

void genghost_sim_set_replay_metadata(const struct CSRPL_METADATA *metadata)
{
	has_replay_metadata = metadata != NULL;
	if (metadata) {
		replay_metadata = *metadata;
	} else {
		memset(&replay_metadata, 0, sizeof(replay_metadata));
	}
}

legacy_uint genghost_sim_frame(void)
{
	return extended_replay_is_active() ? extended_replay_frame() : (legacy_u16)state.game_frame;
}

legacy_uint genghost_sim_frame_limit(void)
{
	return extended_track_active() ? GENGHOST_MAX_FRAMES : GENGHOST_LEGACY_MAX_FRAMES;
}

legacy_u16 genghost_sim_frame_rate(void)
{
	return initialized ? gameconfig.game_framespersec : selected_frame_rate;
}

void genghost_sim_capture(struct GENGHOST_SNAPSHOT *saved)
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

void genghost_sim_restore(const struct GENGHOST_SNAPSHOT *saved)
{
	state = saved->state;
	legacy_execution_residue = saved->residue;
	extended_replay_clocks = saved->clocks;
	if (extended_track_active()) {
		extended_geometry_set_context(saved->context_x * GENGHOST_WORLD_POSITION_SCALE,
									  saved->context_z * GENGHOST_WORLD_POSITION_SCALE);
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

legacy_int genghost_sim_init(const legacy_u8 track[REPLAY_TRACK_SIZE],
							 const legacy_char car[REPLAY_CAR_ID_SIZE], legacy_int transmission,
							 legacy_int argc, legacy_char **argv)
{
	return genghost_sim_init_track(track, REPLAY_TRACK_SIZE, car, transmission, argc, argv);
}

legacy_int genghost_sim_init_track(const legacy_u8 *track, legacy_uint track_size,
								   const legacy_char car[REPLAY_CAR_ID_SIZE],
								   legacy_int transmission, legacy_int argc, legacy_char **argv)
{
	if (initialized || track == NULL || car == NULL || argc < 0 ||
		argc > (legacy_int)LEGACY_S16_MAX ||
		(transmission != TRANSMISSION_AUTOMATIC && transmission != TRANSMISSION_MANUAL)) {
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

	static const legacy_u8 track_magic[] = {'C', 'S', 'T', 'R', 'K', 0x1a, '\r', '\n'};
	legacy_int extended =
		track_size >= sizeof(track_magic) && memcmp(track, track_magic, sizeof(track_magic)) == 0;
	if (extended) {
		legacy_char error[GENGHOST_ERROR_CAPACITY];
		if (!extended_track_load(track, track_size, error, sizeof(error)) ||
			!cstrk_encode(extended_track_model(), &original_extended_track,
						  &original_extended_track_size, error, sizeof(error))) {
			fprintf(stderr, "Cannot simulate extended track: %s\n", error);
			return -1;
		}
	} else {
		if (track_size != REPLAY_TRACK_SIZE) {
			return -1;
		}
		memcpy(original_track, track, REPLAY_TRACK_SIZE);
		memcpy(track_element_map, track, REPLAY_TRACK_SIZE);
	}
	memset(&gameconfig, 0, sizeof(gameconfig));
	memcpy(gameconfig.game_playercarid, car, REPLAY_CAR_ID_SIZE);
	gameconfig.game_playertransmission = (legacy_s8)transmission;
	gameconfig.game_opponentcarid[0] = -1;
	gameconfig.game_framespersec = selected_frame_rate;
	if (has_replay_metadata) {
		gameconfig.game_playermaterial = replay_metadata.player_material;
		gameconfig.game_opponenttype = replay_metadata.opponent_type;
		if (gameconfig.game_opponenttype) {
			memcpy(gameconfig.game_opponentcarid, replay_metadata.opponent_car_id,
				   REPLAY_CAR_ID_SIZE);
		}
		gameconfig.game_opponentmaterial = replay_metadata.opponent_material;
		gameconfig.game_opponenttransmission = replay_metadata.opponent_transmission;
	}
	gameconfigcopy = gameconfig;
	if (!extended) {
		memcpy(track_and_directory_backup, track_element_map, TRACKDATA_LINK_TABLE_SIZE);
		memcpy(track_and_directory_backup + TRACKDATA_LINK_TABLE_SIZE, track_directory,
			   TRACKDATA_CHECKPOINT_DATA_SIZE);
		memcpy(track_and_directory_backup + TRACKDATA_CHECKPOINT_SECOND_OFFSET, replay_directory,
			   TRACKDATA_CHECKPOINT_DATA_SIZE);
	}

	legacy_int setup_result = track_setup();
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
	framespersec = selected_frame_rate;
	if (extended) {
		extended_replay_runtime_begin();
	}
	restore_gamestate(0);
	memset(replay_input_buffer, 0, GENGHOST_LEGACY_MAX_FRAMES);
	genghost_sim_capture(&initial_snapshot);
	initialized = 1;
	return 0;
}

void genghost_sim_reset(void)
{
	genghost_sim_restore(&initial_snapshot);
}

legacy_int genghost_sim_step(legacy_u8 input)
{
	if (!initialized || genghost_sim_frame() >= genghost_sim_frame_limit()) {
		return -1;
	}
	input &= GENGHOST_INPUT_MASK;
	if (extended_replay_is_active()) {
		extended_replay_set_input_frame(genghost_sim_frame());
		if (!extended_replay_record_input(input)) {
			return -1;
		}
	} else {
		replay_input_buffer[(legacy_u16)state.game_frame] = (legacy_s8)input;
	}
	/* Silent keeps the original full tick, penalty checks, collision bugs and
	 * OWOOT enforcement. It only suppresses audio and checkpoint writes. */
	update_gamestate_silent();
	return state.playerstate.car_crashBmpFlag;
}

void genghost_sim_result(struct GENGHOST_RESULT *result)
{
	result->frames = genghost_sim_frame();
	result->finish_frame = extended_replay_is_active() ? extended_replay_clocks.player_end
													   : (legacy_u16)state.game_pEndFrame;
	result->total_frames = extended_replay_is_active() ? extended_replay_clocks.player_finish
													   : (legacy_u16)state.game_total_finish;
	result->finished = state.playerstate.car_crashBmpFlag == CRASH_EVENT_FINISH;
	/* Route tracking continues while a finished car brakes through the replay
	 * tail. Only the penalty latched into the native finish time counts. */
	result->penalty_frames =
		result->finished ? (legacy_u16)(result->total_frames - result->finish_frame - elapsed_time1)
						 : (legacy_u16)state.game_penalty;
	if (extended_replay_is_active()) {
		result->penalty_frames = result->finished ? result->total_frames - result->finish_frame
												  : extended_replay_clocks.penalty;
	}
	result->crashed = state.playerstate.car_crashBmpFlag != CRASH_EVENT_NONE && !result->finished;
}

legacy_int genghost_sim_evaluate(const legacy_u8 *inputs, legacy_uint length,
								 struct GENGHOST_RESULT *result)
{
	if (!initialized || inputs == NULL || result == NULL || length > genghost_sim_frame_limit()) {
		return -1;
	}
	genghost_sim_reset();
	for (legacy_uint frame = 0; frame < length; frame++) {
		legacy_int event = genghost_sim_step(inputs[frame]);
		if (event != CRASH_EVENT_NONE && event != CRASH_EVENT_FINISH) {
			break;
		}
	}
	genghost_sim_result(result);
	return 0;
}

static void set_transmission(legacy_int transmission)
{
	gameconfig.game_playertransmission = (legacy_s8)transmission;
	gameconfigcopy.game_playertransmission = (legacy_s8)transmission;
	initial_snapshot.state.playerstate.car_transmission = (legacy_s8)transmission;
	genghost_sim_reset();
}

legacy_int genghost_sim_manual_inputs(legacy_u8 *inputs, legacy_uint length,
									  struct GENGHOST_RESULT *baseline)
{
	if (!initialized || inputs == NULL || baseline == NULL || length > genghost_sim_frame_limit()) {
		return -1;
	}
	legacy_int transmission = initial_snapshot.state.playerstate.car_transmission;
	if (transmission == TRANSMISSION_MANUAL) {
		return genghost_sim_evaluate(inputs, length, baseline) || !baseline->finished ? -1 : 0;
	}

	legacy_u8 converted[GENGHOST_MAX_FRAMES];
	struct GENGHOST_SNAPSHOT finish_snapshot, manual_snapshot;
	legacy_uint finish_frame = 0;
	genghost_sim_reset();
	for (legacy_uint frame = 0; frame < length; ++frame) {
		legacy_int gear = state.playerstate.car_current_gear;
		legacy_int driving = state.playerstate.car_crashBmpFlag == CRASH_EVENT_NONE;
		legacy_int waiting = state.game_inputmode == GAME_INPUT_MODE_WAITING;
		legacy_int event = genghost_sim_step(inputs[frame]);
		if (event < 0 || (event != CRASH_EVENT_NONE && event != CRASH_EVENT_FINISH)) {
			return -1;
		}
		converted[frame] = inputs[frame] & INPUT_DRIVING_MASK;
		/* current_gear changes when a native shift starts, before the knob reaches
		 * its target and the ratio engages. One tick reproduces that full shift. */
		if (driving && state.playerstate.car_current_gear > gear) {
			converted[frame] |= INPUT_SHIFT_UP_FLAG;
		} else if (driving && state.playerstate.car_current_gear < gear) {
			converted[frame] |= INPUT_SHIFT_DOWN_FLAG;
		}
		/* Any nonzero input starts the race. An action-only automatic input has
		 * no driving effect; a downshift in first (or neutral) preserves that. */
		if (waiting && (inputs[frame] & 63U) && converted[frame] == INPUT_NONE) {
			if (gear > 1) {
				return -1;
			}
			converted[frame] = INPUT_SHIFT_DOWN_FLAG;
		}
		if (event == CRASH_EVENT_FINISH && !finish_frame) {
			finish_frame = frame + 1U;
			genghost_sim_capture(&finish_snapshot);
			finish_snapshot.state.playerstate.car_transmission = TRANSMISSION_MANUAL;
		}
	}
	genghost_sim_result(baseline);
	if (!baseline->finished || !finish_frame) {
		return -1;
	}

	set_transmission(TRANSMISSION_MANUAL);
	for (legacy_uint frame = 0; frame < length; ++frame) {
		legacy_int event = genghost_sim_step(converted[frame]);
		if (event < 0 || (event != CRASH_EVENT_NONE && event != CRASH_EVENT_FINISH)) {
			goto failed;
		}
		if (frame + 1U == finish_frame) {
			genghost_sim_capture(&manual_snapshot);
			if (memcmp(&finish_snapshot, &manual_snapshot, sizeof(finish_snapshot))) {
				goto failed;
			}
		}
	}
	struct GENGHOST_RESULT manual;
	genghost_sim_result(&manual);
	if (!manual.finished || manual.finish_frame != baseline->finish_frame ||
		manual.penalty_frames != baseline->penalty_frames ||
		manual.total_frames != baseline->total_frames) {
		goto failed;
	}
	/* The engine forces braking after the finish, discarding manual shift
	 * buttons. Preserve tail driving inputs and length; only the scored lap
	 * through the finish is required to have an identical native snapshot. */
	memcpy(inputs, converted, length);
	genghost_sim_reset();
	return 0;

failed:
	set_transmission(transmission);
	return -1;
}

legacy_int genghost_sim_write_replay(const legacy_char *path, const legacy_char *track_name,
									 const legacy_u8 *inputs, legacy_uint length)
{
	if (!initialized || path == NULL || track_name == NULL || inputs == NULL ||
		length > genghost_sim_frame_limit()) {
		return -1;
	}
	if (extended_track_active()) {
		struct CSRPL_METADATA metadata = replay_metadata;
		metadata.version = has_replay_metadata ? replay_metadata.version : CSRPL_VERSION;
		metadata.driving_rules = driving_rules_current();
		memcpy(metadata.player_car_id, gameconfig.game_playercarid, CSRPL_CAR_ID_SIZE);
		metadata.player_material = gameconfig.game_playermaterial;
		metadata.player_transmission = gameconfig.game_playertransmission;
		metadata.opponent_type = gameconfig.game_opponenttype;
		memcpy(metadata.opponent_car_id, gameconfig.game_opponentcarid, CSRPL_CAR_ID_SIZE);
		metadata.opponent_material = gameconfig.game_opponentmaterial;
		metadata.opponent_transmission = gameconfig.game_opponenttransmission;
		memset(metadata.track_name, 0, sizeof(metadata.track_name));
		size_t name_length = strlen(track_name);
		if (name_length >= sizeof(metadata.track_name)) {
			name_length = sizeof(metadata.track_name) - 1U;
		}
		memcpy(metadata.track_name, track_name, name_length);
		metadata.frames_per_second = gameconfig.game_framespersec;
		metadata.frame_count = length;
		legacy_u8 *bytes = NULL;
		size_t size = 0;
		legacy_char error[GENGHOST_ERROR_CAPACITY];
		if (!csrpl_encode(&metadata, original_extended_track, original_extended_track_size, inputs,
						  length, &bytes, &size, error, sizeof(error))) {
			fprintf(stderr, "Cannot encode extended ghost: %s\n", error);
			return -1;
		}
		FILE *output = fopen(path, "wb");
		if (!output) {
			perror(path);
			free(bytes);
			return -1;
		}
		legacy_int failed = fwrite(bytes, 1, size, output) != size;
		if (fclose(output)) {
			failed = 1;
		}
		free(bytes);
		return failed ? -1 : 0;
	}
	struct GAMEINFO config = gameconfig;
	memset(config.game_trackname, 0, sizeof(config.game_trackname));
	size_t name_length = strlen(track_name);
	if (name_length >= sizeof(config.game_trackname)) {
		name_length = sizeof(config.game_trackname) - 1U;
	}
	memcpy(config.game_trackname, track_name, name_length);
	config.game_recordedframes = (legacy_u16)length;
	legacy_u8 header[REPLAY_GAMEINFO_SIZE];
	replay_gameinfo_encode(header, &config);
	FILE *output = fopen(path, "wb");
	if (output == NULL) {
		perror(path);
		return -1;
	}
	legacy_int failed =
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
