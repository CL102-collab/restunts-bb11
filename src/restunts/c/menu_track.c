#include "fileio.h"
#include "legacy.h"
#include "memmgr.h"
#include "menu_internal.h"
#include "platform.h"
#include "shape2d.h"
#include "shape3d.h"
#include "ui_text.h"
#include "race_resources.h"
#include "ui_dialog.h"
#include "game_input.h"
#include "ui_input.h"
#include "track_objects.h"
#include "skybox.h"
#include "highscore.h"
#include "menu_common.h"
#include "externs.h"
#include "keyboard.h"
#include "ghost.h"
#include "frame_internal.h"

#define TRACK_EDITOR_RESOURCE_FILE_INDEX 3
#define TRACK_MENU_BUTTON_COUNT 3
#define TRACK_MENU_NO_SELECTION 255U
#define TRACK_MENU_SETUP_WAIT_TICKS 130
#define TRACK_MENU_PREVIEW_WAIT_TICKS 155
#define TRACK_MENU_IDLE_LIMIT_TICKS 6000
#define TRACK_MENU_SCREEN_WIDTH 320U
#define TRACK_MENU_SCREEN_HEIGHT 200U
#define TRACK_MENU_TRANSPARENT_COLOR 15U
#define TRACK_SKYBOX_ELEMENT_INDEX 900U
#define TRACK_PREVIEW_PROJECTION_SCALE 40
#define TRACK_MENU_NAME_Y 6
#define TRACK_MENU_HIGHSCORE_LABEL_Y 18
#define TRACK_MENU_HIGHSCORE_ENTRY_Y 30
#define TRACK_MENU_HIGHSCORE_FIELD_COUNT 4U
#define TRACK_MENU_HIGHSCORE_NAME_X 16
#define TRACK_MENU_HIGHSCORE_CAR_X 120
#define TRACK_MENU_HIGHSCORE_OPPONENT_X 224
#define TRACK_MENU_HIGHSCORE_TIME_X 272
#define HIGHSCORE_UNSET_TIME 65535U
#define TRACK_MENU_FIRST_BUTTON_X 17
#define TRACK_MENU_BUTTON_SPACING 96
#define TRACK_MENU_BUTTON_Y 172
#define TRACK_MENU_BUTTON_WIDTH 94
#define TRACK_MENU_BUTTON_HEIGHT 24

enum TRACK_MENU_BUTTON {
	TRACK_MENU_LOAD_BUTTON = 0,
	TRACK_MENU_EDIT_BUTTON = 1,
	TRACK_MENU_EXIT_BUTTON = 2
};

struct TRACK_MENU_STATE {
	legacy_u8 selected;
	legacy_u8 previous;
	legacy_u8 blit_mode;
};

static void track_menu_draw_preview(void)
{
	show_waiting();
	waitflag = TRACK_MENU_PREVIEW_WAIT_TICKS;
	render_window_sprite = sprite_make_wnd(TRACK_MENU_SCREEN_WIDTH, TRACK_MENU_SCREEN_HEIGHT,
										   TRACK_MENU_TRANSPARENT_COLOR);
	load_skybox((legacy_s8)track_element_map[TRACK_SKYBOX_ELEMENT_INDEX]);
	shape3d_load_all();
	set_projection(TRACK_PREVIEW_PROJECTION_SCALE, TRACK_PREVIEW_PROJECTION_SCALE,
				   TRACK_MENU_SCREEN_WIDTH, TRACK_MENU_SCREEN_HEIGHT);
	init_game_state(GAMESTATE_INIT_SKIP_ROUTE_SETUP);
	sprite_select_render_window();
	sprite_clear_target((legacy_u8)skybox.ground_color);
	sprite_set_target_clip_bounds(0, TRACK_MENU_SCREEN_WIDTH, 0, TRACK_MENU_SCREEN_HEIGHT);
	draw_track_preview();
	shape3d_free_all();
	unload_skybox();
}

