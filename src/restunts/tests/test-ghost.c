#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../c/ghost.h"
#include "../c/externs.h"
#include "../c/platform.h"
#include "../c/residue.h"
#include "../c/trackdata_layout.h"
#ifdef RESTUNTS_SDL3
#include "../c/frame_interpolation.h"
#include "../c/crash_state.h"
#endif

#undef memcpy
#undef memset
#undef strcpy
#undef strlen

#define TEST_FILE_COUNT 12U
#define TEST_RECORDING_FRAMES 8U

struct GAMEINFO gameconfig;
struct GAMESTATE state;
struct SIMD simd_player;
struct LEGACY_EXECUTION_RESIDUE legacy_execution_residue;
legacy_s16 legacy_render_player_headings_active;
legacy_u16 framespersec;
legacy_u8 game_replay_mode;
legacy_s8 followOpponentFlag;
legacy_s8 is_in_replay;
legacy_s8 replay_recording_flags;
legacy_u16 elapsed_time1;
legacy_u16 elapsed_time2;
legacy_s16 checkpoint_frame_interval;
legacy_s16 timer_ticks_per_frame;
legacy_s8 *steerWhlRespTable_ptr;
legacy_u8 frame_callback_countdown;
legacy_u8 slow_replay_countdown;
legacy_u8 race_exit_request;
legacy_u8 race_start_sequence_state;
legacy_s16 start_flag_animation;
static legacy_u8 test_track[REPLAY_TRACK_SIZE];
static legacy_s8 test_inputs[TRACKDATA_REPLAY_INPUT_BUFFER_SIZE];
legacy_u8 *track_element_map = test_track;
legacy_s8 *replay_input_buffer = test_inputs;
static legacy_s8 random_seed[GAMESTATE_RANDOM_SEED_SIZE];
static void (*cleanup_handler)(void);
static legacy_s32 simulation_loads;
static legacy_s32 simulation_steps;
static legacy_s32 simulation_frees;
static legacy_s32 fail_write;
static legacy_s32 fail_read;
#ifdef RESTUNTS_SDL3
static legacy_u32 read_calls;
static legacy_u8 render_fixture;
static legacy_u8 line_fixture;
#endif

struct TEST_FILE {
	legacy_s8 name[REPLAY_FILENAME_SIZE];
	FILE *stream;
	legacy_s16 exists;
};

static struct TEST_FILE files[TEST_FILE_COUNT];

legacy_u16 dos_file_open(const legacy_s8 *name, legacy_s16 create)
{
	for (legacy_u16 index = 1; index < TEST_FILE_COUNT; index++) {
		if (files[index].exists && strcmp((char *)files[index].name, (const char *)name) == 0) {
			assert(create == DOS_FILE_OPEN_EXISTING);
			rewind(files[index].stream);
			return index;
		}
	}
	if (create == DOS_FILE_OPEN_EXISTING) {
		return 0;
	}
	for (legacy_u16 index = 1; index < TEST_FILE_COUNT; index++) {
		if (!files[index].exists) {
			strcpy((char *)files[index].name, (const char *)name);
			files[index].stream = tmpfile();
			assert(files[index].stream != 0);
			files[index].exists = 1;
			return index;
		}
	}
	return 0;
}

legacy_s16 dos_file_close(legacy_u16 file)
{
	assert(file > 0 && file < TEST_FILE_COUNT && files[file].exists);
	return 0;
}

legacy_s16 dos_file_remove(const legacy_s8 *name)
{
	for (legacy_u16 index = 1; index < TEST_FILE_COUNT; index++) {
		if (files[index].exists && strcmp((char *)files[index].name, (const char *)name) == 0) {
			fclose(files[index].stream);
			files[index].exists = 0;
			return 0;
		}
	}
	return -1;
}

legacy_u16 dos_file_read(legacy_u16 file, void *destination, legacy_u16 length)
{
#ifdef RESTUNTS_SDL3
	read_calls++;
#endif
	if (fail_read) {
		return 0;
	}
	return (legacy_u16)fread(destination, 1, length, files[file].stream);
}

legacy_u16 dos_file_write(legacy_u16 file, const void *source, legacy_u16 length)
{
	if (fail_write) {
		return 0;
	}
	return (legacy_u16)fwrite(source, 1, length, files[file].stream);
}

legacy_s16 dos_file_seek(legacy_u16 file, legacy_s32 offset, legacy_s16 origin)
{
	return (legacy_s16)fseek(files[file].stream, offset, origin);
}

legacy_s16 dos_file_error(void)
{
	return 0;
}

const legacy_s8 *dos_file_find_first(const legacy_s8 *name)
{
	for (legacy_u16 index = 1; index < TEST_FILE_COUNT; index++) {
		if (files[index].exists && strcmp((char *)files[index].name, (const char *)name) == 0) {
			return files[index].name;
		}
	}
	return 0;
}

void file_build_path(const legacy_s8 *directory, const legacy_s8 *name, const legacy_s8 *extension,
					 legacy_s8 *destination)
{
	assert(directory == 0 || directory[0] == 0);
	sprintf((char *)destination, "%s%s", name, extension);
}

void add_exit_handler(void (*handler)(void))
{
	cleanup_handler = handler;
}

legacy_s16 track_setup(void)
{
	/* Exercise the canonicalization performed by the real track compiler. */
	test_track[182] = 4;
	return 0;
}

