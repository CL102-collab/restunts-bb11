/* Startup timing, option handling, menu actions and race cleanup use traces
 * captured from the pre-refactor implementation with stubbed DOS endpoints. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "../c/restunts.c"
#undef printf
#undef strlen
#undef strcmp
#undef strcpy
#undef memcpy
void ghost_clear(void)
{
}

static legacy_u32 trace_hash = UINT32_C(2166136261);
static legacy_u32 timer_calls, status_calls;
static legacy_u32 geometry_ticks, clear_ticks, partial_ticks;
static legacy_s16 audio_failure;
static legacy_u16 valid_game_version = 1;
static legacy_u16 startup_error_calls, startup_cleanup_calls;
static legacy_u16 startup_audio_calls, startup_video_calls;
static legacy_s16 startup_exit_status;
static jmp_buf exit_jump;
#ifdef RESTUNTS_SDL3
legacy_u8 supersight_enabled;
legacy_u8 fps_display_enabled;
static legacy_s32 startup_render_enabled;
static legacy_s32 startup_render_scale = HIRES_SCALE;
static legacy_u16 expected_hypervision_preset;
static legacy_u8 expected_hypervision_enabled;
static legacy_s32 startup_audio_driver_loaded;
static legacy_s32 startup_ogg_music_enabled;
static legacy_s32 expected_ogg_music_enabled;
static legacy_u32 startup_ogg_set_calls;
static legacy_u8 expected_fps_display_enabled;
static legacy_u32 startup_fps_reset_calls;
#ifdef __ANDROID__
static legacy_u8 startup_control_layout_visible = STARTUP_CONTROL_LAYOUT_ON;
static legacy_u32 startup_control_layout_set_calls;

void sdl3_touch_set_layout_visible(legacy_u8 visible)
{
	assert(visible == STARTUP_CONTROL_LAYOUT_OFF || visible == STARTUP_CONTROL_LAYOUT_ON);
	startup_control_layout_visible = visible;
	++startup_control_layout_set_calls;
}
#endif

void frame_fps_reset(void)
{
	++startup_fps_reset_calls;
}

void sdl3_music_set_enabled(legacy_s32 enabled)
{
	assert(startup_audio_driver_loaded);
	startup_audio_driver_loaded = 0;
	startup_ogg_music_enabled = enabled;
	++startup_ogg_set_calls;
}

void hires_set_enabled(legacy_s32 enabled)
{
	startup_render_enabled = enabled;
	startup_render_scale = HIRES_SCALE;
}

void hires_set_render_scale(legacy_s32 scale)
{
	assert(startup_render_enabled != 0);
	startup_render_scale = scale;
}

static void check_startup_hypervision(void)
{
	if (expected_hypervision_enabled != 0) {
		assert(supersight_enabled != 0 && startup_render_enabled != 0);
		assert(frame_adaptive.preset == expected_hypervision_preset);
		assert(startup_render_scale == frame_adaptive_render_scale(&frame_adaptive));
	}
}
#endif

static void trace(legacy_u32 value)
{
	for (legacy_u32 i = 0; i < 4; i++) {
		trace_hash = (trace_hash ^ (value & 255U)) * UINT32_C(16777619);
		value >>= 8;
	}
}
void kb_init_interrupt(void)
{
	trace(1);
}
void dos_kb_clear_numlock(void)
{
	trace(2);
}
legacy_s16 kb_call_readchar_callback(void)
{
	trace(3);
	return 0;
}
void kb_reg_callback(legacy_s16 code, void(far *callback)(void))
{
	void (*callbacks[])(void) = {show_graphic_levels_menu,	calibrate_joystick_driving,
								 select_keyboard_driving,	toggle_music_with_dialog,
								 show_pause_dialog,			show_exit_to_dos_dialog,
								 toggle_effects_with_dialog};
	trace(4);
	trace(code);
	for (legacy_u32 i = 0; i < 7; i++) {
		if (callback == callbacks[i]) {
			trace(i);
			return;
		}
	}
	assert(0);
}
void show_graphic_levels_menu(void)
{
	assert(0);
}
void calibrate_joystick_driving(void)
{
	assert(0);
}
void select_keyboard_driving(void)
{
	assert(0);
}
void toggle_music_with_dialog(void)
{
	assert(0);
}
void show_pause_dialog(void)
{
	assert(0);
}
void show_exit_to_dos_dialog(void)
{
	assert(0);
}
void toggle_effects_with_dialog(void)
{
	assert(0);
}
void init_video_geometry_flags(void)
{
	trace(5);
}
void mmgr_init_conventional_arena(void)
{
	trace(6);
}
legacy_u16 file_game_version_matches(void)
{
	return valid_game_version;
}
void dos_show_startup_error(const legacy_s8 *message)
{
	assert(strstr((const legacy_char *)message, "Broderbund Stunts 1.1 (Feb 12 1991)") != 0);
	startup_error_calls++;
}
void call_exitlist(void)
{
	startup_cleanup_calls++;
}
void audio_allocate_car_state_records(void)
{
	startup_audio_calls++;
	trace(7);
}
void dos_video_set_mode_13h(void)
{
	startup_video_calls++;
#ifdef RESTUNTS_SDL3
	check_startup_hypervision();
	assert(fps_display_enabled == expected_fps_display_enabled);
#endif
	trace(8);
}
void dos_video_set_mode4(void)
{
	trace(9);
}
void dos_timer_setup_interrupt(void)
{
	trace(10);
}
void sprite_select_screen_and_clear(void)
{
	trace(11);
}
legacy_s16 dos_mouse_init(legacy_s16 width, legacy_s16 height)
{
	trace(12);
	trace(width);
	trace(height);
	return 0;
}
legacy_s16 audio_load_dos_driver(const legacy_s8 *name, legacy_s16 a, legacy_s16 b)
{
	trace(13);
	trace(name[0]);
	trace(name[1]);
	trace(a);
	trace(b);
#ifdef RESTUNTS_SDL3
	startup_audio_driver_loaded = audio_failure == 0;
	/* Mirror real driver initialization resetting the source preference. */
	startup_ogg_music_enabled = 0;
