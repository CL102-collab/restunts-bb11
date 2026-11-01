#include "../c/video/video_framebuffer.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../c/externs.h"
#include "../c/restunts.h"
#include "../c/fileio.h"
#include "../c/memmgr.h"
#include "../c/platform.h"
#include "../c/frame/frame_internal.h"
#include "../c/hires/frame_interpolation.h"
#include "../c/hires/presentation.h"
#include "../c/physics/physics_internal.h"
#include "../c/game_input.h"
#include "../c/keyboard.h"
#include "../c/race/race_resources.h"
#include "../c/residue.h"
#include "../c/track/track_collision.h"
#include "../c/wheel_transform.h"
#include "../c/camera.h"
#include "../c/shape2d/shape2d.h"
#include "../c/track/track_objects.h"
#include "../c/fatal.h"
#include "../c/hires/hires.h"
#include "../c/crash_state.h"
#include "../c/hires/frame_adaptive.h"

#define RENDER_REPLAY_ADAPTIVE_MODE 3

#undef memcpy
#undef printf

/* Keep the production timer pump, but control its UI timestamp during exact
 * framebuffer comparisons. No wall-clock overlay may expire between snapshots. */
#define dos_timer_get_realtime_counter render_replay_realtime_counter
#include "../platform/sdl3/timer.c"
#undef dos_timer_get_realtime_counter

static legacy_u8 visual_clock_frozen;
static legacy_u32 visual_clock_ticks;

legacy_u32 dos_timer_get_realtime_counter(void)
{
	legacy_u32 now = render_replay_realtime_counter();
	return visual_clock_frozen != 0 ? visual_clock_ticks : now;
}

extern struct MATRIX wheel_heading_rotation;
extern struct MATRIX plane_heading_rotation;
extern legacy_s16 cached_plane_heading;
extern legacy_s16 cached_wheel_heading;

struct RENDER_SCRATCH {
	struct TRACK_COLLISION_SNAPSHOT collision;
	struct LEGACY_EXECUTION_RESIDUE residue;
	struct VECTOR forward;
	struct VECTOR world;
	legacy_s16 plane;
	legacy_s16 heading;
	legacy_s16 pitch;
	legacy_s16 roll;
	legacy_s16 yaw;
	legacy_s16 render_headings;
};

static void capture_scratch(struct RENDER_SCRATCH *scratch)
{
	memset(scratch, 0, sizeof(*scratch));
	track_collision_capture(&scratch->collision);
	scratch->residue = legacy_execution_residue;
	scratch->forward = wheel_forward_travel;
	scratch->world = wheel_world_travel;
	scratch->plane = planindex_copy;
	scratch->heading = wheel_heading_offset;
	scratch->pitch = car_initial_pitch;
	scratch->roll = car_initial_roll;
	scratch->yaw = car_initial_yaw;
	scratch->render_headings = legacy_render_player_headings_active;
}

struct PHYSICS_SCRATCH {
	struct RENDER_SCRATCH render;
	struct MATRIX rotation;
	struct MATRIX wheel_rotation;
	struct MATRIX plane_rotation;
	legacy_s16 wheel_heading;
	legacy_s16 plane_heading;
	struct VECTORLONG position;
	struct VECTOR angles;
	struct POINT2D pole_bounds[2];
	struct POINT2D object_bounds[2];
	struct POINT2D auxiliary_bounds[2];
	legacy_s16 gravity[CARSTATE_WHEEL_COUNT];
	legacy_s16 contact_distance;
	legacy_u16 frame_rate;
	legacy_s16 checkpoint_interval;
	legacy_s16 ticks_per_frame;
	legacy_s8 *steering_table;
	legacy_u8 exit_request;
	legacy_u8 start_sequence;
	legacy_u8 replay_mode;
	legacy_s16 flag_animation;
};

