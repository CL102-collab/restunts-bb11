#ifndef RESTUNTS_SDL3_SOFT_KEYBOARD_H
#define RESTUNTS_SDL3_SOFT_KEYBOARD_H

#include "sdl3.h"
#include "touch_game.h"

/* The keyboard is drawn and hit-tested in window coordinates. */
legacy_u8 sdl3_soft_keyboard_event(const SDL_Event *event, SDL_Window *window);
SDL_FRect sdl3_soft_keyboard_game_area(SDL_Window *window);
void sdl3_soft_keyboard_draw(SDL_Renderer *renderer, SDL_Window *window);
legacy_u8 sdl3_soft_keyboard_take_redraw(void);

#endif
