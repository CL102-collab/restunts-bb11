#include "fileio.h"
#include "legacy.h"
#include "memmgr.h"
#include "menu_internal.h"
#include "platform.h"
#include "shape2d.h"
#include "shape3d.h"
#include "ui_text.h"
#include "ui_dialog.h"
#include "fatal.h"
#include "game_input.h"
#include "audio_control.h"
#include "ui_dialog_internal.h"
#include "replay_viewer_internal.h"
#include "externs.h"
#include "keyboard.h"
#ifdef RESTUNTS_SDL3
#include "../platform/sdl3/controller_game.h"
#endif

#define JOYSTICK_BUTTON_MASK 48U
#define OPTION_MENU_VERSION_TEXT_Y 16
#define OPTION_MENU_GIT_HASH_LENGTH 7U
#define REPLAY_LOAD_WAIT_TICKS 150

enum OPTION_INPUT_INDEX { OPTION_INPUT_KEYBOARD, OPTION_INPUT_JOYSTICK, OPTION_INPUT_MOUSE };

#ifndef RESTUNTS_BUILD_DATE
#define RESTUNTS_BUILD_DATE __DATE__
#endif

enum OPTION_MENU_FRAME_RATE_INDEX {
	OPTION_MENU_LOW_FRAME_RATE_INDEX = 7,
	OPTION_MENU_NORMAL_FRAME_RATE_INDEX = 8
};

void show_insufficient_memory_dialog(void)
{
	show_dialog(DIALOG_TYPE_ACKNOWLEDGEMENT, DIALOG_SAVE_BACKGROUND,
				locate_text_res(mainresptr, insufficient_memory_dialog_id), -1, -1,
				dialog_border_color, 0, 0);
}

void select_keyboard_driving(void)
{
	input_push_status();
	dos_timer_set_callbacks_suspended(1);
	audio_suspend();
	show_dialog(DIALOG_TYPE_DELAY, DIALOG_SAVE_BACKGROUND,
				locate_text_res(mainresptr, keyboard_driving_dialog_id), -1, -1,
				dialog_border_color, 0, 0);
	dos_joystick_set_enabled(0);
	mouse_driving_enabled = 0;
#ifdef RESTUNTS_SDL3
	sdl3_input_set_driving_mode(SDL3_DRIVING_KEYBOARD);
#endif
	dos_timer_set_callbacks_suspended(0);
	audio_resume();
	input_pop_status();
}

static void joy_dialog_finish(void)
{
	kb_check();
	mouse_driving_enabled = 0;
#ifdef RESTUNTS_SDL3
	sdl3_input_set_driving_mode(dos_joystick_is_enabled() != 0 ? SDL3_DRIVING_JOYSTICK
															   : SDL3_DRIVING_KEYBOARD);
	sdl3_input_pop_modal();
#endif
	audio_resume();
	dos_timer_set_callbacks_suspended(0);
	input_pop_status();
}

static void joy_dialog_draw_grid(legacy_s16 *positions, legacy_s16 *button_x, legacy_s16 *button_y,
								 legacy_s16 *button_width, legacy_s16 *button_height)
{
	legacy_s16 line_height =
		LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_SUB(positions[13], positions[3]), 8);
	sprite_fill_rect(LEGACY_S16_WRAP_SUB(positions[2], 4), positions[3], 1, line_height,
					 dialog_border_color);
	sprite_fill_rect(LEGACY_S16_WRAP_SUB(positions[4], 4), positions[5], 1, line_height,
					 dialog_border_color);
	legacy_s16 line_width = LEGACY_S16_WRAP_SUB(positions[6], positions[0]);
	sprite_fill_rect(positions[0], LEGACY_S16_WRAP_SUB(positions[9], 4), line_width, 1,
					 dialog_border_color);
	sprite_fill_rect(positions[0], LEGACY_S16_WRAP_SUB(positions[11], 4), line_width, 1,
					 dialog_border_color);

	button_x[0] = positions[2];
	button_x[1] = positions[2];
	button_x[5] = positions[2];
	button_x[2] = positions[4];
	button_x[3] = positions[4];
	button_x[4] = positions[4];
	button_x[6] = positions[0];
	button_x[7] = positions[0];
	button_x[8] = positions[0];
	button_y[0] = positions[9];
	button_y[3] = positions[9];
	button_y[7] = positions[9];
	button_y[1] = positions[3];
	button_y[2] = positions[3];
	button_y[8] = positions[3];
	button_y[4] = positions[11];
	button_y[5] = positions[11];
	button_y[6] = positions[11];
	*button_width = LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_SUB(positions[2], positions[0]), 8);
	*button_height = LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_SUB(positions[9], positions[1]), 8);
}

