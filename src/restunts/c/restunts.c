#include <stddef.h>
#include <stdarg.h>
#ifdef RESTUNTS_SDL3
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include "frame_adaptive.h"
#include "frame_internal.h"
#include "hires.h"
#include "../platform/sdl3/music.h"
#ifdef __ANDROID__
#include "../platform/sdl3/touch_game.h"
#endif
#endif
#include "audio.h"
#include "dashboard.h"
#include "car_speed.h"
#include "physics_internal.h"
#include "owoot.h"
#include "restunts.h"
#include "video_pages.h"
#include "fileio.h"
#include "fatal.h"
#include "game_input.h"
#include "keyboard.h"
#include "legacy.h"
#include "math.h"
#include "memmgr.h"
#include "menu_internal.h"
#include "platform.h"
#include "race.h"
#include "race_resources.h"
#include "replay.h"
#include "ghost.h"
#include "replay_record.h"
#include "replay_viewer.h"
#include "replay_viewer_internal.h"
#include "shape2d.h"
#include "shape3d.h"
#include "timing.h"
#include "ui_dialog.h"
#include "ui_input.h"
#include "ui_text.h"
#include "track_objects.h"
#include "camera.h"
#include "video_frame.h"
#include "opponent.h"
#include "scene_resources.h"
#include "audio_control.h"
#include "menu_common.h"
#include "externs.h"

#define GAME_SCREEN_WIDTH 320
#define GAME_SCREEN_HEIGHT 200
#define GAME_TOP_PANEL_HEIGHT 100
#define GAME_INITIAL_CLIP_BOTTOM 95
#define GAME_SCREEN_COLOR 15U

#define RANDOM_WAIT_SPIN_LIMIT 12000
#define RANDOM_WAIT_RESOURCE_INDEX 1024
#define NO_OPPONENT_CAR_ID 255U

#define CALLBACK_GRAPHICS_MENU_KEY 7
#define CALLBACK_JOYSTICK_HELP_KEY 10
#define CALLBACK_KEYBOARD_HELP_KEY 11
#define CALLBACK_MOUSE_HELP_KEY 12800
#define CALLBACK_PAUSE_HELP_KEY 16
#define CALLBACK_DOS_HELP_KEY 17
#define CALLBACK_SOUND_HELP_KEY 19
#define CALLBACK_DOS_HELP_ALT_KEY 24

#define STARTUP_HYPERVISION_PREFIX "--hv:"
#define STARTUP_OGG_MUSIC_PREFIX "--ogg:"
#define STARTUP_FPS_DISPLAY_PREFIX "--fps:"
#define STARTUP_SKIP_INTRO_OPTION "--nointro"
#if defined(RESTUNTS_SDL3) && defined(__ANDROID__)
#define STARTUP_CONTROL_LAYOUT_PREFIX "--control-layout:"
#define STARTUP_CONTROL_LAYOUT_OFF 0U
#define STARTUP_CONTROL_LAYOUT_ON 1U
#endif

#define STARTUP_PROJECTION_X 36
#define STARTUP_PROJECTION_Y 17
#define STARTUP_SHAPE_COUNT 116
#define STARTUP_CAR_POSITION_Y (-840)
#define STARTUP_CAR_POSITION_Z 2880

#define TRACK_PATH_STORAGE_SIZE 81
#define TRACK_PRIMARY_PATH_OFFSET REPLAY_TRACK_SIZE
#define TRACK_SECONDARY_PATH_OFFSET (REPLAY_TRACK_SIZE + TRACK_PATH_STORAGE_SIZE)

legacy_s16 video_backbuffer_copy_required(void)
{
	return 0;
}

static void shutdown_dos_game(void)
{
	ghost_clear();
	mouse_draw_opaque_check();
	dos_timer_shutdown();
	dos_audio_shutdown();
	kb_exit_handler();
	dos_kb_set_numlock();
	dos_video_set_mode7();
}

legacy_s16 camera_track_height_offset;

void update_opponent(void)
{
	update_opponent_tick();
}

legacy_s16 get_super_random(void)
{
	legacy_s16 val = rand() + get_kevinrandom() + timer_get_counter() + gState_frame;
	return val < 0 ? -val : val;
}