#endif
	return audio_failure;
}
void dos_timer_shutdown(void)
{
	trace(14);
}
void dos_process_exit(legacy_s16 status)
{
	startup_exit_status = status;
	trace(15);
	trace(status);
	longjmp(exit_jump, 1);
}
legacy_s16 audio_toggle_music(void)
{
	trace(16);
	return 0;
}
legacy_s16 audio_toggle_effects(void)
{
	trace(17);
	return 0;
}
legacy_s16 show_disk_error_dialog(void)
{
	assert(0);
	return 0;
}
void dos_set_critical_error_handler(legacy_s16(far *callback)(void))
{
	assert(callback == show_disk_error_dialog);
	trace(18);
}
void load_palandcursor(void)
{
	trace(19);
}
void sprite_select_screen(void)
{
	trace(20);
}
void sprite_set_target_clip_bounds(legacy_u16 left, legacy_u16 right, legacy_u16 top,
								   legacy_u16 bottom)
{
	trace(21);
	trace(left);
	trace(right);
	trace(top);
	trace(bottom);
}
void sprite_clear_target(legacy_u8 color)
{
	trace(22);
	trace(color);
}
legacy_u32 timer_get_delta_alt(void)
{
	trace(23);
	switch (timer_calls++) {
		case 0:
			return 99;
		case 1:
			return clear_ticks;
		case 2:
			return partial_ticks;
		default:
			assert(timer_calls == 4);
			return geometry_ticks;
	}
}
legacy_s16 dos_video_get_status(void)
{
	trace(24);
	return status_calls++ & 1;
}
legacy_s16 _rand(void)
{
	trace(25);
	return 19;
}
legacy_s16 get_kevinrandom(void)
{
	trace(26);
	return 21;
}
static legacy_u32 menu_scenario, menu_calls, intro_calls, game_calls, score_calls;
static legacy_s32 expected_initial_intro_calls = -1;
static legacy_u8 menu_track_data[REPLAY_TRACK_SIZE];
static legacy_s8 backup_memory[REPLAY_TRACK_SIZE + 162];
static legacy_s8 menu_resource[64];
static legacy_u8 checkpoint_memory[32];

