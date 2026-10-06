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

#endif
