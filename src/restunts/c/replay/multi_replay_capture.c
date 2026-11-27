#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL_iostream.h>

#include "multi_replay_capture.h"
#include "../../platform/sdl3/sdl3.h"
#include "../car/car_speed.h"
#include "../crash_state.h"
#include "../driving_rules.h"
#include "../extended/extended_ghost.h"
#include "../extended/extended_physics_snapshot.h"
#include "../extended/extended_track.h"
#include "../fileio.h"
#include "../ghost/ghost_internal.h"
#include "../opponent/unit_opponent.h"
#include "../owoot/owoot.h"
#include "../physics/physics_internal.h"
#include "../platform.h"
#include "../race/race_resources.h"
#include "../race/race_route.h"
#include "../restunts.h"
#include "../track/track_objects.h"
#include "../track/trackdata_layout.h"

#undef memcpy

#define MULTI_REPLAY_PUMP_INTERVAL 256U

static legacy_int multi_replay_capture_error(legacy_char *error, size_t size,
											 const legacy_char *message)
{
	if (error != NULL && size != 0) {
		snprintf(error, size, "%s", message);
	}
	return 0;
}

/* The launcher supplies exact UTF-8 native paths. Legacy resource lookup
 * intentionally accepts DOS casing and treats backslashes as separators. */
static legacy_u8 *multi_replay_read_file(const legacy_s8 *path, size_t minimum, size_t maximum,
										 size_t *size)
{
	SDL_IOStream *file = SDL_IOFromFile((const legacy_char *)path, "rb");
	if (file == NULL) {
		return NULL;
	}
	legacy_s64 length = SDL_GetIOSize(file);
	legacy_u8 *bytes = NULL;
	if (length >= 0 && (legacy_u64)length >= minimum && (legacy_u64)length <= maximum) {
		bytes = malloc((size_t)length);
		if (bytes != NULL && SDL_ReadIO(file, bytes, (size_t)length) != (size_t)length) {
			free(bytes);
			bytes = NULL;
		}
	}
	SDL_CloseIO(file);
	if (bytes != NULL) {
		*size = (size_t)length;
	}
	return bytes;
}

static legacy_int multi_replay_read_legacy(struct MULTI_REPLAY_SOURCE *source, size_t size)
{
	replay_gameinfo_decode(&source->config, source->bytes);
	source->frame_count = source->config.game_recordedframes;
	if (!ghost_valid_config(&source->config) || source->frame_count == 0U ||
		replay_file_size(source->config.game_recordedframes) != size ||
		(source->config.game_opponenttype != 0 &&
		 (source->config.game_opponenttransmission < TRANSMISSION_MANUAL ||
		  source->config.game_opponenttransmission > TRANSMISSION_AUTOMATIC))) {
		return 0;
	}

	source->track_data = source->bytes + REPLAY_GAMEINFO_SIZE;
	source->track_size = REPLAY_TRACK_SIZE;
	source->inputs = source->bytes + REPLAY_INPUT_OFFSET;
	source->classic_track = 1;
	memcpy(source->track_name, source->config.game_trackname, REPLAY_TRACK_NAME_SIZE);
	source->config.game_trackname[REPLAY_TRACK_NAME_SIZE - 1U] = 0;
	return 1;
}

static legacy_int multi_replay_read_extended(struct MULTI_REPLAY_SOURCE *source, size_t size)
{
	struct CSRPL_VIEW *view = &source->extended;
	if (!extended_ghost_decode(source->bytes, size, view, &source->config)) {
		return 0;
	}
	struct GAMEINFO checked = source->config;
	checked.game_recordedframes = 0;
	if (checked.game_opponenttype == CSRPL_UNIT_OPPONENT_TYPE) {
		checked.game_opponenttype = CSRPL_CLASSIC_OPPONENT_LAST;
	}
	if (!ghost_valid_config(&checked) || view->metadata.frame_count == 0U) {
		return 0;
	}

	source->track_data = view->track_data;
	source->track_size = view->track_size;
	source->inputs = view->inputs;
	source->frame_count = view->metadata.frame_count;
	source->classic_track = (view->metadata.flags & CSRPL_FLAG_CLASSIC_TRACK) != 0U;
	source->is_extended = 1;
	memcpy(source->track_name, view->metadata.track_name, CSRPL_TRACK_NAME_SIZE);
	return 1;
}