static void trace_text(const legacy_s8 *text)
{
	while (*text) {
		trace((legacy_u8)*text++);
	}
	trace(0);
}
void *_memcpy(void *destination, const void *source, legacy_u16 count)
{
	return memcpy(destination, source, count);
}
void dos_install_divide_error_handler(void)
{
	trace(30);
}
void init_row_tables(void)
{
	trace(31);
}
void far *file_load_resfile(const legacy_s8 *name)
{
	trace(32);
	trace_text(name);
	return menu_resource;
}
void far *file_load_resource(legacy_s16 type, const legacy_s8 *name)
{
	trace(33);
	trace(type);
	trace_text(name);
	return menu_resource;
}
void font_set_fontdef(void)
{
	trace(34);
}
void init_polyinfo(void)
{
	trace(35);
}
void init_trackdata(void)
{
	trace(36);
}
void reset_race_loop_state(void)
{
	trace(37);
}
void init_kevinrandom(const legacy_s8 *seed)
{
	trace(38);
	trace_text(seed);
}
legacy_s16 input_do_checking(legacy_s16 ticks)
{
	trace(39);
	trace(ticks);
	return 0;
}
void mouse_draw_opaque_check(void)
{
	trace(40);
}
void ensure_file_exists(legacy_s16 disk)
{
	trace(41);
	trace(disk);
}
void file_build_path(const legacy_s8 *directory, const legacy_s8 *name, const legacy_s8 *ext,
					 legacy_s8 *destination)
{
	trace(42);
	trace_text(directory);
	trace_text(name);
	trace_text(ext);
	strcpy((char *)destination, "fixture.trk");
}
void far *file_read_fatal(const legacy_s8 *name, void far *destination)
{
	trace(43);
	trace_text(name);
	assert(destination == menu_track_data);
	return destination;
}
legacy_s16 run_intro_looped(void)
{
#ifdef RESTUNTS_SDL3
	check_startup_hypervision();
	assert(startup_ogg_music_enabled == expected_ogg_music_enabled);
	assert(fps_display_enabled == expected_fps_display_enabled);
#endif
	trace(44);
	assert(intro_calls < 3);
	if (expected_initial_intro_calls >= 0) {
		intro_calls++;
		return menu_calls == 0 ? 0 : 27;
	}
	return ++intro_calls == 1 ? 0 : 27;
}
legacy_s8 far *locate_text_res(legacy_s8 far *resource, const legacy_s8 *name)
{
	assert(resource == menu_resource);
	trace(45);
	trace_text(name);
	return menu_resource;
}
legacy_u16 show_dialog(legacy_s16 type, legacy_s16 save, void far *text, legacy_u16 x, legacy_u16 y,
					   legacy_s16 color, legacy_s16 *flags, legacy_s16 selected)
{
	assert(text == menu_resource);
	assert(flags == 0);
	trace(46);
	trace(type);
	trace(save);
	trace(x);
	trace(y);
	trace(color);
	trace(selected);
	return 1;
}
void dos_audio_shutdown(void)
{
	trace(47);
}
void kb_exit_handler(void)
{
	trace(48);
}
void dos_kb_set_numlock(void)
{
	trace(49);
}
void dos_video_set_mode7(void)
{
	trace(50);
}
void file_load_audiores(const legacy_s8 *song, const legacy_s8 *voice, const legacy_s8 *id)
{
	trace(51);
	trace_text(song);
	trace_text(voice);
	trace_text(id);
	is_audioloaded = 1;
}
legacy_s8 run_menu(void)
{
	/* The menu now owns music loading as well as its deferred playback. */
	if (is_audioloaded == 0) {
		file_load_audiores("skidslct", "skidms", "SLCT");
	}
#ifdef RESTUNTS_SDL3
	check_startup_hypervision();
#endif
#ifdef RESTUNTS_SDL3
	assert(startup_ogg_music_enabled == expected_ogg_music_enabled);
	assert(fps_display_enabled == expected_fps_display_enabled);
#endif
	legacy_u32 call = menu_calls++;
	trace(52);
	assert(menu_calls < 10);
	if (expected_initial_intro_calls >= 0) {
		assert(menu_calls == 1);
		assert(intro_calls == (legacy_u32)expected_initial_intro_calls);
		assert(is_audioloaded != 0);
		return -1;
	}
	if (menu_scenario & 1) {
		idle_expired = 1;
	}
	static const legacy_s8 actions[] = {1, 2, 3, 4, 7, 0, -1};
	return actions[call];
}
void audio_unload(void)
{
	trace(53);
	is_audioloaded = 0;
}
void check_input(void)
{
	trace(54);
}
void show_waiting(void)
{
	trace(55);
}
void run_car_menu(legacy_s8 *id, legacy_s8 *material, legacy_s8 *transmission, legacy_u16 opponent)
{
	trace(56);
	trace(id[0]);
	trace(*material);
	trace(*transmission);
	trace(opponent);
	*material = 3;
}
void run_opponent_menu(void)
{
	trace(57);
	gameconfig.game_opponenttype = 2;
}
void run_tracks_menu(legacy_s16 reload)
{
	trace(58);
	trace(reload);
}
legacy_u16 run_option_menu(void)
{
	trace(59);
	return menu_scenario & 2;
}
legacy_s16 track_setup(void)
{
	trace(60);
	return menu_scenario & 4;
}
const legacy_s8 *file_find(const legacy_s8 *query)
{
	trace(61);
	trace_text(query);
	return menu_scenario & 4 ? 0 : (legacy_s8 *)"tedit.trk";
}
void fatal_error(const legacy_s8 *format, ...)
{
	(void)format;
	assert(0);
}
void far *mmgr_alloc_resbytes(const legacy_s8 *name, legacy_s32 size)
{
	trace(62);
	trace_text(name);
	trace(size);
	return checkpoint_memory;
}
void init_game_state(legacy_s16 mode)
{
	trace(63);
	trace(mode);
}
void run_game(void)
{
	trace(64);
	trace(gameconfig.game_recordedframes);
	trace(replay_recording_flags);
	assert(++game_calls < 8);
	gameconfig.game_recordedframes = 77;
	replay_recording_flags = menu_scenario & 8 ? 0 : 1;
	menu_track_data[0] = 222;
	track_directory[0] = 'X';
	replay_directory[0] = 'Y';
}
legacy_u16 end_hiscore(void)
{
	trace(65);
	return score_calls++ < 2 ? score_calls - 1 : 2;
}
void mmgr_release(void far *pointer)
{
	assert(pointer == checkpoint_memory);
	trace(66);
}

static void test_menu_lifecycle(void)
{
	legacy_s8 *arguments[] = {(legacy_s8 *)"game"};
	for (menu_scenario = 0; menu_scenario < 16; menu_scenario++) {
		trace(1000 + menu_scenario);
		memset(&gameconfig, 0, sizeof(gameconfig));
		for (legacy_u32 i = 0; i < REPLAY_TRACK_SIZE; i++) {
			menu_track_data[i] = (legacy_u8)i;
		}
		track_element_map = menu_track_data;
		track_and_directory_backup = backup_memory;
		memset(track_directory, 0, 81);
		memset(replay_directory, 0, 81);
		track_directory[0] = 'A';
		replay_directory[0] = 'B';
		menu_calls = intro_calls = game_calls = score_calls = 0;
		timer_calls = status_calls = 0;
		audio_failure = 0;
		is_audioloaded = 0;
		replay_recording_flags = 0;
		geometry_ticks = 55;
		clear_ticks = 15;
		partial_ticks = 16;
		trace(run_main_menu_loop(1, arguments));
		trace(menu_calls);
		trace(intro_calls);
		trace(game_calls);
		trace(score_calls);
		for (legacy_u32 i = 0; i < REPLAY_TRACK_SIZE; i++) {
			trace(menu_track_data[i]);
		}
		trace(track_directory[0]);
		trace(replay_directory[0]);
		trace(gameconfig.game_recordedframes);
		trace(gameconfigcopy.game_recordedframes);
	}
}