void calibrate_joystick_driving(void)
{
#ifdef RESTUNTS_SDL3
	sdl3_input_push_modal();
#endif
	input_push_status();
	dos_timer_set_callbacks_suspended(1);
	audio_suspend();
	legacy_s16 positions[15];
	if (LEGACY_S16_FROM_BITS(show_dialog(
			DIALOG_TYPE_PLACEHOLDERS, DIALOG_SAVE_BACKGROUND, locate_text_res(mainresptr, "joy"),
			DIALOG_AUTO_POSITION, DIALOG_AUTO_POSITION, dialog_border_color, positions, 0)) <= 0) {
		dos_joystick_set_enabled(0);
		joy_dialog_finish();
		return;
	}

	legacy_u8 visited[9];
	for (legacy_u16 i = 0; i < 9U; i++) {
		visited[i] = 0;
	}
	dos_joystick_set_enabled(1);
	mouse_draw_opaque_check();
	legacy_s16 button_height;
	legacy_s16 button_x[9];
	legacy_s16 button_y[9];
	legacy_s16 button_width;
	joy_dialog_draw_grid(positions, button_x, button_y, &button_width, &button_height);

	joystick_reset_calibration();
	legacy_s16 selected = -1;
	for (;;) {
		if (kb_read_char() != 0) {
			break;
		}
		legacy_u16 joy_flags = (legacy_u16)dos_get_joy_flags();
		if ((joy_flags & JOYSTICK_BUTTON_MASK) != 0) {
			break;
		}
		legacy_s16 next_selected = (legacy_s16)input_direction_from_flags(joy_flags);
		if (next_selected == selected) {
			continue;
		}
		for (legacy_u16 i = 0; i < 9U; i++) {
			sprite_fill_rect(button_x[i], button_y[i], button_width, button_height,
							 dialog_background_color);
		}
		sprite_fill_rect(button_x[next_selected], button_y[next_selected], button_width,
						 button_height, dialog_fnt_colour);
		selected = next_selected;
		visited[next_selected] = 1;
	}

	for (legacy_u16 i = 0; i < 9U; i++) {
		dos_joystick_set_enabled(dos_joystick_is_enabled() & visited[i]);
	}
	sprite_pop_background();
	if (dos_joystick_is_enabled() == 0) {
		show_dialog(DIALOG_TYPE_ACKNOWLEDGEMENT, DIALOG_SAVE_BACKGROUND,
					locate_text_res(mainresptr, "jox"), DIALOG_AUTO_POSITION, DIALOG_AUTO_POSITION,
					dialog_border_color, 0, 0);
	}

	joy_dialog_finish();
}

void select_mouse_driving(void)
{
	input_push_status();
	dos_timer_set_callbacks_suspended(1);
	audio_suspend();
	mouse_driving_enabled = 1;
#ifdef RESTUNTS_SDL3
	sdl3_input_set_driving_mode(SDL3_DRIVING_MOUSE);
#endif
	show_dialog(DIALOG_TYPE_DELAY, DIALOG_SAVE_BACKGROUND,
				locate_text_res(mainresptr, mouse_driving_dialog_id), -1, -1, dialog_border_color,
				0, 0);
	dos_timer_set_callbacks_suspended(0);
	audio_resume();
	input_pop_status();
}

void show_pause_dialog(void)
{
	input_push_status();
	dos_timer_set_callbacks_suspended(1);
	audio_suspend();
	show_dialog(DIALOG_TYPE_ACKNOWLEDGEMENT, DIALOG_SAVE_BACKGROUND,
				locate_text_res(mainresptr, pause_dialog_id), -1, -1, dialog_border_color, 0, 0);
	dos_timer_set_callbacks_suspended(0);
	audio_resume();
	input_pop_status();
}