legacy_s16 random_wait(void)
{
	legacy_s16 status1 = dos_video_get_status();

	legacy_s16 i;
	for (i = 0; status1 == dos_video_get_status() && i < RANDOM_WAIT_SPIN_LIMIT; ++i) {
		;
	}

	if (i == RANDOM_WAIT_RESOURCE_INDEX) {
		i = random_wait_legacy_bytes[0];
	}

	while (i--) {
		rand();
		get_kevinrandom();
	}

	i &= LEGACY_U8_MAX;

	while (i--) {
		get_kevinrandom();
		rand();
	}

	return 0;
}

void set_default_car(void)
{
	gameconfig.game_playercarid[0] = 'C';
	gameconfig.game_playercarid[1] = 'O';
	gameconfig.game_playercarid[2] = 'U';
	gameconfig.game_playercarid[3] = 'N';
	gameconfig.game_playermaterial = 0;
	gameconfig.game_playertransmission = TRANSMISSION_AUTOMATIC;
	gameconfig.game_opponenttype = 0;
	gameconfig.game_opponentmaterial = 0;
	gameconfig.game_opponentcarid[0] = NO_OPPONENT_CAR_ID;
}

//extern void shape3d_transform_and_queue(struct TRANSFORMSHAPE3D* shape);

struct RECTANGLE shaperect = {0, GAME_SCREEN_WIDTH, 0, GAME_SCREEN_HEIGHT};
struct TRANSFORMEDSHAPE3D transshape;
struct RECTANGLE cliprect = {0, GAME_SCREEN_WIDTH, 0, GAME_INITIAL_CLIP_BOTTOM};
struct VECTOR carpos = {0, STARTUP_CAR_POSITION_Y, STARTUP_CAR_POSITION_Z}; // from the original
//struct VECTOR carpos = { 0, 0, GAME_SCREEN_WIDTH };

struct SPRITE far *render_window_sprite;
// empty_rect uses the legacy sentinel rectangle <9999, 65535, 9999, 65535>.

void init_div0(void)
{
	dos_install_divide_error_handler();
}

void copy_material_list_pointers(void *clrlist, void *clrlist2, void *patlist, void *patlist2,
								 legacy_u16 reserved_video_word)
{
	material_clrlist_ptr_cpy = clrlist;
	material_clrlist2_ptr_cpy = clrlist2;
	material_patlist_ptr_cpy = patlist;
	material_patlist2_ptr_cpy = patlist2;
	reserved_material_video_word = reserved_video_word;
}

struct STARTUP_OPTIONS {
	legacy_u8 mode4_requested;
	legacy_u8 sound_disabled;
	legacy_u8 unused_nd_option;
	legacy_u8 skip_intro;
#ifdef RESTUNTS_SDL3
	legacy_u16 hypervision_preset;
	legacy_u8 hypervision_specified;
	legacy_u8 ogg_music_enabled;
	legacy_u8 ogg_music_specified;
	legacy_u8 fps_display_enabled;
	legacy_u8 fps_display_specified;
#ifdef __ANDROID__
	legacy_u8 control_layout_enabled;
	legacy_u8 control_layout_specified;
#endif
#endif
};

static struct STARTUP_OPTIONS startup_options;

static void startup_install_keyboard_callbacks(void)
{
	// Keyboard
	kb_init_interrupt();
	dos_kb_clear_numlock();
	kb_call_readchar_callback();

	kb_reg_callback(CALLBACK_GRAPHICS_MENU_KEY, &show_graphic_levels_menu);
	kb_reg_callback(CALLBACK_JOYSTICK_HELP_KEY, &calibrate_joystick_driving);
	kb_reg_callback(CALLBACK_KEYBOARD_HELP_KEY, &select_keyboard_driving);
	kb_reg_callback(CALLBACK_MOUSE_HELP_KEY, &toggle_music_with_dialog);
	kb_reg_callback(CALLBACK_PAUSE_HELP_KEY, &show_pause_dialog);
	kb_reg_callback('p', &show_pause_dialog);
	kb_reg_callback(CALLBACK_DOS_HELP_KEY, &show_exit_to_dos_dialog);
	kb_reg_callback(CALLBACK_SOUND_HELP_KEY, &toggle_effects_with_dialog);
	kb_reg_callback(CALLBACK_DOS_HELP_ALT_KEY, &show_exit_to_dos_dialog);
}

