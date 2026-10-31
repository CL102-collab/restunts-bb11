#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../genghost/simulation.h"
#include "../genghost/routes.h"
#include "../c/externs.h"
#include "../c/cstrk.h"
#include "../c/csrpl.h"
#include "../c/driving_rules.h"
#include "../c/extended_track.h"
#include "../c/game_input.h"
#include "../c/crash_state.h"
#include "../c/physics_internal.h"
#include "../c/roll_ramp.h"

#undef memcpy
#undef strlen
#undef printf

#define EXTENDED_TEST_FRAMES 70001U
#define EXTENDED_TEST_CHECKPOINT 65520U
#define EXTENDED_TEST_BRANCH_FRAMES 80U
#define EXTENDED_TEST_POSITION_SCALE 64L
#define EXTENDED_TEST_ROLL_TOLERANCE (ANGLE_FULL_TURN / 64)
#define EXTENDED_TEST_PHYSICS_ARGUMENTS 9U
#define EXTENDED_TEST_ERROR_CAPACITY 256U
#define EXTENDED_TEST_ROLL_PHASES 4U
#define EXTENDED_TEST_LANDING_DIVISOR 2U
#define EXTENDED_TEST_MAX_TRACK                                                                    \
	(CSTRK_HEADER_SIZE + (CSTRK_MAX_DIMENSION + 1U) * (CSTRK_MAX_DIMENSION + 1U) +                 \
	 2U * CSTRK_MAX_OBJECTS + CSTRK_OBJECT_SIZE * CSTRK_MAX_OBJECTS + CSTRK_MAX_NOTES)
#define EXTENDED_TEST_MAX_SOURCE (CSRPL_HEADER_SIZE + EXTENDED_TEST_MAX_TRACK + GENGHOST_MAX_FRAMES)

static legacy_u8 *read_source(const legacy_char *path, legacy_uint *size)
{
	FILE *file = fopen(path, "rb");
	assert(file != NULL);
	assert(fseek(file, 0, SEEK_END) == 0);
	legacy_s32 length = (legacy_s32)ftell(file);
	assert(length > 0 && (legacy_u32)length <= EXTENDED_TEST_MAX_SOURCE);
	assert(fseek(file, 0, SEEK_SET) == 0);
	legacy_u8 *bytes = malloc((legacy_uint)length);
	assert(bytes != NULL);
	assert(fread(bytes, 1, (legacy_uint)length, file) == (legacy_uint)length);
	assert(fclose(file) == 0);
	*size = (legacy_uint)length;
	return bytes;
}

static void initialize_replay(const struct CSRPL_VIEW *view, const legacy_char *program)
{
	legacy_u16 rules = view->metadata.driving_rules;
	legacy_char *arguments[EXTENDED_TEST_PHYSICS_ARGUMENTS];
	legacy_int count = 0;
	arguments[count++] = (legacy_char *)program;
	arguments[count++] = (legacy_char *)(rules & DRIVING_RULE_POWER_GEAR ? "--pg:on" : "--pg:off");
	arguments[count++] =
		(legacy_char *)(rules & DRIVING_RULE_LEGACY_COLLISION ? "--lc:on" : "--lc:off");
	arguments[count++] =
		(legacy_char *)(rules & DRIVING_RULE_LEFT_CORNER_BIAS ? "--lcb:on" : "--lcb:off");
	arguments[count++] =
		(legacy_char *)(rules & DRIVING_RULE_DUAL_WAY_SWITCHING ? "--dws:on" : "--dws:off");
	arguments[count++] =
		(legacy_char *)(rules & DRIVING_RULE_CHICANE_FAST_GRASS ? "--cfg:on" : "--cfg:off");
	if (rules & DRIVING_RULE_AMIGA_GRASS) {
		arguments[count++] = (legacy_char *)"--amigagrass";
	}
	if (rules & DRIVING_RULE_OWOOT) {
		arguments[count++] = (legacy_char *)"--owoot";
	}
	genghost_sim_set_frame_rate(view->metadata.frames_per_second);
	genghost_sim_set_replay_metadata(&view->metadata);
	assert(genghost_sim_init_track(view->track_data, (legacy_uint)view->track_size,
								   (const legacy_char *)view->metadata.player_car_id,
								   view->metadata.player_transmission, count, arguments) == 0);
	assert(driving_rules_current() == rules);
}