static void test_startup_intro_option(void)
{
	static legacy_s8 *arguments[][5] = {
		{(legacy_s8 *)"game", (legacy_s8 *)"--nointro"},
		{(legacy_s8 *)"game", (legacy_s8 *)"/ns", (legacy_s8 *)"--nointro", (legacy_s8 *)"/sSB"},
		{(legacy_s8 *)"game", (legacy_s8 *)"--nointro", (legacy_s8 *)"/ns", (legacy_s8 *)"/nd"},
		{(legacy_s8 *)"game", (legacy_s8 *)"--nointro", (legacy_s8 *)"--nointro"},
		{(legacy_s8 *)"game", (legacy_s8 *)"/no"},
		{(legacy_s8 *)"game", (legacy_s8 *)"--nointrox"},
		{(legacy_s8 *)"game", (legacy_s8 *)"nointro"},
		{(legacy_s8 *)"game", (legacy_s8 *)"/nointro"},
		{(legacy_s8 *)"game", (legacy_s8 *)"-nointro"},
		{(legacy_s8 *)"game", (legacy_s8 *)"--NOINTRO"},
		{(legacy_s8 *)"game"},
	};
	static const legacy_s16 counts[] = {2, 4, 4, 3, 2, 2, 2, 2, 2, 2, 1};
	for (legacy_u32 scenario = 0; scenario < sizeof(counts) / sizeof(counts[0]); scenario++) {
		expected_initial_intro_calls = scenario < 4 ? 0 : 1;
		menu_calls = intro_calls = game_calls = score_calls = 0;
		timer_calls = status_calls = 0;
		audio_failure = 0;
		is_audioloaded = 0;
		track_element_map = menu_track_data;
		geometry_ticks = 55;
		clear_ticks = 15;
		partial_ticks = 16;
		assert(run_main_menu_loop(counts[scenario], arguments[scenario]) == 1);
		assert(menu_calls == 1);
		assert(intro_calls == (legacy_u32)expected_initial_intro_calls + 1);
		assert(game_calls == 0);
	}
	expected_initial_intro_calls = -1;
}

static void test_startup_physics_options(void)
{
	legacy_s8 *arguments[] = {
		(legacy_s8 *)"game",	 (legacy_s8 *)"--nointro", (legacy_s8 *)"--PG:OFF",
		(legacy_s8 *)"--LC:OFF", (legacy_s8 *)"--LCB:OFF", (legacy_s8 *)"--pg:on",
		(legacy_s8 *)"--lc:on",	 (legacy_s8 *)"--lcb:on",  (legacy_s8 *)"--LCB:OFF"};
	static const legacy_s16 counts[] = {2, 3, 4, 5, 6, 7, 8, 9, 2};
	static const legacy_u16 expected_speeds[] = {17757, 15573, 15573, 15573, 17757,
												 17757, 17757, 17757, 17757};
	static const legacy_s16 expected_crossings[] = {100, 100, 0, 0, 0, 100, 100, 100, 100};
	const legacy_s16 cornering_angle = -7;
	const legacy_u8 cornering_shift = 2;
	const legacy_s16 original_cornering_response = -2;
	const legacy_s16 symmetric_cornering_response = -1;
	const legacy_s16 expected_cornering_responses[] = {
		original_cornering_response,  original_cornering_response,	original_cornering_response,
		symmetric_cornering_response, symmetric_cornering_response, symmetric_cornering_response,
		original_cornering_response,  symmetric_cornering_response, original_cornering_response};
	for (legacy_u32 scenario = 0; scenario < sizeof(counts) / sizeof(counts[0]); scenario++) {
		timer_calls = status_calls = 0;
		audio_failure = 0;
		init_main(counts[scenario], arguments);

		/* An Indy-mass car must decelerate under net drag only with --pg:off. */
		struct SIMD simd = {0};
		struct CARSTATE car = {0};
		legacy_s16 drag[64] = {0};
		drag[16000U >> 10] = 512;
		simd.car_mass = 15;
		simd.num_gears = 5;
		simd.idle_rpm = 1000;
		simd.max_rpm = 12000;
		simd.aerorestable = drag;
		car.car_transmission = TRANSMISSION_MANUAL;
		car.car_current_gear = 1;
		car.car_currpm = 1000;
		car.car_gearratio = 4096;
		car.car_gearratioshr8 = 16;
		car.car_sumSurfRearWheels = 2;
		car.car_sumSurfAllWheels = 4;
		car.car_rev_speed = car.car_actual_speed = 16000;
		framespersec = GAME_FRAME_RATE_NORMAL;
		update_car_speed(INPUT_ACCELERATE_FLAG, PLAYER_CAR_INDEX, &car, &simd);
		assert(car.car_actual_speed == expected_speeds[scenario]);

		/* Collision mode must be configured independently of the power gear option. */
		struct VECTOR first = {100, 200, -100};
		struct VECTOR second = {-100, -200, 100};
		struct VECTOR result;
		interpolate_collision_at_z(&first, &second, &result, 0);
		assert(result.x == expected_crossings[scenario]);
		assert(result.y == expected_crossings[scenario] * 2);
		assert(result.z == 0);

		/* Cornering mode must stay independent of the power gear and collision options. */
		assert(scale_cornering_angle(cornering_angle, cornering_shift) ==
			   expected_cornering_responses[scenario]);
	}
}

#ifdef RESTUNTS_SDL3
static void reset_startup_hypervision(void)
{
	supersight_enabled = 0;
	startup_render_enabled = 0;
	startup_render_scale = HIRES_SCALE;
	expected_hypervision_preset = FRAME_ADAPTIVE_PRESET_AUTO;
	expected_hypervision_enabled = 0;
	frame_adaptive_reset(&frame_adaptive);
	timer_calls = status_calls = 0;
	audio_failure = 0;
}

