#include <stddef.h>
#include <stdarg.h>
#ifdef RESTUNTS_SDL3
#include "renderer/settings.h"
#include "../platform/sdl3/music.h"
#include "../platform/sdl3/video_game.h"
#ifndef __ANDROID__
#include "../platform/sdl3/controller_game.h"
#endif
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
#include "../platform/sdl3/touch_game.h"
#endif
#endif
#include "audio/audio.h"
#include "dashboard.h"
#include "car/car_speed.h"
#include "car/car_resources.h"
#include "physics/physics_internal.h"
#include "race/race_route.h"
#include "owoot/owoot.h"
#include "restunts.h"
#include "video/video_pages.h"
#include "fileio.h"
#include "fatal.h"
#include "game_input.h"
#include "frame/frame_internal.h"
#include "keyboard.h"
#include "legacy.h"
#include "math/math.h"
#include "memmgr.h"
#include "menu/menu_internal.h"
#include "platform.h"
#include "race/race.h"
#include "race/race_resources.h"
#include "replay/replay.h"
#include "ghost/ghost.h"
#include "replay/replay_record.h"
#include "replay/replay_viewer.h"
#include "replay/replay_viewer_internal.h"
#include "shape2d/shape2d.h"
#include "shape3d/shape3d.h"
#include "timing.h"
#include "ui/ui_dialog.h"
#include "ui/ui_input.h"
#include "ui/ui_text.h"
#include "track/track_objects.h"
#include "camera.h"
#include "video/video_frame.h"
#include "opponent/opponent.h"
#include "scene_resources.h"
#include "audio/audio_control.h"
#include "menu/menu_common.h"
#include "externs.h"
#include "startup_internal.h"

struct STARTUP_OPTIONS startup_options;

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

static void startup_option_error_text(const legacy_s8 *text)
{
	dos_write_stderr(text, strlen(text));
}

static void startup_parse_boolean(const legacy_s8 *argument, const legacy_s8 *prefix,
								  const legacy_s8 *name, legacy_u8 *enabled, legacy_u8 *specified)
{
	const legacy_s8 *option = prefix;
	const legacy_s8 *value = argument;
	while (*option != 0) {
		legacy_u8 character = (legacy_u8)*value;
		if (character >= 'A' && character <= 'Z') {
			character += 'a' - 'A';
		}
		if (character != (legacy_u8)*option) {
			break;
		}
		option++;
		value++;
	}
	if (*option != 0) {
		return;
	}
	if (*specified != 0) {
		startup_option_error_text("Only one ");
		startup_option_error_text(name);
		startup_option_error_text(" option may be specified (");
		startup_option_error_text(prefix);
		startup_option_error_text(").\n");
		dos_process_exit(1);
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
	startup_option_error_text("Invalid ");
	startup_option_error_text(name);
	startup_option_error_text(" option '");
	startup_option_error_text(argument);
	startup_option_error_text("'; use ");
	startup_option_error_text(prefix);
	startup_option_error_text("on or ");
	startup_option_error_text(prefix);
	startup_option_error_text("off.\n");
	dos_process_exit(1);
}

static void startup_parse_options(legacy_s16 argc, legacy_s8 *argv[],
								  struct STARTUP_OPTIONS *options)
{
	options->mode4_requested = 0;
	options->sound_disabled = 0;
	options->unused_nd_option = 0;
	options->skip_intro = 0;
	options->fps_display_enabled = FRAME_FPS_DISPLAY_OFF;
	options->fps_display_specified = 0;
#ifdef RESTUNTS_SDL3
	hires_settings_reset_startup(options);
	options->ogg_music_enabled = 0;
	options->ogg_music_specified = 0;
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) && !defined(__DJGPP__)
	options->fullscreen_enabled = SDL3_STARTUP_FULLSCREEN_OFF;
	options->fullscreen_specified = 0;
#endif
#ifndef __ANDROID__
	options->controller_vibration_enabled = 1;
	options->controller_vibration_specified = 0;
#endif
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
	options->control_layout_enabled = STARTUP_CONTROL_LAYOUT_ON;
	options->control_layout_specified = 0;
#endif
#endif
	for (legacy_u16 i = 1; argc > i; ++i) {
#ifdef RESTUNTS_SDL3
		hires_settings_parse_startup(argv[i], options);
		startup_parse_boolean(argv[i], STARTUP_OGG_MUSIC_PREFIX, "Ogg music",
							  &options->ogg_music_enabled, &options->ogg_music_specified);
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) && !defined(__DJGPP__)
		startup_parse_boolean(argv[i], STARTUP_FULLSCREEN_PREFIX, "Fullscreen",
							  &options->fullscreen_enabled, &options->fullscreen_specified);
#endif
#ifndef __ANDROID__
		startup_parse_boolean(argv[i], STARTUP_CONTROLLER_VIBRATION_PREFIX, "Controller vibration",
							  &options->controller_vibration_enabled,
							  &options->controller_vibration_specified);
#endif
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
		startup_parse_boolean(argv[i], STARTUP_CONTROL_LAYOUT_PREFIX, "Control layout",
							  &options->control_layout_enabled, &options->control_layout_specified);
#endif
#endif
		startup_parse_boolean(argv[i], STARTUP_FPS_DISPLAY_PREFIX, "FPS display",
							  &options->fps_display_enabled, &options->fps_display_specified);
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
	STARTUP_TRACE("keyboard and geometry");
	startup_install_keyboard_callbacks();
	// Video
	init_video_geometry_flags();

	STARTUP_TRACE("resource arena");
	mmgr_init_conventional_arena();
	STARTUP_TRACE("game data version");
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
	reset_car_capabilities();
	configure_legacy_collision(argc, argv);
	configure_left_corner_bias(argc, argv);
	configure_dual_way_switching(argc, argv);
	configure_chicane_fast_grass(argc, argv);
	configure_amiga_grass(argc, argv);
	configure_owoot(argc, argv);
	startup_parse_options(argc, argv, &startup_options);
	fps_display_enabled = startup_options.fps_display_enabled;
	frame_fps_reset();
#ifdef RESTUNTS_SDL3
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) && !defined(__DJGPP__)
	sdl3_video_set_startup_fullscreen(startup_options.fullscreen_enabled);
#endif
#ifndef __ANDROID__
	sdl3_controller_set_vibration_enabled(startup_options.controller_vibration_enabled);
#endif
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
	sdl3_touch_set_layout_visible(startup_options.control_layout_enabled);
#endif
	hires_settings_apply_startup(&startup_options);
#endif

	// Unused "/nd" switch. Maybe used when loading other video drivers?
	(void)startup_options.unused_nd_option;

	STARTUP_TRACE("display");
	// Video mode.
	dos_video_set_mode_13h();
	if (startup_options.mode4_requested) {
		dos_video_set_mode4();
	}

	STARTUP_TRACE("timer");
	dos_timer_setup_interrupt();

	sprite_select_screen_and_clear();

	dos_mouse_init(GAME_SCREEN_WIDTH, GAME_SCREEN_HEIGHT);

	STARTUP_TRACE("audio");
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

	STARTUP_TRACE("palette and cursor");
	load_palandcursor();

	STARTUP_TRACE("benchmark");
	startup_measure_video();

	random_wait();

	STARTUP_TRACE("materials");
	copy_material_list_pointers(material_clrlist_ptr, material_clrlist2_ptr, material_patlist_ptr,
								material_patlist2_ptr, 0);
}