legacy_int multi_replay_source_read(const legacy_s8 *path, struct MULTI_REPLAY_SOURCE *source,
									legacy_char *error, size_t error_size)
{
	const legacy_s8 *suffix = (const legacy_s8 *)strrchr((const legacy_char *)path, '.');
	legacy_int extended = suffix != NULL && stricmp(suffix, ".csrpl") == 0;
	if (!extended && (suffix == NULL || stricmp(suffix, ".rpl") != 0)) {
		return multi_replay_capture_error(error, error_size, "Unsupported replay filename.");
	}
	size_t size;
	struct MULTI_REPLAY_SOURCE candidate = {0};
	candidate.bytes = multi_replay_read_file(
		path, extended ? CSRPL_HEADER_SIZE : REPLAY_INPUT_OFFSET,
		extended ? LEGACY_S32_MAX : replay_file_size(TRACKDATA_REPLAY_INPUT_BUFFER_SIZE), &size);
	if (candidate.bytes == NULL || !(extended ? multi_replay_read_extended(&candidate, size)
											  : multi_replay_read_legacy(&candidate, size))) {
		multi_replay_source_free(&candidate);
		return multi_replay_capture_error(error, error_size, "Unable to read the selected replay.");
	}
	*source = candidate;
	return 1;
}

void multi_replay_source_free(struct MULTI_REPLAY_SOURCE *source)
{
	free(source->bytes);
	memset(source, 0, sizeof(*source));
}

legacy_int multi_replay_source_same_track(const struct MULTI_REPLAY_SOURCE *first,
										  const struct MULTI_REPLAY_SOURCE *second)
{
	return first->classic_track == second->classic_track &&
		   first->track_size == second->track_size &&
		   memcmp(first->track_data, second->track_data, first->track_size) == 0;
}

legacy_int multi_replay_source_activate_track(const struct MULTI_REPLAY_SOURCE *source,
											  legacy_char *error, size_t error_size)
{
	if (source->classic_track) {
		extended_track_unload();
		memcpy(track_element_map, source->track_data, source->track_size);
	} else if (!extended_track_load(source->track_data, source->track_size, error, error_size)) {
		return 0;
	}

	extended_track_set_name(source->track_name);
	if (track_setup() != 0) {
		return multi_replay_capture_error(error, error_size,
										  "Unable to set up the embedded track.");
	}
	return 1;
}

static void multi_replay_apply_rules(legacy_u16 rules)
{
	static const struct {
		legacy_u16 flag;
		void (*configure)(legacy_s16, legacy_s8 *[]);
		const legacy_s8 *on;
		const legacy_s8 *off;
	} options[] = {
		{DRIVING_RULE_POWER_GEAR, configure_powergear_bug, "--pg:on", "--pg:off"},
		{DRIVING_RULE_LEGACY_COLLISION, configure_legacy_collision, "--lc:on", "--lc:off"},
		{DRIVING_RULE_LEFT_CORNER_BIAS, configure_left_corner_bias, "--lcb:on", "--lcb:off"},
		{DRIVING_RULE_DUAL_WAY_SWITCHING, configure_dual_way_switching, "--dws:on", "--dws:off"},
		{DRIVING_RULE_CHICANE_FAST_GRASS, configure_chicane_fast_grass, "--cfg:on", "--cfg:off"},
		{DRIVING_RULE_AMIGA_GRASS, configure_amiga_grass, "--amigagrass", ""},
		{DRIVING_RULE_OWOOT, configure_owoot, "--owoot:on", "--owoot:off"},
	};
	for (size_t index = 0; index < sizeof(options) / sizeof(options[0]); index++) {
		legacy_s8 *args[] = {NULL, (legacy_s8 *)(rules & options[index].flag ? options[index].on
																			 : options[index].off)};
		options[index].configure(sizeof(args) / sizeof(args[0]), args);
	}
}