static void expect_startup_option_error(legacy_s16 count, legacy_s8 *arguments[])
{
	reset_startup_hypervision();
#ifdef __ANDROID__
	legacy_u32 before_layout_calls = startup_control_layout_set_calls;
	legacy_u8 before_layout_visible = startup_control_layout_visible;
#endif
	startup_exit_status = EXIT_SUCCESS;
	if (setjmp(exit_jump) == 0) {
		init_main(count, arguments);
		assert(0);
	}
	assert(startup_exit_status == EXIT_FAILURE);
	/* Validate all arguments before enabling rendering or starting the timer. */
	assert(startup_render_enabled == 0 && supersight_enabled == 0);
	assert(frame_adaptive.preset == FRAME_ADAPTIVE_PRESET_AUTO);
	assert(timer_calls == 0);
#ifdef __ANDROID__
	assert(startup_control_layout_set_calls == before_layout_calls);
	assert(startup_control_layout_visible == before_layout_visible);
#endif
}

#define STARTUP_TEST_GEOMETRY_TICKS 55U
#define STARTUP_TEST_CLEAR_TICKS 15U
#define STARTUP_TEST_PARTIAL_TICKS 16U

static void test_startup_hypervision_options(void)
{
	static const struct {
		const legacy_s8 *argument;
		enum FRAME_ADAPTIVE_PRESET preset;
		enum FRAME_ADAPTIVE_QUALITY quality;
		legacy_s32 scale;
	} cases[] = {{"--hv:auto", FRAME_ADAPTIVE_PRESET_AUTO, FRAME_ADAPTIVE_FULL_VIEW, HIRES_SCALE},
				 {"--hv:full", FRAME_ADAPTIVE_PRESET_FULL, FRAME_ADAPTIVE_FULL_VIEW, HIRES_SCALE},
				 {"--hv:high", FRAME_ADAPTIVE_PRESET_HIGH, FRAME_ADAPTIVE_LARGE_VIEW, HIRES_SCALE},
				 {"--hv:medium", FRAME_ADAPTIVE_PRESET_MEDIUM, FRAME_ADAPTIVE_HALF_RESOLUTION,
				  HIRES_MEDIUM_SCALE},
				 {"--hv:low", FRAME_ADAPTIVE_PRESET_LOW, FRAME_ADAPTIVE_MINIMUM_RESOLUTION,
				  HIRES_MINIMUM_SCALE},
				 {"--HV:AuTo", FRAME_ADAPTIVE_PRESET_AUTO, FRAME_ADAPTIVE_FULL_VIEW, HIRES_SCALE},
				 {"--HV:FuLl", FRAME_ADAPTIVE_PRESET_FULL, FRAME_ADAPTIVE_FULL_VIEW, HIRES_SCALE},
				 {"--hV:LoW", FRAME_ADAPTIVE_PRESET_LOW, FRAME_ADAPTIVE_MINIMUM_RESOLUTION,
				  HIRES_MINIMUM_SCALE}};
	for (legacy_u16 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		legacy_s8 *arguments[] = {(legacy_s8 *)"game", (legacy_s8 *)"/sSB",
								  (legacy_s8 *)cases[index].argument, (legacy_s8 *)"--pg:off",
								  (legacy_s8 *)"--nointro"};
		reset_startup_hypervision();
		/* Explicit startup selection must replace any previous adaptive state. */
		frame_adaptive_set_preset(&frame_adaptive, FRAME_ADAPTIVE_PRESET_LOW);
		expected_hypervision_preset = cases[index].preset;
		expected_hypervision_enabled = 1;
		expected_initial_intro_calls = index % 2U;
		menu_calls = intro_calls = game_calls = score_calls = 0;
		is_audioloaded = 0;
		track_element_map = menu_track_data;
		geometry_ticks = STARTUP_TEST_GEOMETRY_TICKS;
		clear_ticks = STARTUP_TEST_CLEAR_TICKS;
		partial_ticks = STARTUP_TEST_PARTIAL_TICKS;
		legacy_s16 count = sizeof(arguments) / sizeof(arguments[0]);
		if (expected_initial_intro_calls != 0) {
			count--;
		}
		assert(run_main_menu_loop(count, arguments) == 1);
		assert(menu_calls == 1 && intro_calls == (legacy_u32)expected_initial_intro_calls + 1U);
		assert(game_calls == 0);
		assert(startup_options.hypervision_specified != 0);
		assert(frame_adaptive.quality == cases[index].quality);
		assert(startup_render_scale == cases[index].scale);
		assert(audiodriverstring[0] == 'a' && audiodriverstring[1] == 'd');
	}
	expected_initial_intro_calls = -1;

	/* Any two preset arguments conflict, including identical repetitions. */
	for (legacy_u16 first = 0; first < sizeof(cases) / sizeof(cases[0]); first++) {
		for (legacy_u16 second = 0; second < sizeof(cases) / sizeof(cases[0]); second++) {
			legacy_s8 *arguments[] = {(legacy_s8 *)"game", (legacy_s8 *)cases[first].argument,
									  (legacy_s8 *)"--nointro",
									  (legacy_s8 *)cases[second].argument};
			expect_startup_option_error(sizeof(arguments) / sizeof(arguments[0]), arguments);
		}
	}
	static const legacy_s8 *invalid[] = {"--hv:",		  "--hv:autox", "--hv:off",
										 "--hv:veryhigh", "--hv:fullx", "--hv:low:high",
										 "--HV:unknown"};
	for (legacy_u16 index = 0; index < sizeof(invalid) / sizeof(invalid[0]); index++) {
		legacy_s8 *arguments[] = {(legacy_s8 *)"game", (legacy_s8 *)invalid[index]};
		expect_startup_option_error(sizeof(arguments) / sizeof(arguments[0]), arguments);
	}

	/* Unrecognized options, including the removed ss: and hv: forms, leave rendering off. */
	static const legacy_s8 *ignored[] = {
		"",			 "s",			"ss",		 "ssfull",		"h",
		"hv",		 "hvfull",		"ss:",		 "ss:full",		"ss:high",
		"ss:medium", "ss:low",		"SS:FuLl",	 "sS:LoW",		"ss:auto",
		"ss:off",	 "ss:veryhigh", "ss:fullx",	 "ss:low:high", "SS:unknown",
		"hv:full",	 "hv:high",		"hv:medium", "hv:low",		"HV:FuLl",
		"hV:LoW",	 "hv:",			"hv:off",	 "hv:invalid",	"--hv",
		"--hvfull"};
	for (legacy_u16 index = 0; index < sizeof(ignored) / sizeof(ignored[0]); index++) {
		legacy_s8 *arguments[] = {(legacy_s8 *)"game", (legacy_s8 *)ignored[index]};
		reset_startup_hypervision();
		init_main(sizeof(arguments) / sizeof(arguments[0]), arguments);
		assert(startup_render_enabled == 0 && supersight_enabled == 0);
		assert(startup_options.hypervision_preset == FRAME_ADAPTIVE_PRESET_AUTO);
		assert(startup_options.hypervision_specified == 0);
	}
}