static void startup_select_audio_driver(const legacy_s8 *argument)
{
	if (strlen(argument) >= 4) {
		if ((argument[2] == 'S' || argument[2] == 's') &&
			(argument[3] == 'B' || argument[3] == 'b')) {
			// Use the Sound Blaster's AdLib-compatible FM synthesizer.
			audiodriverstring[0] = 'a';
			audiodriverstring[1] = 'd';
		} else {
			audiodriverstring[0] = argument[2];
			audiodriverstring[1] = argument[3];
		}
	}
}

#ifdef RESTUNTS_SDL3
static void startup_parse_hypervision(const legacy_s8 *argument, struct STARTUP_OPTIONS *options)
{
	const legacy_s8 *prefix = STARTUP_HYPERVISION_PREFIX;
	const legacy_s8 *value = argument;
	while (*prefix != 0 && tolower((legacy_u8)*value) == *prefix) {
		prefix++;
		value++;
	}
	if (*prefix != 0) {
		return;
	}
	if (options->hypervision_specified != 0) {
		fprintf(stderr, "Only one HyperVision preset may be specified (--hv:).\n");
		dos_process_exit(EXIT_FAILURE);
		return;
	}
	static const struct {
		const legacy_s8 *name;
		enum FRAME_ADAPTIVE_PRESET preset;
	} presets[] = {{"auto", FRAME_ADAPTIVE_PRESET_AUTO},
				   {"full", FRAME_ADAPTIVE_PRESET_FULL},
				   {"high", FRAME_ADAPTIVE_PRESET_HIGH},
				   {"medium", FRAME_ADAPTIVE_PRESET_MEDIUM},
				   {"low", FRAME_ADAPTIVE_PRESET_LOW}};
	for (legacy_u16 index = 0; index < sizeof(presets) / sizeof(presets[0]); index++) {
		if (stricmp(value, presets[index].name) == 0) {
			options->hypervision_preset = presets[index].preset;
			options->hypervision_specified = 1;
			return;
		}
	}
	fprintf(stderr,
			"Invalid HyperVision preset '%s'; use --hv:auto, --hv:full, --hv:high, "
			"--hv:medium or --hv:low.\n",
			argument);
	dos_process_exit(EXIT_FAILURE);
}
static void startup_parse_boolean(const legacy_s8 *argument, const legacy_s8 *prefix,
								  const legacy_s8 *name, legacy_u8 *enabled, legacy_u8 *specified)
{
	const legacy_s8 *option = prefix;
	const legacy_s8 *value = argument;
	while (*option != 0 && tolower((legacy_u8)*value) == *option) {
		option++;
		value++;
	}
	if (*option != 0) {
		return;
	}
	if (*specified != 0) {
		fprintf(stderr, "Only one %s option may be specified (%s).\n", name, prefix);
		dos_process_exit(EXIT_FAILURE);
		return;
	}
	*specified = 1;
	if (stricmp(value, "on") == 0) {
		*enabled = 1;
		return;
	}
	if (stricmp(value, "off") == 0) {
		*enabled = 0;
		return;
	}
	fprintf(stderr, "Invalid %s option '%s'; use %son or %soff.\n", name, argument, prefix, prefix);
	dos_process_exit(EXIT_FAILURE);
}
#endif

