#ifndef RESTUNTS_AUDIO_INTERNAL_H
#define RESTUNTS_AUDIO_INTERNAL_H

#include "audio.h"
#include "../legacy.h"
#include "../math.h"

#define AUDIO_RESOURCE_ID_LENGTH 4U
#define DOS_AUDIO_MASTER_STATE_SIZE 3U
#define DOS_AUDIO_DRIVER_DATA_SIZE 256U
#define DOS_AUDIO_MASTER_STATE_INITIALIZER {16U, 0U, 22U}
#define AUDIO_ENGINE_MAX_VOLUME 127U
#define AUDIO_NOTE_USE_DRIVER_VALUE 255U
#define AUDIO_RESOURCE_MODULATION_ENABLED_OFFSET 40U
#define AUDIO_RESOURCE_SEQUENCE_ENABLED_OFFSET 53U
#define AUDIO_INSTRUMENT_DIRECT_CHANNEL_OFFSET 67U
#define AUDIO_DIRECT_CHANNEL_COUNT 16U
#define AUDIO_DIRECT_CHANNEL_MASK 15U
#define AUDIO_DRIVER_CHANNEL_BASE 1U
#define AUDIO_SEQUENCE_COMMAND_BASE 217U
#define AUDIO_SEQUENCE_COMMAND_LAST 234U
#define AUDIO_SEQUENCE_STATUS_BIT 128U
#define AUDIO_VARIABLE_LENGTH_CONTINUATION_BIT 128U

enum AUDIO_SEQUENCE_COMMAND {
	AUDIO_SEQUENCE_COMMAND_RETURN = 0,
	AUDIO_SEQUENCE_COMMAND_STOP = 1,
	AUDIO_SEQUENCE_COMMAND_RESTART = 2,
	AUDIO_SEQUENCE_COMMAND_SET_INSTRUMENT = 3,
	AUDIO_SEQUENCE_COMMAND_SET_TEMPO = 4,
	AUDIO_SEQUENCE_COMMAND_SET_VOLUME = 5,
	AUDIO_SEQUENCE_COMMAND_SET_CONTROL = 6,
	AUDIO_SEQUENCE_COMMAND_SET_NOTE_LIMIT = 7,
	AUDIO_SEQUENCE_COMMAND_SET_PRIORITY = 8,
	AUDIO_SEQUENCE_COMMAND_LOOP_BEGIN = 9,
	AUDIO_SEQUENCE_COMMAND_LOOP_END = 10,
	AUDIO_SEQUENCE_COMMAND_SET_NOTE_VELOCITY = 11,
	AUDIO_SEQUENCE_COMMAND_SET_PITCH = 12,
	AUDIO_SEQUENCE_COMMAND_CALL = 13,
	AUDIO_SEQUENCE_COMMAND_SKIP_PAYLOAD = 14,
	AUDIO_SEQUENCE_COMMAND_SEND_DRIVER_DATA = 15,
	AUDIO_SEQUENCE_COMMAND_SET_DRIVER_CHANNEL = 16,
	AUDIO_SEQUENCE_COMMAND_SET_CHANNEL_RESERVED = 17
};

enum AUDIO_CONTEXT_STATE {
	AUDIO_CONTEXT_STATE_FREE = 0,
	AUDIO_CONTEXT_STATE_PLAYING = 1,
	AUDIO_CONTEXT_STATE_RELEASING = 2
};

enum AUDIO_ENVELOPE_STATE {
	AUDIO_ENVELOPE_STATE_IDLE = 0,
	AUDIO_ENVELOPE_STATE_ATTACK = 1,
	AUDIO_ENVELOPE_STATE_DECAY = 2,
	AUDIO_ENVELOPE_STATE_SUSTAIN = 3,
	AUDIO_ENVELOPE_STATE_RELEASE = 4
};

enum AUDIO_RESOURCE_TYPE { AUDIO_RESOURCE_TYPE_SONG = 0, AUDIO_RESOURCE_TYPE_EFFECT = 1 };

extern legacy_s8 audio_music_enabled;
extern legacy_s8 audio_effects_enabled;
extern legacy_s16 audio_update_lock;
extern legacy_u8 dos_audio_special_mode;
extern legacy_u8 dos_audio_context_count;
extern legacy_u8 dos_audio_uses_direct_channels;
extern legacy_u8 dos_audio_master_state[];
extern legacy_u8 dos_audio_master_volume;
extern void far *dos_audio_driver_binary;
extern legacy_u8 audio_music_rate;
extern legacy_u8 audio_music_channel_count;
extern legacy_u8 audio_suspended;
extern legacy_u8 audio_music_active;
extern legacy_u8 audio_effect_rate;
extern legacy_u16 audio_sequence_elapsed_ticks;
extern legacy_u16 audio_sequence_tick_period;
extern void far *audio_bass_drum_resource;
extern void far *audio_snare_resource;
extern void far *audio_tom_resource;
extern void far *audio_ride_resource;
extern void far *audio_crash_resource;
extern void far *audio_closed_hihat_resource;
extern void far *audio_open_hihat_resource;

void far *audio_read_far_pointer(const legacy_u8 far *source);
void audio_write_far_pointer(legacy_u8 far *destination, const void far *value);
legacy_s8 *pad_id(const legacy_s8 far *source);
void audio_reset_channels(void);
void audio_stop_music(void);
void audio_driver_timer(void);
void audio_release_channel_range(legacy_s16 first_channel, legacy_s16 last_channel);
void audio_update_driver_contexts(void);
void audio_init_channel_range(legacy_s16 first_channel, legacy_s16 last_channel, void far *resource,
							  legacy_u16 resource_data_offset, legacy_u16 rate, legacy_u8 priority);
void audio_stop_effect_channel(legacy_s16 channel);
legacy_s16 audio_sequence_command_has_byte_argument(legacy_u8 command_index);
legacy_s16 audio_start_sample(legacy_u16 value, legacy_s16 handle);
legacy_s16 audio_reserve_effect_channel(legacy_s16 channel, legacy_u8 priority);
void audio_release_effect_channel(legacy_s16 channel);
legacy_s16 audio_effect_channel_idle(legacy_s16 channel);
void audio_request_context_fade(legacy_s16 index);
void audio_fade_out(legacy_s16 delay_ticks);

void audio_sequence_timer(void);

#endif