static void test_startup_ogg_music_options(void)
{
	static const struct {
		const legacy_s8 *argument;
		legacy_s32 enabled;
	} cases[] = {{"--ogg:on", 1}, {"--ogg:off", 0}, {"--OGG:On", 1}, {"--oGg:OfF", 0}};
	for (legacy_u16 index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
		legacy_s8 *arguments[] = {(legacy_s8 *)"game", (legacy_s8 *)cases[index].argument,
								  (legacy_s8 *)"/ns", (legacy_s8 *)"--hv:low",
								  (legacy_s8 *)"--nointro"};
		reset_startup_hypervision();
		expected_hypervision_preset = FRAME_ADAPTIVE_PRESET_LOW;
		expected_hypervision_enabled = 1;
		expected_ogg_music_enabled = cases[index].enabled;
		expected_initial_intro_calls = index % 2U;
		menu_calls = intro_calls = game_calls = score_calls = 0;
		is_audioloaded = 0;
		track_element_map = menu_track_data;
		geometry_ticks = STARTUP_TEST_GEOMETRY_TICKS;
		clear_ticks = STARTUP_TEST_CLEAR_TICKS;
		partial_ticks = STARTUP_TEST_PARTIAL_TICKS;
		legacy_s16 count = sizeof(arguments) / sizeof(arguments[0]);
		if (expected_initial_intro_calls != 0) {
			--count;
		}
		legacy_u32 before = startup_ogg_set_calls;
		assert(run_main_menu_loop(count, arguments) == 1);
		assert(startup_ogg_set_calls == before + 1U);
		assert(startup_options.ogg_music_specified);
		assert(startup_options.ogg_music_enabled == cases[index].enabled);
		assert(menu_calls == 1 && intro_calls == (legacy_u32)expected_initial_intro_calls + 1U);
	}
	expected_initial_intro_calls = -1;
	expected_ogg_music_enabled = 0;

	/* Repeated startup must return to the default rather than retain the last
	 * session or an earlier explicit --ogg:on preference. */
	legacy_s8 *defaults[] = {(legacy_s8 *)"game"};
	reset_startup_hypervision();
	startup_ogg_music_enabled = 1;
	init_main(sizeof(defaults) / sizeof(defaults[0]), defaults);
	assert(startup_ogg_music_enabled == 0);
	assert(!startup_options.ogg_music_enabled && !startup_options.ogg_music_specified);

	for (legacy_u16 first = 0; first < sizeof(cases) / sizeof(cases[0]); ++first) {
		for (legacy_u16 second = 0; second < sizeof(cases) / sizeof(cases[0]); ++second) {
			legacy_s8 *arguments[] = {(legacy_s8 *)"game", (legacy_s8 *)cases[first].argument,
									  (legacy_s8 *)"--nointro",
									  (legacy_s8 *)cases[second].argument};
			legacy_u32 before = startup_ogg_set_calls;
			expect_startup_option_error(sizeof(arguments) / sizeof(arguments[0]), arguments);
			assert(startup_ogg_set_calls == before);
		}
	}
	static const legacy_s8 *invalid[] = {"--ogg:",	  "--ogg:auto",	  "--ogg:1",
										 "--ogg:onx", "--ogg:off:on", "--OGG:unknown"};
	for (legacy_u16 index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
		legacy_s8 *arguments[] = {(legacy_s8 *)"game", (legacy_s8 *)invalid[index]};
		legacy_u32 before = startup_ogg_set_calls;
		expect_startup_option_error(sizeof(arguments) / sizeof(arguments[0]), arguments);
		assert(startup_ogg_set_calls == before);
	}
}