static void capture_physics_scratch(struct PHYSICS_SCRATCH *scratch)
{
	memset(scratch, 0, sizeof(*scratch));
	capture_scratch(&scratch->render);
	scratch->rotation = car_to_world_rotation;
	scratch->wheel_rotation = wheel_heading_rotation;
	scratch->plane_rotation = plane_heading_rotation;
	scratch->wheel_heading = cached_wheel_heading;
	scratch->plane_heading = cached_plane_heading;
	scratch->position = (struct VECTORLONG){car_working_x, car_working_y, car_working_z};
	scratch->angles = (struct VECTOR){car_working_pitch, car_working_roll, car_working_yaw};
	memcpy(scratch->pole_bounds, start_finish_pole_bounds, sizeof(scratch->pole_bounds));
	memcpy(scratch->object_bounds, breakable_object_bounds, sizeof(scratch->object_bounds));
	memcpy(scratch->auxiliary_bounds, track_auxiliary_obstacle_bounds,
		   sizeof(scratch->auxiliary_bounds));
	memcpy(scratch->gravity, wheel_gravity_steps, sizeof(scratch->gravity));
	scratch->contact_distance = nextPosAndNormalIP;
	scratch->frame_rate = framespersec;
	scratch->checkpoint_interval = checkpoint_frame_interval;
	scratch->ticks_per_frame = timer_ticks_per_frame;
	scratch->steering_table = steerWhlRespTable_ptr;
	scratch->exit_request = race_exit_request;
	scratch->start_sequence = race_start_sequence_state;
	scratch->replay_mode = game_replay_mode;
	scratch->flag_animation = start_flag_animation;
}

static void interpolate_and_check(struct GAMESTATE *result, const struct GAMESTATE *previous,
								  legacy_u32 fraction)
{
	struct GAMESTATE saved = state;
	struct GAMESTATE saved_previous = *previous;
	struct GAMEINFO saved_config = gameconfig;
	struct GAMESTATE checkpoints[GAMESTATE_CHECKPOINT_COUNT];
	struct PHYSICS_SCRATCH before;
	struct PHYSICS_SCRATCH after;
	legacy_s8 seed_before[GAMESTATE_RANDOM_SEED_SIZE];
	legacy_s8 seed_after[GAMESTATE_RANDOM_SEED_SIZE];
	memcpy(checkpoints, cvxptr, sizeof(checkpoints));
	get_kevinrandom_seed(seed_before);
	capture_physics_scratch(&before);
	frame_interpolate_state(result, &state, previous, fraction);
	get_kevinrandom_seed(seed_after);
	capture_physics_scratch(&after);
	assert(memcmp(&saved, &state, sizeof(state)) == 0);
	assert(memcmp(&saved_previous, previous, sizeof(*previous)) == 0);
	assert(memcmp(&saved_config, &gameconfig, sizeof(gameconfig)) == 0);
	assert(memcmp(checkpoints, cvxptr, sizeof(checkpoints)) == 0);
	assert(memcmp(seed_before, seed_after, sizeof(seed_before)) == 0);
	assert(memcmp(&before, &after, sizeof(before)) == 0);
	assert(result->game_frame == state.game_frame);
	/* Repeated presentations must depend only on the two authoritative snapshots. */
	struct GAMESTATE repeated;
	frame_interpolate_state(&repeated, &state, previous, fraction);
	assert(memcmp(result, &repeated, sizeof(repeated)) == 0);
}

static void check_linear_blend(legacy_s32 value, legacy_s32 previous, legacy_s32 current,
							   legacy_s32 last)
{
	if (previous <= current) {
		assert(value >= previous && value <= current && value >= last);
	} else {
		assert(value <= previous && value >= current && value <= last);
	}
}

static legacy_s32 angle_difference(legacy_s16 previous, legacy_s16 current)
{
	return ((legacy_u16)(current - previous + ANGLE_HALF_TURN) & ANGLE_MASK) -
		   (legacy_s32)ANGLE_HALF_TURN;
}

static void check_angle_blend(legacy_s16 value, legacy_s16 previous, legacy_s16 current,
							  legacy_s16 last)
{
	check_linear_blend(angle_difference(previous, value), 0, angle_difference(previous, current),
					   angle_difference(previous, last));
}

