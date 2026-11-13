#ifndef RESTUNTS_PC_SPEAKER_AUDIO_H
#define RESTUNTS_PC_SPEAKER_AUDIO_H

#include "pc_speaker_driver.h"

legacy_u8 sdl3_audio_uses_pc_speaker(void);
legacy_int pc_speaker_audio_initialize(void);
void pc_speaker_audio_shutdown(void);
void pc_speaker_audio_update(void);
void pc_speaker_audio_set_music_muted(legacy_u8 muted);
struct PC_SPEAKER_DRIVER *pc_speaker_audio_driver(void);
const legacy_char *pc_speaker_audio_error(void);

#endif
