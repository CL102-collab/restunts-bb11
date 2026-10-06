#ifndef RESTUNTS_GAME_INPUT_H
#define RESTUNTS_GAME_INPUT_H

#include "legacy.h"

#define INPUT_NONE 0U
#define INPUT_ACCELERATE_FLAG 1U
#define INPUT_BRAKE_FLAG 2U
#define INPUT_STEER_RIGHT_FLAG 4U
#define INPUT_STEER_LEFT_FLAG 8U
#define INPUT_PRIMARY_ACTION_FLAG 16U
#define INPUT_SECONDARY_ACTION_FLAG 32U
#define INPUT_SHIFT_UP_FLAG INPUT_PRIMARY_ACTION_FLAG
#define INPUT_SHIFT_DOWN_FLAG INPUT_SECONDARY_ACTION_FLAG
#define INPUT_PEDAL_MASK (INPUT_ACCELERATE_FLAG | INPUT_BRAKE_FLAG)
#define INPUT_STEERING_SHIFT 2U
#define INPUT_STEERING_MASK (INPUT_STEER_RIGHT_FLAG | INPUT_STEER_LEFT_FLAG)
#define INPUT_DRIVING_MASK (INPUT_PEDAL_MASK | INPUT_STEERING_MASK)
#define INPUT_ACTION_BUTTON_MASK (INPUT_PRIMARY_ACTION_FLAG | INPUT_SECONDARY_ACTION_FLAG)
#define INPUT_NON_STEERING_MASK (INPUT_PEDAL_MASK | INPUT_ACTION_BUTTON_MASK)

typedef legacy_s16(far *readchar_callback_type)(void);

void kb_reg_callback(legacy_s16 code, void(far *callback)(void));
legacy_s16 kb_parse_key(legacy_s16 code);
void kb_remove_callback(legacy_s16 code);
void kb_set_readchar_callback(readchar_callback_type callback);
readchar_callback_type kb_get_readchar_callback(void);
void joystick_reset_calibration(void);
legacy_s16 input_direction_from_flags(legacy_s16 index);
legacy_s16 joystick_get_scaled_x(void);
legacy_s16 joystick_get_scaled_y(void);
void load_palandcursor(void);
legacy_s16 handle_ingame_kb_shortcuts(legacy_s16 key);
#ifdef RESTUNTS_SDL3
legacy_s16 input_update_controller_camera(void);
#endif
void mouse_draw_transparent_check(void);
void mouse_draw_opaque_check(void);
/* A clickable rectangle. Menus keep one array of these per screen. */
struct BUTTON_AREA {
	legacy_s16 x1;
	legacy_s16 x2;
	legacy_s16 y1;
	legacy_s16 y2;
};

legacy_s16 mouse_multi_hittest(legacy_s16 count, const struct BUTTON_AREA *buttons);
legacy_s16 get_kb_or_joy_flags(void);
legacy_s16 input_checking(legacy_s16 frame_delta);
legacy_s16 scrollbar_update(legacy_s16 operation, legacy_s16 x, legacy_s16 width, legacy_s16 y,
							legacy_s16 height, legacy_s16 selected, legacy_s16 selection_width,
							legacy_s16 item_count);
legacy_s16 input_do_checking(legacy_s16 frame_delta);
void check_input(void);
void input_wait_for_press_and_release(void);
void input_push_status(void);
void input_pop_status(void);
legacy_s16 input_repeat_check(legacy_s16 duration);
void mouse_minmax_position(legacy_s16 inset);

extern legacy_s16 input_combined_flags;
extern legacy_u8 supersight_enabled;
/* Preserve the session byte and existing nonzero visibility checks. */
enum FRAME_FPS_DISPLAY_MODE {
	FRAME_FPS_DISPLAY_OFF,
	FRAME_FPS_DISPLAY_ON,
	FRAME_FPS_DISPLAY_MODE_COUNT
};
extern legacy_u8 fps_display_enabled;

#endif
