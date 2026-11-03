#ifndef RESTUNTS_AUDIO_CONTROL_H
#define RESTUNTS_AUDIO_CONTROL_H

#include "../legacy.h"

void audio_suspend(void);

void audio_resume(void);

legacy_s16 audio_load_dos_driver(const legacy_s8 *driver_name, legacy_s16 unused_driver_segment,
								 legacy_s16 mode);
extern void far *load_song_file(const legacy_s8 *filename);
extern void far *load_voice_file(const legacy_s8 *filename);
extern void far *load_sfx_file(const legacy_s8 *filename);
extern void far *init_audio_resources(void far *songptr, void far *voiceptr, const legacy_s8 *name);
extern void load_audio_finalize(void far *audiores);
extern void audio_unload(void);
extern legacy_s16 audio_toggle_music(void);
extern legacy_s16 audio_toggle_effects(void);
extern void audiodrv_atexit(void);
extern void audio_play_crash_and_stop_engine(legacy_s16 index);
extern void audio_add_driver_timer(void);
extern void audio_remove_driver_timer(void);

#endif