static void startup_parse_options(legacy_s16 argc, legacy_s8 *argv[],
								  struct STARTUP_OPTIONS *options)
{
	options->mode4_requested = 0;
	options->sound_disabled = 0;
	options->unused_nd_option = 0;
	options->skip_intro = 0;
#ifdef RESTUNTS_SDL3
	options->hypervision_preset = FRAME_ADAPTIVE_PRESET_AUTO;
	options->hypervision_specified = 0;
	options->ogg_music_enabled = 0;
	options->ogg_music_specified = 0;
	options->fps_display_enabled = FRAME_FPS_DISPLAY_OFF;
	options->fps_display_specified = 0;
#ifdef __ANDROID__
	options->control_layout_enabled = STARTUP_CONTROL_LAYOUT_ON;
	options->control_layout_specified = 0;
#endif
#endif
	for (legacy_u16 i = 1; argc > i; ++i) {
#ifdef RESTUNTS_SDL3
		startup_parse_hypervision(argv[i], options);
		startup_parse_boolean(argv[i], STARTUP_OGG_MUSIC_PREFIX, "Ogg music",
							  &options->ogg_music_enabled, &options->ogg_music_specified);
		startup_parse_boolean(argv[i], STARTUP_FPS_DISPLAY_PREFIX, "FPS display",
							  &options->fps_display_enabled, &options->fps_display_specified);
#ifdef __ANDROID__
		startup_parse_boolean(argv[i], STARTUP_CONTROL_LAYOUT_PREFIX, "Control layout",
							  &options->control_layout_enabled, &options->control_layout_specified);
#endif
#endif
		if (strcmp(argv[i], STARTUP_SKIP_INTRO_OPTION) == 0) {
			options->skip_intro = 1;
		}
		if (argv[i][0] == '/') {
			switch (argv[i][1]) {
				case 'h':
					options->mode4_requested = 4;
					break;

				case 'n':
					if (argv[i][2] == 's') {
						options->sound_disabled = 1;
					} else if (argv[i][2] == 'd') {
						options->unused_nd_option = 1;
					}
					break;

				case 's':
					startup_select_audio_driver(argv[i]);
					break;
			}
		}
	}
}

static void startup_measure_video(void)
{
	// Timing measures.
	sprite_select_screen();
	sprite_set_target_clip_bounds(0, 320, 0, 120);

	timer_get_delta_alt();
	for (legacy_u16 i = 0; i < 15; ++i) {
		sprite_clear_target(0);
	}
	legacy_u32 full_clear_ticks = timer_get_delta_alt();

	sprite_set_target_clip_bounds(0, 320, 0, 60);

	struct RECTANGLE benchmark_bounds;
	struct POINT2D benchmark_point;
	for (legacy_u16 i = 0; i < 15; ++i) {
		benchmark_bounds.left = benchmark_bounds.right = benchmark_bounds.top =
			benchmark_bounds.bottom = 0;

		for (legacy_u16 j = 0; j < 400; ++j) {
			benchmark_point.px = benchmark_point.py = j;
			rect_adjust_from_point(&benchmark_point, &benchmark_bounds);
		}

		sprite_clear_target(0);
	}

	legacy_u32 partial_redraw_ticks = timer_get_delta_alt();

	for (legacy_u16 i = 0; i < 146; ++i) {
		for (legacy_u16 j = 0; j < 255; ++j) {
			rect_adjust_from_point(&benchmark_point, &benchmark_bounds);
		}
	}

	legacy_u32 geometry_benchmark_ticks = timer_get_delta_alt();

	slow_video_mgmt = (partial_redraw_ticks <= full_clear_ticks);
	configured_frame_rate =
		(geometry_benchmark_ticks >= 75) ? GAME_FRAME_RATE_LOW : GAME_FRAME_RATE_NORMAL;

	if (geometry_benchmark_ticks < 35) {
		detail_level = 0;
	} else if (geometry_benchmark_ticks < 55) {
		detail_level = 1;
	} else if (geometry_benchmark_ticks < 75) {
		detail_level = 2;
	} else if (geometry_benchmark_ticks < 100) {
		detail_level = 3;
	} else if (slow_video_mgmt) {
		detail_level = 4;
	} else {
		detail_level = 3;
	}

	framespersec = configured_frame_rate;
	slow_video_mgmt_copy = slow_video_mgmt;
}