static void track_menu_draw_highscore(void)
{
	legacy_u8 text_offsets[TRACK_MENU_HIGHSCORE_FIELD_COUNT];
	struct HIGHSCORE_ENTRY far *scores;
	if (highscore_load_or_create(0) == 0) {
		scores = (struct HIGHSCORE_ENTRY far *)track_highscore_table;
		legacy_u16 score = highscore_entry_time(&scores[ranking_entry_order[0]]);
		if (score != HIGHSCORE_UNSET_TIME) {
			copy_string(&resID_byte1, locate_text_res(mainresptr, "hs0"));
			intro_draw_text(&resID_byte1, font_centered_text_x(&resID_byte1),
							TRACK_MENU_HIGHSCORE_LABEL_Y, dialog_fnt_colour, 0);
			font_set_fontdef2(fontnptr);
			print_highscore_entry(0, text_offsets);
			font_set_colors(0, 0);
			font_draw_text(&resID_byte1 + text_offsets[0], TRACK_MENU_HIGHSCORE_NAME_X,
						   TRACK_MENU_HIGHSCORE_ENTRY_Y);
			font_draw_text(&resID_byte1 + text_offsets[1], TRACK_MENU_HIGHSCORE_CAR_X,
						   TRACK_MENU_HIGHSCORE_ENTRY_Y);
			font_draw_text(&resID_byte1 + text_offsets[2], TRACK_MENU_HIGHSCORE_OPPONENT_X,
						   TRACK_MENU_HIGHSCORE_ENTRY_Y);
			font_draw_text(&resID_byte1 + text_offsets[3], TRACK_MENU_HIGHSCORE_TIME_X,
						   TRACK_MENU_HIGHSCORE_ENTRY_Y);
			font_set_fontdef();
		}
	}
}

static void track_menu_draw_buttons(void)
{
	legacy_s8 far *text_resource = (legacy_s8 far *)file_load_resfile("tedit");
	draw_button(locate_text_res(text_resource, "bmt"), TRACK_MENU_FIRST_BUTTON_X,
				TRACK_MENU_BUTTON_Y, TRACK_MENU_BUTTON_WIDTH, TRACK_MENU_BUTTON_HEIGHT,
				button_top_color, button_bottom_color, button_fill_color, 0);
	draw_button(locate_text_res(text_resource, "bet"),
				TRACK_MENU_FIRST_BUTTON_X + TRACK_MENU_BUTTON_SPACING, TRACK_MENU_BUTTON_Y,
				TRACK_MENU_BUTTON_WIDTH, TRACK_MENU_BUTTON_HEIGHT, button_top_color,
				button_bottom_color, button_fill_color, 0);
	draw_button(locate_text_res(text_resource, "bmm"),
				TRACK_MENU_FIRST_BUTTON_X + TRACK_MENU_BUTTON_SPACING * 2, TRACK_MENU_BUTTON_Y,
				TRACK_MENU_BUTTON_WIDTH, TRACK_MENU_BUTTON_HEIGHT, button_top_color,
				button_bottom_color, button_fill_color, 0);
	unload_resource(text_resource);
}

static void track_menu_draw_background(void)
{
	track_menu_draw_preview();
	sprite_select_render_window();
	strcpy(&resID_byte1, "'");
	strcat(&resID_byte1, gameconfig.game_trackname);
	strcat(&resID_byte1, "'");
	intro_draw_text(&resID_byte1, font_centered_text_x(&resID_byte1), TRACK_MENU_NAME_Y,
					dialog_fnt_colour, 0);
	track_menu_draw_highscore();

	track_menu_draw_buttons();
	if (frame_status_overlay_active() != 0) {
		sprite_select_render_window();
		sprite_set_target_clip_bounds(0, TRACK_MENU_SCREEN_WIDTH, 0, TRACK_MENU_SCREEN_HEIGHT);
		frame_status_draw_text();
	}
}

