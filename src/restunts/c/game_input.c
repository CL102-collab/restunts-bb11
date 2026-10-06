#include "fileio.h"
#include "game_input.h"
#include "keyboard.h"
#include "memmgr.h"
#include "platform.h"
#include "resource.h"
#include "shape2d.h"
#include "timing.h"
#include "ui_text.h"
#include "crash_state.h"
#include "camera.h"
#include "externs.h"
#include "ghost.h"
#include "frame_internal.h"
#ifdef RESTUNTS_SDL3
#include "hires.h"
#include "frame_adaptive.h"
#include "skybox_hires.h"
#include "../platform/sdl3/controller_game.h"
#endif

#define INPUT_DIRECTION_COUNT 16U
#define INPUT_KEY_COUNT 10U
#define INPUT_CALLBACK_COUNT 64U
#define INPUT_ASCII_KEY_COUNT 128U
#define INPUT_EXTENDED_KEY_COUNT 137U
#define INPUT_EXTENDED_KEY_MAX_INDEX (INPUT_EXTENDED_KEY_COUNT - 1U)
#define INPUT_ASCII_BYTE_MASK 255U
#define INPUT_ASCII_INDEX_MASK 127U
#define INPUT_MODE_STACK_LIMIT 8U
#define INPUT_REPEAT_DELAY_FRAMES 20U
#define INPUT_IDLE_LIMIT_FRAMES 500
#define INPUT_COUNTER_WRAP_LIMIT 20000
#define INPUT_COUNTER_WRAP_AMOUNT 10000U
#define VGA_PALETTE_BYTE_COUNT 768U
#define VGA_PALETTE_COLOR_COUNT 256U
#define MOUSE_SPRITE_TRANSPARENT_COLOR 15U
#define MOUSE_LEFT_BUTTON 1U
#define MOUSE_RIGHT_BUTTON 2U
#define MOUSE_BUTTON_MASK 3U
#define MCGA_SCREEN_WIDTH 320
#define MCGA_SCREEN_HEIGHT 200
#define MCGA_SCREEN_CENTER_X 160
#define MCGA_SCREEN_CENTER_Y 100
#define MOUSE_SCREEN_INSET 15
#define MOUSE_TRACK_POSITION_SCALE 4
#define CUSTOM_CAMERA_CHASE_RIGHT_STEPS 3
#define CUSTOM_CAMERA_CHASE_DOWN_STEPS 2
#define CUSTOM_CAMERA_CHASE_ZOOM_IN_STEPS 2
#define CUSTOM_CAMERA_CHASE_ZOOM_OUT_STEPS 3

#define CUSTOM_CAMERA_CHASE_AZIMUTH                                                                \
	(CUSTOM_CAMERA_INITIAL_AZIMUTH_ANGLE + CUSTOM_CAMERA_CHASE_RIGHT_STEPS * CAMERA_ANGLE_STEP)
#define CUSTOM_CAMERA_CHASE_ELEVATION                                                              \
	(CUSTOM_CAMERA_INITIAL_ELEVATION_ANGLE - CUSTOM_CAMERA_CHASE_DOWN_STEPS * CAMERA_ANGLE_STEP)

enum CUSTOM_CAMERA_PRESET {
	CUSTOM_CAMERA_PRESET_ORIGINAL,
	CUSTOM_CAMERA_PRESET_CLOSE,
	CUSTOM_CAMERA_PRESET_NORMAL,
	CUSTOM_CAMERA_PRESET_FAR,
	CUSTOM_CAMERA_PRESET_COUNT
};

static legacy_u8 custom_camera_preset = CUSTOM_CAMERA_PRESET_ORIGINAL;
static struct CUSTOM_CAMERA custom_camera_original;
static legacy_s8 custom_camera_original_dashboard;

static const legacy_u8 far input_direction_table[INPUT_DIRECTION_COUNT] = {0, 1, 5, 0, 3, 2, 4, 3,
																		   7, 8, 6, 7, 0, 1, 5, 0};
static legacy_u8 input_callback_flags[INPUT_ASCII_KEY_COUNT];
static legacy_u8 input_extended_callback_flags[INPUT_EXTENDED_KEY_COUNT];
static void(far *input_callbacks[INPUT_CALLBACK_COUNT])(void);
static legacy_u8 input_callback_dispatching;
static readchar_callback_type input_readchar_callback = kb_read_char;
struct SPRITE far *mouse_small_sprite;
struct SPRITE far *mouse_medium_sprite;
struct SPRITE far *mouse_background_sprite;
legacy_s8 mouse_background_dirty;
static legacy_s8 mouse_transparent_mode;
static legacy_u8 h_key_toggle;
legacy_u8 supersight_enabled;
legacy_u8 fps_display_enabled;
static legacy_s16 input_elapsed_frames;

