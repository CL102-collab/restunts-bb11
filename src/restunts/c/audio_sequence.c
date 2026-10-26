#include "audio_internal.h"
#include "platform.h"
#ifdef RESTUNTS_AMIGA_TRACE
#include <stdio.h>
static legacy_u8 amiga_trace_sequence;
static legacy_uint amiga_trace_sequence_calls;
static legacy_uint amiga_trace_sequence_events;
#define AMIGA_SEQUENCE_TRACE_CALL_LIMIT 2U
#define AMIGA_SEQUENCE_TRACE_EVENT_LIMIT 32U
#define AMIGA_SEQUENCE_TRACE(stage)                                                                \
	do {                                                                                           \
		if (amiga_trace_sequence) {                                                                \
			puts("Amiga sequence: " stage);                                                        \
			fflush(stdout);                                                                        \
		}                                                                                          \
	} while (0)
#else
#define AMIGA_SEQUENCE_TRACE(stage) ((void)0)
#endif
#ifdef RESTUNTS_SDL3
#include "../platform/sdl3/music.h"
#endif

extern legacy_u8 audio_channel_reserved[];
extern legacy_u8 dos_audio_driver_data[];

extern legacy_s16 audio_start_note(struct AUDIO_CHANNEL *timer, legacy_u16 value,
								   legacy_u32 duration, legacy_u8 note, legacy_u16 parameter,
								   legacy_s16 handle);
extern void audio_advance_driver_context(struct AUDIO_CONTEXT *context);

static legacy_u8 audio_sequence_timer_active;

struct audio_sequence_event {
	legacy_u32 delay;
	legacy_u8 command;
	legacy_u8 argument;
	legacy_u32 value;
	legacy_u8 size;
};

typedef void(far *audio_channel_callback_type)(legacy_s16 channel);

#define AUDIO_VARIABLE_LENGTH_DATA_BITS 7U
#define AUDIO_VARIABLE_LENGTH_DATA_MASK 127U
#define AUDIO_NOTE_NUMBER_MASK 127U
#define AUDIO_SEQUENCE_WORD_ARGUMENT_SIZE 2U
#define MIDI_SUSTAIN_CONTROL 64U
#define AUDIO_PITCH_LOW_SIGN_FLAG 256U
#define AUDIO_PITCH_LOW_SIGN_BIT 128U
#define AUDIO_PITCH_HIGH_SCALE_SHIFT 1U
#define AUDIO_PITCH_CENTER 8192
#define AUDIO_TEMPO_NUMERATOR 32000U
#define AUDIO_SEQUENCE_DRIVER_DATA_HEADER_SIZE 4U
#define AUDIO_SEQUENCE_TIMER_TICK_STEP 128U
#define AUDIO_LAST_EFFECT_CHANNEL_EXCLUSIVE 23U

enum AUDIO_SEQUENCE_TIMER_STATE {
	AUDIO_SEQUENCE_TIMER_INACTIVE = 0,
	AUDIO_SEQUENCE_TIMER_ACTIVE = 1
};

legacy_s16 audio_sequence_command_has_byte_argument(legacy_u8 command_index)
{
	switch (command_index) {
		case AUDIO_SEQUENCE_COMMAND_SET_INSTRUMENT:
		case AUDIO_SEQUENCE_COMMAND_SET_TEMPO:
		case AUDIO_SEQUENCE_COMMAND_SET_VOLUME:
		case AUDIO_SEQUENCE_COMMAND_SET_NOTE_LIMIT:
		case AUDIO_SEQUENCE_COMMAND_SET_PRIORITY:
		case AUDIO_SEQUENCE_COMMAND_LOOP_BEGIN:
		case AUDIO_SEQUENCE_COMMAND_SET_NOTE_VELOCITY:
		case AUDIO_SEQUENCE_COMMAND_SET_DRIVER_CHANNEL:
		case AUDIO_SEQUENCE_COMMAND_SET_CHANNEL_RESERVED:
			return 1;
		default:
			return 0;
	}
}

static legacy_u32 audio_parse_variable_length(const legacy_u8 far *source, legacy_u16 *size)
{
	legacy_u8 byte_value;
	legacy_u32 value = 0;
	do {
		byte_value = source[*size];
		*size = LEGACY_U16_WRAP_ADD(*size, 1U);
		value = LEGACY_U32_WRAP_ADD(value << AUDIO_VARIABLE_LENGTH_DATA_BITS,
									(legacy_u32)(byte_value & AUDIO_VARIABLE_LENGTH_DATA_MASK));
	} while ((byte_value & AUDIO_VARIABLE_LENGTH_CONTINUATION_BIT) != 0);
	return value;
}

