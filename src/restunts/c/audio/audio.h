#ifndef RESTUNTS_AUDIO_H
#define RESTUNTS_AUDIO_H

#include <stddef.h>
#include "../legacy.h"

#define AUDIO_TIMER_COUNT 25U
#define AUDIO_CHANNEL_COUNT 24U
#define AUDIO_CONTEXT_COUNT 16U
#define AUDIO_MUSIC_CHANNEL_FIRST 0U
#define AUDIO_MUSIC_CHANNEL_LAST 15U
#define AUDIO_EFFECT_CHANNEL_FIRST 16U
#define AUDIO_EFFECT_CHANNEL_LAST 23U
#define AUDIO_FAR_POINTER_SIZE 4U
#define AUDIO_ENGINE_RESERVED_PARAMETER_SIZE 4U
#define AUDIO_ENGINE_RESOURCE_COUNT 10U
#define AUDIO_ENGINE_DEFINITION_SIZE 48U
#define AUDIO_TIMER_SIZE 76U
#define AUDIO_CHANNEL_SIZE 76U
#define AUDIO_CHANNEL_CALL_STACK_COUNT 4U
#define AUDIO_CHANNEL_DRIVER_STATE_SIZE 4U
#define AUDIO_CHANNEL_RETURN_STACK_COUNT 4U
#define AUDIO_CHANNEL_LOOP_COUNT 4U
#define AUDIO_CONTEXT_DRIVER_STATE_SIZE 5U
#define AUDIO_CONTEXT_SIZE 46U
#define AUDIO_CHANNEL_CALL_STACK_OFFSET 5U
#define AUDIO_CHANNEL_ACTIVE_NOTES_OFFSET 21U
#define AUDIO_CHANNEL_NOTE_LIMIT_OFFSET 22U
#define AUDIO_CHANNEL_DELAY_OFFSET 24U
#define AUDIO_CHANNEL_RESOURCE_OFFSET 30U
#define AUDIO_CHANNEL_NOTE_VELOCITY_OFFSET 34U
#define AUDIO_CHANNEL_PITCH_OFFSET 38U
#define AUDIO_CHANNEL_VOLUME_OFFSET 40U
#define AUDIO_CHANNEL_INSTRUMENTS_OFFSET 46U
#define AUDIO_CHANNEL_STACK_DEPTH_OFFSET 50U
#define AUDIO_CHANNEL_RETURN_STACK_OFFSET 51U
#define AUDIO_CHANNEL_LOOP_COUNTS_OFFSET 67U
#define AUDIO_CHANNEL_DRIVER_CHANNEL_OFFSET 71U
#define AUDIO_CHANNEL_FINISH_CALLBACK_OFFSET 72U
#define AUDIO_TIMER_DEFINITION_OFFSET 28U
#define AUDIO_CONTEXT_FADE_OUT_FLAG_OFFSET 12U
#define AUDIO_CONTEXT_RESOURCE_OFFSET 16U
#define AUDIO_CONTEXT_TIMER_OFFSET_OFFSET 42U
enum AUDIO_STATE { AUDIO_STATE_DISABLED = 0, AUDIO_STATE_ENABLED = 1 };

enum AUDIO_UPDATE_STATE { AUDIO_UPDATE_UNLOCKED = 0, AUDIO_UPDATE_LOCKED = 1 };

#pragma pack(push, 1)

/* A far pointer as stored in the original DOS data segment. */
struct AUDIO_FAR_POINTER {
	legacy_u16 offset;
	legacy_u16 segment;
};

struct AUDIO_ENGINE_DEFINITION {
	legacy_u16 sample_count;
	legacy_u8 reserved_parameters[AUDIO_ENGINE_RESERVED_PARAMETER_SIZE];
	legacy_u8 initialized;
	legacy_u8 reserved_initialization_byte;
	struct AUDIO_FAR_POINTER resources[AUDIO_ENGINE_RESOURCE_COUNT];
};

struct AUDIO_TIMER {
	legacy_u8 active;
	legacy_u8 engine_active;
	legacy_s16 channel;
	legacy_u16 current_volume;
	legacy_u32 current_pitch;
	legacy_u8 target_volume;
	legacy_u8 reserved_target_volume_byte;
	legacy_u16 target_pitch;
	legacy_u8 last_volume;
	legacy_u8 reserved_last_volume_byte;
	legacy_u16 last_pitch;
	legacy_s16 engine_context;
	legacy_s16 effect_channel;
	legacy_s16 secondary_effect_channel;
	legacy_u16 sample_count;
	legacy_u8 parameters_changed;
	legacy_u8 restart_engine;
	struct AUDIO_ENGINE_DEFINITION definition;
};