static void test_wide_snapshot(const legacy_u8 *track, legacy_uint size, const legacy_char *output,
							   const legacy_char *program, legacy_u16 rate)
{
	legacy_char *arguments[] = {(legacy_char *)program};
	genghost_sim_set_frame_rate(rate);
	assert(genghost_sim_init_track(track, size, "COUN", TRANSMISSION_AUTOMATIC,
								   sizeof(arguments) / sizeof(arguments[0]), arguments) == 0);
	assert(extended_track_width() == CSTRK_MAX_DIMENSION);
	assert(state.playerstate.car_position.lx >
		   (legacy_s32)LEGACY_U16_MAX * EXTENDED_TEST_POSITION_SCALE);
	assert(state.playerstate.car_position.lz >
		   (legacy_s32)LEGACY_U16_MAX * EXTENDED_TEST_POSITION_SCALE);
	struct GENGHOST_SNAPSHOT checkpoint, expected, actual;
	for (legacy_uint frame = 0; frame < EXTENDED_TEST_CHECKPOINT; frame++) {
		assert(genghost_sim_step(INPUT_NONE) == CRASH_EVENT_NONE);
		assert(genghost_sim_frame() == frame + 1U);
	}
	genghost_sim_capture(&checkpoint);
	for (legacy_uint frame = EXTENDED_TEST_CHECKPOINT; frame < EXTENDED_TEST_FRAMES; frame++) {
		assert(genghost_sim_step(INPUT_NONE) == CRASH_EVENT_NONE);
	}
	genghost_sim_capture(&expected);
	assert(genghost_sim_frame() == EXTENDED_TEST_FRAMES);
	genghost_sim_restore(&checkpoint);
	for (legacy_uint frame = 0; frame < EXTENDED_TEST_BRANCH_FRAMES; frame++) {
		assert(genghost_sim_step(INPUT_ACCELERATE_FLAG | INPUT_STEER_LEFT_FLAG) >= 0);
	}
	genghost_sim_restore(&checkpoint);
	for (legacy_uint frame = EXTENDED_TEST_CHECKPOINT; frame < EXTENDED_TEST_FRAMES; frame++) {
		assert(genghost_sim_step(INPUT_NONE) == CRASH_EVENT_NONE);
	}
	genghost_sim_capture(&actual);
	assert(memcmp(&expected, &actual, sizeof(actual)) == 0);
	struct GENGHOST_RESULT result;
	genghost_sim_result(&result);
	assert(result.frames == EXTENDED_TEST_FRAMES);
	assert(!result.finished && !result.crashed);
	legacy_u8 *inputs = calloc(EXTENDED_TEST_FRAMES, sizeof(*inputs));
	assert(inputs != NULL);
	assert(genghost_sim_write_replay(output, "WIDELONG", inputs, EXTENDED_TEST_FRAMES) == 0);
	free(inputs);
	legacy_uint written_size;
	legacy_u8 *written = read_source(output, &written_size);
	struct CSRPL_VIEW replay;
	assert(csrpl_decode(written, written_size, &replay, NULL, 0));
	assert(replay.metadata.frame_count == EXTENDED_TEST_FRAMES);
	assert(replay.metadata.frames_per_second == rate);
	assert(replay.track_size == size && memcmp(replay.track_data, track, size) == 0);
	for (legacy_uint frame = 0; frame < EXTENDED_TEST_FRAMES; frame++) {
		assert(replay.inputs[frame] == INPUT_NONE);
	}
	free(written);
	printf("{\"frames\":%u,\"restored\":true,\"track_size\":%u}\n", result.frames, size);
}

