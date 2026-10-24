#ifndef RESTUNTS_SDL3_VIDEO_GAME_H
#define RESTUNTS_SDL3_VIDEO_GAME_H

#include "../../c/legacy.h"

#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) && !defined(__DJGPP__)
#define SDL3_STARTUP_FULLSCREEN_OFF 0U
#define SDL3_STARTUP_FULLSCREEN_ON 1U

/* Set before creating the game window; ordinary fullscreen shortcuts remain available. */
void sdl3_video_set_startup_fullscreen(legacy_u8 enabled);
#endif

#endif
