#ifndef RESTUNTS_SDL3_AUDIO_COMPLETIONS_H
#define RESTUNTS_SDL3_AUDIO_COMPLETIONS_H

#include "../../c/legacy.h"

#if defined(RESTUNTS_AUDIO_WORKER) && !defined(__DJGPP__)
/* Function pointers remain in the game module. Commands carry only channel,
 * registration generation, and whether a completion should be reported. */
void sdl3_audio_completion_set(legacy_s16 channel, void *callback);
void sdl3_audio_completion_register(legacy_s16 channel, legacy_s32 enabled, legacy_u32 generation);
legacy_u32 audio_pop_finish(void);
void sdl3_audio_completions_pump(void);
void sdl3_audio_completions_clear(void);
#endif

#endif