/* Supply a real 10 Hz finished seed without resampling a 20 Hz recording. */
static void test_drive_10hz(const legacy_u8 *track, legacy_uint size, const legacy_char *output,
							const legacy_char *program)
{
	legacy_char *arguments[] = {(legacy_char *)program};
	genghost_sim_set_frame_rate(GAME_FRAME_RATE_LOW);
	assert(genghost_sim_init_track(track, size, "COUN", TRANSMISSION_MANUAL,
								   sizeof(arguments) / sizeof(arguments[0]), arguments) == 0);
	struct GENGHOST_ROUTES routes;
	legacy_char error[EXTENDED_TEST_ERROR_CAPACITY];
	assert(genghost_routes_build(&routes, error, sizeof(error)) == 0 && routes.count > 0);
	struct GENGHOST_POLICY policy;
	struct GENGHOST_DRIVER driver;
	genghost_policy_default(&policy);
	genghost_driver_reset(&driver);
	legacy_u8 *inputs = calloc(GENGHOST_MAX_FRAMES, sizeof(*inputs));
	assert(inputs != NULL);
	legacy_uint frames = 0;
	struct GENGHOST_RESULT result;
	do {
		inputs[frames] = genghost_driver_step(&routes.routes[0], &policy, &driver, 1, 1, 0);
		assert(genghost_sim_step(inputs[frames]) >= 0);
		frames++;
		genghost_sim_result(&result);
	} while (!result.finished && !result.crashed && frames < genghost_sim_frame_limit());
	assert(result.finished);
	assert(genghost_sim_write_replay(output, "TENHZ", inputs, frames) == 0);
	genghost_routes_free(&routes);
	free(inputs);
	printf("{\"frames\":%u,\"finished\":true,\"frames_per_second\":%u}\n", result.frames,
		   genghost_sim_frame_rate());
}

struct EXTENDED_TEST_ROLL {
	legacy_s32 x, z;
	legacy_s32 forward_x, forward_z;
	legacy_s32 total, maximum;
	legacy_s16 previous;
	legacy_u16 settled;
	legacy_u8 phase, present, complete, surface;
};

static void initialize_roll(struct EXTENDED_TEST_ROLL *roll)
{
	memset(roll, 0, sizeof(*roll));
	const struct CSTRK_TRACK *track = extended_track_model();
	for (legacy_u32 index = 0; index < track->object_count; index++) {
		const struct CSTRK_OBJECT *object = &track->objects[index];
		if (!object->roll_ramp) {
			continue;
		}
		assert(!roll->present);
		roll->present = 1;
		roll->surface = object->surface + CAR_SURFACE_PAVED;
		roll->x = object->x * CSTRK_TILE_WORLD_SIZE + object->width * CSTRK_TILE_WORLD_SIZE / 2;
		roll->z = (track->height - object->y) * CSTRK_TILE_WORLD_SIZE -
				  object->height * CSTRK_TILE_WORLD_SIZE / 2;
		legacy_s16 orientation = roll_ramp_orientation(object);
		roll->forward_x = orientation == ANGLE_QUARTER_TURN			? -1
						  : orientation == ANGLE_THREE_QUARTER_TURN ? 1
																	: 0;
		roll->forward_z = orientation == ANGLE_HALF_TURN ? 1 : orientation == 0 ? -1 : 0;
	}
	roll->previous = state.playerstate.car_rotate.z;
}

static void sample_roll(struct EXTENDED_TEST_ROLL *roll)
{
	if (!roll->present || roll->complete) {
		return;
	}
	const struct CARSTATE *car = &state.playerstate;
	legacy_s16 delta =
		(legacy_s16)(((legacy_u16)(car->car_rotate.z - roll->previous + ANGLE_HALF_TURN) &
					  ANGLE_MASK) -
					 ANGLE_HALF_TURN);
	roll->previous = car->car_rotate.z;
	roll->total += delta;
	legacy_s32 along =
		(car->car_position.lx / EXTENDED_TEST_POSITION_SCALE - roll->x) * roll->forward_x +
		(car->car_position.lz / EXTENDED_TEST_POSITION_SCALE - roll->z) * roll->forward_z;
	if (along < ROLL_RAMP_HALF_LENGTH) {
		return;
	}
	if (car->car_sumSurfAllWheels == CAR_WHEEL_CONTACT_NONE) {
		if (roll->total > roll->maximum) {
			roll->maximum = roll->total;
		}
		while (roll->phase < EXTENDED_TEST_ROLL_PHASES &&
			   roll->total >= (legacy_s32)(roll->phase + 1U) * ANGLE_QUARTER_TURN -
								  (roll->phase + 1U == EXTENDED_TEST_ROLL_PHASES
									   ? EXTENDED_TEST_ROLL_TOLERANCE
									   : 0)) {
			roll->phase++;
		}
		roll->settled = 0;
		return;
	}
	legacy_s16 bank =
		(legacy_s16)(((legacy_u16)(car->car_rotate.z + ANGLE_HALF_TURN) & ANGLE_MASK) -
					 ANGLE_HALF_TURN);
	legacy_u8 on_road = 1;
	for (legacy_u8 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
		on_road &= car->car_surfaceWhl[wheel] == roll->surface;
	}
	if (roll->phase == EXTENDED_TEST_ROLL_PHASES && on_road &&
		bank >= -EXTENDED_TEST_ROLL_TOLERANCE && bank <= EXTENDED_TEST_ROLL_TOLERANCE &&
		car->car_crashBmpFlag == CRASH_EVENT_NONE) {
		roll->settled++;
		roll->complete = roll->settled >= genghost_sim_frame_rate() / EXTENDED_TEST_LANDING_DIVISOR;
	} else {
		roll->settled = 0;
	}
}