/* A control that has not changed still fires again once the configured repeat
 * delay has elapsed. */
static legacy_s16 input_repeat_due(legacy_s16 repeat_at)
{
	return LEGACY_S16_FROM_BITS(LEGACY_U16_WRAP_ADD(repeat_at, INPUT_REPEAT_DELAY_FRAMES)) <
		   LEGACY_S16_FROM_BITS(input_elapsed_frames);
}
static legacy_s16 input_mouse_repeat_at;
static legacy_s16 input_joystick_repeat_at;
static legacy_s16 input_mouse_idle_frames;
legacy_s16 input_combined_flags;
static legacy_s16 input_joystick_flags;
static legacy_s16 input_new_joystick_flags;
static legacy_s16 input_joystick_keycode;
static legacy_s16 input_mouse_previous_x;
static legacy_s16 input_mouse_previous_y;
static legacy_s16 input_mouse_previous_buttons;
static legacy_s16 input_mouse_keycode;
static const legacy_u8 input_key_scancodes[INPUT_KEY_COUNT] = {57, 28, 71, 72, 73,
															   77, 81, 80, 79, 75};
static legacy_u8 input_mode_stack_depth;
static legacy_s8 input_draw_mode_stack[INPUT_MODE_STACK_LIMIT];
static legacy_s8 input_device_mode_stack[INPUT_MODE_STACK_LIMIT];

void kb_reg_callback(legacy_s16 code, void(far *callback)(void))
{
	legacy_u16 callback_index;
	for (callback_index = 0; callback_index < INPUT_CALLBACK_COUNT; callback_index++) {
		if (input_callbacks[callback_index] == callback) {
			break;
		}
		if (input_callbacks[callback_index] == 0) {
			input_callbacks[callback_index] = callback;
			break;
		}
	}
	if (callback_index == INPUT_CALLBACK_COUNT) {
		return;
	}

	legacy_u16 code_bits = (legacy_u16)code;
	if ((code_bits & INPUT_ASCII_BYTE_MASK) != 0) {
		if (code_bits <= INPUT_ASCII_INDEX_MASK) {
			input_callback_flags[code_bits] = (legacy_u8)(callback_index + 1U);
		}
		return;
	}
	legacy_u16 key_index = (legacy_u16)(code_bits >> LEGACY_BYTE_BITS);
	if (key_index <= INPUT_EXTENDED_KEY_MAX_INDEX) {
		input_extended_callback_flags[key_index] = (legacy_u8)(callback_index + 1U);
	}
}

legacy_s16 kb_parse_key(legacy_s16 code)
{
	legacy_u16 code_bits = (legacy_u16)code;
	dos_interrupts_disable();
	if (input_callback_dispatching != 0) {
		dos_interrupts_enable();
		return LEGACY_S16_FROM_BITS(code_bits);
	}
	input_callback_dispatching = 1;
	dos_interrupts_enable();

	legacy_u8 callback_number;
	legacy_u16 key_index;
	if ((code_bits & INPUT_ASCII_BYTE_MASK) != 0) {
		key_index = code_bits & INPUT_ASCII_INDEX_MASK;
		callback_number = input_callback_flags[key_index];
		code_bits = key_index;
	} else {
		key_index = code_bits >> LEGACY_BYTE_BITS;
		if (key_index >= INPUT_EXTENDED_KEY_MAX_INDEX) {
			key_index = INPUT_EXTENDED_KEY_MAX_INDEX;
		}
		callback_number = input_extended_callback_flags[key_index];
	}

	if (callback_number != 0) {
		input_callbacks[(legacy_u16)callback_number - 1U]();
		code_bits = 0;
	}
	input_callback_dispatching = 0;
	return LEGACY_S16_FROM_BITS(code_bits);
}

void kb_remove_callback(legacy_s16 code)
{
	legacy_u16 code_bits = (legacy_u16)code;
	if ((code_bits & INPUT_ASCII_BYTE_MASK) != 0) {
		if (code_bits <= INPUT_ASCII_INDEX_MASK) {
			input_callback_flags[code_bits] = 0;
		}
		return;
	}
	legacy_u16 key_index = (legacy_u16)(code_bits >> LEGACY_BYTE_BITS);
	if (key_index <= INPUT_EXTENDED_KEY_MAX_INDEX) {
		input_extended_callback_flags[key_index] = 0;
	}
}

void kb_set_readchar_callback(readchar_callback_type callback)
{
	input_readchar_callback = callback;
}

readchar_callback_type kb_get_readchar_callback(void)
{
	return input_readchar_callback;
}

void joystick_reset_calibration(void)
{
	dos_joystick_reset_calibration();
}

legacy_s16 input_direction_from_flags(legacy_s16 index)
{
	return input_direction_table[(legacy_u16)index & INPUT_DRIVING_MASK];
}