static void audio_parse_sequence_event(const legacy_u8 far *source,
									   struct audio_sequence_event *event)
{
	legacy_u16 size = 0;
	event->delay = audio_parse_variable_length(source, &size);
	event->command = source[size++];
	event->argument = 0;
	event->value = 0;

	legacy_u16 payload_size;
	if (event->command >= AUDIO_SEQUENCE_COMMAND_BASE &&
		event->command <= AUDIO_SEQUENCE_COMMAND_LAST) {
		legacy_u8 command_index = (legacy_u8)(event->command - AUDIO_SEQUENCE_COMMAND_BASE);
		if (audio_sequence_command_has_byte_argument(command_index)) {
			event->argument = source[size++];
		} else {
			switch (command_index) {
				case AUDIO_SEQUENCE_COMMAND_SET_CONTROL:
					event->argument = source[size++];
					event->value = source[size++];
					break;
				case AUDIO_SEQUENCE_COMMAND_SET_PITCH:
					event->value = LEGACY_READ_U16_LE(source + size);
					size = LEGACY_U16_WRAP_ADD(size, AUDIO_SEQUENCE_WORD_ARGUMENT_SIZE);
					break;
				case AUDIO_SEQUENCE_COMMAND_CALL:
					event->argument = source[size++];
					event->value = LEGACY_READ_U32_LE(source + size);
					size = LEGACY_U16_WRAP_ADD(size, AUDIO_FAR_POINTER_SIZE);
					break;
				case AUDIO_SEQUENCE_COMMAND_SKIP_PAYLOAD:
					payload_size = source[size];
					size = LEGACY_U16_WRAP_ADD(size, LEGACY_U16_WRAP_ADD(payload_size, 1U));
					break;
				case AUDIO_SEQUENCE_COMMAND_SEND_DRIVER_DATA:
					payload_size = source[size++];
					for (legacy_u16 index = 0; index < payload_size; index++) {
						dos_audio_driver_data[index] = source[size + index];
					}
					size = LEGACY_U16_WRAP_ADD(size, payload_size);
					break;
				default:
					break;
			}
		}
	} else {
		if (event->command > AUDIO_SEQUENCE_STATUS_BIT) {
			event->argument = source[size++];
		}
		event->value = audio_parse_variable_length(source, &size);
	}
	event->size = (legacy_u8)size;
}

static void far *audio_sequence_instrument(struct AUDIO_CHANNEL *chunk, legacy_u8 instrument)
{
	const legacy_u8 far *instruments =
		(const legacy_u8 far *)audio_read_far_pointer((legacy_u8 *)&chunk->instruments);
	return audio_read_far_pointer(instruments + (legacy_u16)instrument * AUDIO_FAR_POINTER_SIZE);
}

static void audio_sequence_bind_instrument(legacy_s16 channel, struct AUDIO_CHANNEL *chunk,
										   legacy_u8 instrument)
{
	void far *resource = audio_sequence_instrument(chunk, instrument);
	audio_write_far_pointer((legacy_u8 *)&chunk->resource, resource);
	if (resource == 0 || dos_audio_uses_direct_channels == 0) {
		return;
	}

	legacy_u8 driver_channel;
	if (((legacy_u8 far *)resource)[AUDIO_INSTRUMENT_DIRECT_CHANNEL_OFFSET] <
		AUDIO_DIRECT_CHANNEL_COUNT) {
		driver_channel = ((legacy_u8 far *)resource)[AUDIO_INSTRUMENT_DIRECT_CHANNEL_OFFSET];
	} else {
		driver_channel = (legacy_u8)(((legacy_u16)channel & AUDIO_DIRECT_CHANNEL_MASK) +
									 AUDIO_DRIVER_CHANNEL_BASE);
	}
	chunk->driver_channel = driver_channel;
	dos_audio_set_channel_volume(channel, chunk->volume);
	dos_audio_driver_prepare_context(driver_channel, 0, (legacy_u8 *)chunk, resource);
}