/* One music or effect channel of the sequencer. The sound driver is handed
 * this record directly, so the layout is fixed by the driver ABI. */
struct AUDIO_CHANNEL {
	struct AUDIO_FAR_POINTER cursor;
	/* Call stack for the sequence. Slot 0 is seeded from the resource
	   header; deeper calls push at the pre-incremented depth, so the array
	   is deliberately unaligned and one push past slot 3 runs into
	   active_notes, exactly as the original record did. */
	legacy_u8 call_depth;
	struct AUDIO_FAR_POINTER call_stack[AUDIO_CHANNEL_CALL_STACK_COUNT];
	legacy_u8 active_notes;
	legacy_u8 note_limit;
	legacy_u8 reserved_note_limit_byte;
	legacy_u32 delay;
	legacy_u8 reserved_delay_byte;
	legacy_u8 reserved_resource_byte;
	struct AUDIO_FAR_POINTER resource;
	legacy_u8 note_velocity;
	legacy_u8 channel;
	legacy_u8 priority;
	legacy_u8 sustain;
	legacy_u16 pitch;
	legacy_u8 volume;
	legacy_u8 driver_private_state[AUDIO_CHANNEL_DRIVER_STATE_SIZE];
	legacy_u8 reserved_instrument_byte;
	struct AUDIO_FAR_POINTER instruments;
	legacy_u8 stack_depth;
	struct AUDIO_FAR_POINTER return_stack[AUDIO_CHANNEL_RETURN_STACK_COUNT];
	legacy_u8 loop_counts[AUDIO_CHANNEL_LOOP_COUNT];
	legacy_u8 driver_channel;
	struct AUDIO_FAR_POINTER finish_callback;
};

struct AUDIO_CONTEXT {
	legacy_u8 channel;
	legacy_u8 state;
	legacy_u8 priority;
	legacy_u8 driver_private_state[AUDIO_CONTEXT_DRIVER_STATE_SIZE];
	legacy_u32 age;
	legacy_u32 fade_out_flag;
	struct AUDIO_FAR_POINTER resource;
	legacy_s16 level;
	legacy_u8 envelope_state;
	legacy_u8 reserved_envelope_byte;
	legacy_u16 modulation_delay;
	legacy_u16 modulation_count;
	legacy_s16 modulation;
	legacy_u16 sequence_delay;
	legacy_u16 sequence_count;
	legacy_u8 sequence_value;
	legacy_u8 reserved_sequence_byte;
	legacy_u16 modulation_step;
	legacy_u8 modulation_direction;
	legacy_u8 modulation_tick;
	legacy_u8 sequence_tick;
	legacy_u8 sequence_index;
	legacy_u16 timer_offset;
	legacy_u8 driver_channel;
	legacy_u8 reserved_driver_channel_byte;
};

#pragma pack(pop)

typedef char audio_far_pointer_must_be_4_bytes
	[(sizeof(struct AUDIO_FAR_POINTER) == AUDIO_FAR_POINTER_SIZE) ? 1 : -1];
typedef char audio_engine_definition_must_be_48_bytes
	[(sizeof(struct AUDIO_ENGINE_DEFINITION) == AUDIO_ENGINE_DEFINITION_SIZE) ? 1 : -1];
typedef char
	audio_timer_must_be_76_bytes[(sizeof(struct AUDIO_TIMER) == AUDIO_TIMER_SIZE) ? 1 : -1];
typedef char
	audio_channel_must_be_76_bytes[(sizeof(struct AUDIO_CHANNEL) == AUDIO_CHANNEL_SIZE) ? 1 : -1];
typedef char audio_channel_call_stack_must_be_at_05
	[(offsetof(struct AUDIO_CHANNEL, call_stack) == AUDIO_CHANNEL_CALL_STACK_OFFSET) ? 1 : -1];
typedef char audio_channel_active_notes_must_be_at_15
	[(offsetof(struct AUDIO_CHANNEL, active_notes) == AUDIO_CHANNEL_ACTIVE_NOTES_OFFSET) ? 1 : -1];