void get_kevinrandom_seed(legacy_s8 *seed)
{
	memcpy(seed, random_seed, sizeof(random_seed));
}

void init_kevinrandom(const legacy_s8 *seed)
{
	memcpy(random_seed, seed, sizeof(random_seed));
}

void ghost_load_simulation_resources(void)
{
	assert(gameconfig.game_opponenttype == 3);
	assert(memcmp(gameconfig.game_playercarid, "ANSX", REPLAY_CAR_ID_SIZE) == 0);
	simulation_loads++;
	simd_player.car_height = 47;
}

void ghost_free_simulation_resources(void)
{
	simulation_frees++;
}

static void set_recorded_camera(legacy_s16 frame)
{
	state.game_follow_camera_position[PLAYER_CAR_INDEX].x = 100 + frame;
	state.game_follow_camera_position[PLAYER_CAR_INDEX].y = 200 + frame;
	state.game_follow_camera_position[PLAYER_CAR_INDEX].z = 300 + frame;
	state.game_player_camera_previous.x = 99 + frame;
	state.game_player_camera_previous.y = 199 + frame;
	state.game_player_camera_previous.z = 299 + frame;
	state.game_trackside_camera_index[PLAYER_CAR_INDEX] = (legacy_s8)(frame % 8);
	state.game_pEndFrame = frame >= 2 ? 2 : 0;
#ifdef RESTUNTS_SDL3
	if (line_fixture != 0) {
		legacy_s16 x = 1000;
		legacy_s16 y = 0;
		legacy_s16 z = 1000 + frame * (line_fixture == 3 ? 2 : 100);
		if (line_fixture == 2 && frame > 4) {
			x += (frame - 4) * 100;
			z = 1400;
		}
		if (line_fixture == 4) {
			y = frame * 100;
			z = 1000;
		}
		if (line_fixture == 5 && frame >= 4) {
			z += 10000;
		}
		if (line_fixture == 6) {
			z = 1000;
		}
		if (line_fixture == 7) {
			z = 1000 + (frame % 28) * 1000;
		}
		if (line_fixture == 8) {
			z = 1000 + (frame > 0 ? frame - 1 : 0) * 100;
		}
		const legacy_s16 side[4] = {-10, 10, 10, -10};
		const legacy_s16 front[4] = {20, 20, -20, -20};
		for (legacy_u16 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
			state.playerstate.car_wheel_contact_positions[wheel] =
				(struct VECTOR){x + side[wheel], line_fixture == 4 ? y + front[wheel] : y,
								line_fixture == 4 ? z : z + front[wheel]};
			if (line_fixture == 8) {
				state.playerstate.car_wheel_contact_positions[wheel] =
					frame == 0 ? (struct VECTOR){x, y, z}
							   : (struct VECTOR){x, y + side[wheel], z + front[wheel]};
			}
		}
		state.playerstate.car_actual_speed = (40 + frame * 5) * 256;
	}
	if (render_fixture != 0) {
		state.playerstate.car_position.lx = frame * 100L;
		state.playerstate.car_actual_speed = 1000 + frame;
		for (legacy_u16 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
			state.playerstate.car_surfaceWhl[wheel] = (legacy_s8)(frame % 3);
		}
		state.game_follow_camera_position[PLAYER_CAR_INDEX] =
			(struct VECTOR){100 + frame * 100, 200 + frame * 100, 300 + frame * 100};
		state.game_player_camera_previous =
			(struct VECTOR){frame * 100, 100 + frame * 100, 200 + frame * 100};
		state.game_trackside_camera_index[PLAYER_CAR_INDEX] =
			render_fixture == 2 && frame >= 4 ? 3 : 2;
		state.playerstate.car_crashBmpFlag =
			render_fixture == 2 && frame >= 3 ? CRASH_EVENT_COLLISION : CRASH_EVENT_NONE;
	}
#endif
}

static void assert_camera_at_frame(legacy_s16 frame)
{
	const struct GHOST_CAMERA_STATE *camera = ghost_camera_state();
	assert(camera != 0);
	assert(camera->follow_position.x == 100 + frame);
	assert(camera->follow_position.y == 200 + frame);
	assert(camera->follow_position.z == 300 + frame);
	assert(camera->previous_position.x == 99 + frame);
	assert(camera->previous_position.y == 199 + frame);
	assert(camera->previous_position.z == 299 + frame);
	assert(camera->trackside_index == frame % 8);
	assert(camera->frame == frame);
	assert(camera->crash_frame == (frame >= 2 ? 2 : 0));
}

static void assert_camera_motion(legacy_s16 x, legacy_s16 y, legacy_s16 z)
{
	struct VECTOR previous = {90, 220, 270};
	const struct VECTOR current = {100, 200, 300};
	const struct GHOST_CAMERA_STATE *camera = ghost_camera_state();
	struct GHOST_CAMERA_STATE before;
	if (camera != 0) {
		before = *camera;
	}
	ghost_adjust_camera_motion(&previous, &current);
	assert(previous.x == x && previous.y == y && previous.z == z);
	if (camera != 0) {
		assert(memcmp(&before, camera, sizeof(before)) == 0);
	}
}

