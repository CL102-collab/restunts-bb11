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
#include "../c/driving_rules.h"
#include "../c/car_speed.h"
#include "../c/car_resources.h"
#include "../c/owoot.h"
#include "../c/physics_internal.h"
#include "../c/roll_ramp.h"
#include "../c/opponent.h"

#undef memcpy
#undef printf
#undef strlen

#define ENGINE_TEST_FRAMES 70001U
#define ENGINE_TEST_HILL_REPLAY_FRAMES 120U
#define ENGINE_TEST_LEFT_COLUMN 40U
#define ENGINE_TEST_RIGHT_COLUMN 46U
#define ENGINE_TEST_TOP_ROW 40U
#define ENGINE_TEST_BOTTOM_ROW 48U
#define ENGINE_TEST_FLAT_START_ROW 43U
#define ENGINE_TEST_RAISED_START_ROW 47U
#define ENGINE_TEST_FIRST_SLOPE_ROW 43U
#define ENGINE_TEST_LAST_SLOPE_ROW 47U
#define ENGINE_TEST_POSITION_SCALE 64L
#define ENGINE_TEST_INITIAL_BODY_HEIGHT 8L
#define ENGINE_TEST_ERROR_CAPACITY 192U
#define ENGINE_TEST_FUTURE_REPLAY_VERSION 101U
#define ENGINE_TEST_FUTURE_TRACK_VERSION 15U
#define ENGINE_TEST_SLOPE_ROTATIONS 4U
#define ENGINE_TEST_SLOPE_SURFACES 3U
#define ENGINE_TEST_SLOPE_DRIVING_FRAMES 200U
#define ENGINE_TEST_SLOPE_MINIMUM_SAMPLES 20U
#define ENGINE_TEST_SLOPE_MAXIMUM_PITCH_STEP 1
#define ENGINE_TEST_SLOPE_START_ROW (ENGINE_TEST_FIRST_SLOPE_ROW - 1U)
#define ENGINE_TEST_SLOPE_MINIMUM_PROGRESS CSTRK_TILE_WORLD_SIZE
#define ENGINE_TEST_SLOPE_MAXIMUM_PROGRESS (3L * CSTRK_TILE_WORLD_SIZE)
#define ENGINE_TEST_SLOPE_END_PROGRESS ((legacy_s32)CSTRK_MAX_HEIGHT * CSTRK_TILE_WORLD_SIZE)
#define ENGINE_TEST_SLOPE_STUNT_NONE 0U
#define ENGINE_TEST_SLOPE_IMPACT_PROGRESS (2L * CSTRK_TILE_WORLD_SIZE)
#define ENGINE_TEST_SLALOM_LATERAL_OFFSET ((SLALOM_POLE_INNER_X + SLALOM_POLE_OUTER_X) / 2)
#define ENGINE_TEST_SLALOM_DRIVING_FRAMES (2U * ENGINE_TEST_SLOPE_DRIVING_FRAMES)
#define ENGINE_TEST_SLALOM_APPROACH_SPEED 4096U
#define ENGINE_TEST_WALL_SOUND_FLAG 16U
#define ENGINE_OPPONENT_MAXIMUM_SECONDS 120U
#define ENGINE_OPPONENT_PARK_TILE 10L

enum ENGINE_OPPONENT_TERRAIN { ENGINE_OPPONENT_FLAT, ENGINE_OPPONENT_RAISED };

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

static legacy_u8 grade_height(legacy_u16 row)
{
	return row <= ENGINE_TEST_FIRST_SLOPE_ROW  ? 0U
		   : row >= ENGINE_TEST_LAST_SLOPE_ROW ? CSTRK_MAX_HEIGHT
											   : row - ENGINE_TEST_FIRST_SLOPE_ROW;
}

static void create_track(legacy_int raised_start)
{
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	legacy_char error[ENGINE_TEST_ERROR_CAPACITY];
	assert(cstrk_create(&track, 90U, 90U, error, sizeof(error)));
	assert(cstrk_set_notes(&track, "Long replay integration fixture", 31U, error, sizeof(error)));
	legacy_u16 start_row = raised_start ? ENGINE_TEST_RAISED_START_ROW : ENGINE_TEST_FLAT_START_ROW;
	add_piece(&track, 6U, ENGINE_TEST_LEFT_COLUMN, ENGINE_TEST_TOP_ROW);
	add_piece(&track, 7U, ENGINE_TEST_RIGHT_COLUMN, ENGINE_TEST_TOP_ROW);
	add_piece(&track, 8U, ENGINE_TEST_LEFT_COLUMN, ENGINE_TEST_BOTTOM_ROW);
	add_piece(&track, 9U, ENGINE_TEST_RIGHT_COLUMN, ENGINE_TEST_BOTTOM_ROW);
	for (legacy_u16 column = ENGINE_TEST_LEFT_COLUMN + 1U; column < ENGINE_TEST_RIGHT_COLUMN;
		 column++) {
		add_piece(&track, 5U, column, ENGINE_TEST_TOP_ROW);
		add_piece(&track, 5U, column, ENGINE_TEST_BOTTOM_ROW);
	}
	for (legacy_u16 row = ENGINE_TEST_TOP_ROW + 1U; row < ENGINE_TEST_BOTTOM_ROW; row++) {
		add_piece(&track, row == start_row ? 1U : 4U, ENGINE_TEST_LEFT_COLUMN, row);
		add_piece(&track, 4U, ENGINE_TEST_RIGHT_COLUMN, row);
	}
	if (raised_start) {
		for (legacy_u16 row = 0U; row <= track.height; row++) {
			for (legacy_u16 column = 0U; column <= track.width; column++) {
				track.corner_heights[row * (track.width + 1U) + column] = grade_height(row);
			}
		}
		for (legacy_u32 index = 0U; index < track.object_count; index++) {
			struct CSTRK_OBJECT *object = &track.objects[index];
			object->base_height = grade_height(object->y);
			object->slope_y = (legacy_s8)(grade_height(object->y + 1U) - object->base_height);
		}
	}
	if (!extended_track_activate(&track, error, sizeof(error))) {
		fprintf(stderr, "Track activation: %s\n", error);
		assert(0);
	}
	assert(track_setup() == 0);
	assert(start_finish_column == ENGINE_TEST_LEFT_COLUMN && start_finish_row == start_row);
	cstrk_free(&track);
}

