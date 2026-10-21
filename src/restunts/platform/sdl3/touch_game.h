#ifndef RESTUNTS_SDL3_TOUCH_GAME_H
#define RESTUNTS_SDL3_TOUCH_GAME_H

#include "../../c/legacy.h"

/* The game-side bridge deliberately has no SDL header dependency. */
void sdl3_touch_set_layout_visible(legacy_u8 visible);
void sdl3_touch_set_intro_active(legacy_u8 active);
void sdl3_touch_set_game_active(legacy_u8 active);
void sdl3_touch_set_replay_active(legacy_u8 active);
void sdl3_touch_set_rewind_active(legacy_u8 active);
legacy_u16 sdl3_touch_take_shift_flags(void);
legacy_s16 sdl3_touch_take_seek(void);

/* The web keyboard feeds the regular text editor without taking over its loop. */
void sdl3_soft_keyboard_enable(legacy_u8 enabled);
legacy_u8 sdl3_soft_keyboard_enabled(void);
void sdl3_soft_keyboard_begin(const legacy_s8 *text, legacy_u16 cursor, const legacy_char *label);
void sdl3_soft_keyboard_update(const legacy_s8 *text, legacy_u16 cursor);
void sdl3_soft_keyboard_end(void);
legacy_u8 sdl3_soft_keyboard_active(void);
legacy_u8 sdl3_soft_keyboard_take_redraw(void);

#endif