legacy_s16 joystick_get_scaled_x(void)
{
	return dos_joystick_get_scaled_axis(0U);
}

legacy_s16 joystick_get_scaled_y(void)
{
	return dos_joystick_get_scaled_axis(1U);
}

void load_palandcursor(void)
{
	legacy_s8 far *resource = (legacy_s8 far *)file_load_shape2d_fatal("sdmain");
	struct SHAPE2D far *mouse_shape = (struct SHAPE2D far *)locate_shape_fatal(resource, "!pal");
	legacy_u8 palette[VGA_PALETTE_BYTE_COUNT];
	for (legacy_u16 i = 0; i < sizeof(palette); ++i) {
		palette[i] = ((legacy_u8 far *)mouse_shape)[SHAPE2D_HEADER_SIZE + i];
	}
	dos_video_set_palette(0, VGA_PALETTE_COLOR_COUNT, palette);
#ifdef RESTUNTS_SDL3
	skybox_hires_set_palette(palette);
#endif

	mouse_shape = (struct SHAPE2D far *)locate_shape_fatal(resource, "smou");
	legacy_u16 mouse_width = (legacy_u16)(shape2d_get_width(mouse_shape) * video_x_alignment);
	legacy_u16 mouse_height = shape2d_get_height(mouse_shape);
	mmgr_free(resource);

	mouse_small_sprite = sprite_make_wnd(mouse_width, mouse_height, MOUSE_SPRITE_TRANSPARENT_COLOR);
	mouse_medium_sprite =
		sprite_make_wnd(mouse_width, mouse_height, MOUSE_SPRITE_TRANSPARENT_COLOR);
	mouse_background_sprite = sprite_make_wnd(mouse_width + video_x_alignment, mouse_height,
											  MOUSE_SPRITE_TRANSPARENT_COLOR);

	resource = (legacy_s8 far *)file_load_shape2d_fatal("sdmain");
	sprite_select_target(mouse_small_sprite);
	mouse_shape = (struct SHAPE2D far *)locate_shape_fatal(resource, "smou");
	sprite_shape_to_1(mouse_shape, 0, 0);

	sprite_select_target(mouse_medium_sprite);
	mouse_shape = (struct SHAPE2D far *)locate_shape_fatal(resource, "mmou");
	sprite_shape_to_1(mouse_shape, 0, 0);

	mmgr_free(resource);
	sprite_select_screen_compat();
}

#ifdef RESTUNTS_SDL3
static void input_select_supersight(legacy_s16 key)
{
	static const legacy_s8 *preset_names[] = {"Auto", "Full", "High", "Medium", "Low"};
	legacy_u8 was_enabled = supersight_enabled;
	if (key == KEY_SHIFT_F12) {
		legacy_u16 preset =
			supersight_enabled != 0 ? frame_adaptive.preset : FRAME_ADAPTIVE_PRESET_AUTO;
		preset = preset >= FRAME_ADAPTIVE_PRESET_LOW ? FRAME_ADAPTIVE_PRESET_FULL : preset + 1U;
		frame_adaptive_set_preset(&frame_adaptive, (enum FRAME_ADAPTIVE_PRESET)preset);
		supersight_enabled = 1;
	} else {
		supersight_enabled ^= 1U;
		frame_adaptive_set_preset(&frame_adaptive, FRAME_ADAPTIVE_PRESET_AUTO);
	}
	if (was_enabled != supersight_enabled) {
		frame_render_timing_reset();
		hires_set_enabled(supersight_enabled);
	}
	if (supersight_enabled != 0) {
		hires_set_render_scale(frame_adaptive_render_scale(&frame_adaptive));
	}
	frame_supersight_reset();
	frame_supersight_show_status(supersight_enabled != 0 ? preset_names[frame_adaptive.preset]
														 : (const legacy_s8 *)"Off");
	full_redraw_frames_remaining = (legacy_s8)video_page_count;
}
#endif

static legacy_s16 input_handle_display_shortcut(legacy_s16 key)
{
	switch (key) {
		case KEY_F11:
			fps_display_enabled = fps_display_enabled == FRAME_FPS_DISPLAY_OFF
									  ? FRAME_FPS_DISPLAY_ON
									  : FRAME_FPS_DISPLAY_OFF;
			frame_fps_reset();
			full_redraw_frames_remaining = (legacy_s8)video_page_count;
			return 1;

#ifdef RESTUNTS_SDL3
		case KEY_SHIFT_F12:
		case KEY_F12:
			input_select_supersight(key);
			return 1;
#else
		case KEY_F12:
			supersight_enabled ^= 1U;
			frame_supersight_reset();
			frame_supersight_show_status(supersight_enabled != 0 ? "On" : "Off");
			full_redraw_frames_remaining = (legacy_s8)video_page_count;
			return 1;
#endif

		case 'D':
		case 'd':
			dashb_toggle ^= 1;
			return 1;

		case 'H':
		case 'h':
			h_key_toggle ^= 1;
			return 1;

		case 'M':
		case 'm':
			select_mouse_driving();
			mouse_minmax_position(LEGACY_S8_FROM_BITS(mouse_driving_enabled));
			return 1;

		case 'R':
		case 'r':
			replaybar_toggle ^= 1;
			return 1;
	}
	return 0;
}

