#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "simulation.h"
#include "../c/externs.h"
#include "../c/restunts.h"
#include "../c/camera.h"
#include "../c/track/track_objects.h"
#include "../c/memmgr.h"
#include "../c/race/race_resources.h"
#include "../c/race/race_stats.h"
#include "../c/physics/physics_internal.h"
#include "../c/wheel_transform.h"
#include "../c/crash_state.h"
#include "../c/track/trackdata_layout.h"
#include "../c/game_input.h"
#include "extended_simulation.h"
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

void genghost_sim_set_frame_rate(legacy_u16 rate)
{
	if (!initialized && (rate == GAME_FRAME_RATE_LOW || rate == GAME_FRAME_RATE_NORMAL)) {
		selected_frame_rate = rate;
	}
}

void genghost_sim_set_replay_metadata(const struct CSRPL_METADATA *metadata)
{
	extended_genghost_sim_set_metadata(metadata);
}

legacy_uint genghost_sim_frame(void)
{
	return extended_genghost_sim_frame();
}

legacy_uint genghost_sim_frame_limit(void)
{
	return extended_genghost_sim_frame_limit();
}

legacy_u16 genghost_sim_frame_rate(void)
{
	return initialized ? gameconfig.game_framespersec : selected_frame_rate;
}

void genghost_sim_capture(struct GENGHOST_SNAPSHOT *saved)
{
	extended_physics_capture(saved);
}

void genghost_sim_restore(const struct GENGHOST_SNAPSHOT *saved)
{
	extended_physics_restore(saved);
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

	legacy_int handled;
	if (extended_genghost_sim_load(track, track_size, &handled)) {
		return -1;
	}
	if (!handled) {
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
	extended_genghost_sim_configure();
	gameconfigcopy = gameconfig;
	if (!handled) {
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
	if (extended_genghost_sim_begin(handled)) {
		return -1;
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
	input = extended_genghost_sim_input(input);
	legacy_int handled = extended_genghost_sim_record(input);
	if (handled < 0) {
		return -1;
	}
	if (!handled) {
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
	result->finished = state.playerstate.car_crashBmpFlag == CRASH_EVENT_FINISH;
	if (!extended_genghost_sim_result(result)) {
		result->finish_frame = (legacy_u16)state.game_pEndFrame;
		result->total_frames = (legacy_u16)state.game_total_finish;
		/* Route tracking continues while a finished car brakes through the replay
		 * tail. Only the penalty latched into the native finish time counts. */
		result->penalty_frames =
			result->finished
				? (legacy_u16)(result->total_frames - result->finish_frame - elapsed_time1)
				: (legacy_u16)state.game_penalty;
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
	legacy_int handled;
	legacy_int result = extended_genghost_sim_write(path, track_name, inputs, length, &handled);
	if (handled) {
		return result;
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