typedef char audio_channel_note_limit_must_be_at_16
	[(offsetof(struct AUDIO_CHANNEL, note_limit) == AUDIO_CHANNEL_NOTE_LIMIT_OFFSET) ? 1 : -1];
typedef char audio_channel_delay_must_be_at_18
	[(offsetof(struct AUDIO_CHANNEL, delay) == AUDIO_CHANNEL_DELAY_OFFSET) ? 1 : -1];
typedef char audio_channel_resource_must_be_at_1E
	[(offsetof(struct AUDIO_CHANNEL, resource) == AUDIO_CHANNEL_RESOURCE_OFFSET) ? 1 : -1];
typedef char audio_channel_note_velocity_must_be_at_22
	[(offsetof(struct AUDIO_CHANNEL, note_velocity) == AUDIO_CHANNEL_NOTE_VELOCITY_OFFSET) ? 1
																						   : -1];
typedef char audio_channel_pitch_must_be_at_26
	[(offsetof(struct AUDIO_CHANNEL, pitch) == AUDIO_CHANNEL_PITCH_OFFSET) ? 1 : -1];
typedef char audio_channel_volume_must_be_at_28
	[(offsetof(struct AUDIO_CHANNEL, volume) == AUDIO_CHANNEL_VOLUME_OFFSET) ? 1 : -1];
typedef char audio_channel_instruments_must_be_at_2E
	[(offsetof(struct AUDIO_CHANNEL, instruments) == AUDIO_CHANNEL_INSTRUMENTS_OFFSET) ? 1 : -1];
typedef char audio_channel_stack_depth_must_be_at_32
	[(offsetof(struct AUDIO_CHANNEL, stack_depth) == AUDIO_CHANNEL_STACK_DEPTH_OFFSET) ? 1 : -1];
typedef char audio_channel_return_stack_must_be_at_33
	[(offsetof(struct AUDIO_CHANNEL, return_stack) == AUDIO_CHANNEL_RETURN_STACK_OFFSET) ? 1 : -1];
typedef char audio_channel_loop_counts_must_be_at_43
	[(offsetof(struct AUDIO_CHANNEL, loop_counts) == AUDIO_CHANNEL_LOOP_COUNTS_OFFSET) ? 1 : -1];
typedef char audio_channel_driver_channel_must_be_at_47
	[(offsetof(struct AUDIO_CHANNEL, driver_channel) == AUDIO_CHANNEL_DRIVER_CHANNEL_OFFSET) ? 1
																							 : -1];
typedef char
	audio_channel_finish_callback_must_be_at_48[(offsetof(struct AUDIO_CHANNEL, finish_callback) ==
												 AUDIO_CHANNEL_FINISH_CALLBACK_OFFSET)
													? 1
													: -1];
typedef char
	audio_context_must_be_46_bytes[(sizeof(struct AUDIO_CONTEXT) == AUDIO_CONTEXT_SIZE) ? 1 : -1];
typedef char audio_timer_definition_must_be_at_1C
	[(offsetof(struct AUDIO_TIMER, definition) == AUDIO_TIMER_DEFINITION_OFFSET) ? 1 : -1];
typedef char audio_context_fade_out_flag_must_be_at_0C
	[(offsetof(struct AUDIO_CONTEXT, fade_out_flag) == AUDIO_CONTEXT_FADE_OUT_FLAG_OFFSET) ? 1
																						   : -1];
typedef char audio_context_resource_must_be_at_10
	[(offsetof(struct AUDIO_CONTEXT, resource) == AUDIO_CONTEXT_RESOURCE_OFFSET) ? 1 : -1];
typedef char audio_context_timer_offset_must_be_at_2A
	[(offsetof(struct AUDIO_CONTEXT, timer_offset) == AUDIO_CONTEXT_TIMER_OFFSET_OFFSET) ? 1 : -1];

extern struct AUDIO_TIMER audio_timers[AUDIO_TIMER_COUNT];
extern struct AUDIO_CHANNEL audio_channels[AUDIO_CHANNEL_COUNT];
extern struct AUDIO_CHANNEL *audio_sfx_channels;
extern struct AUDIO_CONTEXT dos_audio_contexts[AUDIO_CONTEXT_COUNT];

#endif