void init_game_state(legacy_s16 mode)
{
	assert(mode == GAMESTATE_INIT_RESET_CHECKPOINTS);
	elapsed_time1 = 0;
	elapsed_time2 = 0;
	checkpoint_frame_interval = (legacy_s16)(framespersec * 30);
	timer_ticks_per_frame = (legacy_s16)(100 / framespersec);
	memset(&state, 0, sizeof(state));
	set_recorded_camera(0);
}

void update_gamestate_silent(void)
{
	assert(game_replay_mode == REPLAY_MODE_PLAYBACK);
	assert(is_in_replay == 1);
	assert(replay_recording_flags == REPLAY_RECORDING_RESTARTABLE_FLAG);
	assert(gameconfig.game_opponenttype == 3);
	state.playerstate.car_position.lx += replay_input_buffer[state.game_frame];
	state.playerstate.car_position.lz += gameconfig.game_opponenttype;
	state.playerstate.car_steeringAngle = state.game_frame;
	state.playerstate.car_sound_flags = CAR_SOUND_ENGINE_ACTIVE_FLAG;
	state.game_frame++;
	set_recorded_camera(state.game_frame);
	legacy_execution_residue.penalty_route_word++;
	legacy_render_player_headings_active = 1;
	random_seed[0]++;
	simulation_steps++;
}

static void make_replay(const char *name, legacy_u16 fps, legacy_u16 frame_count,
						legacy_s16 truncated)
{
	struct GAMEINFO config = {0};
	memcpy(config.game_playercarid, "ANSX", REPLAY_CAR_ID_SIZE);
	memcpy(config.game_opponentcarid, "P964", REPLAY_CAR_ID_SIZE);
	memcpy(config.game_trackname, "GHOSTTRK", 9);
	config.game_opponenttype = 3;
	config.game_framespersec = fps;
	config.game_recordedframes = frame_count;
	legacy_u8 header[REPLAY_GAMEINFO_SIZE];
	replay_gameinfo_encode(header, &config);
	legacy_u16 file = dos_file_open((const legacy_s8 *)name, DOS_FILE_CREATE);
	dos_file_write(file, header, sizeof(header));
	legacy_u8 track[REPLAY_TRACK_SIZE];
	for (legacy_u16 index = 0; index < sizeof(track); index++) {
		track[index] = (legacy_u8)(index * 7U);
	}
	dos_file_write(file, track, sizeof(track));
	for (legacy_u16 frame = 0; frame < frame_count && !truncated; frame++) {
		legacy_u8 input = (legacy_u8)(frame + 1U);
		dos_file_write(file, &input, 1);
	}
}

static void test_selection_is_separate(void)
{
	memcpy(gameconfig.game_playercarid, "COUN", REPLAY_CAR_ID_SIZE);
	gameconfig.game_playertransmission = TRANSMISSION_AUTOMATIC;
	gameconfig.game_recordedframes = 17;
	framespersec = GAME_FRAME_RATE_NORMAL;
	make_replay("source.rpl", GAME_FRAME_RATE_LOW, TEST_RECORDING_FRAMES, 0);
	assert(ghost_select_replay(0, (const legacy_s8 *)"source") == 0);
	assert(ghost_is_selected());
	assert(!ghost_is_active());
	assert(ghost_car_state() == 0);
	assert(ghost_camera_state() == 0);
	assert(memcmp(gameconfig.game_playercarid, "COUN", REPLAY_CAR_ID_SIZE) == 0);
	assert(gameconfig.game_playertransmission == TRANSMISSION_AUTOMATIC);
	assert(gameconfig.game_recordedframes == 17);
	assert(strcmp((char *)gameconfig.game_trackname, "GHOSTTRK") == 0);
	assert(test_track[42] == (legacy_u8)(42U * 7U));
	assert(cleanup_handler != 0);
	ghost_check_track();
	assert(ghost_is_selected());
}

static void test_invalid_selection_preserves_previous(void)
{
	make_replay("invalid.rpl", 0, 0, 0);
	make_replay("short.rpl", GAME_FRAME_RATE_NORMAL, 100, 1);
	make_replay("long.rpl", GAME_FRAME_RATE_NORMAL, 12001, 1);
	assert(ghost_select_replay(0, (const legacy_s8 *)"invalid") != 0);
	assert(ghost_select_replay(0, (const legacy_s8 *)"short") != 0);
	assert(ghost_select_replay(0, (const legacy_s8 *)"long") != 0);
	assert(ghost_select_replay(0, (const legacy_s8 *)"missing") != 0);
	fail_write = 1;
	assert(ghost_select_replay(0, (const legacy_s8 *)"source") != 0);
	fail_write = 0;
	assert(ghost_is_selected());
	assert(memcmp(ghost_car_id(), "ANSX", REPLAY_CAR_ID_SIZE) == 0);
}