static void test_startup_fps_display_options(void)
{
	static const struct {
		const legacy_s8 *argument;
		enum FRAME_FPS_DISPLAY_MODE mode;
	} cases[] = {{"--fps:on", FRAME_FPS_DISPLAY_ON},
				 {"--fps:off", FRAME_FPS_DISPLAY_OFF},
				 {"--FPS:On", FRAME_FPS_DISPLAY_ON},
				 {"--fPs:OfF", FRAME_FPS_DISPLAY_OFF}};
	for (legacy_u16 index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
		legacy_s8 *arguments[] = {(legacy_s8 *)"game",	   (legacy_s8 *)cases[index].argument,
								  (legacy_s8 *)"/ns",	   (legacy_s8 *)"--hv:low",
								  (legacy_s8 *)"--ogg:on", (legacy_s8 *)"--nointro"};
		reset_startup_hypervision();
		expected_hypervision_preset = FRAME_ADAPTIVE_PRESET_LOW;
		expected_hypervision_enabled = 1;
		expected_ogg_music_enabled = 1;
		expected_fps_display_enabled = cases[index].mode;
		fps_display_enabled = cases[index].mode == FRAME_FPS_DISPLAY_OFF ? FRAME_FPS_DISPLAY_ON
																		 : FRAME_FPS_DISPLAY_OFF;
		expected_initial_intro_calls = index % 2U;
		menu_calls = intro_calls = game_calls = score_calls = 0;
		is_audioloaded = 0;
		track_element_map = menu_track_data;
		geometry_ticks = STARTUP_TEST_GEOMETRY_TICKS;
		clear_ticks = STARTUP_TEST_CLEAR_TICKS;
		partial_ticks = STARTUP_TEST_PARTIAL_TICKS;
		legacy_s16 count = sizeof(arguments) / sizeof(arguments[0]);
		if (expected_initial_intro_calls != 0) {
			--count;
		}
		legacy_u32 before = startup_fps_reset_calls;
		assert(run_main_menu_loop(count, arguments) == 1);
		assert(startup_fps_reset_calls == before + 1U);
		assert(startup_options.fps_display_specified);
		assert(startup_options.fps_display_enabled == cases[index].mode);
		assert(fps_display_enabled == cases[index].mode);
		assert(menu_calls == 1 && intro_calls == (legacy_u32)expected_initial_intro_calls + 1U);
	}
	expected_initial_intro_calls = -1;
	expected_ogg_music_enabled = 0;
	expected_fps_display_enabled = FRAME_FPS_DISPLAY_OFF;

	/* Default startup clears a prior session's display choice and FPS samples. */
	legacy_s8 *defaults[] = {(legacy_s8 *)"game"};
	reset_startup_hypervision();
	fps_display_enabled = FRAME_FPS_DISPLAY_ON;
	legacy_u32 before = startup_fps_reset_calls;
	init_main(sizeof(defaults) / sizeof(defaults[0]), defaults);
	assert(startup_fps_reset_calls == before + 1U);
	assert(fps_display_enabled == FRAME_FPS_DISPLAY_OFF);
	assert(startup_options.fps_display_enabled == FRAME_FPS_DISPLAY_OFF);
	assert(!startup_options.fps_display_specified);

	for (legacy_u16 first = 0; first < sizeof(cases) / sizeof(cases[0]); ++first) {
		for (legacy_u16 second = 0; second < sizeof(cases) / sizeof(cases[0]); ++second) {
			legacy_s8 *arguments[] = {(legacy_s8 *)"game", (legacy_s8 *)cases[first].argument,
									  (legacy_s8 *)"--hv:low", (legacy_s8 *)cases[second].argument};
			fps_display_enabled = FRAME_FPS_DISPLAY_ON;
			before = startup_fps_reset_calls;
			expect_startup_option_error(sizeof(arguments) / sizeof(arguments[0]), arguments);
			assert(startup_fps_reset_calls == before);
			assert(fps_display_enabled == FRAME_FPS_DISPLAY_ON);
		}
	}
	static const legacy_s8 *invalid[] = {"--fps:",	  "--fps:auto",	  "--fps:1",
										 "--fps:onx", "--fps:off:on", "--FPS:unknown"};
	for (legacy_u16 index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
		legacy_s8 *arguments[] = {(legacy_s8 *)"game", (legacy_s8 *)invalid[index]};
		fps_display_enabled = FRAME_FPS_DISPLAY_ON;
		before = startup_fps_reset_calls;
		expect_startup_option_error(sizeof(arguments) / sizeof(arguments[0]), arguments);
		assert(startup_fps_reset_calls == before);
		assert(fps_display_enabled == FRAME_FPS_DISPLAY_ON);
	}
	/* A later invalid option must leave the display and sample state untouched. */
	static const legacy_s8 *other_invalid[] = {"--ogg:invalid", "--hv:invalid"};
	for (legacy_u16 index = 0; index < sizeof(other_invalid) / sizeof(other_invalid[0]); ++index) {
		legacy_s8 *arguments[] = {(legacy_s8 *)"game", (legacy_s8 *)"--fps:off",
								  (legacy_s8 *)other_invalid[index]};
		fps_display_enabled = FRAME_FPS_DISPLAY_ON;
		before = startup_fps_reset_calls;
		expect_startup_option_error(sizeof(arguments) / sizeof(arguments[0]), arguments);
		assert(startup_fps_reset_calls == before);
		assert(fps_display_enabled == FRAME_FPS_DISPLAY_ON);
	}
	static const legacy_s8 *ignored[] = {"", "--fps", "fps:on", "--fps=on", "--fpson"};
	for (legacy_u16 index = 0; index < sizeof(ignored) / sizeof(ignored[0]); ++index) {
		legacy_s8 *arguments[] = {(legacy_s8 *)"game", (legacy_s8 *)ignored[index]};
		reset_startup_hypervision();
		init_main(sizeof(arguments) / sizeof(arguments[0]), arguments);
		assert(fps_display_enabled == FRAME_FPS_DISPLAY_OFF);
		assert(!startup_options.fps_display_specified);
	}
}
#ifdef __ANDROID__
static void test_startup_control_layout_options(void)
{
	static const struct {
		const legacy_s8 *argument;
		legacy_u8 visible;
	} cases[] = {{"--control-layout:off", STARTUP_CONTROL_LAYOUT_OFF},
				 {"--CONTROL-LAYOUT:On", STARTUP_CONTROL_LAYOUT_ON}};
	for (legacy_u16 index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
		legacy_s8 *arguments[] = {(legacy_s8 *)"game", (legacy_s8 *)cases[index].argument};
		reset_startup_hypervision();
		startup_control_layout_visible = cases[index].visible == STARTUP_CONTROL_LAYOUT_ON
											 ? STARTUP_CONTROL_LAYOUT_OFF
											 : STARTUP_CONTROL_LAYOUT_ON;
		legacy_u32 before = startup_control_layout_set_calls;
		init_main(sizeof(arguments) / sizeof(arguments[0]), arguments);
		assert(startup_control_layout_set_calls == before + 1U);
		assert(startup_control_layout_visible == cases[index].visible);
		assert(startup_options.control_layout_enabled == cases[index].visible);
		assert(startup_options.control_layout_specified != 0);
	}

	/* A later session without an explicit option restores the enabled default. */
	legacy_s8 *defaults[] = {(legacy_s8 *)"game"};
	reset_startup_hypervision();
	startup_control_layout_visible = STARTUP_CONTROL_LAYOUT_OFF;
	legacy_u32 before = startup_control_layout_set_calls;
	init_main(sizeof(defaults) / sizeof(defaults[0]), defaults);
	assert(startup_control_layout_set_calls == before + 1U);
	assert(startup_control_layout_visible == STARTUP_CONTROL_LAYOUT_ON);
	assert(startup_options.control_layout_enabled == STARTUP_CONTROL_LAYOUT_ON);
	assert(startup_options.control_layout_specified == 0);

	static const legacy_s8 *invalid[] = {"--control-layout:", "--control-layout:auto"};
	for (legacy_u16 index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
		legacy_s8 *arguments[] = {(legacy_s8 *)"game", (legacy_s8 *)invalid[index]};
		expect_startup_option_error(sizeof(arguments) / sizeof(arguments[0]), arguments);
	}
	legacy_s8 *duplicate[] = {(legacy_s8 *)"game", (legacy_s8 *)"--control-layout:off",
							  (legacy_s8 *)"--control-layout:on"};
	expect_startup_option_error(sizeof(duplicate) / sizeof(duplicate[0]), duplicate);
	/* Validation of a later option must finish before touching the current layout. */
	legacy_s8 *later_invalid[] = {(legacy_s8 *)"game", (legacy_s8 *)"--control-layout:off",
								  (legacy_s8 *)"--fps:invalid"};
	expect_startup_option_error(sizeof(later_invalid) / sizeof(later_invalid[0]), later_invalid);
}
#endif
#endif