static void audio_sequence_set_control(struct AUDIO_CHANNEL *chunk, legacy_u8 control,
									   legacy_u16 value)
{
	if (control == MIDI_SUSTAIN_CONTROL) {
		chunk->sustain = (legacy_u8)value;
	}

	if (dos_audio_uses_direct_channels != 0) {
		dos_audio_driver_set_control(chunk->driver_channel, 0, control, value);
	}

	struct AUDIO_CONTEXT *context = dos_audio_contexts;
	for (legacy_u16 context_index = 0; context_index < dos_audio_context_count; context_index++) {
		if (context->channel == chunk->channel) {
			if (dos_audio_uses_direct_channels == 0) {
				dos_audio_driver_set_control((legacy_s16)context_index, context, control, value);
			}
			if (control == MIDI_SUSTAIN_CONTROL && value == 0 &&
				context->state == AUDIO_CONTEXT_STATE_RELEASING) {
				context->envelope_state = AUDIO_ENVELOPE_STATE_RELEASE;
			}
		}
		context++;
	}
}

static void audio_sequence_set_pitch(struct AUDIO_CHANNEL *chunk, legacy_u16 value)
{
	if ((value & AUDIO_PITCH_LOW_SIGN_FLAG) != 0) {
		value = LEGACY_U16_REPLACE_LOW_BYTE(value,
											LEGACY_U16_LOW_BYTE(value) | AUDIO_PITCH_LOW_SIGN_BIT);
	}
	legacy_s16 low_value = LEGACY_S8_FROM_BITS(LEGACY_U16_LOW_BYTE(value));
	legacy_s16 pitch = LEGACY_S16_WRAP_ADD(
		LEGACY_S16_FROM_BITS((value & LEGACY_U16_HIGH_BYTE_MASK) >> AUDIO_PITCH_HIGH_SCALE_SHIFT),
		low_value);
	pitch = LEGACY_S16_WRAP_SUB(pitch, AUDIO_PITCH_CENTER);
	chunk->pitch = pitch;
	dos_audio_driver_set_pitch((legacy_u8 *)chunk, pitch, chunk->driver_channel);
}

static void audio_sequence_finish_channel(legacy_s16 channel, struct AUDIO_CHANNEL *chunk,
										  legacy_s16 reset_channel)
{
	audio_channel_callback_type callback =
		(audio_channel_callback_type)audio_read_far_pointer((legacy_u8 *)&chunk->finish_callback);
	chunk->cursor.offset = 0;
	chunk->cursor.segment = 0;
	audio_release_channel_range(channel, channel);
	if (reset_channel != 0) {
		audio_init_channel_range(channel, channel, 0, 0, audio_effect_rate, 0);
	}
	if (callback != 0) {
		callback(channel);
	}
}

static legacy_s16 audio_sequence_flow_command(legacy_s16 channel, struct AUDIO_CHANNEL *chunk,
											  const struct audio_sequence_event *event)
{
	legacy_u8 depth;
	void far *pointer;
	switch ((legacy_u8)(event->command - AUDIO_SEQUENCE_COMMAND_BASE)) {
		case AUDIO_SEQUENCE_COMMAND_RETURN:
			depth = chunk->call_depth;
			if (depth != 0) {
				pointer = audio_read_far_pointer((legacy_u8 *)&chunk->call_stack[depth]);
				audio_write_far_pointer((legacy_u8 *)&chunk->cursor, pointer);
				chunk->call_depth--;
			} else {
				audio_sequence_finish_channel(channel, chunk, 0);
			}
			break;
		case AUDIO_SEQUENCE_COMMAND_STOP:
			audio_sequence_finish_channel(channel, chunk, 1);
			break;
		case AUDIO_SEQUENCE_COMMAND_RESTART:
			chunk->call_depth = 0;
			chunk->stack_depth = 0;
			pointer = audio_read_far_pointer((legacy_u8 *)&chunk->call_stack[0]);
			audio_write_far_pointer((legacy_u8 *)&chunk->cursor, pointer);
			break;
		case AUDIO_SEQUENCE_COMMAND_LOOP_BEGIN:
			depth = chunk->stack_depth;
			audio_write_far_pointer((legacy_u8 *)&chunk->return_stack[depth],
									audio_read_far_pointer((legacy_u8 *)&chunk->cursor));
			chunk->loop_counts[depth] = (legacy_u8)(event->argument - 1U);
			chunk->stack_depth++;
			break;
		case AUDIO_SEQUENCE_COMMAND_LOOP_END:
			depth = chunk->stack_depth;
			if (depth != 0) {
				pointer = audio_read_far_pointer((legacy_u8 *)&chunk->return_stack[depth - 1U]);
				audio_write_far_pointer((legacy_u8 *)&chunk->cursor, pointer);
				legacy_u8 count = chunk->loop_counts[depth - 1U];
				chunk->loop_counts[depth - 1U]--;
				if (count == 0) {
					chunk->stack_depth--;
				}
			}
			break;
		case AUDIO_SEQUENCE_COMMAND_CALL:
			chunk->call_depth++;
			depth = chunk->call_depth;
			audio_write_far_pointer((legacy_u8 *)&chunk->call_stack[depth],
									audio_read_far_pointer((legacy_u8 *)&chunk->cursor));
			pointer = dos_memory_make_pointer(
				(legacy_u16)(event->value >> LEGACY_WORD_BITS),
				LEGACY_U16_WRAP_ADD((legacy_u16)event->value, AUDIO_FAR_POINTER_SIZE));
			audio_write_far_pointer((legacy_u8 *)&chunk->cursor, pointer);
			break;
		default:
			return 0;
	}
	return 1;
}