static void test_playback_timing_and_isolation(void)
{
	elapsed_time1 = 111;
	elapsed_time2 = 222;
	checkpoint_frame_interval = 999;
	timer_ticks_per_frame = 11;
	state.playerstate.car_position.lx = 987654;
	state.game_frame = 21;
	state.game_follow_camera_position[PLAYER_CAR_INDEX].x = 30001;
	state.game_player_camera_previous.y = 30002;
	state.game_trackside_camera_index[PLAYER_CAR_INDEX] = 13;
	state.game_pEndFrame = 14;
	game_replay_mode = REPLAY_MODE_PAUSED;
	is_in_replay = 0;
	replay_recording_flags = REPLAY_RECORDING_ACTIVE_FLAG;
	random_seed[0] = 99;
	legacy_execution_residue.penalty_route_word = 77;
	legacy_render_player_headings_active = 5;
	struct GAMESTATE before = state;
	struct GAMEINFO config = gameconfig;
	assert(ghost_prepare_race() == 0);
	assert(memcmp(&state, &before, sizeof(state)) == 0);
	assert(memcmp(&gameconfig, &config, sizeof(config)) == 0);
	assert(random_seed[0] == 99);
	assert(legacy_execution_residue.penalty_route_word == 77);
	assert(legacy_render_player_headings_active == 5);
	assert(game_replay_mode == REPLAY_MODE_PAUSED);
	assert(is_in_replay == 0);
	assert(replay_recording_flags == REPLAY_RECORDING_ACTIVE_FLAG);
	assert(framespersec == GAME_FRAME_RATE_NORMAL);
	assert(elapsed_time1 == 111 && elapsed_time2 == 222);
	assert(checkpoint_frame_interval == 999 && timer_ticks_per_frame == 11);
	assert(simulation_loads == 1 && simulation_frees == 1);
	assert(simulation_steps == TEST_RECORDING_FRAMES);
	assert(ghost_car_simd()->car_height == 47);
	assert(ghost_car_state()->car_position.lx == 0);
	assert_camera_at_frame(0);
	assert_camera_motion(100, 200, 300);
	ghost_update(6, GAME_FRAME_RATE_NORMAL);
	assert(ghost_car_state()->car_position.lx == 6);
	assert(ghost_car_state()->car_position.lz == 9);
	assert_camera_at_frame(3);
	assert_camera_motion(95, 210, 285);
	assert(ghost_car_state()->car_sound_flags == CAR_SOUND_NONE);
	ghost_update(16, GAME_FRAME_RATE_NORMAL);
	assert(ghost_car_state()->car_position.lx == 36);
	assert_camera_at_frame(8);
	assert_camera_motion(95, 210, 285);
	/* The next live tick holds the same pose but must stop listener motion. */
	ghost_update(17, GAME_FRAME_RATE_NORMAL);
	assert_camera_at_frame(8);
	assert_camera_motion(100, 200, 300);
	ghost_update(200000UL, GAME_FRAME_RATE_NORMAL);
	assert(ghost_car_state()->car_position.lx == 36);
	assert_camera_at_frame(8);
	assert_camera_motion(100, 200, 300);
	ghost_update(2, GAME_FRAME_RATE_NORMAL);
	assert(ghost_car_state()->car_position.lx == 1);
	assert_camera_at_frame(1);
	assert_camera_motion(95, 210, 285);
	/* A frame-rate change can reuse a pose while changing its audio delta. */
	ghost_update(1, GAME_FRAME_RATE_LOW);
	assert_camera_at_frame(1);
	assert_camera_motion(90, 220, 270);
	assert(memcmp(&state, &before, sizeof(state)) == 0);
	followOpponentFlag = 1;
	ghost_end_race();
	assert(followOpponentFlag == 0);
	assert_camera_motion(90, 220, 270);
	assert(ghost_is_selected() && !ghost_is_active());
	assert(ghost_camera_state() == 0);
	assert(ghost_prepare_race() == 0);
	assert(ghost_car_state()->car_position.lx == 0);
	assert_camera_at_frame(0);
	gameconfig.game_opponenttype = 1;
	followOpponentFlag = 1;
	assert(!ghost_is_active() && ghost_car_state() == 0);
	assert(ghost_camera_state() == 0);
	assert(ghost_prepare_race() == 0);
	assert(simulation_loads == 2);
	assert(followOpponentFlag == 1);
	gameconfig.game_opponenttype = 0;
	assert(ghost_prepare_race() == 0);
	followOpponentFlag = 1;
	fail_read = 1;
	ghost_update(4, GAME_FRAME_RATE_NORMAL);
	fail_read = 0;
	assert(followOpponentFlag == 0);
	assert(!ghost_is_active());
	assert(ghost_camera_state() == 0);
	assert(ghost_is_selected());
}

