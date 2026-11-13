#ifndef RESTUNTS_MT32_DRIVER_H
#define RESTUNTS_MT32_DRIVER_H

#include "../../c/audio/audio.h"

#define MT32_MIDI_CHANNEL_COUNT 16U
#define MT32_ENGINE_PITCH_SCALE 60U
#define MT32_PITCH_BEND_CENTER 8192U
#define MT32_MASTER_VOLUME_MAX 100U

typedef void (*mt32_midi_sink)(void *instance, const legacy_u8 *bytes, size_t length);

/* The sequencer owns notes and timing. This adapter only translates the
 * original MT instrument records and context callbacks to MIDI messages. */
struct MT32_DRIVER {
	mt32_midi_sink sink;
	void *instance;
	legacy_u8 volumes[MT32_MIDI_CHANNEL_COUNT];
	legacy_u8 music_channels[MT32_MIDI_CHANNEL_COUNT];
	legacy_u8 music_muted;
};

void mt32_driver_initialize(struct MT32_DRIVER *driver, mt32_midi_sink sink, void *instance);
void mt32_driver_reset(struct MT32_DRIVER *driver);
void mt32_driver_silence_channel(struct MT32_DRIVER *driver, legacy_s16 channel);
void mt32_driver_set_music_muted(struct MT32_DRIVER *driver, legacy_u8 muted);
void mt32_driver_prepare(struct MT32_DRIVER *driver, legacy_s16 channel,
						 struct AUDIO_CHANNEL *source, const legacy_u8 *resource);
void mt32_driver_volume(struct MT32_DRIVER *driver, legacy_s16 channel, legacy_u16 volume);
void mt32_driver_control(struct MT32_DRIVER *driver, legacy_s16 channel, legacy_u16 control,
						 legacy_u16 value);
void mt32_driver_bend(struct MT32_DRIVER *driver, legacy_s16 channel, legacy_u16 value);
void mt32_driver_activate(struct MT32_DRIVER *driver, legacy_s16 channel,
						  struct AUDIO_CONTEXT *context, struct AUDIO_CHANNEL *source,
						  legacy_s16 pitch, legacy_u16 velocity, const legacy_u8 *resource);
void mt32_driver_stop(struct MT32_DRIVER *driver, legacy_s16 channel,
					  struct AUDIO_CONTEXT *context);
void mt32_driver_update_context(struct MT32_DRIVER *driver, legacy_s16 channel,
								struct AUDIO_CONTEXT *context, const legacy_u8 *resource);
legacy_int mt32_driver_load_bank(struct MT32_DRIVER *driver, const legacy_u8 *bank, size_t length);
void mt32_driver_master_volume(struct MT32_DRIVER *driver, legacy_u8 volume);
void mt32_driver_send_data(struct MT32_DRIVER *driver, const legacy_u8 *data, size_t length);

#endif