void toggle_music_with_dialog(void)
{
	input_push_status();
	dos_timer_set_callbacks_suspended(1);
	legacy_s8 *message_id =
		audio_toggle_music() != 0 ? music_enabled_message_id : music_disabled_message_id;
	show_dialog(DIALOG_TYPE_DELAY, DIALOG_SAVE_BACKGROUND, locate_text_res(mainresptr, message_id),
				-1, -1, dialog_border_color, 0, 0);
	dos_timer_set_callbacks_suspended(0);
	input_pop_status();
}

void toggle_effects_with_dialog(void)
{
	input_push_status();
	dos_timer_set_callbacks_suspended(1);
	legacy_s8 *message_id =
		audio_toggle_effects() != 0 ? effects_enabled_message_id : effects_disabled_message_id;
	show_dialog(DIALOG_TYPE_DELAY, DIALOG_SAVE_BACKGROUND, locate_text_res(mainresptr, message_id),
				-1, -1, dialog_border_color, 0, 0);
	dos_timer_set_callbacks_suspended(0);
	input_pop_status();
}

void show_exit_to_dos_dialog(void)
{
	input_push_status();
	dos_timer_set_callbacks_suspended(1);
	audio_suspend();
	legacy_s16 result = show_dialog(DIALOG_TYPE_MENU, DIALOG_SAVE_BACKGROUND,
									locate_text_res(mainresptr, exit_to_dos_dialog_id), -1, -1,
									dialog_border_color, 0, 0);
	if (result == 1) {
		call_exitlist2();
	}
	dos_timer_set_callbacks_suspended(0);
	audio_resume();
	input_pop_status();
}

static void option_menu_mark_graphics(legacy_s8 *menu_text)
{
	copy_string(menu_text, locate_text_res(mainresptr, graphics_options_dialog_id));
	legacy_s8 selected_options[9];
	for (legacy_u16 option_index = 0; option_index < 9U; option_index++) {
		selected_options[option_index] = 0;
	}
	selected_options[detail_level] = 1;
	selected_options[5U + slow_video_mgmt] = 1;
	selected_options[configured_frame_rate == GAME_FRAME_RATE_LOW
						 ? OPTION_MENU_LOW_FRAME_RATE_INDEX
						 : OPTION_MENU_NORMAL_FRAME_RATE_INDEX] = 1;

	legacy_u16 text_index = 0;
	for (legacy_u16 option_index = 0; option_index < 9U; option_index++) {
		while (menu_text[text_index] != '[') {
			text_index++;
		}
		if (selected_options[option_index] != 0) {
			menu_text[text_index + 1U] = '*';
		}
		text_index++;
	}
}

void show_graphic_levels_menu(void)
{
	input_push_status();
	dos_timer_set_callbacks_suspended(1);
	audio_suspend();
	legacy_u16 original_frame_rate = configured_frame_rate;
	legacy_s8 menu_text[512];
	legacy_s8 selected = 0;
	for (;;) {
		option_menu_mark_graphics(menu_text);

		selected = LEGACY_S8_FROM_BITS(show_dialog(DIALOG_TYPE_MENU, DIALOG_SAVE_BACKGROUND,
												   (void far *)menu_text, -1, -1, performGraphColor,
												   0, (legacy_s16)selected));
		if (selected == -1 || selected == 9) {
			break;
		}
		switch (selected) {
			case 5:
				slow_video_mgmt = 0;
				break;
			case 6:
				slow_video_mgmt = 1;
				break;
			case 7:
				configured_frame_rate = GAME_FRAME_RATE_LOW;
				break;
			case 8:
				configured_frame_rate = GAME_FRAME_RATE_NORMAL;
				break;
			default:
				detail_level = (legacy_u8)selected;
				break;
		}
	}

	if (original_frame_rate != configured_frame_rate) {
		show_dialog(DIALOG_TYPE_ACKNOWLEDGEMENT, DIALOG_SAVE_BACKGROUND,
					locate_text_res(mainresptr, frame_rate_changed_message_id), -1, -1,
					dialog_border_color, 0, 0);
	}
	dos_timer_set_callbacks_suspended(0);
	audio_resume();
	input_pop_status();
}