static void test_long_recording_and_preparation_failure(void)
{
	make_replay("maximum.rpl", GAME_FRAME_RATE_NORMAL, TRACKDATA_REPLAY_INPUT_BUFFER_SIZE, 0);
	assert(ghost_select_replay(0, (const legacy_s8 *)"maximum") == 0);
#ifdef RESTUNTS_SDL3
	/* Unrealistic high travel exhausts the bounded optional cache; the ghost
	 * still works, no incomplete route is exposed, and preparation can retry. */
	line_fixture = 7;
	assert(ghost_prepare_race() == 0);
	assert(ghost_is_active());
	for (legacy_s16 south = 0; south < 30; south++) {
		for (legacy_s16 east = 0; east < 30; east++) {
			assert(ghost_racing_line_first(east, south) == 0);
		}
	}
	line_fixture = 0;
#endif
	struct GAMESTATE before = state;
	struct GAMEINFO config = gameconfig;
	fail_write = 1;
	assert(ghost_prepare_race() != 0);
	fail_write = 0;
	assert(ghost_is_selected() && !ghost_is_active());
	assert(ghost_camera_state() == 0);
	assert(memcmp(&state, &before, sizeof(state)) == 0);
	assert(memcmp(&gameconfig, &config, sizeof(config)) == 0);
	assert(ghost_prepare_race() == 0);
	/* This seek crosses the DOS 64 KiB boundary and converts 10 fps live
	 * timing into a 20 fps recording's timeline. */
	ghost_update(500, GAME_FRAME_RATE_LOW);
	legacy_s32 expected = 0;
	for (legacy_u16 frame = 0; frame < 1000; frame++) {
		expected += test_inputs[frame];
	}
	assert(ghost_car_state()->car_position.lx == expected);
	assert(ghost_car_state()->car_position.lz == 3000);
	assert_camera_at_frame(1000);
	assert_camera_motion(80, 240, 240);
	ghost_update(6000, GAME_FRAME_RATE_LOW);
	assert(ghost_car_state()->car_position.lz == 36000);
	assert_camera_at_frame(TRACKDATA_REPLAY_INPUT_BUFFER_SIZE);
	ghost_update(65536UL, GAME_FRAME_RATE_NORMAL);
	assert(ghost_car_state()->car_position.lz == 36000);
	assert_camera_at_frame(TRACKDATA_REPLAY_INPUT_BUFFER_SIZE);
	ghost_update(0, GAME_FRAME_RATE_NORMAL);
	assert(ghost_car_state()->car_position.lx == 0);
	assert_camera_at_frame(0);
	assert(ghost_car_state()->car_position.lz == 0);
	make_replay("slowmax.rpl", GAME_FRAME_RATE_LOW, TRACKDATA_REPLAY_INPUT_BUFFER_SIZE, 0);
	assert(ghost_select_replay(0, (const legacy_s8 *)"slowmax") == 0);
	assert(ghost_prepare_race() == 0);
	ghost_update(24000UL, GAME_FRAME_RATE_NORMAL);
	assert(ghost_car_state()->car_position.lz == 36000);
	assert_camera_at_frame(TRACKDATA_REPLAY_INPUT_BUFFER_SIZE);
	ghost_update(1, GAME_FRAME_RATE_NORMAL);
	assert(ghost_car_state()->car_position.lz == 0);
	assert_camera_at_frame(0);
}

#ifdef RESTUNTS_SDL3
static legacy_u32 collect_racing_line(const struct GHOST_RACING_LINE_SEGMENT **segments,
									  legacy_u32 capacity)
{
	legacy_u32 count = 0;
	for (legacy_s16 south = 0; south < 30; south++) {
		for (legacy_s16 east = 0; east < 30; east++) {
			for (const struct GHOST_RACING_LINE_SEGMENT *segment =
					 ghost_racing_line_first(east, south);
				 segment != 0; segment = ghost_racing_line_next(segment)) {
				assert(count < capacity);
				segments[count++] = segment;
				legacy_s32 x = (segment->start_left.lx + segment->start_right.lx +
								segment->end_left.lx + segment->end_right.lx) /
							   4;
				legacy_s32 z = (segment->start_left.lz + segment->start_right.lz +
								segment->end_left.lz + segment->end_right.lz) /
							   4;
				assert((x >> 16) == east);
				assert(29 - (z >> 16) == south);
			}
		}
	}
	return count;
}

static void test_racing_line_speed_colors(void)
{
	legacy_u16 speed = 60U * 256U;
	assert(ghost_racing_line_color(speed, speed) == GHOST_RACING_LINE_YELLOW);
	assert(ghost_racing_line_color(speed + 768U, speed) == GHOST_RACING_LINE_YELLOW);
	assert(ghost_racing_line_color(speed - 768U, speed) == GHOST_RACING_LINE_YELLOW);
	assert(ghost_racing_line_color(speed + 769U, speed) == GHOST_RACING_LINE_RED);
	assert(ghost_racing_line_color(speed - 769U, speed) == GHOST_RACING_LINE_GREEN);
	assert(ghost_racing_line_color(0, 65535U) == GHOST_RACING_LINE_GREEN);
	assert(ghost_racing_line_color(65535U, 0) == GHOST_RACING_LINE_RED);
	assert(ghost_racing_line_color(0, 768U) == GHOST_RACING_LINE_YELLOW);
	assert(ghost_racing_line_color(65535U, 65535U - 768U) == GHOST_RACING_LINE_YELLOW);
}