void init_main(legacy_s16 argc, legacy_s8 *argv[])
{
	startup_install_keyboard_callbacks();
	// Video
	init_video_geometry_flags();

	mmgr_init_conventional_arena();
	if (file_game_version_matches() == 0) {
		static const legacy_s8 message[] =
			"Incompatible game data. Use Broderbund Stunts 1.1 (Feb 12 1991). "
			"MISC.RES/MISC.PRE is missing, invalid, or has a different version.\n";
		dos_show_startup_error(message);
		call_exitlist();
		dos_process_exit(1);
		return;
	}
	audio_allocate_car_state_records();

	video_uses_page_flipping = 0;
	video_page_count = 1;

	textresprefix = 'e';

	configure_powergear_bug(argc, argv);
	configure_legacy_collision(argc, argv);
	configure_left_corner_bias(argc, argv);
	configure_owoot(argc, argv);
	startup_parse_options(argc, argv, &startup_options);
#ifdef RESTUNTS_SDL3
#ifdef __ANDROID__
	sdl3_touch_set_layout_visible(startup_options.control_layout_enabled);
#endif
	fps_display_enabled = startup_options.fps_display_enabled;
	frame_fps_reset();
	if (startup_options.hypervision_specified != 0) {
		frame_adaptive_set_preset(&frame_adaptive,
								  (enum FRAME_ADAPTIVE_PRESET)startup_options.hypervision_preset);
		supersight_enabled = 1;
		hires_set_enabled(supersight_enabled);
		hires_set_render_scale(frame_adaptive_render_scale(&frame_adaptive));
	}
#endif

	// Unused "/nd" switch. Maybe used when loading other video drivers?
	(void)startup_options.unused_nd_option;

	// Video mode.
	dos_video_set_mode_13h();
	if (startup_options.mode4_requested) {
		dos_video_set_mode4();
	}

	dos_timer_setup_interrupt();

	sprite_select_screen_and_clear();

	dos_mouse_init(GAME_SCREEN_WIDTH, GAME_SCREEN_HEIGHT);

	// Audio driver.
	if (audio_load_dos_driver(audiodriverstring, 0, 0)) {
		dos_timer_shutdown();
		dos_process_exit(1);
	}

#ifdef RESTUNTS_SDL3
	/* Driver setup resets audio state, so apply the requested source afterward
	 * and before the first intro or menu song is loaded. */
	sdl3_music_set_enabled(startup_options.ogg_music_enabled);
#endif

	if (startup_options.sound_disabled) {
		audio_toggle_music();
		audio_toggle_effects();
	}

	dos_set_critical_error_handler(&show_disk_error_dialog);

	load_palandcursor();

	startup_measure_video();

	random_wait();

	copy_material_list_pointers(material_clrlist_ptr, material_clrlist2_ptr, material_patlist_ptr,
								material_patlist2_ptr, 0);
}

static void init_full_game(legacy_s16 argc, legacy_s8 *argv[])
{
	init_main(argc, argv);
#ifdef RESTUNTS_HAS_VGA_PAGES
	/* Dump tools call init_main and hash the packed mode-13h screen. */
	if (startup_options.mode4_requested == 0) {
		mouse_draw_opaque_check();
		video_pages_initialize();
	}
#endif
	init_div0();
	init_row_tables();

	mainresptr = file_load_resfile("main");
	fontdefptr = file_load_resource(FILE_RESOURCE_BINARY_FATAL, "fontdef.fnt");
	fontnptr = file_load_resource(FILE_RESOURCE_BINARY_FATAL, "fontn.fnt");

	font_set_fontdef();
	init_polyinfo();
	init_trackdata();

	reset_race_loop_state();

	init_kevinrandom("kevin");

	strcpy(gameconfig.game_trackname, "DEFAULT");
}

static void init_main_input_state(void)
{
	input_do_checking(1);
	input_do_checking(1);
	mouse_draw_opaque_check();
	kbormouse = 0;
	passed_security = 1; // set to 0 for the original copy protection
}

