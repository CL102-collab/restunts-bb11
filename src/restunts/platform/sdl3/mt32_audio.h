#ifndef RESTUNTS_MT32_AUDIO_H
#define RESTUNTS_MT32_AUDIO_H

#include "../../c/legacy.h"
#include "mt32_driver.h"

/* Configuration is established before loading game resources. Muted startup
 * bypasses synthesis and never requires an MT-32 ROM directory. */
void sdl3_audio_configure(legacy_u8 mode, const legacy_char *rom_directory,
						  const legacy_char *model, legacy_u8 disabled);
legacy_u8 sdl3_audio_uses_mt32(void);

#ifdef RESTUNTS_MT32
legacy_int mt32_audio_initialize(const legacy_char *rom_directory, const legacy_char *model);
void mt32_audio_shutdown(void);
void mt32_audio_update(void);
void mt32_audio_set_music_muted(legacy_u8 muted);
struct MT32_DRIVER *mt32_audio_driver(void);
void mt32_audio_finish_bank_upload(void);
const legacy_char *mt32_audio_error(void);
#endif

#endif