static void test_replay(const legacy_u8 *bytes, legacy_uint size, const legacy_char *program)
{
	struct CSRPL_VIEW replay;
	assert(csrpl_decode(bytes, size, &replay, NULL, 0));
	initialize_replay(&replay, program);
	struct EXTENDED_TEST_ROLL roll;
	initialize_roll(&roll);
	legacy_s32 maximum_x = 0, maximum_z = 0;
	for (legacy_uint frame = 0; frame < replay.metadata.frame_count; frame++) {
		assert(genghost_sim_step(replay.inputs[frame]) >= 0);
		assert(genghost_sim_frame() == frame + 1U);
		sample_roll(&roll);
		if (state.playerstate.car_position.lx > maximum_x) {
			maximum_x = state.playerstate.car_position.lx;
		}
		if (state.playerstate.car_position.lz > maximum_z) {
			maximum_z = state.playerstate.car_position.lz;
		}
	}
	struct GENGHOST_RESULT result;
	genghost_sim_result(&result);
	printf("{\"frames\":%u,\"finished\":%s,\"crashed\":%s,\"finish_frame\":%u,"
		   "\"penalty_frames\":%u,\"total_frames\":%u,\"frames_per_second\":%u,"
		   "\"maximum_x\":%d,\"maximum_z\":%d,\"barrel_present\":%s,"
		   "\"barrel_complete\":%s,\"airborne_roll\":%d}\n",
		   result.frames, result.finished ? "true" : "false", result.crashed ? "true" : "false",
		   result.finish_frame, result.penalty_frames, result.total_frames,
		   genghost_sim_frame_rate(), (legacy_int)(maximum_x / EXTENDED_TEST_POSITION_SCALE),
		   (legacy_int)(maximum_z / EXTENDED_TEST_POSITION_SCALE), roll.present ? "true" : "false",
		   roll.complete ? "true" : "false", (legacy_int)roll.maximum);
}

legacy_s16 stuntsmain(legacy_s16 argc, legacy_s8 **argv)
{
	assert(argc >= 3);
	legacy_uint size;
	legacy_u8 *source = read_source((const legacy_char *)argv[2], &size);
	if (strcmp((const legacy_char *)argv[1], "--snapshot") == 0 ||
		strcmp((const legacy_char *)argv[1], "--snapshot10") == 0) {
		assert(argc == 4);
		test_wide_snapshot(source, size, (const legacy_char *)argv[3], (const legacy_char *)argv[0],
						   strcmp((const legacy_char *)argv[1], "--snapshot10") == 0
							   ? GAME_FRAME_RATE_LOW
							   : GAME_FRAME_RATE_NORMAL);
	} else if (strcmp((const legacy_char *)argv[1], "--drive10") == 0) {
		assert(argc == 4);
		test_drive_10hz(source, size, (const legacy_char *)argv[3], (const legacy_char *)argv[0]);
	} else {
		assert(argc == 3 && strcmp((const legacy_char *)argv[1], "--replay") == 0);
		test_replay(source, size, (const legacy_char *)argv[0]);
	}
	free(source);
	return 0;
}
