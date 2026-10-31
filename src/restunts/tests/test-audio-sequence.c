/* Regression fingerprints captured from the original audio routines before extraction. */
#include "audio-test-support.h"
#ifndef AUDIO_SEQUENCE_SOURCE
#define AUDIO_SEQUENCE_SOURCE "../c/audio/audio_sequence.c"
#endif
#include AUDIO_SEQUENCE_SOURCE
legacy_s16 audio_start_note(struct AUDIO_CHANNEL *channel, legacy_u16 value, legacy_u32 duration,
							legacy_u8 note, legacy_u16 parameter, legacy_s16 handle)
{
	hash_word(8);
	hash_word(channel - audio_channels);
	hash_word(value);
	hash_word(duration);
	hash_word(duration >> 16);
	hash_word(note);
	hash_word(parameter);
	hash_word(handle);
	return 0;
}
void audio_update_driver_contexts(void)
{
	hash_word(9);
	if (nested_timer) {
		nested_timer = 0;
		audio_sequence_timer();
	}
}
void audio_advance_driver_context(struct AUDIO_CONTEXT *context)
{
	hash_word(10);
	hash_word(context - dos_audio_contexts);
}

static void hash_channel_state(void)
{
	hash_bytes(audio_channels, sizeof(audio_channels));
	hash_bytes(dos_audio_contexts, sizeof(dos_audio_contexts));
	hash_bytes(audio_channel_reserved, sizeof(audio_channel_reserved));
	hash_bytes(dos_audio_driver_data, sizeof(dos_audio_driver_data));
	hash_word(audio_sequence_tick_period);
	hash_word(audio_sequence_elapsed_ticks);
	hash_word(audio_sequence_timer_active);
}
static legacy_u32 parse_fingerprint(void)
{
	trace_hash = 2166136261UL;
	struct audio_sequence_event parsed;
	for (legacy_u32 command = 0; command < 256; command++) {
		for (legacy_u32 variant = 0; variant < 16; variant++) {
			reset_audio_fixture();
			memory_bytes[0] = variant & 1 ? 0x81 : 0;
			legacy_u32 index = variant & 1 ? 2 : 1;
			memory_bytes[1] = 0x7f;
			memory_bytes[index++] = command;
			memory_bytes[index++] = variant * 17;
			memory_bytes[index++] = 0x82;
			memory_bytes[index++] = 0x41;
			audio_parse_sequence_event(memory_bytes, &parsed);
			hash_word(parsed.delay);
			hash_word(parsed.delay >> 16);
			hash_word(parsed.command);
			hash_word(parsed.argument);
			hash_word(parsed.value);
			hash_word(parsed.value >> 16);
			hash_word(parsed.size);
			hash_bytes(dos_audio_driver_data, sizeof(dos_audio_driver_data));
		}
	}
	return trace_hash;
}
static void configure_channel(legacy_u32 channel, legacy_u32 variant)
{
	struct AUDIO_CHANNEL *chunk = &audio_channels[channel];
	chunk->channel = channel;
	chunk->note_velocity = 87;
	chunk->volume = 65;
	chunk->driver_channel = channel & 15;
	audio_write_far_pointer((legacy_u8 *)&chunk->cursor, memory_bytes + 100);
	audio_write_far_pointer((legacy_u8 *)&chunk->call_stack[0], memory_bytes + 200);
	audio_write_far_pointer((legacy_u8 *)&chunk->call_stack[1], memory_bytes + 200);
	audio_write_far_pointer((legacy_u8 *)&chunk->return_stack[0], memory_bytes + 200);
	audio_write_far_pointer((legacy_u8 *)&chunk->finish_callback, (void *)finish_callback);
	audio_write_far_pointer((legacy_u8 *)&chunk->instruments, memory_bytes + 1024);
	for (legacy_u32 index = 0; index < 256; index++) {
		audio_write_far_pointer(memory_bytes + 1024 + index * 4, memory_bytes + 3000);
	}
	memory_bytes[3067] = variant & 2 ? 255 : 3;
	chunk->call_depth = variant & 1;
	chunk->stack_depth = (variant >> 1) & 1;
	chunk->loop_counts[0] = (variant >> 2) & 1;
	memory_bytes[200] = 1;
	memory_bytes[201] = AUDIO_SEQUENCE_COMMAND_BASE + AUDIO_SEQUENCE_COMMAND_STOP;
	for (legacy_u32 index = 0; index < 4; index++) {
		dos_audio_contexts[index].channel = index & 1 ? channel : channel + 1;
		dos_audio_contexts[index].state = index % 3;
	}
}
static legacy_u32 command_fingerprint(void)
{
	trace_hash = 2166136261UL;
	struct audio_sequence_event parsed;
	for (legacy_u32 command = 0; command < 256; command++) {
		for (legacy_u32 variant = 0; variant < 16; variant++) {
			reset_audio_fixture();
			legacy_u32 channel = variant & 8 ? 19 : 2;
			dos_audio_uses_direct_channels = variant & 1;
			configure_channel(channel, variant);
			memory_bytes[100] = 0;
			memory_bytes[101] = command;
			memory_bytes[102] = variant * 17;
			memory_bytes[103] = 0x41;
			if (command == AUDIO_SEQUENCE_COMMAND_BASE + AUDIO_SEQUENCE_COMMAND_SEND_DRIVER_DATA ||
				command == AUDIO_SEQUENCE_COMMAND_BASE + AUDIO_SEQUENCE_COMMAND_SKIP_PAYLOAD) {
				memory_bytes[102] = variant & 3;
			}
			if (command == AUDIO_SEQUENCE_COMMAND_BASE + AUDIO_SEQUENCE_COMMAND_CALL) {
				audio_write_far_pointer(memory_bytes + 103, memory_bytes + 196);
			}
			audio_parse_sequence_event(memory_bytes + 100, &parsed);
			memory_bytes[100 + parsed.size] = 1;
			memory_bytes[101 + parsed.size] =
				AUDIO_SEQUENCE_COMMAND_BASE + AUDIO_SEQUENCE_COMMAND_STOP;
			for (legacy_u32 tick = 0; tick < 4; tick++) {
				audio_service_sequence_channel(channel);
				hash_channel_state();
			}
		}
	}
	return trace_hash;
}
static legacy_u32 timer_fingerprint(void)
{
	trace_hash = 2166136261UL;
	for (legacy_u32 sample = 0; sample < 512; sample++) {
		reset_audio_fixture();
		audio_sequence_timer_active = 0;
		segments_match = sample & 1;
		audio_update_lock = (sample >> 1) & 1;
		audio_music_active = (sample >> 2) & 1;
		audio_music_enabled = (sample >> 3) & 1;
		audio_suspended = (sample >> 4) & 1;
		nested_timer = (sample >> 5) & 1;
		if (sample & 64) {
			dos_audio_driver_binary = 0;
		}
		audio_sequence_elapsed_ticks = sample & 128 ? 65500 : 0;
		audio_sequence_tick_period = sample & 256 ? 64 : 200;
		for (legacy_u32 channel = 0; channel < 24; channel++) {
			audio_channels[channel].delay = channel;
		}
		for (legacy_u32 channel = 0; channel < 4; channel++) {
			dos_audio_contexts[channel].state = channel % 3;
			dos_audio_contexts[channel].channel = channel & 1 ? 17 : 2;
		}
		for (legacy_u32 tick = 0; tick < 4; tick++) {
			audio_sequence_timer();
			hash_channel_state();
		}
	}
	return trace_hash;
}
static void test_cursor_wrap(void)
{
	reset_audio_fixture();
	configure_channel(2, 0);
	audio_write_far_pointer((legacy_u8 *)&audio_channels[2].cursor, memory_bytes + 65533);
	memory_bytes[65533] = 0;
	memory_bytes[65534] = 60;
	memory_bytes[65535] = 1;
	memory_bytes[0] = 2;
	memory_bytes[1] = AUDIO_SEQUENCE_COMMAND_BASE + AUDIO_SEQUENCE_COMMAND_STOP;
	audio_service_sequence_channel(2);
	assert(audio_channels[2].cursor.offset == 0 && audio_channels[2].cursor.segment == 0x1000);
	assert(audio_channels[2].delay == 1);
}
static void test_call_return_and_loop_end(void)
{
	reset_audio_fixture();
	configure_channel(2, 0);
	struct AUDIO_CHANNEL *chunk = &audio_channels[2];
	memory_bytes[100] = 0;
	memory_bytes[101] = AUDIO_SEQUENCE_COMMAND_BASE + AUDIO_SEQUENCE_COMMAND_CALL;
	memory_bytes[102] = 0;
	audio_write_far_pointer(memory_bytes + 103, memory_bytes + 196);
	memory_bytes[107] = 2;
	memory_bytes[108] = AUDIO_SEQUENCE_COMMAND_BASE + AUDIO_SEQUENCE_COMMAND_STOP;
	memory_bytes[200] = 0;
	memory_bytes[201] = AUDIO_SEQUENCE_COMMAND_BASE + AUDIO_SEQUENCE_COMMAND_RETURN;
	audio_service_sequence_channel(2);
	assert(chunk->call_depth == 0 && chunk->call_stack[1].offset == 107);
	assert(chunk->cursor.offset == 107 && chunk->delay == 1);
	reset_audio_fixture();
	configure_channel(2, 0);
	chunk = &audio_channels[2];
	chunk->stack_depth = 1;
	chunk->loop_counts[0] = 0;
	memory_bytes[100] = 0;
	memory_bytes[101] = AUDIO_SEQUENCE_COMMAND_BASE + AUDIO_SEQUENCE_COMMAND_LOOP_END;
	audio_service_sequence_channel(2);
	assert(chunk->stack_depth == 0 && chunk->loop_counts[0] == 255);
	assert(chunk->cursor.offset == 200 && chunk->delay == 0);
}

int main(void)
{
	legacy_u32 parse = parse_fingerprint();
	legacy_u32 commands = command_fingerprint();
	legacy_u32 timer = timer_fingerprint();
	test_cursor_wrap();
	test_call_return_and_loop_end();
#ifdef AUDIO_SEQUENCE_BASELINE
	printf("%08" LEGACY_PRIx32 " %08" LEGACY_PRIx32 " %08" LEGACY_PRIx32 "\n", parse, commands,
		   timer);
#else
	assert(parse == 0x7f5c87f2UL);
	assert(commands == 0x67f9d469UL);
	assert(timer == 0x4899a0cdUL);
#endif
	return 0;
}