static void audio_sequence_apply_command(legacy_s16 channel, struct AUDIO_CHANNEL *chunk,
										 const struct audio_sequence_event *event)
{
	legacy_u16 offset;

	switch ((legacy_u8)(event->command - AUDIO_SEQUENCE_COMMAND_BASE)) {
		case AUDIO_SEQUENCE_COMMAND_SET_INSTRUMENT:
			audio_sequence_bind_instrument(channel, chunk, event->argument);
			break;
		case AUDIO_SEQUENCE_COMMAND_SET_TEMPO:
			if ((legacy_u16)channel < AUDIO_DIRECT_CHANNEL_COUNT) {
				audio_sequence_tick_period =
					LEGACY_U16_DIV_OR_ZERO(AUDIO_TEMPO_NUMERATOR, event->argument);
			}
			break;
		case AUDIO_SEQUENCE_COMMAND_SET_VOLUME:
			dos_audio_set_channel_volume(channel, event->argument);
			break;
		case AUDIO_SEQUENCE_COMMAND_SET_CONTROL:
			audio_sequence_set_control(chunk, event->argument, (legacy_u16)event->value);
			break;
		case AUDIO_SEQUENCE_COMMAND_SET_NOTE_LIMIT:
			chunk->note_limit = event->argument;
			break;
		case AUDIO_SEQUENCE_COMMAND_SET_PRIORITY:
			chunk->priority = event->argument;
			break;
		case AUDIO_SEQUENCE_COMMAND_SET_NOTE_VELOCITY:
			chunk->note_velocity = event->argument;
			break;
		case AUDIO_SEQUENCE_COMMAND_SET_PITCH:
			audio_sequence_set_pitch(chunk, (legacy_u16)event->value);
			break;
		case AUDIO_SEQUENCE_COMMAND_SEND_DRIVER_DATA:
			offset = LEGACY_U16_WRAP_SUB(event->size, AUDIO_SEQUENCE_DRIVER_DATA_HEADER_SIZE);
			dos_audio_driver_send_data(offset, dos_audio_driver_data);
			break;
		case AUDIO_SEQUENCE_COMMAND_SET_DRIVER_CHANNEL:
			chunk->driver_channel = event->argument;
			break;
		case AUDIO_SEQUENCE_COMMAND_SET_CHANNEL_RESERVED:
			audio_channel_reserved[(legacy_u16)channel] = event->argument;
			break;
		default:
			break;
	}
}

static void audio_service_sequence_channel(legacy_s16 channel)
{
	struct AUDIO_CHANNEL *chunk = &audio_channels[channel];
	struct audio_sequence_event next_event;
	struct audio_sequence_event event;
	for (;;) {
		legacy_u32 delay = chunk->delay;
		if (delay != 0) {
			chunk->delay = LEGACY_U32_WRAP_SUB(delay, 1UL);
			return;
		}
		void far *pointer = audio_read_far_pointer((legacy_u8 *)&chunk->cursor);
		if (pointer == 0) {
			return;
		}

		audio_parse_sequence_event((const legacy_u8 far *)pointer, &event);
#ifdef RESTUNTS_AMIGA_TRACE
		if (amiga_trace_sequence &&
			amiga_trace_sequence_events++ < AMIGA_SEQUENCE_TRACE_EVENT_LIMIT) {
			printf("Amiga sequence: channel=%d event=%u arg=%u delay=%" LEGACY_PRIu32
				   " value=%" LEGACY_PRIu32 "\n",
				   (legacy_int)channel, (legacy_uint)event.command, (legacy_uint)event.argument,
				   event.delay, event.value);
			fflush(stdout);
		}
#endif
		LEGACY_WRITE_U16_LE(
			(legacy_u8 *)&chunk->cursor.offset,
			LEGACY_U16_WRAP_ADD(LEGACY_READ_U16_LE((const legacy_u8 *)&chunk->cursor.offset),
								event.size));
		if (event.command < AUDIO_SEQUENCE_COMMAND_BASE) {
			if (event.command < AUDIO_SEQUENCE_STATUS_BIT) {
				event.argument = chunk->note_velocity;
			}
			event.command &= AUDIO_NOTE_NUMBER_MASK;
			audio_start_note(chunk, 0, event.value, event.command, event.argument, channel);
		} else {
			if (audio_sequence_flow_command(channel, chunk, &event) == 0) {
				audio_sequence_apply_command(channel, chunk, &event);
			}
		}

		pointer = audio_read_far_pointer((legacy_u8 *)&chunk->cursor);
		if (pointer != 0) {
			audio_parse_sequence_event((const legacy_u8 far *)pointer, &next_event);
			chunk->delay = next_event.delay;
		}
	}
}