static void input_restore_custom_camera(void)
{
	if (custom_camera_preset != CUSTOM_CAMERA_PRESET_ORIGINAL) {
		custom_camera = custom_camera_original;
		dashb_toggle = custom_camera_original_dashboard;
		custom_camera_preset = CUSTOM_CAMERA_PRESET_ORIGINAL;
	}
}

void camera_select_mode(legacy_s8 mode)
{
	if (mode != cameramode) {
		input_restore_custom_camera();
	}
	cameramode = mode;
}

static void input_cycle_custom_camera(void)
{
	static const legacy_s16 distances[CUSTOM_CAMERA_PRESET_COUNT] = {
		CUSTOM_CAMERA_INITIAL_DISTANCE,
		CUSTOM_CAMERA_INITIAL_DISTANCE - CUSTOM_CAMERA_CHASE_ZOOM_IN_STEPS * CAMERA_ZOOM_STEP,
		CUSTOM_CAMERA_INITIAL_DISTANCE,
		CUSTOM_CAMERA_INITIAL_DISTANCE + CUSTOM_CAMERA_CHASE_ZOOM_OUT_STEPS * CAMERA_ZOOM_STEP};

	if (cameramode != CAMERA_MODE_CUSTOM) {
		camera_select_mode(CAMERA_MODE_CUSTOM);
		return;
	}
	if (custom_camera_preset == CUSTOM_CAMERA_PRESET_FAR) {
		input_restore_custom_camera();
		return;
	}
	if (custom_camera_preset == CUSTOM_CAMERA_PRESET_ORIGINAL) {
		custom_camera_original = custom_camera;
		custom_camera_original_dashboard = dashb_toggle;
	}
	custom_camera_preset++;
	custom_camera.distance = distances[custom_camera_preset];
	custom_camera.elevation_angle = CUSTOM_CAMERA_CHASE_ELEVATION;
	custom_camera.azimuth_angle = CUSTOM_CAMERA_CHASE_AZIMUTH;
	dashb_toggle = 0;
}

static legacy_s16 input_handle_camera_shortcut(legacy_s16 key)
{
	switch (key) {
		case 'C':
		case 'c':
			if (game_replay_mode != REPLAY_MODE_PAUSED) {
				camera_select_mode((legacy_s8)(((legacy_u8)cameramode + 1U) & CAMERA_MODE_MASK));
			}
			return 1;

		case KEY_F1:
			camera_select_mode(CAMERA_MODE_COCKPIT);
			return 1;
		case KEY_F2:
			camera_select_mode(CAMERA_MODE_FOLLOW);
			return 1;
		case KEY_F3:
			input_cycle_custom_camera();
			return 1;
		case KEY_F4:
			camera_select_mode(CAMERA_MODE_TRACKSIDE);
			return 1;
	}
	return 0;
}

#ifdef RESTUNTS_SDL3
legacy_s16 input_update_controller_camera(void)
{
	legacy_u16 flags = (legacy_u16)sdl3_controller_camera_flags();
	if (cameramode != CAMERA_MODE_CUSTOM) {
		return 0;
	}
	legacy_s16 changed = 0;
	if ((flags & INPUT_STEER_RIGHT_FLAG) != 0) {
		changed |= camera_adjust_custom_direction(KEY_RIGHT);
	} else if ((flags & INPUT_STEER_LEFT_FLAG) != 0) {
		changed |= camera_adjust_custom_direction(KEY_LEFT);
	}
	if ((flags & INPUT_ACCELERATE_FLAG) != 0) {
		changed |= camera_adjust_custom_direction(KEY_UP);
	} else if ((flags & INPUT_BRAKE_FLAG) != 0) {
		changed |= camera_adjust_custom_direction(KEY_DOWN);
	}
	return changed;
}
#endif

legacy_s16 handle_ingame_kb_shortcuts(legacy_s16 key)
{
	if (input_handle_display_shortcut(key) || input_handle_camera_shortcut(key)) {
		return 1;
	}
	switch (key) {
		case KEY_ESCAPE:
			if (game_replay_mode == REPLAY_MODE_LIVE) {
				update_crash_state(CRASH_EVENT_EXIT, PLAYER_CAR_INDEX);
			}
			race_exit_request = 1;
			return 1;

		case 't':
			if (followOpponentFlag != 0 || gameconfig.game_opponenttype != 0 ||
				ghost_car_state() != 0) {
				followOpponentFlag ^= 1;
			}
			return 1;
	}

	if (game_replay_mode != REPLAY_MODE_PAUSED) {
		return 0;
	}

	game_replay_mode = REPLAY_MODE_LIVE;
	race_start_sequence_state = RACE_START_SEQUENCE_INACTIVE;
	init_game_state_with_frame_rate_byte(configured_frame_rate);
	return 1;
}