static void test_racing_line_sampling_and_lifetime(void)
{
	const struct GHOST_RACING_LINE_SEGMENT *segments[64];
	line_fixture = 1;
	assert(ghost_select_replay(0, (const legacy_s8 *)"source") == 0);
	struct GAMESTATE before = state;
	assert(ghost_prepare_race() == 0);
	assert(memcmp(&state, &before, sizeof(before)) == 0);
	/* 800 renderer units = 48.768 metres: 24 complete pieces and a short tail.
	 * The whole route is present before the ghost or the live car advances. */
	assert(collect_racing_line(segments, 64) == 25);
	legacy_u32 full_count = 0;
	legacy_s32 total_length = 0;
	for (legacy_u16 index = 0; index < 25; index++) {
		const struct GHOST_RACING_LINE_SEGMENT *segment = segments[index];
		legacy_s32 length = segment->end_left.lz - segment->start_left.lz;
		assert(length > 0 && length <= 2100);
		if (length >= 2099) {
			full_count++;
		}
		total_length += length;
		assert(segment->start_left.lx == 998L * 64L);
		assert(segment->start_right.lx == 1002L * 64L);
		assert(segment->start_left.ly == 128);
		assert(segment->end_left.ly == 128);
		legacy_f64 midpoint = (segment->start_left.lz + segment->end_left.lz) * 0.5;
		legacy_s32 expected_speed = (legacy_s32)(10240 + (midpoint - 64000) * 0.2 + 0.5);
		assert((legacy_s32)segment->speed - expected_speed >= -1);
		assert((legacy_s32)segment->speed - expected_speed <= 1);
	}
	assert(full_count == 24 && total_length == 800L * 64L);
	assert(ghost_racing_line_first(-1, 0) == 0);
	assert(ghost_racing_line_first(30, 0) == 0);
	assert(ghost_racing_line_first(0, -1) == 0);
	assert(ghost_racing_line_first(0, 30) == 0);
	assert(ghost_racing_line_next(0) == 0);
	struct GHOST_RACING_LINE_SEGMENT cached = *segments[0];
	struct CARSTATE ghost_before = *ghost_car_state();
	legacy_u32 reads = read_calls;
	legacy_s32 steps = simulation_steps;
	for (legacy_u16 speed = 0; speed < 200U * 256U; speed += 256U) {
		(void)ghost_racing_line_color(speed, segments[0]->speed);
		assert(collect_racing_line(segments, 64) == 25);
	}
	assert(read_calls == reads && simulation_steps == steps);
	assert(memcmp(&cached, segments[0], sizeof(cached)) == 0);
	assert(memcmp(&ghost_before, ghost_car_state(), sizeof(ghost_before)) == 0);
	ghost_update(24000, GAME_FRAME_RATE_NORMAL);
	assert(collect_racing_line(segments, 64) == 25);
	assert(memcmp(&cached, segments[0], sizeof(cached)) == 0);
	ghost_update(0, GAME_FRAME_RATE_LOW);
	assert(collect_racing_line(segments, 64) == 25);
	assert(memcmp(&cached, segments[0], sizeof(cached)) == 0);
	ghost_end_race();
	assert(collect_racing_line(segments, 64) == 0);
	assert(ghost_prepare_race() == 0);
	assert(collect_racing_line(segments, 64) == 25);
	/* A 20 Hz recording produces the same distance samples as the 10 Hz source. */
	assert(ghost_select_replay(0, (const legacy_s8 *)"render") == 0);
	assert(collect_racing_line(segments, 64) == 0);
	assert(ghost_prepare_race() == 0);
	assert(collect_racing_line(segments, 64) == 25);
	assert(memcmp(&cached.start_left, &segments[0]->start_left, 4U * sizeof(struct VECTORLONG)) ==
		   0);
	assert(cached.speed == segments[0]->speed && cached.next == segments[0]->next);
	line_fixture = 0;
}

static void test_racing_line_curves_and_short_recordings(void)
{
	const struct GHOST_RACING_LINE_SEGMENT *segments[64];
	line_fixture = 2;
	assert(ghost_select_replay(0, (const legacy_s8 *)"source") == 0);
	assert(ghost_prepare_race() == 0);
	assert(collect_racing_line(segments, 64) == 25);
	legacy_u16 turning = 0;
	for (legacy_u16 index = 0; index < 25; index++) {
		const struct GHOST_RACING_LINE_SEGMENT *segment = segments[index];
		if (segment->start_left.lx != segment->end_left.lx &&
			segment->start_left.lz != segment->end_left.lz) {
			turning++;
		}
	}
	assert(turning == 1);
	line_fixture = 3;
	assert(ghost_prepare_race() == 0);
	assert(collect_racing_line(segments, 64) == 1);
	assert(segments[0]->end_left.lz - segments[0]->start_left.lz == 16L * 64L);
	assert(segments[0]->speed == 60U * 256U);
	/* A vertical loop face keeps the ribbon in the wheel plane: its lift points
	 * toward -Z while its width remains across X, rather than floating up Y. */
	line_fixture = 4;
	assert(ghost_prepare_race() == 0);
	assert(collect_racing_line(segments, 64) == 25);
	for (legacy_u16 index = 0; index < 25; index++) {
		assert(segments[index]->start_left.lz == 998L * 64L);
		assert(segments[index]->end_left.lz == 998L * 64L);
		assert(segments[index]->end_left.ly > segments[index]->start_left.ly);
	}
	/* Initialization has coincident wheels. Their real bank becomes available
	 * while stationary, before the first movement: the initial ribbon endpoint
	 * must acquire that bank too, rather than keeping the default X/Y axes. */
	line_fixture = 8;
	assert(ghost_prepare_race() == 0);
	assert(collect_racing_line(segments, 64) == 22);
	legacy_u16 initial_endpoint = 0;
	for (legacy_u16 index = 0; index < 22; index++) {
		if (segments[index]->start_left.lz == 1000L * 64L) {
			initial_endpoint++;
			assert(segments[index]->start_left.lx == 998L * 64L);
			assert(segments[index]->start_right.lx == 998L * 64L);
			assert(segments[index]->start_left.ly == -128);
			assert(segments[index]->start_right.ly == 128);
		}
	}
	assert(initial_endpoint == 1);
	line_fixture = 5;
	assert(ghost_prepare_race() == 0);
	assert(collect_racing_line(segments, 64) == 23);
	for (legacy_u16 index = 0; index < 23; index++) {
		assert(segments[index]->end_left.lz - segments[index]->start_left.lz <= 2100);
	}
	line_fixture = 6;
	assert(ghost_prepare_race() == 0);
	assert(collect_racing_line(segments, 64) == 0);
	line_fixture = 1;
	fail_write = 1;
	assert(ghost_prepare_race() != 0);
	fail_write = 0;
	assert(collect_racing_line(segments, 64) == 0);
	assert(ghost_prepare_race() == 0);
	assert(collect_racing_line(segments, 64) == 25);
	test_track[100]++;
	ghost_check_track();
	assert(collect_racing_line(segments, 64) == 0);
	line_fixture = 0;
}

