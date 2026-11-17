#ifndef RESTUNTS_TANDY_DRIVER_H
#define RESTUNTS_TANDY_DRIVER_H

#include "pc_speaker_driver.h"
#include "mame_psg.h"

#define TANDY_CONTEXT_COUNT 6U
#define TANDY_SAMPLE_CHANNEL 0U
#define TANDY_SAMPLE_RATE 44100U
#define TANDY_PSG_CLOCK_HZ 3579545U
#define TANDY_SAMPLE_HEADER_BYTES PC_SPEAKER_SAMPLE_HEADER_BYTES
#define TANDY_SAMPLE_LENGTH_OFFSET PC_SPEAKER_SAMPLE_LENGTH_OFFSET
#define TANDY_SAMPLE_REFERENCE_OFFSET PC_SPEAKER_SAMPLE_REFERENCE_OFFSET

struct TANDY_VOICE {
	struct AUDIO_CHANNEL *channel;
	legacy_u16 base_period;
	legacy_u16 period;
	legacy_u8 note;
	legacy_u8 volume;
	legacy_u8 velocity;
	legacy_u8 level;
	legacy_u8 active;
};

/* TD15 has distinct DAC, three PSG tone, PSG noise, and PIT destinations. */
struct TANDY_DRIVER {
	struct TANDY_VOICE voices[TANDY_CONTEXT_COUNT];
	struct MAME_PSG psg;
	struct PC_SPEAKER_DRIVER sample;
	struct PC_SPEAKER_DRIVER speaker;
	legacy_u8 music_muted;
	legacy_u8 register_valid[MAME_PSG_REGISTERS];
	legacy_u16 registers[MAME_PSG_REGISTERS];
};

void tandy_driver_initialize(struct TANDY_DRIVER *driver, enum MAME_PSG_PROFILE profile);
void tandy_driver_shutdown(struct TANDY_DRIVER *driver);
legacy_u16 tandy_driver_note_period(legacy_s16 channel, legacy_u8 note);
void tandy_driver_prepare(struct TANDY_DRIVER *driver, legacy_s16 channel,
						  struct AUDIO_CHANNEL *source);
void tandy_driver_engine_value(struct TANDY_DRIVER *driver, legacy_s16 channel,
							   struct AUDIO_CONTEXT *context, legacy_u16 value);
void tandy_driver_activate(struct TANDY_DRIVER *driver, legacy_s16 channel,
						   struct AUDIO_CONTEXT *context, struct AUDIO_CHANNEL *source,
						   legacy_s16 note, legacy_u16 parameter, const legacy_u8 *resource,
						   const legacy_u8 *sample, size_t sample_bytes);
void tandy_driver_release(struct TANDY_DRIVER *driver, legacy_s16 channel);
void tandy_driver_start(struct TANDY_DRIVER *driver);
void tandy_driver_update_context(struct TANDY_DRIVER *driver, legacy_s16 channel,
								 struct AUDIO_CONTEXT *context, const legacy_u8 *resource);
void tandy_driver_volume(struct TANDY_DRIVER *driver, legacy_s16 channel, legacy_u8 volume);
void tandy_driver_set_music_muted(struct TANDY_DRIVER *driver, legacy_u8 muted);
void tandy_driver_mix(struct TANDY_DRIVER *driver);
/* Audio-owner rendering uses TANDY_SAMPLE_RATE; other rates yield silence. */
void tandy_driver_render(struct TANDY_DRIVER *driver, legacy_s16 *samples, legacy_u32 frames,
						 legacy_u32 sample_rate);

#endif
