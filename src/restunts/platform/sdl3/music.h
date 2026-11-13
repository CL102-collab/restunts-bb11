#ifndef RESTUNTS_SDL3_MUSIC_H
#define RESTUNTS_SDL3_MUSIC_H

#include "../../c/legacy.h"

void sdl3_music_register(const void *resource, const legacy_s8 *name);
legacy_s32 sdl3_music_start(const void *resource);
void sdl3_music_stop(void);
void sdl3_music_set_enabled(legacy_s32 enabled);
#ifndef __DJGPP__
legacy_s32 sdl3_music_get_enabled(void);
#endif
legacy_s32 sdl3_music_toggle(void);
void sdl3_music_tick(void);
void sdl3_music_set_volume(legacy_s16 value);
void sdl3_music_sync(void);
void sdl3_music_update(void);
#ifndef __DJGPP__
void sdl3_music_clear_queue(void);
legacy_s32 sdl3_music_queued_frames(void);
#endif
void sdl3_music_shutdown(void);

#endif