static void assert_render_sample(legacy_u32 live_frame, legacy_u16 live_rate, legacy_u32 lag,
								 legacy_s32 position)
{
	struct CARSTATE authoritative = *ghost_car_state();
	struct GHOST_CAMERA_STATE authoritative_camera = *ghost_camera_state();
	struct GAMESTATE before = state;
	struct GAMEINFO config = gameconfig;
	legacy_s32 steps = simulation_steps;
	struct CARSTATE rendered;
	struct GHOST_CAMERA_STATE camera;
	assert(ghost_sample_render_pose(live_frame, live_rate, lag, &rendered, &camera));
	assert(rendered.car_position.lx == position);
	assert(camera.follow_position.x == 100 + position);
	assert(camera.previous_position.x == position);
	assert(camera.frame == authoritative_camera.frame);
	assert(camera.crash_frame == authoritative_camera.crash_frame);
	assert(rendered.car_crashBmpFlag == authoritative.car_crashBmpFlag);
	assert(rendered.car_sound_flags == CAR_SOUND_NONE);
	assert(rendered.car_actual_speed == authoritative.car_actual_speed);
	assert(memcmp(rendered.car_surfaceWhl, authoritative.car_surfaceWhl,
				  sizeof(rendered.car_surfaceWhl)) == 0);
	assert(memcmp(&authoritative, ghost_car_state(), sizeof(authoritative)) == 0);
	assert(memcmp(&authoritative_camera, ghost_camera_state(), sizeof(authoritative_camera)) == 0);
	assert(memcmp(&before, &state, sizeof(before)) == 0);
	assert(memcmp(&config, &gameconfig, sizeof(config)) == 0);
	assert(simulation_steps == steps);
}

static void test_render_sampling(void)
{
	render_fixture = 1;
	assert(ghost_select_replay(0, (const legacy_s8 *)"source") == 0);
	assert(ghost_prepare_race() == 0);
	/* A 10 Hz recorded ghost moves in 60 Hz slots alongside a 20 Hz live car,
	 * even when the authoritative ghost repeats its last recorded pose. */
	ghost_update(1, GAME_FRAME_RATE_NORMAL);
	legacy_u32 reads = read_calls;
	assert_render_sample(1, GAME_FRAME_RATE_NORMAL,
						 FRAME_INTERPOLATION_ONE - FRAME_INTERPOLATION_ONE / 3U, 16);
	assert(read_calls <= reads + 2U);
	reads = read_calls;
	assert_render_sample(1, GAME_FRAME_RATE_NORMAL,
						 FRAME_INTERPOLATION_ONE - FRAME_INTERPOLATION_ONE * 2U / 3U, 33);
	assert_render_sample(1, GAME_FRAME_RATE_NORMAL, 0, 50);
	assert_render_sample(2, GAME_FRAME_RATE_NORMAL,
						 FRAME_INTERPOLATION_ONE - FRAME_INTERPOLATION_ONE / 3U, 66);
	assert_render_sample(2, GAME_FRAME_RATE_NORMAL,
						 FRAME_INTERPOLATION_ONE - FRAME_INTERPOLATION_ONE * 2U / 3U, 83);
	assert(read_calls == reads);
	ghost_update(2, GAME_FRAME_RATE_NORMAL);
	assert_render_sample(2, GAME_FRAME_RATE_NORMAL, 0, 100);
	assert_camera_motion(95, 210, 285);

	/* Presentation I/O failure preserves both fallback outputs and the live ghost. */
	struct CARSTATE before = *ghost_car_state();
	struct GHOST_CAMERA_STATE before_camera = *ghost_camera_state();
	struct CARSTATE rendered = before;
	struct GHOST_CAMERA_STATE camera = before_camera;
	fail_read = 1;
	assert(!ghost_sample_render_pose(14, GAME_FRAME_RATE_NORMAL, FRAME_INTERPOLATION_ONE / 2U,
									 &rendered, &camera));
	fail_read = 0;
	assert(memcmp(&rendered, &before, sizeof(before)) == 0);
	assert(memcmp(&camera, &before_camera, sizeof(before_camera)) == 0);
	assert(memcmp(ghost_car_state(), &before, sizeof(before)) == 0);
	assert(memcmp(ghost_camera_state(), &before_camera, sizeof(before_camera)) == 0);
	assert(ghost_is_active());
	assert_render_sample(14, GAME_FRAME_RATE_NORMAL, FRAME_INTERPOLATION_ONE / 2U, 675);
	assert_render_sample(1, GAME_FRAME_RATE_NORMAL, FRAME_INTERPOLATION_ONE / 2U, 25);
	assert_render_sample(0, GAME_FRAME_RATE_NORMAL, FRAME_INTERPOLATION_ONE, 0);
	assert_render_sample(UINT32_C(0xffffffff), GAME_FRAME_RATE_NORMAL, 0, 800);
	assert(!ghost_sample_render_pose(1, 0, 0, &rendered, &camera));
	assert(memcmp(&rendered, &before, sizeof(before)) == 0);
	assert(memcmp(&camera, &before_camera, sizeof(before_camera)) == 0);

	/* Replacing the recording invalidates the sampled bracket. A 20 Hz source
	 * also advances smoothly between the larger 10 Hz live intervals. */
	make_replay("render.rpl", GAME_FRAME_RATE_NORMAL, TEST_RECORDING_FRAMES, 0);
	assert(ghost_select_replay(0, (const legacy_s8 *)"render") == 0);
	assert(ghost_prepare_race() == 0);
	ghost_update(1, GAME_FRAME_RATE_LOW);
	assert_render_sample(1, GAME_FRAME_RATE_LOW,
						 FRAME_INTERPOLATION_ONE - FRAME_INTERPOLATION_ONE / 6U, 33);
	assert_render_sample(1, GAME_FRAME_RATE_LOW,
						 FRAME_INTERPOLATION_ONE - FRAME_INTERPOLATION_ONE * 2U / 6U, 66);
	assert_render_sample(1, GAME_FRAME_RATE_LOW, FRAME_INTERPOLATION_ONE / 2U, 100);
	assert_render_sample(1, GAME_FRAME_RATE_LOW,
						 FRAME_INTERPOLATION_ONE - FRAME_INTERPOLATION_ONE * 4U / 6U, 133);
	assert_render_sample(1, GAME_FRAME_RATE_LOW,
						 FRAME_INTERPOLATION_ONE - FRAME_INTERPOLATION_ONE * 5U / 6U, 166);
	assert_render_sample(1, GAME_FRAME_RATE_LOW, 0, 200);
	ghost_end_race();
	assert(!ghost_sample_render_pose(1, GAME_FRAME_RATE_LOW, 0, &rendered, &camera));
	render_fixture = 0;
}