static void audio_advance_music_contexts(void)
{
	struct AUDIO_CONTEXT *context = dos_audio_contexts;
	for (legacy_u16 context_index = 0; context_index < dos_audio_context_count; context_index++) {
		if (context->state != AUDIO_CONTEXT_STATE_FREE &&
			context->channel < AUDIO_DIRECT_CHANNEL_COUNT) {
			audio_advance_driver_context(context);
		}
		context++;
	}
}

void audio_sequence_timer(void)
{
	if (dos_data_stack_segments_match() == 0 || dos_audio_driver_binary == 0 ||
		audio_update_lock != AUDIO_UPDATE_UNLOCKED ||
		audio_sequence_timer_active != AUDIO_SEQUENCE_TIMER_INACTIVE) {
		return;
	}

	audio_sequence_timer_active = AUDIO_SEQUENCE_TIMER_ACTIVE;
#ifdef RESTUNTS_AMIGA_TRACE
	amiga_trace_sequence = audio_music_active == AUDIO_STATE_ENABLED &&
						   amiga_trace_sequence_calls < AMIGA_SEQUENCE_TRACE_CALL_LIMIT;
	if (amiga_trace_sequence) {
		amiga_trace_sequence_calls++;
		amiga_trace_sequence_events = 0;
		printf("Amiga sequence: tick %u period=%u elapsed=%u\n", amiga_trace_sequence_calls,
			   (legacy_uint)audio_sequence_tick_period, (legacy_uint)audio_sequence_elapsed_ticks);
		fflush(stdout);
	}
#endif
	AMIGA_SEQUENCE_TRACE("update contexts begin");
	audio_update_driver_contexts();
	AMIGA_SEQUENCE_TRACE("update contexts complete");
	if (audio_music_active == AUDIO_STATE_ENABLED && audio_music_enabled == AUDIO_STATE_ENABLED &&
		audio_suspended == AUDIO_STATE_DISABLED) {
#ifdef RESTUNTS_SDL3
		sdl3_music_tick();
#endif
		audio_sequence_elapsed_ticks =
			LEGACY_U16_WRAP_ADD(audio_sequence_elapsed_ticks, AUDIO_SEQUENCE_TIMER_TICK_STEP);
		while (audio_sequence_elapsed_ticks >= audio_sequence_tick_period) {
			AMIGA_SEQUENCE_TRACE("music contexts begin");
			audio_advance_music_contexts();
			AMIGA_SEQUENCE_TRACE("music contexts complete");
			audio_sequence_elapsed_ticks =
				LEGACY_U16_WRAP_SUB(audio_sequence_elapsed_ticks, audio_sequence_tick_period);
			for (legacy_u16 channel = 0; channel < audio_music_channel_count; channel++) {
				AMIGA_SEQUENCE_TRACE("channel begin");
				audio_service_sequence_channel((legacy_s16)channel);
				AMIGA_SEQUENCE_TRACE("channel complete");
			}
		}
	} else {
		audio_advance_music_contexts();
	}
	for (legacy_u16 channel = AUDIO_EFFECT_CHANNEL_FIRST;
		 channel < AUDIO_LAST_EFFECT_CHANNEL_EXCLUSIVE; channel++) {
		audio_service_sequence_channel((legacy_s16)channel);
	}
	audio_sequence_timer_active = AUDIO_SEQUENCE_TIMER_INACTIVE;
	AMIGA_SEQUENCE_TRACE("tick complete");
}