static void test_recording_version_roundtrips(size_t size)
{
	static const legacy_u16 versions[] = {CSRPL_VERSION, ENGINE_TEST_FUTURE_REPLAY_VERSION,
										  LEGACY_U16_MAX};
	static const legacy_u16 recorded_rules[] = {0U, DRIVING_RULE_KNOWN_MASK,
												DRIVING_RULE_POWER_GEAR};
	legacy_u16 original_rules = driving_rules_current();
	legacy_s8 *arguments[] = {
		(legacy_s8 *)"restunts",
		(legacy_s8 *)(original_rules & DRIVING_RULE_POWER_GEAR ? "--pg:off" : "--pg:on")};
	configure_powergear_bug(2, arguments);
	legacy_u16 current_rules = driving_rules_current();
	assert(current_rules == (original_rules ^ DRIVING_RULE_POWER_GEAR));
	FILE *original = fopen(replay_path, "rb");
	assert(original);
	legacy_u8 *bytes = malloc(size);
	assert(bytes && fread(bytes, 1U, size, original) == size);
	assert(fclose(original) == 0);
	/* Embedded track metadata is independent of the recording's revision. */
	LEGACY_WRITE_U16_LE(bytes + CSRPL_HEADER_SIZE + CSTRK_VERSION_OFFSET,
						ENGINE_TEST_FUTURE_TRACK_VERSION);
	const struct GAMEINFO prior_config = gameconfig;
	const struct GAMESTATE prior_state = state;
	const struct EXTENDED_REPLAY_CLOCKS prior_clocks = extended_replay_clocks;
	const legacy_u8 *prior_inputs = extended_replay_inputs();
	const struct CSTRK_TRACK *prior_track = extended_track_model();
	legacy_u16 prior_version = extended_replay_version();
	legacy_u16 prior_rules = extended_replay_driving_rules();
	legacy_u32 prior_frames = extended_replay_recorded_frames();
	for (legacy_u16 old_version = 0; old_version < CSRPL_MIN_VERSION; old_version++) {
		LEGACY_WRITE_U16_LE(bytes + CSRPL_VERSION_OFFSET, old_version);
		FILE *candidate = fopen(invalid_path, "wb");
		assert(candidate && fwrite(bytes, 1U, size, candidate) == size);
		assert(fclose(candidate) == 0);
		assert(extended_file_load_replay((const legacy_s8 *)invalid_path) != 0);
		assert(extended_replay_version() == prior_version);
		assert(extended_replay_driving_rules() == prior_rules);
		assert(driving_rules_current() == current_rules);
		assert(extended_replay_inputs() == prior_inputs);
		assert(extended_replay_recorded_frames() == prior_frames);
		assert(extended_track_model() == prior_track);
		assert(memcmp(&gameconfig, &prior_config, sizeof(gameconfig)) == 0);
		assert(memcmp(&state, &prior_state, sizeof(state)) == 0);
		assert(memcmp(&extended_replay_clocks, &prior_clocks, sizeof(prior_clocks)) == 0);
	}
	for (size_t index = 0; index < sizeof(versions) / sizeof(versions[0]); index++) {
		LEGACY_WRITE_U16_LE(bytes + CSRPL_VERSION_OFFSET, versions[index]);
		LEGACY_WRITE_U16_LE(bytes + CSRPL_DRIVING_RULES_OFFSET, recorded_rules[index]);
		FILE *candidate = fopen(invalid_path, "wb");
		assert(candidate && fwrite(bytes, 1U, size, candidate) == size);
		assert(fclose(candidate) == 0);
		assert(extended_file_load_replay((const legacy_s8 *)invalid_path) == 0);
		assert(extended_replay_version() == versions[index]);
		assert(extended_replay_driving_rules() == recorded_rules[index]);
		assert(driving_rules_current() == current_rules);
		assert(extended_track_model()->version == ENGINE_TEST_FUTURE_TRACK_VERSION);
		assert(extended_file_write_replay((const legacy_s8 *)invalid_path) == 0);
		FILE *resaved = fopen(invalid_path, "rb");
		assert(resaved);
		legacy_u8 *saved_bytes = malloc(size);
		assert(saved_bytes && fread(saved_bytes, 1U, size, resaved) == size);
		assert(fgetc(resaved) == EOF && fclose(resaved) == 0);
		assert(memcmp(saved_bytes, bytes, size) == 0);
		struct CSRPL_VIEW decoded;
		legacy_char error[ENGINE_TEST_ERROR_CAPACITY];
		assert(csrpl_decode(saved_bytes, size, &decoded, error, sizeof(error)));
		assert(decoded.metadata.version == versions[index]);
		assert(decoded.metadata.driving_rules == recorded_rules[index]);
		free(saved_bytes);
	}
	free(bytes);
	extended_replay_runtime_begin();
	assert(extended_replay_driving_rules() == current_rules);
	arguments[1] = (legacy_s8 *)(original_rules & DRIVING_RULE_POWER_GEAR ? "--pg:on" : "--pg:off");
	configure_powergear_bug(2, arguments);
	assert(driving_rules_current() == original_rules);
	/* Recording settings stay frozen after the configured flags change. */
	assert(extended_replay_driving_rules() == current_rules);
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

/* Exercise the real AI steering, speed control and physics on the wide map,
 * with the human car parked clear of the circuit so it cannot block a lap. */
static void test_extended_opponent_laps(void)
{
	static const legacy_u16 rates[] = {GAME_FRAME_RATE_LOW, GAME_FRAME_RATE_NORMAL};
	const struct GAMEINFO saved_config = gameconfig;
	ghost_free_simulation_resources();
	memcpy(gameconfig.game_playercarid, "COUN", CSRPL_CAR_ID_SIZE);
	memcpy(gameconfig.game_opponentcarid, "COUN", CSRPL_CAR_ID_SIZE);
	gameconfig.game_playertransmission = TRANSMISSION_AUTOMATIC;
	gameconfig.game_opponenttransmission = TRANSMISSION_AUTOMATIC;
	for (legacy_u8 terrain = ENGINE_OPPONENT_FLAT; terrain <= ENGINE_OPPONENT_RAISED; terrain++) {
		for (legacy_u8 rate = 0; rate < sizeof(rates) / sizeof(rates[0]); rate++) {
			gameconfig.game_framespersec = rates[rate];
			for (legacy_u8 opponent = OPPONENT_FIRST; opponent <= OPPONENT_LAST; opponent++) {
				gameconfig.game_opponenttype = opponent;
				create_track(terrain == ENGINE_OPPONENT_RAISED);
				ghost_load_simulation_resources();
				extended_replay_runtime_begin();
				initialize_simulation();
				state.game_inputmode = GAME_INPUT_MODE_ACTIVE;
				state.playerstate.car_position.lx =
					ENGINE_OPPONENT_PARK_TILE * CSTRK_TILE_WORLD_SIZE * ENGINE_TEST_POSITION_SCALE;
				state.playerstate.car_position.lz = state.playerstate.car_position.lx;
				state.playerstate.car_position.ly =
					ENGINE_TEST_INITIAL_BODY_HEIGHT * ENGINE_TEST_POSITION_SCALE;
				state.playerstate.car_previous_position = state.playerstate.car_position;
				legacy_u16 frame = 0;
				while (frame < ENGINE_OPPONENT_MAXIMUM_SECONDS * rates[rate] &&
					   state.opponentstate.car_crashBmpFlag == CRASH_EVENT_NONE) {
					assert(extended_replay_record_input(INPUT_NONE));
					update_gamestate();
					frame++;
				}
				if (state.opponentstate.car_crashBmpFlag != CRASH_EVENT_FINISH) {
					fprintf(stderr, "Opponent %u terrain %u at %u Hz: event %u after %u frames\n",
							opponent, terrain, rates[rate], state.opponentstate.car_crashBmpFlag,
							frame);
				}
				assert(state.opponentstate.car_crashBmpFlag == CRASH_EVENT_FINISH);
				assert(state.opponentstate.car_lap_count > 0);
				extended_replay_runtime_clear();
				ghost_free_simulation_resources();
				extended_track_unload();
			}
		}
	}
	gameconfig = saved_config;
	framespersec = gameconfig.game_framespersec;
	ghost_load_simulation_resources();
	puts("Extended opponents: all six finish flat and four-level circuits at 10 and 20 Hz.");
}

/* Rotate the complete four-level climb, including its entry and exit flats.
 * Keeping the fixture beyond the signed-word boundary also exercises the
 * extended world's wrapped collision coordinates in every direction. */
static void create_rotated_slope_track(legacy_u8 rotation, legacy_u8 surface,
									   legacy_u16 slope_stunt)
{
	create_track(1);
	const struct CSTRK_TRACK *source = extended_track_model();
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	legacy_char error[ENGINE_TEST_ERROR_CAPACITY];
	assert(cstrk_create(&track, source->width, source->height, error, sizeof(error)));
	for (legacy_u16 row = 0; row <= source->height; row++) {
		for (legacy_u16 column = 0; column <= source->width; column++) {
			legacy_u16 x = column, y = row;
			for (legacy_u8 turn = 0; turn < rotation; turn++) {
				legacy_u16 previous_x = x;
				x = source->width - y;
				y = previous_x;
			}
			track.corner_heights[y * (track.width + 1U) + x] =
				source->corner_heights[row * (source->width + 1U) + column];
		}
	}
	for (legacy_u32 index = 0; index < source->object_count; index++) {
		struct CSTRK_OBJECT object = source->objects[index];
		if (slope_stunt != ENGINE_TEST_SLOPE_STUNT_NONE && (object.slope_x || object.slope_y)) {
			object.legacy_id = slope_stunt;
		}
		for (legacy_u8 turn = 0; turn < rotation; turn++) {
			legacy_u8 previous_x = object.x;
			object.x = source->width - 1U - object.y;
			object.y = previous_x;
		}
		object.rotation = (object.rotation + rotation) % ENGINE_TEST_SLOPE_ROTATIONS;
		object.surface = surface;
		legacy_u32 corner = object.y * (track.width + 1U) + object.x;
		object.base_height = track.corner_heights[corner];
		object.slope_x = track.corner_heights[corner + 1U] - object.base_height;
		object.slope_y = track.corner_heights[corner + track.width + 1U] - object.base_height;
		assert(cstrk_add_object(&track, &object, error, sizeof(error)));
	}
	assert(extended_track_activate(&track, error, sizeof(error)));
	assert(track_setup() == 0);
	cstrk_free(&track);
}

static legacy_s32 slope_driving_progress(const struct CARSTATE *car, legacy_u8 rotation)
{
	legacy_s32 x = car->car_position.lx / ENGINE_TEST_POSITION_SCALE;
	legacy_s32 z = car->car_position.lz / ENGINE_TEST_POSITION_SCALE;
	legacy_s32 size = extended_track_width() * CSTRK_TILE_WORLD_SIZE;
	for (legacy_u8 turn = 0; turn < rotation; turn++) {
		legacy_s32 previous_x = x;
		x = size - z;
		z = previous_x;
	}
	return (extended_track_height() - ENGINE_TEST_FIRST_SLOPE_ROW) * CSTRK_TILE_WORLD_SIZE - z;
}

static void initialize_slope_driver(legacy_u8 rotation, legacy_u8 uphill)
{
	extended_replay_runtime_begin();
	initialize_simulation();
	if (!uphill) {
		return;
	}
	legacy_s32 x = ENGINE_TEST_LEFT_COLUMN * CSTRK_TILE_WORLD_SIZE + CSTRK_TILE_WORLD_SIZE / 2;
	legacy_s32 z = (extended_track_height() - ENGINE_TEST_SLOPE_START_ROW) * CSTRK_TILE_WORLD_SIZE -
				   CSTRK_TILE_WORLD_SIZE / 2;
	legacy_s32 size = extended_track_width() * CSTRK_TILE_WORLD_SIZE;
	for (legacy_u8 turn = 0; turn < rotation; turn++) {
		legacy_s32 previous_x = x;
		x = z;
		z = size - previous_x;
	}
	init_carstate_from_simd(&state.playerstate, &simd_player, TRANSMISSION_AUTOMATIC,
							x * ENGINE_TEST_POSITION_SCALE, 0, z * ENGINE_TEST_POSITION_SCALE,
							(state.playerstate.car_rotate.x + ANGLE_HALF_TURN) & ANGLE_MASK);
}

static void test_smooth_consecutive_slopes(legacy_u16 slope_stunt)
{
	static const legacy_u16 rates[] = {GAME_FRAME_RATE_LOW, GAME_FRAME_RATE_NORMAL};
	legacy_u16 original_rate = gameconfig.game_framespersec;
	for (legacy_u8 rate = 0; rate < sizeof(rates) / sizeof(rates[0]); rate++) {
		gameconfig.game_framespersec = rates[rate];
		legacy_s16 maximum_pitch_step =
			ENGINE_TEST_SLOPE_MAXIMUM_PITCH_STEP * GAME_FRAME_RATE_NORMAL / rates[rate];
		for (legacy_u8 surface = 0; surface < ENGINE_TEST_SLOPE_SURFACES; surface++) {
			for (legacy_u8 rotation = 0; rotation < ENGINE_TEST_SLOPE_ROTATIONS; rotation++) {
				for (legacy_u8 uphill = 0; uphill <= 1U; uphill++) {
					create_rotated_slope_track(rotation, surface, slope_stunt);
					initialize_slope_driver(rotation, uphill);
					legacy_u16 samples = 0;
					legacy_s16 previous_pitch = 0;
					legacy_u8 exited_slope = 0;
					legacy_u8 crest_airborne = 0;
					for (legacy_u16 frame = 0; frame < ENGINE_TEST_SLOPE_DRIVING_FRAMES *
														   rates[rate] / GAME_FRAME_RATE_NORMAL;
						 frame++) {
						assert(extended_replay_record_input(INPUT_ACCELERATE_FLAG));
						update_gamestate();
						const struct CARSTATE *car = &state.playerstate;
						assert(car->car_crashBmpFlag == CRASH_EVENT_NONE);
						legacy_s32 progress = slope_driving_progress(car, rotation);
						if ((uphill && progress > ENGINE_TEST_SLOPE_END_PROGRESS) ||
							(!uphill && progress < 0)) {
							exited_slope = 1;
						}
						if (uphill && progress > ENGINE_TEST_SLOPE_END_PROGRESS &&
							car->car_sumSurfAllWheels == CAR_WHEEL_CONTACT_NONE) {
							crest_airborne = 1;
						}

						if (progress < ENGINE_TEST_SLOPE_MINIMUM_PROGRESS ||
							progress > ENGINE_TEST_SLOPE_MAXIMUM_PROGRESS) {
							continue;
						}
						/* Entry/exit slope changes legitimately pitch the car. Inside this
						 * uninterrupted grade, a tile boundary must not add a suspension kick.
						 * One angle unit per 20 Hz tick permits fixed-point pose quantization. */
						if (samples != 0) {
							legacy_s16 pitch_step = car->car_rotate.y - previous_pitch;
							if (pitch_step < -maximum_pitch_step ||
								pitch_step > maximum_pitch_step) {
								fprintf(stderr,
										"Slope pitch step %d at rate %u, surface %u, rotation %u, "
										"uphill %u, frame %u, progress %d\n",
										pitch_step, rates[rate], surface, rotation, uphill, frame,
										progress);
							}

							assert(pitch_step >= -maximum_pitch_step &&
								   pitch_step <= maximum_pitch_step);
						}
						for (legacy_u8 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
							assert(car->car_surfaceWhl[wheel] == surface + CAR_SURFACE_PAVED);
						}
						previous_pitch = car->car_rotate.y;
						samples++;
					}
					assert(samples >= ENGINE_TEST_SLOPE_MINIMUM_SAMPLES);
					assert(exited_slope);
					assert(!uphill || crest_airborne);
				}
			}
		}
	}
	gameconfig.game_framespersec = original_rate;
	framespersec = original_rate;
	extended_replay_runtime_clear();
	extended_track_unload();
}

#define ENGINE_ROLL_RAMP_ROW 43U
#define ENGINE_ROLL_APPROACH_ROW 41U
#define ENGINE_ROLL_TOP_ROW 38U
#define ENGINE_ROLL_BOTTOM_ROW 55U
#define ENGINE_ROLL_LANDING_ROW 48U
#define ENGINE_ROLL_TARGET_SPEED (100U * 256U)
#define ENGINE_ROLL_APPROACH_GEAR 4U
#define ENGINE_ROLL_MAXIMUM_FRAMES 360U
#define ENGINE_ROLL_UPRIGHT_TOLERANCE (ANGLE_FULL_TURN / 64)

static void create_roll_ramp_track(legacy_u8 rotation, legacy_u8 ground_surface)
{
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	legacy_char error[ENGINE_TEST_ERROR_CAPACITY];
	assert(cstrk_create(&track, CSTRK_MAX_DIMENSION, CSTRK_MAX_DIMENSION, error, sizeof(error)));
	for (legacy_u32 cell = 0; cell < (legacy_u32)track.width * track.height; cell++) {
		track.cells[cell].surface = ground_surface;
	}
	add_piece(&track, 6U, ENGINE_TEST_LEFT_COLUMN, ENGINE_ROLL_TOP_ROW);
	add_piece(&track, 7U, ENGINE_TEST_RIGHT_COLUMN, ENGINE_ROLL_TOP_ROW);
	add_piece(&track, 8U, ENGINE_TEST_LEFT_COLUMN, ENGINE_ROLL_BOTTOM_ROW);
	add_piece(&track, 9U, ENGINE_TEST_RIGHT_COLUMN, ENGINE_ROLL_BOTTOM_ROW);
	for (legacy_u16 x = ENGINE_TEST_LEFT_COLUMN + 1U; x < ENGINE_TEST_RIGHT_COLUMN; x++) {
		add_piece(&track, 5U, x, ENGINE_ROLL_TOP_ROW);
		add_piece(&track, 5U, x, ENGINE_ROLL_BOTTOM_ROW);
	}
	for (legacy_u16 y = ENGINE_ROLL_TOP_ROW + 1U; y < ENGINE_ROLL_BOTTOM_ROW; y++) {
		add_piece(&track, 4U, ENGINE_TEST_RIGHT_COLUMN, y);
		if (y < ENGINE_ROLL_RAMP_ROW || y >= ENGINE_ROLL_LANDING_ROW) {
			add_piece(&track, y == ENGINE_ROLL_APPROACH_ROW - 1U ? 179U : 4U,
					  ENGINE_TEST_LEFT_COLUMN, y);
		}
	}
	struct CSTRK_OBJECT object = {0};
	object.legacy_id = CSTRK_ROLL_RAMP_FIRST_ID;
	object.x = ENGINE_TEST_LEFT_COLUMN;
	object.y = ENGINE_ROLL_RAMP_ROW;
	object.width = 1;
	object.height = 2;
	object.occupied_mask = 3;
	object.roll_ramp = 1;
	assert(cstrk_add_object(&track, &object, error, sizeof(error)));
	for (legacy_u32 index = 0; index < track.object_count; index++) {
		struct CSTRK_OBJECT *piece = &track.objects[index];
		for (legacy_u8 turn = 0; turn < rotation; turn++) {
			legacy_u8 previous_x = piece->x, previous_width = piece->width;
			piece->x = track.width - piece->y - piece->height;
			piece->y = previous_x;
			piece->width = piece->height;
			piece->height = previous_width;
		}
		piece->rotation = (piece->rotation + rotation) % ENGINE_TEST_SLOPE_ROTATIONS;
	}
	assert(extended_track_activate(&track, error, sizeof(error)));
	assert(track_setup() == 0);
	cstrk_free(&track);
}

static void initialize_roll_driver(legacy_u8 rotation, legacy_u16 speed, legacy_u16 row)
{
	extended_replay_runtime_begin();
	initialize_simulation();
	legacy_s32 x = ENGINE_TEST_LEFT_COLUMN * CSTRK_TILE_WORLD_SIZE + CSTRK_TILE_WORLD_SIZE / 2;
	legacy_s32 z =
		(extended_track_height() - row) * CSTRK_TILE_WORLD_SIZE - CSTRK_TILE_WORLD_SIZE / 2;
	legacy_s32 size = extended_track_width() * CSTRK_TILE_WORLD_SIZE;
	for (legacy_u8 turn = 0; turn < rotation; turn++) {
		legacy_s32 previous_x = x;
		x = z;
		z = size - previous_x;
	}
	init_carstate_from_simd(&state.playerstate, &simd_player, TRANSMISSION_AUTOMATIC,
							x * ENGINE_TEST_POSITION_SCALE, 0, z * ENGINE_TEST_POSITION_SCALE,
							(ANGLE_HALF_TURN - rotation * ANGLE_QUARTER_TURN) & ANGLE_MASK);
	struct CARSTATE *car = &state.playerstate;
	car->car_actual_speed = car->car_rev_speed = speed;
	car->car_lastspeed = speed;
	car->car_current_gear = ENGINE_ROLL_APPROACH_GEAR;
	car->car_gearratio = simd_player.gear_ratios[ENGINE_ROLL_APPROACH_GEAR];
	car->car_gearratioshr8 = car->car_gearratio >> LEGACY_BYTE_BITS;
}

static legacy_s32 roll_driver_progress(const struct CARSTATE *car, legacy_u8 rotation)
{
	legacy_s32 x = car->car_position.lx / ENGINE_TEST_POSITION_SCALE;
	legacy_s32 z = car->car_position.lz / ENGINE_TEST_POSITION_SCALE;
	legacy_s32 size = extended_track_width() * CSTRK_TILE_WORLD_SIZE;
	for (legacy_u8 turn = 0; turn < rotation; turn++) {
		legacy_s32 previous_x = x;
		x = size - z;
		z = previous_x;
	}
	return (extended_track_height() - ENGINE_ROLL_RAMP_ROW) * CSTRK_TILE_WORLD_SIZE - z;
}

static void test_roll_ramp_flight(legacy_u16 rate, legacy_u8 rotation)
{
	legacy_u16 saved_rate = gameconfig.game_framespersec;
	legacy_s16 saved_owoot = owoot_enabled;
	owoot_enabled = 1;
	gameconfig.game_framespersec = rate;
	create_roll_ramp_track(rotation, CSTRK_TERRAIN_GRASS);
	initialize_roll_driver(rotation, ENGINE_ROLL_TARGET_SPEED, ENGINE_ROLL_APPROACH_ROW);
	assert(extended_replay_driving_rules() & DRIVING_RULE_OWOOT);
	struct CARSTATE *car = &state.playerstate;
	legacy_s16 previous_roll = car->car_rotate.z;
	legacy_s32 total_roll = 0, maximum_air_roll = 0;
	legacy_u8 launched = 0, landed = 0, side = 0, inverted = 0, upright_in_air = 0;
	legacy_u16 landing_frame = 0;
	legacy_u32 airborne_frame = 0;
	struct GAMESTATE airborne_state;
	legacy_s8 airborne_seed[GAMESTATE_RANDOM_SEED_SIZE];
	for (legacy_u16 frame = 0; frame < ENGINE_ROLL_MAXIMUM_FRAMES * rate / GAME_FRAME_RATE_NORMAL;
		 frame++) {
		legacy_u8 input = frame == 0 || car->car_actual_speed < ENGINE_ROLL_TARGET_SPEED
							  ? INPUT_ACCELERATE_FLAG
							  : INPUT_NONE;
		assert(extended_replay_record_input(input));
		update_gamestate();
		assert(owoot_enabled && state.game_inputmode == GAME_INPUT_MODE_ACTIVE);
		legacy_s16 delta =
			(legacy_s16)(((legacy_u16)(car->car_rotate.z - previous_roll + ANGLE_HALF_TURN) &
						  ANGLE_MASK) -
						 ANGLE_HALF_TURN);
		total_roll += delta;
		previous_roll = car->car_rotate.z;
		if (car->car_crashBmpFlag != CRASH_EVENT_NONE) {
			fprintf(stderr, "Roll ramp flight rate %u rotation %u crashes at frame %u, roll %d\n",
					rate, rotation, frame, (legacy_int)total_roll);
		}
		assert(car->car_crashBmpFlag == CRASH_EVENT_NONE);
		if (roll_driver_progress(car, rotation) >= ROLL_RAMP_LENGTH &&
			car->car_sumSurfAllWheels == CAR_WHEEL_CONTACT_NONE) {
			launched = 1;
			side |= total_roll >= ANGLE_QUARTER_TURN;
			inverted |= total_roll >= ANGLE_HALF_TURN;
			upright_in_air |= total_roll >= ANGLE_FULL_TURN - ENGINE_ROLL_UPRIGHT_TOLERANCE;
			if (total_roll > maximum_air_roll) {
				maximum_air_roll = total_roll;
			}
			if (!airborne_frame && inverted) {
				airborne_frame = extended_replay_frame();
				airborne_state = state;
				get_kevinrandom_seed(airborne_seed);
			}
		} else if (launched && car->car_sumSurfAllWheels != CAR_WHEEL_CONTACT_NONE) {
			if (!landed) {
				landed = 1;
				landing_frame = frame;
			}
			if (frame >= landing_frame + rate) {
				break;
			}
		}
	}
	if (!upright_in_air) {
		fprintf(stderr, "Roll ramp rate %u rotation %u reaches only %d angle units\n", rate,
				rotation, (legacy_int)maximum_air_roll);
	}
	assert(launched && landed && side && inverted && upright_in_air);
	assert(total_roll >= ANGLE_FULL_TURN - ENGINE_ROLL_UPRIGHT_TOLERANCE &&
		   total_roll <= ANGLE_FULL_TURN + ENGINE_ROLL_UPRIGHT_TOLERANCE);
	assert(car->car_rotate.y >= -ENGINE_ROLL_UPRIGHT_TOLERANCE &&
		   car->car_rotate.y <= ENGINE_ROLL_UPRIGHT_TOLERANCE);
	for (legacy_u8 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
		assert(car->car_surfaceWhl[wheel] == CAR_SURFACE_PAVED);
	}
	struct GAMESTATE expected = state;
	legacy_s8 expected_seed[GAMESTATE_RANDOM_SEED_SIZE];
	get_kevinrandom_seed(expected_seed);
	legacy_u32 final_frame = extended_replay_frame();
	assert(airborne_frame > 0 && extended_replay_seek(airborne_frame));
	assert_same_simulation(&airborne_state, airborne_seed);
	assert(extended_replay_seek(final_frame));
	assert_same_simulation(&expected, expected_seed);
	printf(
		"Roll ramp: %u Hz rotation %u passes OWOOT with a free-air roll and upright landing at 100 "
		"mph.\n",
		rate, rotation);
	gameconfig.game_framespersec = saved_rate;
	framespersec = saved_rate;
	owoot_enabled = saved_owoot;
	extended_replay_runtime_clear();
	extended_track_unload();
}

static void test_terrain_below_roll_ramp_does_not_arm(void)
{
	static const legacy_u8 surfaces[] = {CSTRK_TERRAIN_DIRT, CSTRK_TERRAIN_ICE};
	for (legacy_u8 surface = 0; surface < sizeof(surfaces) / sizeof(surfaces[0]); surface++) {
		create_roll_ramp_track(0, surfaces[surface]);
		initialize_roll_driver(0, 0, ENGINE_ROLL_RAMP_ROW + 1U);
		for (legacy_u8 frame = 0; frame < 3U; frame++) {
			assert(extended_replay_record_input(INPUT_BRAKE_FLAG));
			update_gamestate();
			assert(state.playerstate.car_crashBmpFlag == CRASH_EVENT_NONE);
			assert(state.playerstate.car_rotate.z == 0);
			for (legacy_u8 word = 0; word < CARSTATE_WHEEL_COUNT; word++) {
				assert(state.playerstate.car_reserved_contact_state[word] == 0);
			}
		}
	}
	extended_replay_runtime_clear();
	extended_track_unload();
}

static void test_sloped_slalom_impacts(void)
{
	static const legacy_u16 rates[] = {GAME_FRAME_RATE_LOW, GAME_FRAME_RATE_NORMAL};
	legacy_u16 original_rate = gameconfig.game_framespersec;
	legacy_u16 original_collision = legacy_collision_is_enabled();
	for (legacy_u8 legacy_collision = 0; legacy_collision <= 1U; legacy_collision++) {
		legacy_s8 *arguments[] = {(legacy_s8 *)"restunts",
								  (legacy_s8 *)(legacy_collision ? "--lc:on" : "--lc:off")};
		configure_legacy_collision((legacy_s16)(sizeof(arguments) / sizeof(arguments[0])),
								   arguments);
		for (legacy_u8 rate = 0; rate < sizeof(rates) / sizeof(rates[0]); rate++) {
			gameconfig.game_framespersec = rates[rate];
			for (legacy_u8 rotation = 0; rotation < ENGINE_TEST_SLOPE_ROTATIONS; rotation++) {
				for (legacy_u8 uphill = 0; uphill <= 1U; uphill++) {
					create_rotated_slope_track(rotation, CSTRK_ROAD_PAVED, CSTRK_SLALOM_FIRST_ID);
					initialize_slope_driver(rotation, uphill);
					/* The stock P962 fits between the center-line barrier edges. Aim
					 * its center at one barrier so an unsteered pass must hit it. */
					struct CARSTATE *driver = &state.playerstate;
					legacy_s32 x = driver->car_position.lx / ENGINE_TEST_POSITION_SCALE;
					legacy_s32 z = driver->car_position.lz / ENGINE_TEST_POSITION_SCALE;
					legacy_s32 size = extended_track_width() * CSTRK_TILE_WORLD_SIZE;
					for (legacy_u8 turn = 0; turn < rotation; turn++) {
						legacy_s32 previous_x = x;
						x = size - z;
						z = previous_x;
					}
					x = ENGINE_TEST_LEFT_COLUMN * CSTRK_TILE_WORLD_SIZE +
						CSTRK_TILE_WORLD_SIZE / 2 + ENGINE_TEST_SLALOM_LATERAL_OFFSET;
					for (legacy_u8 turn = 0; turn < rotation; turn++) {
						legacy_s32 previous_x = x;
						x = z;
						z = size - previous_x;
					}
					init_carstate_from_simd(driver, &simd_player, TRANSMISSION_AUTOMATIC,
											x * ENGINE_TEST_POSITION_SCALE,
											driver->car_position.ly -
												ENGINE_TEST_INITIAL_BODY_HEIGHT *
													ENGINE_TEST_POSITION_SCALE,
											z * ENGINE_TEST_POSITION_SCALE, driver->car_rotate.x);
					legacy_u8 hit = 0;
					for (legacy_u16 frame = 0; frame < ENGINE_TEST_SLALOM_DRIVING_FRAMES *
														   rates[rate] / GAME_FRAME_RATE_NORMAL;
						 frame++) {
						/* Keep the approach slower than the thin barrier's depth per tick;
						 * intentional legacy collision bugs can skip it at racing speed. */
						driver->car_actual_speed = ENGINE_TEST_SLALOM_APPROACH_SPEED;
						driver->car_rev_speed = ENGINE_TEST_SLALOM_APPROACH_SPEED;
						assert(extended_replay_record_input(INPUT_ACCELERATE_FLAG));
						update_gamestate();
						const struct CARSTATE *car = &state.playerstate;
						legacy_s32 progress = slope_driving_progress(car, rotation);
						if (car->car_crashBmpFlag != CRASH_EVENT_NONE ||
							(car->car_sound_flags & ENGINE_TEST_WALL_SOUND_FLAG) != 0) {
							/* Allow the natural jump from the downhill entry plateau. Once
							 * grounded, the car must hit barriers at the hillside elevation. */
							assert(progress > 0 && progress < ENGINE_TEST_SLOPE_END_PROGRESS);
							assert(uphill ? progress < ENGINE_TEST_SLOPE_IMPACT_PROGRESS
										  : progress > ENGINE_TEST_SLOPE_IMPACT_PROGRESS);
							hit = 1;
							break;
						}
						if ((uphill && progress > ENGINE_TEST_SLOPE_IMPACT_PROGRESS) ||
							(!uphill && progress < ENGINE_TEST_SLOPE_IMPACT_PROGRESS)) {
							break;
						}
					}
					if (!hit) {
						fprintf(stderr,
								"Missed sloped slalom at rate %u, rotation %u, uphill %u, "
								"legacy collision %u\n",
								rates[rate], rotation, uphill, legacy_collision);
					}
					assert(hit);
				}
			}
		}
	}
	legacy_s8 *arguments[] = {(legacy_s8 *)"restunts",
							  (legacy_s8 *)(original_collision ? "--lc:on" : "--lc:off")};
	configure_legacy_collision((legacy_s16)(sizeof(arguments) / sizeof(arguments[0])), arguments);
	gameconfig.game_framespersec = original_rate;
	framespersec = original_rate;
	extended_replay_runtime_clear();
	extended_track_unload();
}

#define ENGINE_PIPE_MAP_SIZE 30U
#define ENGINE_PIPE_LEFT_COLUMN 6U
#define ENGINE_PIPE_RIGHT_COLUMN 12U
#define ENGINE_PIPE_TOP_ROW 12U
#define ENGINE_PIPE_BOTTOM_ROW 18U
#define ENGINE_PIPE_START_ROW 17U
#define ENGINE_PIPE_APPROACH_ROW 16U
#define ENGINE_PIPE_ENTRANCE_ROW 15U
#define ENGINE_PIPE_CENTER_ROW 14U
#define ENGINE_PIPE_EXIT_ROW 13U
#define ENGINE_PIPE_HALF_PIPE_ID 83U
#define ENGINE_PIPE_ENTRANCE_ID 70U
#define ENGINE_PIPE_EXIT_ID 71U
#define ENGINE_PIPE_CLEAR_LATERAL_OFFSET (-80)
#define ENGINE_PIPE_APPROACH_SPEED 4096U
#define ENGINE_PIPE_TEST_FRAMES 200U
#define ENGINE_PIPE_MAXIMUM_ROLL ANGLE_QUARTER_TURN

static legacy_u8 pipe_test_height(legacy_u16 row)
{
	return row <= ENGINE_PIPE_EXIT_ROW		 ? ENGINE_PIPE_APPROACH_ROW - ENGINE_PIPE_EXIT_ROW
		   : row >= ENGINE_PIPE_APPROACH_ROW ? 0U
											 : ENGINE_PIPE_APPROACH_ROW - row;
}

static void create_half_pipe_test_track(legacy_u8 graded)
{
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	legacy_char error[ENGINE_TEST_ERROR_CAPACITY];
	assert(cstrk_create(&track, ENGINE_PIPE_MAP_SIZE, ENGINE_PIPE_MAP_SIZE, error, sizeof(error)));
	add_piece(&track, 6U, ENGINE_PIPE_LEFT_COLUMN, ENGINE_PIPE_TOP_ROW);
	add_piece(&track, 7U, ENGINE_PIPE_RIGHT_COLUMN, ENGINE_PIPE_TOP_ROW);
	add_piece(&track, 8U, ENGINE_PIPE_LEFT_COLUMN, ENGINE_PIPE_BOTTOM_ROW);
	add_piece(&track, 9U, ENGINE_PIPE_RIGHT_COLUMN, ENGINE_PIPE_BOTTOM_ROW);
	for (legacy_u16 x = ENGINE_PIPE_LEFT_COLUMN + 1U; x < ENGINE_PIPE_RIGHT_COLUMN; x++) {
		add_piece(&track, 5U, x, ENGINE_PIPE_TOP_ROW);
		add_piece(&track, 5U, x, ENGINE_PIPE_BOTTOM_ROW);
	}
	for (legacy_u16 row = ENGINE_PIPE_TOP_ROW + 1U; row < ENGINE_PIPE_BOTTOM_ROW; row++) {
		legacy_u16 id = row == ENGINE_PIPE_START_ROW	  ? 1U
						: row == ENGINE_PIPE_ENTRANCE_ROW ? ENGINE_PIPE_ENTRANCE_ID
						: row == ENGINE_PIPE_CENTER_ROW	  ? ENGINE_PIPE_HALF_PIPE_ID
						: row == ENGINE_PIPE_EXIT_ROW	  ? ENGINE_PIPE_EXIT_ID
														  : 4U;
		add_piece(&track, id, ENGINE_PIPE_LEFT_COLUMN, row);
		add_piece(&track, 4U, ENGINE_PIPE_RIGHT_COLUMN, row);
	}
	if (graded) {
		for (legacy_u16 row = 0; row <= track.height; row++) {
			for (legacy_u16 column = 0; column <= track.width; column++) {
				track.corner_heights[row * (track.width + 1U) + column] = pipe_test_height(row);
			}
		}
		for (legacy_u32 index = 0; index < track.object_count; index++) {
			struct CSTRK_OBJECT *object = &track.objects[index];
			object->base_height = pipe_test_height(object->y);
			object->slope_y = pipe_test_height(object->y + 1U) - object->base_height;
		}
	}
	assert(extended_track_activate(&track, error, sizeof(error)));
	assert(track_setup() == 0);
	cstrk_free(&track);
}

static void test_mini_half_pipe_on_straight_slope(void)
{
	legacy_s8 original_car[CSRPL_CAR_ID_SIZE];
	memcpy(original_car, gameconfig.game_playercarid, sizeof(original_car));
	ghost_free_simulation_resources();
	memcpy(gameconfig.game_playercarid, "PMIN", sizeof(original_car));
	ghost_load_simulation_resources();
	legacy_s16 original_owoot = owoot_enabled;
	legacy_u8 original_adhesion = car_surface_adhesion[PLAYER_CAR_INDEX];
	owoot_enabled = 0;
	car_surface_adhesion[PLAYER_CAR_INDEX] = 0;
	legacy_u16 original_rate = gameconfig.game_framespersec;
	gameconfig.game_framespersec = GAME_FRAME_RATE_NORMAL;
	for (legacy_u8 graded = 0; graded <= 1U; graded++) {
		for (legacy_u8 center_line = 0; center_line <= 1U; center_line++) {
			create_half_pipe_test_track(graded);
			extended_replay_runtime_begin();
			initialize_simulation();
			legacy_s32 x = ENGINE_PIPE_LEFT_COLUMN * CSTRK_TILE_WORLD_SIZE +
						   CSTRK_TILE_WORLD_SIZE / 2 +
						   (center_line ? 0 : ENGINE_PIPE_CLEAR_LATERAL_OFFSET);
			legacy_s32 z =
				(ENGINE_PIPE_MAP_SIZE - ENGINE_PIPE_APPROACH_ROW) * CSTRK_TILE_WORLD_SIZE -
				CSTRK_TILE_WORLD_SIZE / 2;
			init_carstate_from_simd(&state.playerstate, &simd_player, TRANSMISSION_AUTOMATIC,
									x * ENGINE_TEST_POSITION_SCALE, 0,
									z * ENGINE_TEST_POSITION_SCALE, 0);
			state.playerstate.car_actual_speed = ENGINE_PIPE_APPROACH_SPEED;
			state.playerstate.car_rev_speed = ENGINE_PIPE_APPROACH_SPEED;
			legacy_u8 passed = 0, hit = 0;
			for (legacy_u16 frame = 0; frame < ENGINE_PIPE_TEST_FRAMES; frame++) {
				assert(extended_replay_record_input(INPUT_ACCELERATE_FLAG));
				update_gamestate();
				const struct CARSTATE *car = &state.playerstate;
				if (car->car_crashBmpFlag != CRASH_EVENT_NONE) {
					assert(center_line);
					legacy_s32 row =
						ENGINE_PIPE_MAP_SIZE - 1U -
						car->car_position.lz / (ENGINE_TEST_POSITION_SCALE * CSTRK_TILE_WORLD_SIZE);
					assert(row == ENGINE_PIPE_CENTER_ROW);
					hit = 1;
					break;
				}
				assert(car->car_rotate.z > -ENGINE_PIPE_MAXIMUM_ROLL &&
					   car->car_rotate.z < ENGINE_PIPE_MAXIMUM_ROLL);
				if (car->car_position.lz >= (ENGINE_PIPE_MAP_SIZE - ENGINE_PIPE_EXIT_ROW) *
												CSTRK_TILE_WORLD_SIZE *
												ENGINE_TEST_POSITION_SCALE) {
					passed = 1;
					break;
				}
			}
			/* The original half-pipe has a central lip. Its side route must remain
			 * driveable upright, and its central wall must remain solid on a hill. */
			assert(center_line ? hit && !passed : passed && !hit);
		}
	}
	owoot_enabled = original_owoot;
	car_surface_adhesion[PLAYER_CAR_INDEX] = original_adhesion;
	gameconfig.game_framespersec = original_rate;
	framespersec = original_rate;
	ghost_free_simulation_resources();
	memcpy(gameconfig.game_playercarid, original_car, sizeof(original_car));
	ghost_load_simulation_resources();
}

#define ENGINE_HALF_PIPE_REPLAY_VERSION 7U
#define ENGINE_HALF_PIPE_REPLAY_FRAMES 1178U
#define ENGINE_HALF_PIPE_REPLAY_CRASH_FRAME 1097U

static void test_recorded_half_pipe_body_collision(void)
{
	FILE *fixture = fopen(RESTUNTS_HALF_PIPE_REPLAY_FIXTURE, "rb");
	assert(fixture != NULL);
	assert(fseek(fixture, 0, SEEK_END) == 0);
	legacy_s32 length = (legacy_s32)ftell(fixture);
	assert(length > (legacy_s32)CSRPL_HEADER_SIZE);
	assert(fseek(fixture, 0, SEEK_SET) == 0);
	legacy_u8 *bytes = malloc((size_t)length);
	assert(bytes != NULL);
	assert(fread(bytes, 1U, (size_t)length, fixture) == (size_t)length);
	assert(fclose(fixture) == 0);
	assert(LEGACY_READ_U16_LE(bytes + CSRPL_VERSION_OFFSET) == ENGINE_HALF_PIPE_REPLAY_VERSION);
	/* Keep the recorded fixture unchanged. Only its in-memory admission header
	 * advances; the embedded track and every original input remain identical. */
	LEGACY_WRITE_U16_LE(bytes + CSRPL_VERSION_OFFSET, CSRPL_VERSION);
	struct CSRPL_VIEW view;
	legacy_char error[ENGINE_TEST_ERROR_CAPACITY];
	assert(csrpl_decode(bytes, (size_t)length, &view, error, sizeof(error)));
	assert(view.metadata.frame_count == ENGINE_HALF_PIPE_REPLAY_FRAMES);
	assert(view.metadata.frames_per_second == GAME_FRAME_RATE_NORMAL);
	assert(memcmp(view.metadata.player_car_id, "PMIN", CSRPL_CAR_ID_SIZE) == 0);
	assert(view.metadata.driving_rules == driving_rules_current());
	struct GAMEINFO original_config = gameconfig;
	ghost_free_simulation_resources();
	memcpy(gameconfig.game_playercarid, view.metadata.player_car_id, CSRPL_CAR_ID_SIZE);
	memcpy(gameconfig.game_opponentcarid, view.metadata.opponent_car_id, CSRPL_CAR_ID_SIZE);
	memcpy(gameconfig.game_trackname, view.metadata.track_name, CSRPL_TRACK_NAME_SIZE);
	gameconfig.game_trackname[CSRPL_TRACK_NAME_SIZE - 1U] = 0;
	gameconfig.game_playermaterial = view.metadata.player_material;
	gameconfig.game_playertransmission = view.metadata.player_transmission;
	gameconfig.game_opponenttype = view.metadata.opponent_type;
	gameconfig.game_opponentmaterial = view.metadata.opponent_material;
	gameconfig.game_opponenttransmission = view.metadata.opponent_transmission;
	gameconfig.game_framespersec = view.metadata.frames_per_second;
	assert(extended_track_load(view.track_data, view.track_size, error, sizeof(error)));
	assert(track_setup() == 0);
	ghost_load_simulation_resources();
	assert(extended_replay_runtime_load(&view));
	free(bytes);
	initialize_simulation();
	for (legacy_u32 frame = 0; frame < ENGINE_HALF_PIPE_REPLAY_FRAMES; frame++) {
		update_gamestate();
		assert(state.playerstate.car_crashBmpFlag == CRASH_EVENT_NONE);
		if (frame == ENGINE_HALF_PIPE_REPLAY_CRASH_FRAME) {
			assert(state.playerstate.car_actual_speed > 0);
		}
	}
	assert(extended_replay_frame() == ENGINE_HALF_PIPE_REPLAY_FRAMES);
	assert(state.playerstate.car_crashBmpFlag == CRASH_EVENT_NONE);
	ghost_free_simulation_resources();
	gameconfig = original_config;
	ghost_load_simulation_resources();
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
	assert(LEGACY_READ_U16_LE(bytes + CSRPL_VERSION_OFFSET) == CSRPL_VERSION);
	assert(LEGACY_READ_U16_LE(bytes + CSRPL_DRIVING_RULES_OFFSET) == driving_rules_current());
	legacy_u16 recorded_driving_rules = extended_replay_driving_rules();
	assert(recorded_driving_rules == driving_rules_current());
	assert(extended_replay_version() == CSRPL_VERSION);
	assert(fclose(saved) == 0);
	/* An invalid load must preserve the currently usable track and recording. */
	LEGACY_WRITE_U16_LE(bytes + CSRPL_VERSION_OFFSET, ENGINE_TEST_FUTURE_REPLAY_VERSION);
	LEGACY_WRITE_U16_LE(bytes + CSRPL_DRIVING_RULES_OFFSET,
						recorded_driving_rules ^ DRIVING_RULE_KNOWN_MASK);
	bytes[20] ^= 1U;
	FILE *invalid = fopen(invalid_path, "wb");
	assert(invalid && fwrite(bytes, 1U, (size_t)saved_size, invalid) == (size_t)saved_size);
	assert(fclose(invalid) == 0);
	assert(file_load_replay_format((const legacy_s8 *)temporary_directory, "BAD", ".csrpl") != 0);
	assert(extended_replay_frame() == ENGINE_TEST_FRAMES);
	assert(extended_replay_recorded_frames() == ENGINE_TEST_FRAMES);
	assert(extended_replay_version() == CSRPL_VERSION);
	assert(extended_replay_driving_rules() == recorded_driving_rules);
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
	assert(extended_replay_driving_rules() == recorded_driving_rules);
	assert(extended_replay_version() == CSRPL_VERSION);
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
	assert(extended_replay_version() == CSRPL_VERSION);
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
	legacy_s32 raised_start_height =
		(CSTRK_MAX_HEIGHT * CSTRK_LEVEL_WORLD_HEIGHT + ENGINE_TEST_INITIAL_BODY_HEIGHT) *
		ENGINE_TEST_POSITION_SCALE;
	assert(state.playerstate.car_rotate.y == 0 && state.playerstate.car_rotate.z == 0);
	assert(state.playerstate.car_position.ly == raised_start_height);
	for (legacy_u16 frame = 0U; frame < ENGINE_TEST_HILL_REPLAY_FRAMES; frame++) {
		assert(extended_replay_record_input(INPUT_ACCELERATE_FLAG));
		update_gamestate();
		assert(state.playerstate.car_crashBmpFlag == CRASH_EVENT_NONE);
	}
	assert(state.playerstate.car_position.ly < raised_start_height);
	struct GAMESTATE hill_expected = state;
	legacy_s8 hill_seed[GAMESTATE_RANDOM_SEED_SIZE];
	get_kevinrandom_seed(hill_seed);
	assert(extended_replay_seek(0U));
	assert(state.playerstate.car_position.ly == raised_start_height);
	assert(extended_replay_seek(ENGINE_TEST_HILL_REPLAY_FRAMES));
	assert_same_simulation(&hill_expected, hill_seed);
	test_recording_version_roundtrips((size_t)saved_size);
	test_smooth_consecutive_slopes(ENGINE_TEST_SLOPE_STUNT_NONE);
	test_smooth_consecutive_slopes(CSTRK_TUNNEL_FIRST_ID);
	test_sloped_slalom_impacts();
	test_mini_half_pipe_on_straight_slope();
	test_recorded_half_pipe_body_collision();
	for (legacy_u8 rotation = 0; rotation < ENGINE_TEST_SLOPE_ROTATIONS; rotation++) {
		test_roll_ramp_flight(GAME_FRAME_RATE_NORMAL, rotation);
		test_roll_ramp_flight(GAME_FRAME_RATE_LOW, rotation);
	}
	test_terrain_below_roll_ramp_does_not_arm();
	test_extended_opponent_laps();
	extended_replay_runtime_clear();
	extended_track_unload();
	puts("Extended engine replay: 70001 frames, deterministic rewind/branch, file round trip, wide "
		 "finish.");
	return 0;
}