void mouse_draw_transparent_check(void)
{
	mouse_transparent_mode = 1;
	if (kbormouse != 0 && mouse_background_dirty == 0) {
		mouse_draw_transparent();
	}
}

void mouse_draw_opaque_check(void)
{
	mouse_transparent_mode = 0;
	if (mouse_background_dirty != 0) {
		mouse_draw_opaque();
	}
}

legacy_s16 mouse_multi_hittest(legacy_s16 count, const struct BUTTON_AREA *buttons)
{
	if (kbormouse == 0) {
		return -1;
	}

	for (legacy_s16 i = 0; i < count; i++) {
		if (buttons[i].x1 <= mouse_xpos && mouse_xpos <= buttons[i].x2 &&
			buttons[i].y1 <= mouse_ypos && mouse_ypos <= buttons[i].y2) {
			return (legacy_s8)i;
		}
	}

	return -1;
}

legacy_s16 get_kb_or_joy_flags(void)
{
	static const legacy_u8 action_flags[INPUT_KEY_COUNT] = {
		INPUT_PRIMARY_ACTION_FLAG,
		INPUT_SECONDARY_ACTION_FLAG,
		INPUT_ACCELERATE_FLAG | INPUT_STEER_LEFT_FLAG,
		INPUT_ACCELERATE_FLAG,
		INPUT_ACCELERATE_FLAG | INPUT_STEER_RIGHT_FLAG,
		INPUT_STEER_RIGHT_FLAG,
		INPUT_BRAKE_FLAG | INPUT_STEER_RIGHT_FLAG,
		INPUT_BRAKE_FLAG,
		INPUT_BRAKE_FLAG | INPUT_STEER_LEFT_FLAG,
		INPUT_STEER_LEFT_FLAG};

	legacy_u16 flags = 0;
	for (legacy_u16 index = 0; index < INPUT_KEY_COUNT; index++) {
		if (kb_get_key_state(input_key_scancodes[index]) != 0) {
			flags |= action_flags[index];
		}
	}
#ifdef RESTUNTS_SDL3
	flags |= (legacy_u16)dos_get_joy_flags() & INPUT_NON_STEERING_MASK;
	if (sdl3_input_driving_mode() != SDL3_DRIVING_KEYBOARD) {
		flags &= INPUT_NON_STEERING_MASK;
	}
#else
	if (flags == 0) {
		flags = (legacy_u16)dos_get_joy_flags();
	}
#endif
	return LEGACY_S16_FROM_BITS(flags);
}

static void input_advance_clock(legacy_s16 frame_delta)
{
	input_elapsed_frames = LEGACY_U16_WRAP_ADD(input_elapsed_frames, frame_delta);
	if (LEGACY_S16_FROM_BITS(input_elapsed_frames) > INPUT_COUNTER_WRAP_LIMIT) {
		input_elapsed_frames = LEGACY_U16_WRAP_SUB(input_elapsed_frames, INPUT_COUNTER_WRAP_AMOUNT);
		input_mouse_repeat_at =
			LEGACY_U16_WRAP_SUB(input_mouse_repeat_at, INPUT_COUNTER_WRAP_AMOUNT);
		input_joystick_repeat_at =
			LEGACY_U16_WRAP_SUB(input_joystick_repeat_at, INPUT_COUNTER_WRAP_AMOUNT);
	}
}

static void input_translate_joystick_key(void)
{
	if (((legacy_u16)input_new_joystick_flags & INPUT_SECONDARY_ACTION_FLAG) != 0) {
		input_joystick_keycode = KEY_ENTER;
	} else if (((legacy_u16)input_new_joystick_flags & INPUT_PRIMARY_ACTION_FLAG) != 0) {
		input_joystick_keycode = KEY_SPACE;
	} else if (((legacy_u16)input_new_joystick_flags & INPUT_ACCELERATE_FLAG) != 0) {
		input_joystick_keycode = KEY_UP;
	} else if (((legacy_u16)input_new_joystick_flags & INPUT_BRAKE_FLAG) != 0) {
		input_joystick_keycode = KEY_DOWN;
	} else if (((legacy_u16)input_new_joystick_flags & INPUT_STEER_LEFT_FLAG) != 0) {
		input_joystick_keycode = KEY_LEFT;
	} else if (((legacy_u16)input_new_joystick_flags & INPUT_STEER_RIGHT_FLAG) != 0) {
		input_joystick_keycode = KEY_RIGHT;
	}
}

