#ifndef RESTUNTS_SDL3_CONTROLLER_GAME_H
#define RESTUNTS_SDL3_CONTROLLER_GAME_H

#include "../../c/legacy.h"

/* Racing steering uses one source; menus always accept every available device. */
enum SDL3_DRIVING_INPUT {
	SDL3_DRIVING_KEYBOARD,
	SDL3_DRIVING_JOYSTICK,
	SDL3_DRIVING_MOUSE,
	SDL3_DRIVING_CONTROLLER,
	SDL3_DRIVING_TILT
};

void sdl3_input_set_driving_mode(enum SDL3_DRIVING_INPUT mode);
enum SDL3_DRIVING_INPUT sdl3_input_driving_mode(void);
void sdl3_input_set_gameplay_active(legacy_u8 active);
void sdl3_input_set_replay_active(legacy_u8 active);
void sdl3_input_push_modal(void);
void sdl3_input_pop_modal(void);
legacy_s16 sdl3_joystick_menu_flags(void);
legacy_s16 sdl3_controller_driving_flags(void);
legacy_s16 sdl3_controller_scaled_x(void);
/* Safe from the Android sensor thread; axis is negative left, positive right. */
void sdl3_input_set_tilt(legacy_s16 axis, legacy_u8 enabled);
legacy_s16 sdl3_tilt_scaled_x(void);
/* Direction flags repeat at a fixed interval, independently of polling frequency. */
legacy_s16 sdl3_controller_camera_flags(void);
legacy_u8 sdl3_controller_take_menu_request(void);
/* Preserve a quick right-stick movement until the truck start wait polls it. */
legacy_u8 sdl3_controller_take_race_start_request(void);

#ifndef __ANDROID__
legacy_u8 sdl3_controller_vibration_enabled(void);
void sdl3_controller_set_vibration_enabled(legacy_u8 enabled);
#endif

#endif