static void test_startup_game_version(void)
{
	legacy_s8 *arguments[] = {(legacy_s8 *)"game"};
	valid_game_version = 0;
	startup_error_calls = startup_cleanup_calls = 0;
	startup_audio_calls = startup_video_calls = 0;
	if (setjmp(exit_jump) == 0) {
		init_main(sizeof(arguments) / sizeof(arguments[0]), arguments);
		assert(0);
	}
	assert(startup_exit_status != 0);
	assert(startup_error_calls == 1 && startup_cleanup_calls == 1);
	assert(startup_audio_calls == 0 && startup_video_calls == 0);
	valid_game_version = 1;
}

int main(void)
{
	static legacy_s8 *arguments[][8] = {
		{(legacy_s8 *)"game"},
		{(legacy_s8 *)"game", (legacy_s8 *)"/h", (legacy_s8 *)"/ns", (legacy_s8 *)"/nd"},
		{(legacy_s8 *)"game", (legacy_s8 *)"/sSB", (legacy_s8 *)"/unknown"},
		{(legacy_s8 *)"game", (legacy_s8 *)"/ssb", (legacy_s8 *)"/sAD", (legacy_s8 *)"/s"},
		{(legacy_s8 *)"game", (legacy_s8 *)"/sxy", (legacy_s8 *)"/sSb", (legacy_s8 *)"ignore"},
	};
	static legacy_u32 boundaries[] = {0, 34, 35, 54, 55, 74, 75, 99, 100, 65535};
	static legacy_u32 scenario;
	static legacy_s16 counts[] = {1, 4, 3, 4, 4};
	for (scenario = 0; scenario < 110; scenario++) {
		trace(scenario);
		geometry_ticks = boundaries[scenario % 10];
		clear_ticks = 15;
		partial_ticks = (scenario & 1) ? 15 : 16;
		audio_failure = scenario >= 100;
		timer_calls = status_calls = 0;
		audiodriverstring[0] = 's';
		audiodriverstring[1] = 't';
		detail_level = slow_video_mgmt = -7;
		if (setjmp(exit_jump) == 0) {
			init_main(counts[scenario % 5], arguments[scenario % 5]);
			trace(27);
		}
		trace(timer_calls);
		trace(status_calls);
		trace(detail_level);
		trace(slow_video_mgmt);
		if (!audio_failure) {
			trace(framespersec);
			trace(configured_frame_rate);
			trace(slow_video_mgmt_copy);
			trace(video_uses_page_flipping);
			trace(video_page_count);
			trace(textresprefix);
		}
	}
	test_menu_lifecycle();
#ifdef STARTUP_RECORD_BASELINE
	printf("Startup fingerprint: %08" LEGACY_PRIx32 "\n", trace_hash);
#else
	assert(trace_hash == UINT32_C(0x00524607));
#endif
	test_startup_game_version();
	test_startup_intro_option();
	test_startup_physics_options();
#ifdef RESTUNTS_SDL3
	test_startup_hypervision_options();
	test_startup_ogg_music_options();
	test_startup_fps_display_options();
#ifdef __ANDROID__
	test_startup_control_layout_options();
#endif
#endif
	return 0;
}