static void input_update_joystick(legacy_u16 current_joy_flags)
{
	legacy_s16 changed_or_repeating = 0;
	if ((legacy_u16)input_joystick_flags != current_joy_flags) {
		input_new_joystick_flags =
			((legacy_u16)input_joystick_flags ^ current_joy_flags) & current_joy_flags;
		input_joystick_flags = current_joy_flags;
		changed_or_repeating = 1;
	} else if (current_joy_flags != 0 && input_repeat_due(input_joystick_repeat_at)) {
		changed_or_repeating = 1;
	}

	if (changed_or_repeating) {
		input_translate_joystick_key();

		if (input_joystick_keycode != 0) {
			input_joystick_repeat_at = input_elapsed_frames;
			kbormouse = 0;
		}
	}
}

static void input_update_mouse_activity(legacy_s16 frame_delta)
{
	dos_mouse_get_state(&mouse_butstate, &mouse_xpos, &mouse_ypos);
	if (input_mouse_previous_x != mouse_xpos || input_mouse_previous_y != mouse_ypos ||
		input_mouse_previous_buttons != mouse_butstate) {
		input_mouse_previous_x = mouse_xpos;
		input_mouse_previous_y = mouse_ypos;
		kbormouse = 1;
		input_mouse_idle_frames = 0;
		if (mouse_transparent_mode != 0) {
			if (mouse_background_dirty != 0) {
				mouse_draw_opaque();
			}
			mouse_draw_transparent();
		}
	} else if (kbormouse != 0) {
		input_mouse_idle_frames = LEGACY_U16_WRAP_ADD(input_mouse_idle_frames, frame_delta);
		if (LEGACY_S16_FROM_BITS(input_mouse_idle_frames) > INPUT_IDLE_LIMIT_FRAMES) {
			input_mouse_idle_frames = 0;
			kbormouse = 0;
			if (mouse_background_dirty != 0) {
				mouse_draw_opaque();
			}
		}
	}
}

static void input_update_mouse_buttons(void)
{
	if (kbormouse != 0) {
		legacy_s16 changed_or_repeating = 0;
		if (input_mouse_previous_buttons != mouse_butstate) {
			input_mouse_previous_buttons = mouse_butstate;
			changed_or_repeating = 1;
		} else if (mouse_butstate != 0 && input_repeat_due(input_mouse_repeat_at)) {
			changed_or_repeating = 1;
		}

		if (changed_or_repeating) {
			if (((legacy_u16)mouse_butstate & MOUSE_LEFT_BUTTON) != 0) {
				input_mouse_keycode = KEY_SPACE;
			} else if (((legacy_u16)mouse_butstate & MOUSE_RIGHT_BUTTON) != 0) {
				input_mouse_keycode = KEY_ENTER;
			}
			if (input_mouse_keycode != 0) {
				input_mouse_repeat_at = input_elapsed_frames;
			}
			input_mouse_idle_frames = 0;
		}

		if (mouse_butstate != 0) {
			if (((legacy_u16)mouse_butstate & MOUSE_LEFT_BUTTON) != 0) {
				input_combined_flags =
					(legacy_u16)input_combined_flags | INPUT_SECONDARY_ACTION_FLAG;
			} else if (((legacy_u16)mouse_butstate & MOUSE_RIGHT_BUTTON) != 0) {
				input_combined_flags = (legacy_u16)input_combined_flags | INPUT_PRIMARY_ACTION_FLAG;
			}
		}
	}
}

legacy_s16 input_checking(legacy_s16 frame_delta)
{
	input_advance_clock(frame_delta);
	legacy_u16 key = (legacy_u16)dos_kb_get_char();
	if (key != 0) {
		kbormouse = 0;
	}
#ifdef RESTUNTS_SDL3
	legacy_u16 current_joy_flags = (legacy_u16)sdl3_joystick_menu_flags();
#else
	legacy_u16 current_joy_flags = (legacy_u16)dos_get_joy_flags();
#endif
	input_combined_flags = get_kb_or_joy_flags();
#ifdef RESTUNTS_SDL3
	input_combined_flags = (legacy_u16)input_combined_flags | current_joy_flags;
#endif
	input_update_joystick(current_joy_flags);
	input_update_mouse_activity(frame_delta);
	input_update_mouse_buttons();
	if (key != 0) {
		return key;
	}
	if (input_joystick_keycode != 0) {
		key = (legacy_u16)input_joystick_keycode;
		input_joystick_keycode = 0;
		return key;
	}
	if (input_mouse_keycode != 0) {
		key = (legacy_u16)input_mouse_keycode;
		input_mouse_keycode = 0;
		return key;
	}
	return 0;
}