legacy_s16 run_shape_preview(legacy_s16 argc, legacy_s8 *argv[])
{
	// initialization
	init_full_game(argc, argv);
	init_main_input_state();
	//set_default_car();

	// try do something
	menu_reset_animation_timers();
	set_projection(STARTUP_PROJECTION_X, STARTUP_PROJECTION_Y, GAME_SCREEN_WIDTH,
				   GAME_TOP_PANEL_HEIGHT);
	// The camera projection would at best draw a pixel without this setup.

	render_window_sprite =
		sprite_make_wnd(GAME_SCREEN_WIDTH, GAME_TOP_PANEL_HEIGHT, GAME_SCREEN_COLOR);

	//run_intro_looped();

	legacy_s16 carposangle = polarAngle(carpos.y, carpos.z);

	shape3d_load_all();
	shape3d_load_car_shapes("coun", "coun");
	select_cliprect_rotate(0, carposangle, 0, &cliprect, 0);

	//shaperect = cliprect;
	transshape.material = 0;
	transshape.rotvec.x = 0;
	transshape.rotvec.y = 0;
	transshape.pos = carpos;

	transshape.culling_distance = 0; // The abandoned experiment used 30000.
	transshape.ts_flags = 0;
	transshape.rectptr = &shaperect;

	legacy_s16 input_flags;
	legacy_s16 counter = 0;
	legacy_s16 shapeindex = 24;
	for (;; counter++) {
		transshape.rotvec.z = 0; // An abandoned experiment added 560.

		// The original adds 2728 bytes, or 124 22-byte SHAPE3D records,
		// to reach the first car shape loaded by shape3d_load_car_shapes().

		transshape.shapeptr = &game3dshapes[shapeindex];

		//transshape.shapeptr = &game3dshapes[124];
		//transshape.shapeptr = &game3dshapes[124];

		shape3d_transform_and_queue(&transshape);

		sprite_select_render_window();
		sprite_clear_target(3);

		//sprite_set_target_clip_bounds(50, 200, 50, 100);
		shape3d_render_queued_primitives(); // renders to drawing_sprite

		//sprite_select_screen_compat();
		sprite_blit_to_video(render_window_sprite, 0);

		input_flags = get_kb_or_joy_flags(); //kb_get_char();
		if (input_flags == 4) {				 // right
			shapeindex++;
			shapeindex = (shapeindex + STARTUP_SHAPE_COUNT) % STARTUP_SHAPE_COUNT;
		} else if (input_flags == 8) { // left
			shapeindex--;
			shapeindex = (shapeindex + STARTUP_SHAPE_COUNT) % STARTUP_SHAPE_COUNT;
		} else if (input_flags != 0) {
			legacy_s8 far *textresptr = locate_text_res(mainresptr, "dos");
			// DIALOG_AUTO_POSITION centers both dialog coordinates.
			legacy_s16 result = show_dialog(DIALOG_TYPE_MENU, DIALOG_SAVE_BACKGROUND, textresptr, 0,
											170, dialog_border_color, 0, 0);
			if (result >= 1) {
				break;
			}
		}
	}

	//sprite_select_mcga_backbuffer();
	//sprite_set_target_clip_bounds(0, 320, 0, 200);
	//sprite_select_screen_compat();
	//sprite_clear_target(2);
	//sprite_select_render_window();
	//sprite_select_screen_compat();

	//sprite_putimage(render_window_sprite->sprite_bitmapptr);

	//fatal_error("happy yet?");

	// shutdown
	shutdown_dos_game();

	fatal_error("err %i", input_flags);

	return 0;
}

static legacy_s16 main_menu_select_race(legacy_s16 result, legacy_s8 *start_in_replay)
{
	if (result == -1) {
		return -1;
	} else if (result == 0) {
		*start_in_replay = 0;
	} else if (result == 1) {
		check_input();
		show_waiting();
		run_car_menu(&gameconfig.game_playercarid[0], &gameconfig.game_playermaterial,
					 &gameconfig.game_playertransmission, 0);
		return 0;
	} else if (result == 2) {
		check_input();
		show_waiting();
		run_opponent_menu();
		return 0;
	} else if (result == 3) {
		run_tracks_menu(0);
		return 0;
	} else if (result == 4) {
		check_input();
		show_waiting();
		result = run_option_menu();
		if (result == 0) {
			return 0;
		} else {
			// Enter replay mode if the option-menu result is nonzero.
			*start_in_replay = 1;
		}
	} else {
		return 0;
	}

	return 1;
}

static void main_menu_backup_track(void)
{
	_memcpy(&gameconfigcopy, &gameconfig, sizeof(struct GAMEINFO));
	for (legacy_u16 i = 0; i < REPLAY_TRACK_SIZE; i++) {
		track_and_directory_backup[i] = track_element_map[i];
	}
	for (legacy_s16 i = 0; i < TRACK_PATH_STORAGE_SIZE; i++) {
		track_and_directory_backup[i + TRACK_PRIMARY_PATH_OFFSET] = track_directory[i];
		track_and_directory_backup[i + TRACK_SECONDARY_PATH_OFFSET] = replay_directory[i];
	}
}