static void check_car_blend(const struct CARSTATE *result, const struct CARSTATE *current,
							const struct CARSTATE *previous, const struct CARSTATE *last)
{
	if (current->car_crashBmpFlag != previous->car_crashBmpFlag) {
		assert(memcmp(result, current, sizeof(*result)) == 0);
		return;
	}
	check_linear_blend(result->car_position.lx, previous->car_position.lx, current->car_position.lx,
					   last->car_position.lx);
	check_linear_blend(result->car_position.ly, previous->car_position.ly, current->car_position.ly,
					   last->car_position.ly);
	check_linear_blend(result->car_position.lz, previous->car_position.lz, current->car_position.lz,
					   last->car_position.lz);
	check_angle_blend(result->car_rotate.x, previous->car_rotate.x, current->car_rotate.x,
					  last->car_rotate.x);
	check_angle_blend(result->car_rotate.y, previous->car_rotate.y, current->car_rotate.y,
					  last->car_rotate.y);
	check_angle_blend(result->car_rotate.z, previous->car_rotate.z, current->car_rotate.z,
					  last->car_rotate.z);
	check_linear_blend(result->car_steeringAngle, previous->car_steeringAngle,
					   current->car_steeringAngle, last->car_steeringAngle);
	for (legacy_u16 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
		check_linear_blend(
			result->car_suspension_deflection[wheel], previous->car_suspension_deflection[wheel],
			current->car_suspension_deflection[wheel], last->car_suspension_deflection[wheel]);
	}
}

static void check_state_blend(const struct GAMESTATE *result, const struct GAMESTATE *previous,
							  const struct GAMESTATE *last)
{
	check_car_blend(&result->playerstate, &state.playerstate, &previous->playerstate,
					&last->playerstate);
	check_car_blend(&result->opponentstate, &state.opponentstate, &previous->opponentstate,
					&last->opponentstate);
	for (legacy_u16 car = 0; car < GAMESTATE_CAR_VECTOR_COUNT; car++) {
		const struct VECTOR *value = &result->game_follow_camera_position[car];
		const struct VECTOR *start = &previous->game_follow_camera_position[car];
		const struct VECTOR *end = &state.game_follow_camera_position[car];
		const struct VECTOR *last_value = &last->game_follow_camera_position[car];
		const struct CARSTATE *current_car =
			car == PLAYER_CAR_INDEX ? &state.playerstate : &state.opponentstate;
		const struct CARSTATE *previous_car =
			car == PLAYER_CAR_INDEX ? &previous->playerstate : &previous->opponentstate;
		if (state.game_trackside_camera_index[car] != previous->game_trackside_camera_index[car] ||
			current_car->car_crashBmpFlag != previous_car->car_crashBmpFlag) {
			assert(memcmp(value, end, sizeof(*value)) == 0);
		} else {
			check_linear_blend(value->x, start->x, end->x, last_value->x);
			check_linear_blend(value->y, start->y, end->y, last_value->y);
			check_linear_blend(value->z, start->z, end->z, last_value->z);
		}
	}
}

static void render_and_check(const struct GAMESTATE *snapshot)
{
	struct GAMESTATE saved = state;
	struct GAMEINFO saved_config = gameconfig;
	legacy_s8 seed_before[GAMESTATE_RANDOM_SEED_SIZE];
	legacy_s8 seed_after[GAMESTATE_RANDOM_SEED_SIZE];
	struct RENDER_SCRATCH before;
	struct RENDER_SCRATCH after;
	get_kevinrandom_seed(seed_before);
	capture_scratch(&before);
	sprite_select_render_window();
	if (snapshot != NULL) {
		update_frame_snapshot(0, &rect_windshield, snapshot, NULL, NULL);
	} else if (supersight_enabled != 0) {
		update_frame_snapshot(0, &rect_windshield, &saved, NULL, NULL);
	} else {
		update_frame(0, &rect_windshield);
	}
	frame_present(&rect_windshield);
	get_kevinrandom_seed(seed_after);
	assert(memcmp(&saved, &state, sizeof(state)) == 0);
	assert(memcmp(&saved_config, &gameconfig, sizeof(gameconfig)) == 0);
	assert(memcmp(seed_before, seed_after, sizeof(seed_before)) == 0);
	if (snapshot != NULL || supersight_enabled != 0) {
		capture_scratch(&after);
		assert(memcmp(&before, &after, sizeof(before)) == 0);
	}
	if (full_redraw_frames_remaining != 0) {
		full_redraw_frames_remaining--;
	}
}