static void option_menu_select_input(void)
{
	legacy_s8 initial_input;
#ifdef RESTUNTS_SDL3
	switch (sdl3_input_driving_mode()) {
		case SDL3_DRIVING_JOYSTICK:
			initial_input = OPTION_INPUT_JOYSTICK;
			break;
		case SDL3_DRIVING_MOUSE:
			initial_input = OPTION_INPUT_MOUSE;
			break;
		default:
			initial_input = OPTION_INPUT_KEYBOARD;
			break;
	}
#else
	if (mouse_driving_enabled != 0) {
		initial_input = OPTION_INPUT_MOUSE;
	} else if (dos_joystick_is_enabled() != 0) {
		initial_input = OPTION_INPUT_JOYSTICK;
	} else {
		initial_input = OPTION_INPUT_KEYBOARD;
	}
#endif
	legacy_s8 *input_text = locate_text_res(miscptr, "mid");
	legacy_s8 selected = LEGACY_S8_FROM_BITS(
		show_dialog(DIALOG_TYPE_MENU, DIALOG_SAVE_BACKGROUND, input_text, DIALOG_AUTO_POSITION,
					DIALOG_AUTO_POSITION, performGraphColor, 0, initial_input));
	if (selected == OPTION_INPUT_KEYBOARD) {
		select_keyboard_driving();
	} else if (selected == OPTION_INPUT_JOYSTICK) {
		calibrate_joystick_driving();
	} else if (selected == OPTION_INPUT_MOUSE) {
		select_mouse_driving();
	}
}

legacy_u16 run_option_menu(void)
{
#ifdef RESTUNTS_SDL3
	sdl3_input_push_modal();
#endif
	miscptr = file_load_resfile("misc");
	sprite_select_screen_compat();
	sprite_clear_target((legacy_u8)graphics_menu_background_color);
	legacy_s8 title_text[] = "Chocolate Stunts";
	intro_draw_text(title_text, font_centered_text_x(title_text), 6, dialog_fnt_colour, 0);
	legacy_s8 version_text[] = "Version 0000000 (" RESTUNTS_BUILD_DATE ")";
	for (legacy_u16 i = 0; i < OPTION_MENU_GIT_HASH_LENGTH; i++) {
		version_text[sizeof("Version ") - 1U + i] = RESTUNTS_GIT_HASH[i];
	}
	intro_draw_text(version_text, font_centered_text_x(version_text), OPTION_MENU_VERSION_TEXT_Y,
					dialog_fnt_colour, 0);

	legacy_s8 far *prompt;
	legacy_u8 menu_active = 1;
	while (menu_active != 0) {
		legacy_s8 selected = LEGACY_S8_FROM_BITS(
			show_dialog(DIALOG_TYPE_MENU, DIALOG_SAVE_BACKGROUND, locate_text_res(miscptr, "mop"),
						DIALOG_AUTO_POSITION, DIALOG_AUTO_POSITION, dialog_border_color, 0, 0));
		switch (selected) {
			case -1:
			case 6:
				menu_active = 0;
				break;

			case 0:
				option_menu_select_input();
				break;

			case 1:
				toggle_music_with_dialog();
				break;

			case 2:
				toggle_effects_with_dialog();
				break;

			case 3:
				prompt = locate_text_res(mainresptr, "rep");
				if (do_fileselect_dialog(replay_directory, replay_filename_input, ".rpl", prompt) !=
					0) {
					waitflag = REPLAY_LOAD_WAIT_TICKS;
					show_waiting();
					file_load_replay(replay_directory, replay_filename_input);
					menu_active = 1;
					unload_resource(miscptr);
#ifdef RESTUNTS_SDL3
					sdl3_input_pop_modal();
#endif
					return menu_active;
				}
				break;

			case 4:
				show_graphic_levels_menu();
				break;

			case 5:
				show_exit_to_dos_dialog();
				break;
		}
	}

	unload_resource(miscptr);
#ifdef RESTUNTS_SDL3
	sdl3_input_pop_modal();
#endif
	return menu_active;
}