static legacy_int multi_replay_car_resource_exists(const legacy_s8 *car_id, const legacy_char *stem,
												   const legacy_char *const *extensions,
												   size_t extension_count)
{
	for (size_t index = 0; index < extension_count; index++) {
		legacy_char name[sizeof("stdaXXXX.vsh")];
		snprintf(name, sizeof(name), "%s%.*s%s", stem, (legacy_int)REPLAY_CAR_ID_SIZE,
				 (const legacy_char *)car_id, extensions[index]);
		legacy_u16 handle = file_open_resource((const legacy_s8 *)name);
		if (handle != 0) {
			dos_file_close(handle);
			return 1;
		}
	}
	return 0;
}

legacy_int multi_replay_car_available(const legacy_s8 *car_id, legacy_int needs_dashboard)
{
	static const legacy_char *const physics_extensions[] = {".res", ".pre"};
	static const legacy_char *const model_extensions[] = {".p3s", ".3sh"};
	static const legacy_char *const dashboard_extensions[] = {".pvs", ".vsh", ".xvs", ".pes",
															  ".esh"};
	return multi_replay_car_resource_exists(car_id, "car", physics_extensions,
											sizeof(physics_extensions) /
												sizeof(physics_extensions[0])) &&
		   multi_replay_car_resource_exists(car_id, "st", model_extensions,
											sizeof(model_extensions) /
												sizeof(model_extensions[0])) &&
		   (!needs_dashboard ||
			(multi_replay_car_resource_exists(car_id, "stda", dashboard_extensions,
											  sizeof(dashboard_extensions) /
												  sizeof(dashboard_extensions[0])) &&
			 multi_replay_car_resource_exists(car_id, "stdb", dashboard_extensions,
											  sizeof(dashboard_extensions) /
												  sizeof(dashboard_extensions[0]))));
}

static void multi_replay_save_pose(struct MULTI_REPLAY_CAPTURE_POSE *pose,
								   const struct MULTI_REPLAY_CAPTURE_POSE *previous, legacy_u16 car,
								   legacy_u32 frame)
{
	legacy_int player = car == PLAYER_CAR_INDEX;
	pose->car = player ? state.playerstate : state.opponentstate;
	pose->camera.follow_position = state.game_follow_camera_position[car];
	pose->camera.previous_position =
		player ? state.game_player_camera_previous : state.game_opponent_camera_previous;
	pose->camera.trackside_index = state.game_trackside_camera_index[car];
	pose->camera.frame = frame;
	pose->camera.crash_frame = 0;
	if (pose->car.car_crashBmpFlag != CRASH_EVENT_NONE) {
		/* Keep the first affected frame in the full native clock, even after
		 * the original 16-bit end timestamp wraps. */
		pose->camera.crash_frame =
			previous != NULL && previous->car.car_crashBmpFlag != CRASH_EVENT_NONE
				? previous->camera.crash_frame
				: frame;
	}
}

static void multi_replay_simulate(struct MULTI_REPLAY_CAPTURE *capture)
{
	ghost_load_simulation_resources();
	capture->simd[PLAYER_CAR_INDEX] = simd_player;
	capture->simd[PLAYER_CAR_INDEX].aerorestable = NULL;
	if (capture->car_count > 1U) {
		capture->simd[OPPONENT_CAR_INDEX] = simd_opponent;
		capture->simd[OPPONENT_CAR_INDEX].aerorestable = NULL;
	}
	init_game_state(GAMESTATE_INIT_RESET_CHECKPOINTS);
	for (legacy_u32 frame = 0;; frame++) {
		for (legacy_u16 car = 0; car < capture->car_count; car++) {
			const struct MULTI_REPLAY_CAPTURE_POSE *previous =
				frame == 0U ? NULL : &capture->frames[frame - 1U].cars[car];
			multi_replay_save_pose(&capture->frames[frame].cars[car], previous, car, frame);
		}
		if (frame == capture->frame_count) {
			break;
		}
		if (frame % MULTI_REPLAY_PUMP_INTERVAL == 0U) {
			sdl3_platform_pump();
		}
		update_gamestate_silent();
	}
	ghost_free_simulation_resources();
}

