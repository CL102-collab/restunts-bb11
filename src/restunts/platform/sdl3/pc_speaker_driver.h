#ifndef RESTUNTS_PC_SPEAKER_DRIVER_H
#define RESTUNTS_PC_SPEAKER_DRIVER_H

#include "../../c/audio/audio.h"

#define PC_SPEAKER_CONTEXT_COUNT 7U
#define PC_SPEAKER_PIT_HZ 1193180U
#define PC_SPEAKER_SAMPLE_HEADER_BYTES 50U
#define PC_SPEAKER_SAMPLE_LENGTH_OFFSET 8U
#define PC_SPEAKER_SAMPLE_REFERENCE_OFFSET 6U
#define PC_SPEAKER_SAMPLE_MAX_BYTES 1048576U
#define PC_SPEAKER_PHASE_BITS 32U
#define PC_SPEAKER_PCM_UNSIGNED_CENTER 128
#define PC_SPEAKER_PCM_SAMPLE_SCALE 64

struct PC_SPEAKER_VOICE {
	struct AUDIO_CHANNEL *channel;
	legacy_u16 base_period;
	legacy_u16 period;
	legacy_u8 note;
	legacy_u8 volume;
	legacy_u8 active;
};

/* The original driver has six logical tone contexts but one physical speaker.
 * Arbitration runs once per sequencer tick. Only the main thread mutates this
 * state; the output backend consumes generated PCM or the current PIT state. */
struct PC_SPEAKER_DRIVER {
	struct PC_SPEAKER_VOICE voices[PC_SPEAKER_CONTEXT_COUNT];
	legacy_u8 mix_phase;
	legacy_u8 music_muted;
	legacy_u8 output_active;
	legacy_u16 output_period;
	legacy_s8 selected_voice;
	legacy_u32 tone_phase;
	legacy_u8 *sample_data;
	size_t sample_length;
	legacy_u64 sample_position;
	legacy_u32 sample_rate;
	legacy_u8 sample_active;
};

void pc_speaker_driver_initialize(struct PC_SPEAKER_DRIVER *driver);
void pc_speaker_driver_shutdown(struct PC_SPEAKER_DRIVER *driver);
legacy_u16 pc_speaker_driver_note_period(legacy_u8 note);
void pc_speaker_driver_prepare(struct PC_SPEAKER_DRIVER *driver, legacy_s16 channel,
							   struct AUDIO_CHANNEL *source);
void pc_speaker_driver_engine_value(struct PC_SPEAKER_DRIVER *driver, legacy_s16 channel,
									struct AUDIO_CONTEXT *context, legacy_u16 value);
void pc_speaker_driver_activate(struct PC_SPEAKER_DRIVER *driver, legacy_s16 channel,
								struct AUDIO_CONTEXT *context, struct AUDIO_CHANNEL *source,
								legacy_s16 note, const legacy_u8 *sample, size_t sample_bytes);
void pc_speaker_driver_release(struct PC_SPEAKER_DRIVER *driver, legacy_s16 channel);
void pc_speaker_driver_start(struct PC_SPEAKER_DRIVER *driver);
void pc_speaker_driver_update_context(struct PC_SPEAKER_DRIVER *driver, legacy_s16 channel,
									  struct AUDIO_CONTEXT *context, const legacy_u8 *resource);
void pc_speaker_driver_volume(struct PC_SPEAKER_DRIVER *driver, legacy_s16 channel,
							  legacy_u8 volume);
void pc_speaker_driver_set_music_muted(struct PC_SPEAKER_DRIVER *driver, legacy_u8 muted);
void pc_speaker_driver_mix(struct PC_SPEAKER_DRIVER *driver);
void pc_speaker_driver_render(struct PC_SPEAKER_DRIVER *driver, legacy_s16 *samples,
							  legacy_u32 frames, legacy_u32 sample_rate);

#endif
