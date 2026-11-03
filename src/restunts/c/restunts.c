#include "extended/extended_menu_runtime.h"
#include <stddef.h>
#include <stdarg.h>
#ifdef RESTUNTS_SDL3
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
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
#ifdef RESTUNTS_SDL3
	extended_menu_backup_track();
#endif
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
#ifdef RESTUNTS_SDL3
	if (extended_menu_restore_track()) {
#else
	_memcpy(&gameconfigcopy, &gameconfig, sizeof(struct GAMEINFO));
#endif
		for (legacy_u16 i = 0; i < REPLAY_TRACK_SIZE; i++) {
			track_element_map[i] = track_and_directory_backup[i];
		}
#ifdef RESTUNTS_SDL3
	}
#endif
	for (legacy_s16 i = 0; i < TRACK_PATH_STORAGE_SIZE; i++) {
		track_directory[i] = track_and_directory_backup[i + TRACK_PRIMARY_PATH_OFFSET];
		replay_directory[i] = track_and_directory_backup[i + TRACK_SECONDARY_PATH_OFFSET];
	}
}

static legacy_s16 main_menu_prepare_track(void)
{
	STARTUP_TRACE("menu track begin");
	if (idle_expired == 0) {
#ifdef RESTUNTS_SDL3
		extended_menu_warn_track_version();
#endif
		legacy_s16 result = track_setup();
		STARTUP_TRACE("menu track complete");
		//result = setup_track();
		if (result != 0) {
#ifdef RESTUNTS_SDL3
			extended_menu_validation_error(result, track_validation_column, track_validation_row);
#endif
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
	STARTUP_TRACE("menu race begin");
	cvxptr = mmgr_alloc_resbytes("cvx", sizeof(struct GAMESTATE) * GAMESTATE_CHECKPOINT_COUNT);
	init_game_state(GAMESTATE_INIT_RESET_CHECKPOINTS);

	if (start_in_replay != 0) {
		replay_recording_flags = 0;
	} else {
		gameconfig.game_recordedframes = 0;
#ifdef RESTUNTS_SDL3
		extended_menu_begin_track_recording();
#endif
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
#ifdef RESTUNTS_SDL3
				extended_menu_begin_track_recording();
#endif
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
#ifdef RESTUNTS_SDL3
			extended_menu_load_startup_track();
#else
			file_read_fatal(g_path_buf, track_element_map);
#endif
		}

		idle_expired = 0;
		legacy_s16 result = 0;
		if (startup_options.skip_intro != 0) {
			startup_options.skip_intro = 0;
			/* The intro normally places the waiting dialog below the title. */
			waitflag = STARTUP_WAITING_MESSAGE_Y;
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