static legacy_s16 mouse_track_divide(legacy_s16 numerator, legacy_s16 denominator)
{
	return LEGACY_S16_FROM_BITS(
		(legacy_u16)LEGACY_S32_DIV_OR_ZERO((legacy_s32)numerator, (legacy_s32)denominator));
}

static legacy_s16 mouse_track_position(legacy_s16 length, legacy_s16 selected,
									   legacy_s16 item_count)
{
	legacy_s16 numerator = LEGACY_S16_WRAP_MUL(LEGACY_S16_WRAP_SUB(length, 1), selected);
	numerator = LEGACY_S16_WRAP_MUL(numerator, MOUSE_TRACK_POSITION_SCALE);
	legacy_s16 denominator = LEGACY_S16_WRAP_MUL(item_count, MOUSE_TRACK_POSITION_SCALE);
	return mouse_track_divide(numerator, denominator);
}

static legacy_s16 mouse_track_thumb_size(legacy_s16 length, legacy_s16 selected,
										 legacy_s16 selection_width, legacy_s16 item_count,
										 legacy_s16 *thumb_start, legacy_s16 *thumb_end)
{
	*thumb_start = mouse_track_position(length, selected, item_count);
	*thumb_end =
		mouse_track_position(length, LEGACY_S16_WRAP_ADD(selected, selection_width), item_count);
	return LEGACY_S16_WRAP_SUB(*thumb_end, *thumb_start);
}

static void mouse_track_draw(legacy_s16 horizontal, legacy_s16 x, legacy_s16 width, legacy_s16 y,
							 legacy_s16 height, legacy_s16 thumb_start, legacy_s16 thumb_size)
{
	sprite_fill_rect(x, y, width, height, 0);
	if (horizontal) {
		sprite_fill_rect(LEGACY_S16_WRAP_ADD(x, thumb_start), y, thumb_size, height,
						 dialog_fnt_colour);
	} else {
		sprite_fill_rect(x, LEGACY_S16_WRAP_ADD(y, thumb_start), width, thumb_size,
						 dialog_fnt_colour);
	}
}

struct MOUSE_TRACK_DRAG {
	legacy_s16 x;
	legacy_s16 width;
	legacy_s16 y;
	legacy_s16 height;
	legacy_s16 horizontal;
	legacy_s16 length;
	legacy_s16 thumb_start;
	legacy_s16 thumb_size;
};

static legacy_s16 mouse_track_page(legacy_s16 coordinate, legacy_s16 thumb_start,
								   legacy_s16 selected, legacy_s16 item_count)
{
	do {
		input_checking((legacy_s16)timer_get_delta_alt());
	} while (((legacy_u16)mouse_butstate & MOUSE_BUTTON_MASK) != 0);
	if (coordinate < thumb_start) {
		if (selected != 0) {
			selected = LEGACY_S16_WRAP_SUB(selected, 1);
		}
	} else if (LEGACY_S16_FROM_BITS(selected) < LEGACY_S16_WRAP_SUB(item_count, 1)) {
		selected = LEGACY_S16_WRAP_ADD(selected, 1);
	}
	return selected;
}

static legacy_s16 mouse_track_drag(const struct MOUSE_TRACK_DRAG *track, legacy_s16 coordinate)
{
	legacy_s16 previous_start = track->thumb_start;
	legacy_s16 dragged_start;
	do {
		input_checking((legacy_s16)timer_get_delta_alt());
		legacy_s16 current_coordinate = track->horizontal
											? LEGACY_S16_WRAP_SUB(mouse_xpos, track->x)
											: LEGACY_S16_WRAP_SUB(mouse_ypos, track->y);
		dragged_start = LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_SUB(current_coordinate, coordinate),
											track->thumb_start);
		if (dragged_start < 0) {
			dragged_start = 0;
		} else if (LEGACY_S16_WRAP_ADD(dragged_start, track->thumb_size) >
				   LEGACY_S16_WRAP_SUB(track->length, 1)) {
			dragged_start =
				LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_SUB(track->length, track->thumb_size), 1);
		}

		if (dragged_start != previous_start) {
			previous_start = dragged_start;
			mouse_draw_opaque_check();
			mouse_track_draw(track->horizontal, track->x, track->width, track->y, track->height,
							 dragged_start, track->thumb_size);
			mouse_draw_transparent_check();
		}
	} while (((legacy_u16)mouse_butstate & MOUSE_BUTTON_MASK) != 0);
	return dragged_start;
}

