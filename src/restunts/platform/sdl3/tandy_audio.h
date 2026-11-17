#ifndef RESTUNTS_TANDY_AUDIO_H
#define RESTUNTS_TANDY_AUDIO_H

#include "tandy_driver.h"

legacy_u8 sdl3_audio_uses_tandy(void);
legacy_int tandy_audio_initialize(void);
void tandy_audio_shutdown(void);
void tandy_audio_update(void);
void tandy_audio_clear_queue(void);
legacy_s32 tandy_audio_queued_frames(void);
void tandy_audio_set_music_muted(legacy_u8 muted);
struct TANDY_DRIVER *tandy_audio_driver(void);
const legacy_char *tandy_audio_error(void);

#endif