static legacy_u8 *capture_snapshot_pixels(const struct GAMESTATE *snapshot, size_t *size)
{
	/* Redraw completely so the comparison includes cockpit overlays and car visibility. */
	full_redraw_frames_remaining = 1;
	render_and_check(snapshot);
	legacy_s32 width;
	legacy_s32 height;
	const legacy_u8 *pixels =
		video_framebuffer(dos_memory_make_pointer(0xA000U, 0), &width, &height);
	assert(pixels != NULL);
	*size = (size_t)width * height;
	legacy_u8 *copy = malloc(*size);
	assert(copy != NULL);
	memcpy(copy, pixels, *size);
	return copy;
}

static void check_authoritative_event_visuals(void)
{
	assert(visual_clock_frozen == 0);
	visual_clock_ticks = dos_timer_get_realtime_counter();
	visual_clock_frozen = 1;
	struct GAMESTATE saved = state;
	legacy_s8 saved_camera = cameramode;
	legacy_s8 saved_follow = followOpponentFlag;
	static const legacy_s8 events[] = {CRASH_EVENT_NONE, CRASH_EVENT_COLLISION, CRASH_EVENT_WATER};
	static const legacy_s8 cameras[] = {CAMERA_MODE_COCKPIT, CAMERA_MODE_CUSTOM};
	for (legacy_u16 camera = 0; camera < sizeof(cameras) / sizeof(cameras[0]); camera++) {
		cameramode = cameras[camera];
		for (legacy_u16 target = 0; target < (gameconfig.game_opponenttype != 0 ? 2U : 1U);
			 target++) {
			followOpponentFlag = (legacy_s8)target;
			for (legacy_u16 confirmed = 0; confirmed < sizeof(events) / sizeof(events[0]);
				 confirmed++) {
				state.playerstate.car_crashBmpFlag = events[confirmed];
				state.opponentstate.car_crashBmpFlag = events[confirmed];
				state.game_pEndFrame = state.game_oEndFrame = state.game_frame - 3;
				struct GAMESTATE presentation = state;
				size_t expected_size;
				legacy_u8 *expected = capture_snapshot_pixels(&presentation, &expected_size);
				for (legacy_u16 old_event = 0; old_event < sizeof(events) / sizeof(events[0]);
					 old_event++) {
					struct GAMESTATE previous = state;
					previous.playerstate.car_crashBmpFlag = events[old_event];
					previous.opponentstate.car_crashBmpFlag = events[old_event];
					previous.game_pEndFrame = previous.game_oEndFrame = state.game_frame;
					if (events[old_event] != events[confirmed]) {
						/* A confirmed crash must snap both cars even at the old endpoint. */
						previous.playerstate.car_position.ly += 128;
						previous.opponentstate.car_position.ly += 128;
					}
					interpolate_and_check(&presentation, &previous, 0);
					assert(memcmp(&presentation.playerstate, &state.playerstate,
								  sizeof(state.playerstate)) == 0);
					assert(memcmp(&presentation.opponentstate, &state.opponentstate,
								  sizeof(state.opponentstate)) == 0);
					size_t actual_size;
					legacy_u8 *actual = capture_snapshot_pixels(&presentation, &actual_size);
					assert(actual_size == expected_size);
					assert(memcmp(actual, expected, actual_size) == 0);
					free(actual);
				}
				free(expected);
			}
		}
	}
	state = saved;
	cameramode = saved_camera;
	followOpponentFlag = saved_follow;
	visual_clock_frozen = 0;
}