static void main_menu_restore_track(void)
{
	_memcpy(&gameconfigcopy, &gameconfig, sizeof(struct GAMEINFO));
	for (legacy_u16 i = 0; i < REPLAY_TRACK_SIZE; i++) {
		track_element_map[i] = track_and_directory_backup[i];
	}
	for (legacy_s16 i = 0; i < TRACK_PATH_STORAGE_SIZE; i++) {
		track_directory[i] = track_and_directory_backup[i + TRACK_PRIMARY_PATH_OFFSET];
		replay_directory[i] = track_and_directory_backup[i + TRACK_SECONDARY_PATH_OFFSET];
	}
}

static legacy_s16 main_menu_prepare_track(void)
{
	if (idle_expired == 0) {
		legacy_s16 result = track_setup();
		//result = setup_track();
		if (result != 0) {
			run_tracks_menu(1);
			return 0;
		}
		random_wait();
		if (passed_security == 0) {
			fatal_error("security check");
			//get_super_random();
			//security_check();
		}
	} else if (file_find("tedit.*") == 0) {
		return -1;
	}

	return 1;
}

static void main_menu_run_race(legacy_s8 start_in_replay)
{
	cvxptr = mmgr_alloc_resbytes("cvx", sizeof(struct GAMESTATE) * GAMESTATE_CHECKPOINT_COUNT);
	init_game_state(GAMESTATE_INIT_RESET_CHECKPOINTS);

	if (start_in_replay != 0) {
		replay_recording_flags = 0;
	} else {
		gameconfig.game_recordedframes = 0;
	}

	while (1) {
		show_waiting();
		run_game();
		if (idle_expired == 0 && replay_recording_flags != 0) {
			legacy_s16 result = end_hiscore();
			if (result == 0) {
				// view replay
				replay_recording_flags = REPLAY_RECORDING_RESTARTABLE_FLAG;
				continue;
			} else if (result == 1) {
				// drive
				gameconfig.game_recordedframes = 0;
				continue;
			}
		}
		// main menu
		break;
	}

	main_menu_restore_track();
	mmgr_release(cvxptr);
}

legacy_s16 run_main_menu_loop(legacy_s16 argc, legacy_s8 *argv[])
{
	init_full_game(argc, argv);

	//fatal_error("ai");
	init_main_input_state();
	set_default_car();

	legacy_s8 start_in_replay;
	legacy_s16 reload_track = 1;
	while (1) {
		ensure_file_exists(2);

		if (reload_track != 0) {
			file_build_path(track_directory, gameconfig.game_trackname, ".trk", g_path_buf);
			file_read_fatal(g_path_buf, track_element_map);
		}

		idle_expired = 0;
		legacy_s16 result = 0;
		if (startup_options.skip_intro != 0) {
			startup_options.skip_intro = 0;
		} else {
			result = run_intro_looped();
		}
		if (result == 27) {
			legacy_s8 far *textresptr = locate_text_res(mainresptr, "dos");
			result =
				show_dialog(DIALOG_TYPE_MENU, DIALOG_SAVE_BACKGROUND, textresptr,
							DIALOG_AUTO_POSITION, DIALOG_AUTO_POSITION, dialog_border_color, 0, 0);
			if (result >= 1) {
				shutdown_dos_game();
				return result;
			}
			reload_track = 0;
			continue;
		}

		while (1) {
			ensure_file_exists(2);
			result = main_menu_select_race(run_menu(), &start_in_replay);
			if (result == 0) {
				continue;
			}
			if (result == -1) {
				audio_unload();
				reload_track = 0;
				break;
			}
			main_menu_backup_track();
			result = main_menu_prepare_track();
			if (result == 0) {
				continue;
			}
			audio_unload();
			if (result == -1) {
				reload_track = 0;
				break;
			}
			main_menu_run_race(start_in_replay);
			if (idle_expired != 0) {
				reload_track = 0;
				break;
			}
		}
	}
}