static void test_render_discontinuities(void)
{
	render_fixture = 2;
	assert(ghost_select_replay(0, (const legacy_s8 *)"source") == 0);
	assert(ghost_prepare_race() == 0);
	ghost_update(5, GAME_FRAME_RATE_NORMAL);
	/* The upper interpolation endpoint knows a future collision, but neither
	 * its body position nor its camera/effect appears before confirmation. */
	assert_render_sample(5, GAME_FRAME_RATE_NORMAL, 0, 200);
	ghost_update(6, GAME_FRAME_RATE_NORMAL);
	assert_render_sample(6, GAME_FRAME_RATE_NORMAL, FRAME_INTERPOLATION_ONE / 2U, 300);

	struct CARSTATE rendered;
	struct GHOST_CAMERA_STATE camera;
	ghost_update(7, GAME_FRAME_RATE_NORMAL);
	struct GHOST_CAMERA_STATE authoritative_camera = *ghost_camera_state();
	assert(ghost_sample_render_pose(7, GAME_FRAME_RATE_NORMAL, 0, &rendered, &camera));
	assert(rendered.car_position.lx == 350);
	assert(memcmp(&camera, &authoritative_camera, sizeof(camera)) == 0);
	ghost_update(8, GAME_FRAME_RATE_NORMAL);
	authoritative_camera = *ghost_camera_state();
	assert(ghost_sample_render_pose(8, GAME_FRAME_RATE_NORMAL, FRAME_INTERPOLATION_ONE / 2U,
									&rendered, &camera));
	assert(rendered.car_position.lx == 375);
	assert(memcmp(&camera, &authoritative_camera, sizeof(camera)) == 0);
	/* Preparation discards cached poses even when the selection is unchanged. */
	render_fixture = 1;
	assert(ghost_prepare_race() == 0);
	ghost_update(5, GAME_FRAME_RATE_NORMAL);
	assert_render_sample(5, GAME_FRAME_RATE_NORMAL, 0, 250);
	render_fixture = 0;
}
#endif

static void test_track_changes_and_cleanup(void)
{
	test_track[100]++;
	ghost_check_track();
	assert(!ghost_is_selected());
	assert(ghost_select_replay(0, (const legacy_s8 *)"source") == 0);
	gameconfig.game_trackname[0] = 'X';
	ghost_check_track();
	assert(!ghost_is_selected());
	assert(ghost_select_replay(0, (const legacy_s8 *)"source") == 0);
	cleanup_handler();
	assert(!ghost_is_selected());
	for (legacy_u16 index = 1; index < TEST_FILE_COUNT; index++) {
		if (files[index].exists) {
			assert(files[index].name[0] != 'G');
			fclose(files[index].stream);
		}
	}
}

int main(void)
{
	test_selection_is_separate();
	test_invalid_selection_preserves_previous();
	test_playback_timing_and_isolation();
#ifdef RESTUNTS_SDL3
	test_render_sampling();
	test_render_discontinuities();
	test_racing_line_speed_colors();
	test_racing_line_sampling_and_lifetime();
	test_racing_line_curves_and_short_recordings();
#endif
	test_long_recording_and_preparation_failure();
	test_track_changes_and_cleanup();
	return 0;
}