static void initialize_replay(const legacy_s8 *name)
{
	assert(file_load_replay("", name) == 0);
	replay_filename[0] = 0;
	gameconfigcopy = gameconfig;
	assert(track_setup() == 0);
	cvxptr = mmgr_alloc_resbytes("cvx", sizeof(struct GAMESTATE) * GAMESTATE_CHECKPOINT_COUNT);
	init_game_state(-1);
	viewport_bottom_cache = -1;
	run_game_random = LEGACY_S16_SHL(get_kevinrandom(), 3U);
	replaybar_toggle = 0;
	is_in_replay = 1;
	idle_expired = 0;
	cameramode = CAMERA_MODE_COCKPIT;
	game_replay_mode = REPLAY_MODE_PLAYBACK;
	detail_level = 0;
	slow_video_mgmt = slow_video_mgmt_copy = 1;
	assert(setup_player_cars_without_dashboard() == 0);
	kbormouse = 0;
	replay_playback_speed = REPLAY_PLAYBACK_NORMAL;
	race_exit_request = 1;
	game_replay_mode_copy = -1;
	frame_buffer_index = dashboard_buffer_index = 0;
	recording_limit_warning_requested = 0;
	dashb_toggle = 0;
	followOpponentFlag = 0;
	framespersec = gameconfig.game_framespersec;
	start_flag_animation = 500;
	rect_windshield = (struct RECTANGLE){0, 320, 0, 200};
	set_projection(35, 200 / 6, 320, 200);
	restore_gamestate(0);
	init_rect_arrays();
	full_redraw_frames_remaining = 1;
	/* This is an ordinary live renderer, with no archived dump stack emulation. */
	shape3d_set_legacy_render_stack(NULL, 0, 0, NULL);
}

