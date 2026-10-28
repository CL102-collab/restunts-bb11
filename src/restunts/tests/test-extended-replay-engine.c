#define _GNU_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../c/externs.h"
#include "../c/restunts.h"
#include "../c/fileio.h"
#include "../c/memmgr.h"
#include "../c/platform.h"
#include "../c/race_resources.h"
#include "../c/replay_record.h"
#include "../c/game_input.h"
#include "../c/track_objects.h"
#include "../c/track_collision.h"
#include "../c/crash_state.h"
#include "../c/cstrk.h"
#include "../c/extended_track.h"
#include "../c/extended_replay_runtime.h"
#include "../c/extended_file.h"

#undef memcpy
#undef printf
#undef strlen

#define ENGINE_TEST_FRAMES 70001U

static char temporary_directory[] = "/tmp/restunts-extended-replay-XXXXXX";
static char replay_path[128];
static char invalid_path[128];

static void cleanup(void)
{
	if (replay_path[0]) {
		unlink(replay_path);
	}
	if (invalid_path[0]) {
		unlink(invalid_path);
	}
	if (replay_path[0]) {
		rmdir(temporary_directory);
	}
}

static void add_piece(struct CSTRK_TRACK *track, unsigned id, unsigned x, unsigned y)
{
	struct CSTRK_OBJECT object = {0};
	object.legacy_id = (uint16_t)id;
	object.x = (uint8_t)x;
	object.y = (uint8_t)y;
	object.width = object.height = object.occupied_mask = 1U;
	char error[128];
	assert(cstrk_add_object(track, &object, error, sizeof(error)));
}

static unsigned grade_height(unsigned row)
{
	return row <= 43U ? 0U : row >= 47U ? 4U : row - 43U;
}

static void create_track(int sloped_start)
{
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	char error[192];
	assert(cstrk_create(&track, 90U, 90U, error, sizeof(error)));
	assert(cstrk_set_notes(&track, "Long replay integration fixture", 31U, error, sizeof(error)));
	add_piece(&track, 6U, 40U, 40U);
	add_piece(&track, 7U, 46U, 40U);
	add_piece(&track, 8U, 40U, 46U);
	add_piece(&track, 9U, 46U, 46U);
	for (unsigned coordinate = 41U; coordinate < 46U; coordinate++) {
		add_piece(&track, 5U, coordinate, 40U);
		add_piece(&track, 5U, coordinate, 46U);
		add_piece(&track, coordinate == 43U ? 1U : 4U, 40U, coordinate);
		add_piece(&track, 4U, 46U, coordinate);
	}
	if (sloped_start) {
		for (unsigned row = 0U; row <= track.height; row++) {
			for (unsigned column = 0U; column <= track.width; column++) {
				track.corner_heights[row * (track.width + 1U) + column] =
					(uint8_t)grade_height(row);
			}
		}
		for (uint32_t index = 0U; index < track.object_count; index++) {
			struct CSTRK_OBJECT *object = &track.objects[index];
			object->base_height = (uint8_t)grade_height(object->y);
			object->slope_y = (int8_t)(grade_height(object->y + 1U) - object->base_height);
		}
	}
	if (!extended_track_activate(&track, error, sizeof(error))) {
		fprintf(stderr, "Track activation: %s\n", error);
		assert(0);
	}
	assert(track_setup() == 0);
	assert(start_finish_column == 40U && start_finish_row == 43U);
	cstrk_free(&track);
}

static void assert_same_simulation(const struct GAMESTATE *expected, const legacy_s8 *seed)
{
	static unsigned comparison;
	comparison++;
	legacy_s8 actual_seed[GAMESTATE_RANDOM_SEED_SIZE];
	get_kevinrandom_seed(actual_seed);
	assert(memcmp(actual_seed, seed, sizeof(actual_seed)) == 0);
	if (memcmp(&state, expected, sizeof(state)) != 0) {
		fprintf(
			stderr,
			"Camera actual %d,%d,%d expected %d,%d,%d; target actual %d,%d,%d expected %d,%d,%d\n",
			state.game_follow_camera_position[0].x, state.game_follow_camera_position[0].y,
			state.game_follow_camera_position[0].z, expected->game_follow_camera_position[0].x,
			expected->game_follow_camera_position[0].y, expected->game_follow_camera_position[0].z,
			state.playerstate.car_route_target.x, state.playerstate.car_route_target.y,
			state.playerstate.car_route_target.z, expected->playerstate.car_route_target.x,
			expected->playerstate.car_route_target.y, expected->playerstate.car_route_target.z);
		for (size_t index = 0U; index < sizeof(state); index++) {
			if (((const uint8_t *)&state)[index] != ((const uint8_t *)expected)[index]) {
				fprintf(stderr, "Comparison %u differs at frame %u byte %u: %u vs %u\n", comparison,
						(unsigned)extended_replay_frame(), (unsigned)index,
						(unsigned)((const uint8_t *)&state)[index],
						(unsigned)((const uint8_t *)expected)[index]);
				break;
			}
		}
	}
	assert(memcmp(&state, expected, sizeof(state)) == 0);
}