static legacy_u16 track_menu_poll_input(struct TRACK_MENU_STATE *menu)
{
	if (frame_status_expire_idle() != 0) {
		mouse_draw_opaque_check();
		sprite_free_wnd(render_window_sprite);
		track_menu_draw_background();
		menu->previous = TRACK_MENU_NO_SELECTION;
		mouse_draw_transparent_check();
	}
	if (menu->selected != menu->previous) {
		menu->previous = menu->selected;
		sprite_blit_to_video(render_window_sprite, LEGACY_S8_FROM_BITS(menu->blit_mode));
		menu->blit_mode = MENU_BLIT_MODE_REFRESH;
		sprite_select_screen_compat();
		menu_reset_animation_timers();
	}

	legacy_u16 elapsed = (legacy_u16)menu_animate_button_highlight(
		menu->selected, trackmenu_buttons, menu_highlight_second_color, menu_highlight_first_color);
	menu_update_idle_counter(elapsed, TRACK_MENU_IDLE_LIMIT_TICKS);
	legacy_u16 key = (legacy_u16)input_checking(LEGACY_S16_FROM_BITS(elapsed));
	legacy_s16 hit = (legacy_s16)mouse_multi_hittest(TRACK_MENU_BUTTON_COUNT, trackmenu_buttons);
	if (hit != -1) {
		menu->selected = (legacy_u8)hit;
	}
	if (idle_expired != 0) {
		menu->selected = TRACK_MENU_EXIT_BUTTON;
		key = KEY_ENTER;
	}
	return key;
}

static legacy_u8 track_menu_activate_key(struct TRACK_MENU_STATE *menu, legacy_u16 key)
{
	if (key == (legacy_u16)KEY_F12
#ifdef RESTUNTS_SDL3
		|| key == (legacy_u16)KEY_SHIFT_F12
#endif
	) {
		mouse_draw_opaque_check();
		handle_ingame_kb_shortcuts(LEGACY_S16_FROM_BITS(key));
		sprite_free_wnd(render_window_sprite);
		track_menu_draw_background();
		menu->previous = TRACK_MENU_NO_SELECTION;
		mouse_draw_transparent_check();
		return 0;
	}
	if (key == 0) {
		return 0;
	}
	if (key == KEY_LEFT) {
		menu->selected = menu->selected == TRACK_MENU_LOAD_BUTTON
							 ? TRACK_MENU_EXIT_BUTTON
							 : (legacy_u8)(menu->selected - 1U);
		return 0;
	}
	if (key == KEY_RIGHT) {
		menu->selected = menu->selected >= TRACK_MENU_EXIT_BUTTON
							 ? TRACK_MENU_LOAD_BUTTON
							 : (legacy_u8)(menu->selected + 1U);
		return 0;
	}
	if (key == KEY_ESCAPE) {
		menu->selected = TRACK_MENU_NO_SELECTION;
	} else if (key != KEY_ENTER && key != KEY_SPACE) {
		return 0;
	}
	return 1;
}

void run_tracks_menu(legacy_s16 reload_track)
{
	ensure_file_exists(TRACK_EDITOR_RESOURCE_FILE_INDEX);
	legacy_s16 needs_track_setup = reload_track != 0;
	struct TRACK_MENU_STATE menu;
	for (;;) {
		if (needs_track_setup != 0) {
			check_input();
			show_waiting();
			waitflag = TRACK_MENU_SETUP_WAIT_TICKS;
			track_setup();
			load_tracks_menu_shapes();
			ghost_check_track();
			needs_track_setup = 0;
		}

		menu.selected = 0;
		menu.previous = TRACK_MENU_NO_SELECTION;
		menu.blit_mode = MENU_BLIT_MODE_INITIAL;
		track_menu_draw_background();

		for (;;) {
			legacy_u16 key = track_menu_poll_input(&menu);

			if (track_menu_activate_key(&menu, key) == 0) {
				continue;
			}

			if (menu.selected == TRACK_MENU_LOAD_BUTTON) {
				legacy_s8 far *prompt = locate_text_res(mainresptr, "trk");
				legacy_s8 chosen = do_fileselect_dialog(track_directory, gameconfig.game_trackname,
														".trk", prompt);
				file_build_path(track_directory, gameconfig.game_trackname, ".trk", g_path_buf);
				if (chosen != 0) {
					file_read_fatal(g_path_buf, track_element_map);
					track_setup();
					ghost_check_track();
					sprite_free_wnd(render_window_sprite);
					break;
				}
				menu.previous = TRACK_MENU_NO_SELECTION;
				continue;
			}

			sprite_free_wnd(render_window_sprite);
			if (menu.selected == TRACK_MENU_EDIT_BUTTON) {
				needs_track_setup = 1;
			} else {
				return;
			}
			break;
		}
	}
}