legacy_s16 scrollbar_update(legacy_s16 operation, legacy_s16 x, legacy_s16 width, legacy_s16 y,
							legacy_s16 height, legacy_s16 selected, legacy_s16 selection_width,
							legacy_s16 item_count)
{
	legacy_s16 horizontal = LEGACY_S16_FROM_BITS(width) > LEGACY_S16_FROM_BITS(height);
	legacy_s16 length = horizontal ? (legacy_s16)width : (legacy_s16)height;
	legacy_s16 thumb_end;
	legacy_s16 thumb_start;
	legacy_s16 thumb_size = mouse_track_thumb_size(length, selected, selection_width, item_count,
												   &thumb_start, &thumb_end);

	if (operation == 0) {
		mouse_track_draw(horizontal, x, width, y, height, thumb_start, thumb_size);
		return selected;
	}
	if (operation != 1) {
		return selected;
	}

	legacy_s16 coordinate =
		horizontal ? LEGACY_S16_WRAP_SUB(mouse_xpos, x) : LEGACY_S16_WRAP_SUB(mouse_ypos, y);
	if (coordinate < thumb_start || coordinate > thumb_end) {
		selected = mouse_track_page(coordinate, thumb_start, selected, item_count);
	} else {
		struct MOUSE_TRACK_DRAG track;
		track.x = x;
		track.width = width;
		track.y = y;
		track.height = height;
		track.horizontal = horizontal;
		track.length = length;
		track.thumb_start = thumb_start;
		track.thumb_size = thumb_size;
		legacy_s16 dragged_start = mouse_track_drag(&track, coordinate);
		legacy_s16 quotient = mouse_track_divide(length, (legacy_s16)item_count);
		quotient = LEGACY_S16_SAR(quotient, 1U);
		legacy_s16 scaled =
			LEGACY_S16_WRAP_MUL(LEGACY_S16_WRAP_ADD(dragged_start, quotient), item_count);
		selected = mouse_track_divide(scaled, length);
	}

	thumb_size = mouse_track_thumb_size(length, selected, selection_width, item_count, &thumb_start,
										&thumb_end);
	mouse_draw_opaque_check();
	mouse_track_draw(horizontal, x, width, y, height, thumb_start, thumb_size);
	mouse_draw_transparent_check();
	return selected;
}

legacy_s16 input_do_checking(legacy_s16 frame_delta)
{
	return input_checking(frame_delta);
}

void check_input(void)
{
	legacy_s16 pressed;

	do {
		pressed = (get_kb_or_joy_flags() & INPUT_ACTION_BUTTON_MASK) != 0;
#ifdef RESTUNTS_SDL3
		pressed |= (sdl3_joystick_menu_flags() & INPUT_ACTION_BUTTON_MASK) != 0;
#endif
		if (!pressed) {
			pressed = input_checking((legacy_s16)timer_get_delta_alt()) != 0;
		}
		if (!pressed && kbormouse != 0 && (mouse_butstate & MOUSE_BUTTON_MASK) != 0) {
			pressed = 1;
		}
	} while (pressed);
}

void input_wait_for_press_and_release(void)
{
	do {
		/* Keep advancing input state until an event is reported. */
	} while (input_checking((legacy_s16)timer_get_delta_alt()) == 0);

	check_input();
}

void input_push_status(void)
{
	legacy_s16 index = (legacy_s8)input_mode_stack_depth;

	input_draw_mode_stack[index] = mouse_transparent_mode;
	input_device_mode_stack[index] = kbormouse;
	input_mode_stack_depth++;
}

void input_pop_status(void)
{
	if (input_mode_stack_depth == 0) {
		return;
	}

	input_mode_stack_depth--;
	legacy_s16 index = (legacy_s8)input_mode_stack_depth;
	mouse_transparent_mode = input_draw_mode_stack[index];
	kbormouse = input_device_mode_stack[index];
	if (kbormouse == 0) {
		mouse_draw_opaque_check();
	}
}

legacy_s16 input_repeat_check(legacy_s16 duration)
{
	timer_get_delta_alt();
	legacy_u16 elapsed = 0;
	while (LEGACY_S16_FROM_BITS((legacy_u16)duration) > LEGACY_S16_FROM_BITS(elapsed)) {
		legacy_u16 delta = (legacy_u16)timer_get_delta_alt();
		elapsed = LEGACY_U16_WRAP_ADD(elapsed, delta);
		legacy_s16 result = input_do_checking(LEGACY_S16_FROM_BITS(delta));
		if (result != 0) {
			return result;
		}
	}
	return 0;
}

void mouse_minmax_position(legacy_s16 inset)
{
	if (inset != 0) {
		dos_mouse_set_minmax(MOUSE_SCREEN_INSET, 0, MCGA_SCREEN_WIDTH - MOUSE_SCREEN_INSET,
							 MCGA_SCREEN_HEIGHT);
		dos_mouse_set_position(MCGA_SCREEN_CENTER_X, MCGA_SCREEN_CENTER_Y);
	} else {
		dos_mouse_set_minmax(0, 0, MCGA_SCREEN_WIDTH, MCGA_SCREEN_HEIGHT);
	}
}