legacy_int multi_replay_capture(const struct MULTI_REPLAY_SOURCE *source,
								struct MULTI_REPLAY_CAPTURE *capture, legacy_char *error,
								size_t error_size)
{
	legacy_int has_opponent = source->config.game_opponenttype != 0;
	legacy_u64 count = (legacy_u64)source->frame_count + 1U;
	if (count > SIZE_MAX / sizeof(*capture->frames)) {
		return multi_replay_capture_error(error, error_size, "The replay is too long to prepare.");
	}
	struct MULTI_REPLAY_CAPTURE loaded = {0};
	loaded.frames = malloc((size_t)count * sizeof(*loaded.frames));
	if (loaded.frames == NULL) {
		return multi_replay_capture_error(error, error_size,
										  "Not enough memory to prepare the replay.");
	}
	loaded.frame_count = source->frame_count;
	loaded.config = source->config;
	loaded.car_count = has_opponent ? MULTI_REPLAY_CARS_PER_RECORDING : 1U;

	struct EXTENDED_PHYSICS_SNAPSHOT saved_physics;
	extended_physics_capture(&saved_physics);
	struct EXTENDED_REPLAY_SUSPENDED saved_extended;
	extended_replay_runtime_suspend(&saved_extended);
	struct GAMEINFO saved_config = gameconfig;
	gameconfig = source->config;
	if (!has_opponent) {
		gameconfig.game_opponentcarid[0] = -1;
	}

	legacy_int extended = source->is_extended;
	if (extended && !extended_replay_runtime_load(&source->extended)) {
		extended_replay_runtime_resume(&saved_extended);
		gameconfig = saved_config;
		extended_physics_restore(&saved_physics);
		multi_replay_capture_free(&loaded);
		return multi_replay_capture_error(error, error_size,
										  "Unable to prepare the replay inputs.");
	}
	if (!extended) {
		memcpy(replay_input_buffer, source->inputs, source->frame_count);
	}

	legacy_u16 saved_rules = driving_rules_current();
	legacy_u16 saved_rate = framespersec;
	legacy_s16 saved_checkpoint_interval = checkpoint_frame_interval;
	legacy_s16 saved_timer_ticks = timer_ticks_per_frame;
	legacy_u8 saved_mode = game_replay_mode;
	legacy_s8 saved_pause = is_in_replay;
	legacy_s8 saved_recording = replay_recording_flags;
	legacy_s8 *saved_steering_table = steerWhlRespTable_ptr;

	if (extended) {
		multi_replay_apply_rules(source->extended.metadata.driving_rules);
	}
	framespersec = source->config.game_framespersec;
	game_replay_mode = REPLAY_MODE_PLAYBACK;
	is_in_replay = 1;
	replay_recording_flags = REPLAY_RECORDING_RESTARTABLE_FLAG;
	legacy_execution_residue = (struct LEGACY_EXECUTION_RESIDUE){0};
	legacy_render_player_headings_active = 0;
	init_kevinrandom("kevin");
	/* Replay setup normally consumes a random value for the scene's scenery. */
	(void)get_kevinrandom();

	multi_replay_simulate(&loaded);

	unit_opponent_stop();
	multi_replay_apply_rules(saved_rules);
	extended_replay_runtime_resume(&saved_extended);
	gameconfig = saved_config;
	extended_physics_restore(&saved_physics);
	framespersec = saved_rate;
	checkpoint_frame_interval = saved_checkpoint_interval;
	timer_ticks_per_frame = saved_timer_ticks;
	game_replay_mode = saved_mode;
	is_in_replay = saved_pause;
	replay_recording_flags = saved_recording;
	steerWhlRespTable_ptr = saved_steering_table;
	*capture = loaded;
	return 1;
}

void multi_replay_capture_free(struct MULTI_REPLAY_CAPTURE *capture)
{
	free(capture->frames);
	memset(capture, 0, sizeof(*capture));
}
