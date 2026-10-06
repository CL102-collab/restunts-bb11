#ifndef RESTUNTS_SDL3_TOUCH_H
#define RESTUNTS_SDL3_TOUCH_H

#include "sdl3.h"
#include "touch_game.h"

enum TOUCH_CONTROL {
	TOUCH_ESCAPE,
	TOUCH_LEFT,
	TOUCH_RIGHT,
	TOUCH_SHIFT_UP,
	TOUCH_SHIFT_DOWN,
	TOUCH_REWIND,
	TOUCH_ACCELERATE,
	TOUCH_BRAKE,
	TOUCH_CAMERA,
	TOUCH_FOLLOW,
	TOUCH_CONTROL_COUNT
};

struct TOUCH_CIRCLE {
	legacy_f32 x;
	legacy_f32 y;
	legacy_f32 radius;
};

void sdl3_touch_enable(legacy_u8 enabled);
void sdl3_touch_configure(legacy_u8 keyboard, legacy_u8 racing, legacy_u8 manual, legacy_u8 replay);
void sdl3_touch_sync_game(void);
void sdl3_touch_reset(void);
void sdl3_touch_event(const SDL_TouchFingerEvent *event, const SDL_Rect *area);
void sdl3_touch_update(void);
void sdl3_touch_draw(SDL_Renderer *renderer, SDL_Window *window);
legacy_u8 sdl3_touch_visible(enum TOUCH_CONTROL control);
struct TOUCH_CIRCLE sdl3_touch_circle(enum TOUCH_CONTROL control, const SDL_Rect *area);
legacy_u8 sdl3_touch_key_state(SDL_Scancode scancode);

/* Deliver touch through the same queues as physical keyboard/mouse input. */
void sdl3_input_queue_key(legacy_u16 value);
/* Requests can originate off-thread; apply and consume shifts on the game thread. */
legacy_u8 sdl3_input_request_shift(legacy_u8 up);
void sdl3_input_shift_event(const SDL_UserEvent *event);
void sdl3_input_queue_shift(legacy_u16 flag);
void sdl3_input_reset_shifts(void);
void sdl3_input_touch_mouse(legacy_f32 x, legacy_f32 y, legacy_u8 down);

#endif