static void initialize_simulation(void)
{
	framespersec = gameconfig.game_framespersec;
	init_kevinrandom("kevin");
	init_game_state(GAMESTATE_INIT_RESET_CHECKPOINTS);
	game_replay_mode = REPLAY_MODE_PLAYBACK;
	is_in_replay = 1;
	idle_expired = 0;
	race_exit_request = 0;
}

legacy_s16 stuntsmain(legacy_s16 argc, legacy_s8 *argv[])
{
	assert(argc == 1);
	assert(mkdtemp(temporary_directory));
	assert(atexit(cleanup) == 0);
	assert(snprintf(replay_path, sizeof(replay_path), "%s/LONG.csrpl", temporary_directory) > 0);
	assert(snprintf(invalid_path, sizeof(invalid_path), "%s/BAD.csrpl", temporary_directory) > 0);
	init_main(argc, argv);
	init_div0();
	init_row_tables();
	init_polyinfo();
	init_trackdata();
	cvxptr = mmgr_alloc_resbytes("cvx", sizeof(struct GAMESTATE) * GAMESTATE_CHECKPOINT_COUNT);
	assert(cvxptr);
	memcpy(gameconfig.game_playercarid, "P962", 4U);
	memcpy(gameconfig.game_opponentcarid, "P962", 4U);
	memcpy(gameconfig.game_trackname, "LONG", 5U);
	gameconfig.game_playertransmission = TRANSMISSION_AUTOMATIC;
	gameconfig.game_opponenttransmission = TRANSMISSION_AUTOMATIC;
	gameconfig.game_opponenttype = 0;
	gameconfig.game_playermaterial = gameconfig.game_opponentmaterial = 0;
	gameconfig.game_framespersec = 20U;
	create_track(0);
	ghost_load_simulation_resources();
	extended_replay_runtime_begin();
	initialize_simulation();
	assert(state.playerstate.car_position.lx > 32767L * 64L);
	/* Exercise the real timer-input recording path past both legacy limits. */
	game_replay_mode = REPLAY_MODE_LIVE;
	for (uint32_t frame = 0U; frame < ENGINE_TEST_FRAMES; frame++) {
		replay_update_input_tick(1);
		assert(extended_replay_input_frame() == frame + 1U);
		if (frame % 53U == 0U) {
			assert(extended_replay_replace_input(frame, INPUT_BRAKE_FLAG));
		}
		assert(recording_limit_warning_requested == 0U);
		update_gamestate();
		if (state.playerstate.car_crashBmpFlag != CRASH_EVENT_NONE) {
			fprintf(stderr, "Unexpected event %d at frame %u, position %d,%d,%d\n",
					(int)state.playerstate.car_crashBmpFlag, (unsigned)frame,
					(int)state.playerstate.car_position.lx, (int)state.playerstate.car_position.ly,
					(int)state.playerstate.car_position.lz);
			assert(0);
		}
	}
	assert(extended_replay_frame() == ENGINE_TEST_FRAMES);
	assert(extended_replay_recorded_frames() == ENGINE_TEST_FRAMES);
	assert(state.playerstate.car_crashBmpFlag == CRASH_EVENT_NONE);
	struct GAMESTATE expected = state;
	legacy_s8 expected_seed[GAMESTATE_RANDOM_SEED_SIZE];
	get_kevinrandom_seed(expected_seed);
	game_replay_mode = REPLAY_MODE_PLAYBACK;
	assert(extended_replay_seek(65534U));
	assert(extended_replay_seek(ENGINE_TEST_FRAMES));
	assert_same_simulation(&expected, expected_seed);
	assert(extended_replay_seek(65537U));
	assert(extended_replay_branch(65537U));
	game_replay_mode = REPLAY_MODE_LIVE;
	while (extended_replay_frame() < ENGINE_TEST_FRAMES) {
		uint32_t frame = extended_replay_frame();
		replay_update_input_tick(1);
		if (frame % 53U == 0U) {
			assert(extended_replay_replace_input(frame, INPUT_BRAKE_FLAG));
		}
		update_gamestate();
	}
	assert_same_simulation(&expected, expected_seed);
	uint8_t *track_before = NULL;
	size_t track_size = 0U;
	char error[128];
	assert(cstrk_encode(extended_track_model(), &track_before, &track_size, error, sizeof(error)));
	assert(file_write_replay((const legacy_s8 *)replay_path) == 0);
	FILE *saved = fopen(replay_path, "rb");
	assert(saved);
	assert(fseek(saved, 0L, SEEK_END) == 0);
	long saved_size = ftell(saved);
	assert(saved_size == (long)(CSRPL_HEADER_SIZE + track_size + ENGINE_TEST_FRAMES));
	assert(fseek(saved, 0L, SEEK_SET) == 0);
	uint8_t *bytes = malloc((size_t)saved_size);
	assert(bytes && fread(bytes, 1U, (size_t)saved_size, saved) == (size_t)saved_size);
	assert(fclose(saved) == 0);
	/* An invalid load must preserve the currently usable track and recording. */
	bytes[20] ^= 1U;
	FILE *invalid = fopen(invalid_path, "wb");
	assert(invalid && fwrite(bytes, 1U, (size_t)saved_size, invalid) == (size_t)saved_size);
	assert(fclose(invalid) == 0);
	assert(file_load_replay_format((const legacy_s8 *)temporary_directory, "BAD", ".csrpl") != 0);
	assert(extended_replay_frame() == ENGINE_TEST_FRAMES);
	assert(extended_replay_recorded_frames() == ENGINE_TEST_FRAMES);
	assert_same_simulation(&expected, expected_seed);
	/* This container is structurally valid. Admission fails only after the
	 * candidate replay has allocated its input history, exercising rollback. */
	bytes[20] ^= 1U;
	size_t first_object = CSRPL_HEADER_SIZE + CSTRK_HEADER_SIZE + 91U * 91U + 2U * 90U * 90U;
	bytes[first_object] = 182U; /* Runtime-only original hill shape. */
	bytes[first_object + 1U] = 0U;
	struct CSRPL_VIEW candidate;
	assert(csrpl_decode(bytes, (size_t)saved_size, &candidate, error, sizeof(error)));
	invalid = fopen(invalid_path, "wb");
	assert(invalid && fwrite(bytes, 1U, (size_t)saved_size, invalid) == (size_t)saved_size);
	assert(fclose(invalid) == 0);
	struct GAMEINFO prior_config = gameconfig;
	struct EXTENDED_REPLAY_CLOCKS prior_clocks = extended_replay_clocks;
	const uint8_t *prior_inputs = extended_replay_inputs();
	const struct CSTRK_TRACK *prior_track = extended_track_model();
	assert(file_load_replay_format((const legacy_s8 *)temporary_directory, "BAD", ".csrpl") != 0);
	assert(memcmp(&gameconfig, &prior_config, sizeof(gameconfig)) == 0);
	assert(memcmp(&extended_replay_clocks, &prior_clocks, sizeof(prior_clocks)) == 0);
	assert(extended_replay_inputs() == prior_inputs);
	assert(extended_track_model() == prior_track);
	uint8_t *rollback_track = NULL;
	size_t rollback_size = 0U;
	assert(cstrk_encode(extended_track_model(), &rollback_track, &rollback_size, error,
						sizeof(error)));
	assert(rollback_size == track_size && memcmp(rollback_track, track_before, track_size) == 0);
	free(rollback_track);
	assert_same_simulation(&expected, expected_seed);
	free(bytes);
	extended_replay_runtime_clear();
	extended_track_unload();
	assert(file_load_replay_format((const legacy_s8 *)temporary_directory, "LONG", ".csrpl") == 0);
	assert(extended_replay_recorded_frames() == ENGINE_TEST_FRAMES);
	assert(extended_track_width() == 90 && extended_track_height() == 90);
	uint8_t *track_after = NULL;
	size_t after_size = 0U;
	assert(cstrk_encode(extended_track_model(), &track_after, &after_size, error, sizeof(error)));
	assert(after_size == track_size && memcmp(track_before, track_after, track_size) == 0);
	free(track_before);
	free(track_after);
	for (uint32_t frame = 0U; frame < ENGINE_TEST_FRAMES; frame++) {
		assert(extended_replay_inputs()[frame] == (frame % 53U == 0U ? INPUT_BRAKE_FLAG : 0U));
	}
	assert(track_setup() == 0);
	initialize_simulation();
	assert(extended_replay_seek(ENGINE_TEST_FRAMES));
	assert_same_simulation(&expected, expected_seed);
	/* Finish events use the wide engine clock even after a file round trip. */
	extended_replay_clocks.penalty = 80000U;
	update_crash_state(CRASH_EVENT_FINISH, PLAYER_CAR_INDEX);
	assert(extended_replay_clocks.player_end == ENGINE_TEST_FRAMES);
	assert(extended_replay_clocks.player_finish == ENGINE_TEST_FRAMES + 80000U);
	extended_replay_runtime_clear();
	extended_track_unload();
	create_track(1);
	extended_replay_runtime_begin();
	initialize_simulation();
	assert(state.playerstate.car_rotate.y != 0 || state.playerstate.car_rotate.z != 0);
	for (unsigned frame = 0U; frame < 20U; frame++) {
		assert(extended_replay_record_input(INPUT_BRAKE_FLAG));
		update_gamestate();
		assert(state.playerstate.car_crashBmpFlag == CRASH_EVENT_NONE);
	}
	extended_replay_runtime_clear();
	extended_track_unload();
	puts("Extended engine replay: 70001 frames, deterministic rewind/branch, file round trip, wide "
		 "finish.");
	return 0;
}