legacy_s16 stuntsmain(legacy_s16 argc, legacy_s8 *argv[])
{
	assert(argc == 7 || argc == 9);
	legacy_s16 mode = (legacy_s16)atoi((const legacy_char *)argv[3]);
	legacy_u16 limit = (legacy_u16)atoi((const legacy_char *)argv[4]);
	assert(mode >= 0 && mode <= RENDER_REPLAY_ADAPTIVE_MODE);
	legacy_u16 landing_start = (legacy_u16)atoi((const legacy_char *)argv[5]);
	legacy_u16 landing_end = (legacy_u16)atoi((const legacy_char *)argv[6]);
	legacy_u16 settling_start = argc == 9 ? (legacy_u16)atoi((const legacy_char *)argv[7]) : 0;
	legacy_u16 settling_end = argc == 9 ? (legacy_u16)atoi((const legacy_char *)argv[8]) : 0;
	init_main(argc, argv);
	init_div0();
	init_row_tables();
	mainresptr = file_load_resfile("main");
	fontdefptr = file_load_resource(0, "fontdef.fnt");
	fontnptr = file_load_resource(0, "fontn.fnt");
	font_set_fontdef();
	init_polyinfo();
	init_trackdata();
	reset_race_loop_state();
	init_kevinrandom("kevin");
	initialize_replay(argv[1]);
	if (limit == 0 || limit > gameconfig.game_recordedframes) {
		limit = gameconfig.game_recordedframes;
	}
	legacy_u8 *recording = malloc(gameconfig.game_recordedframes);
	assert(recording != NULL);
	memcpy(recording, replay_input_buffer, gameconfig.game_recordedframes);
	FILE *output = fopen((const legacy_char *)argv[2], "wb");
	assert(output != NULL);
	struct GAMESTATE previous = state;
	legacy_u32 extra_frames = 0;
	legacy_u16 landing_interpolations = 0;
	legacy_u16 settling_interpolations = 0;
	for (legacy_u16 tick = 0; tick <= limit; tick++) {
		assert((legacy_u16)state.game_frame == tick);
		legacy_s16 enhanced = mode == 1 || mode == RENDER_REPLAY_ADAPTIVE_MODE ||
							  (mode == 2 && (tick / 17U) % 2U != 0);
		if (enhanced != supersight_enabled) {
			assert(handle_ingame_kb_shortcuts(KEY_F12) != 0);
		}
		if (mode == RENDER_REPLAY_ADAPTIVE_MODE) {
			/* Sweep every reduction and recovery state independently of host speed. */
			frame_adaptive.quality = tick % (FRAME_ADAPTIVE_MAX_QUALITY + 1U);
			hires_set_render_scale(frame_adaptive_render_scale(&frame_adaptive));
		}
		/* Cover each camera and both targets without modifying recorded input. */
		cameramode = (legacy_s8)((tick / 29U) % CAMERA_MODE_COUNT);
		if (settling_end != 0 && tick >= settling_start && tick <= settling_end) {
			cameramode = CAMERA_MODE_COCKPIT;
		}
		followOpponentFlag = gameconfig.game_opponenttype != 0 && (tick / 13U) % 2U != 0;
		if (enhanced != 0 && tick == 30) {
			check_authoritative_event_visuals();
		}
		if (enhanced != 0 && tick != 0) {
			legacy_u16 count = PRESENTATION_RATE / gameconfig.game_framespersec;
			struct GAMESTATE last;
			interpolate_and_check(&last, &previous, 0);
			/* Match production: sample the completed interval in 60 Hz slots,
			 * starting at one third for 20 Hz physics and ending at its endpoint. */
			for (legacy_u16 frame = 1; frame <= count; frame++) {
				struct GAMESTATE interpolated;
				legacy_u32 fraction = (FRAME_INTERPOLATION_ONE * frame) / count;
				interpolate_and_check(&interpolated, &previous, fraction);
				/* Render poses, suspension and cameras may only move toward the
				 * next confirmed endpoint, including landings and loop exits. */
				check_state_blend(&interpolated, &previous, &last);
				if (frame == 1 && tick % 31U == 0 && tick < gameconfig.game_recordedframes) {
					legacy_s8 future_input = replay_input_buffer[tick];
					replay_input_buffer[tick] = (legacy_s8) ~(legacy_u8)future_input;
					struct GAMESTATE altered_future;
					interpolate_and_check(&altered_future, &previous, fraction);
					replay_input_buffer[tick] = future_input;
					assert(memcmp(&altered_future, &interpolated, sizeof(interpolated)) == 0);
				}
				if (frame < count && landing_end != 0 && tick >= landing_start &&
					tick <= landing_end) {
					/* Known poses above the level road cannot blend below it. */
					if (previous.playerstate.car_position.ly >= 0 &&
						state.playerstate.car_position.ly >= 0) {
						assert(interpolated.playerstate.car_position.ly >= 0);
					}
					landing_interpolations++;
				}
				if (frame < count && settling_end != 0 && tick >= settling_start &&
					tick <= settling_end) {
					settling_interpolations++;
				}
				render_and_check(&interpolated);
				last = interpolated;
				if (frame < count) {
					extra_frames++;
				}
			}
			assert(memcmp(&last, &state, sizeof(state)) == 0);
		} else {
			render_and_check(NULL);
		}
		legacy_u8 bytes[GAMESTATE_SERIALIZED_SIZE];
		assert(gamestate_serialize(bytes, &state) == sizeof(bytes));
		assert(fwrite(bytes, 1, sizeof(bytes), output) == sizeof(bytes));
		legacy_s8 seed[GAMESTATE_RANDOM_SEED_SIZE];
		get_kevinrandom_seed(seed);
		assert(fwrite(seed, 1, sizeof(seed), output) == sizeof(seed));
		assert(memcmp(recording, replay_input_buffer, gameconfig.game_recordedframes) == 0);
		if (tick < limit) {
			previous = state;
			update_gamestate();
		}
	}
	assert(mode == 0 ? extra_frames == 0 : extra_frames != 0);
	if (mode == 1 && landing_end != 0) {
		assert(landing_interpolations != 0);
	}
	if (mode == 1 && settling_end != 0) {
		assert(settling_interpolations != 0);
	}
	printf("mode %d: %" LEGACY_PRIu32 " interpolated extra frames (%u landing, %u loop exit)\n",
		   mode, extra_frames, landing_interpolations, settling_interpolations);
	assert(fclose(output) == 0);
	free(recording);
	/* Match pixldump shutdown: this fixture never loaded dashboard resources. */
	call_exitlist();
	return 0;
}
