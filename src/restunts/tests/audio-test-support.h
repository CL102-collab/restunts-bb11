/* Host fixtures model stored 16:16 pointers independently of native pointer width. */
#ifndef RESTUNTS_TEST_AUDIO_SUPPORT_H
#define RESTUNTS_TEST_AUDIO_SUPPORT_H
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "../c/audio/audio_internal.h"
#include "../c/platform.h"

static legacy_u32 trace_hash;
static legacy_u8 memory_bytes[131328];
static const void *external_pointers[128];
static legacy_u32 external_count;
static legacy_u8 segments_match = 1, nested_timer;
struct AUDIO_CHANNEL audio_channels[AUDIO_CHANNEL_COUNT];
struct AUDIO_CONTEXT dos_audio_contexts[AUDIO_CONTEXT_COUNT];
legacy_u8 audio_channel_reserved[AUDIO_CHANNEL_COUNT], dos_audio_driver_data[256];
legacy_s8 audio_music_enabled, audio_effects_enabled;
legacy_s16 audio_update_lock;
legacy_u8 dos_audio_special_mode, dos_audio_context_count, dos_audio_uses_direct_channels;
legacy_u8 dos_audio_master_state[16], dos_audio_master_volume;
void *dos_audio_driver_binary;
legacy_u8 audio_music_rate, audio_music_channel_count, audio_suspended, audio_music_active,
	audio_effect_rate;
legacy_u16 audio_sequence_elapsed_ticks, audio_sequence_tick_period;
void *audio_bass_drum_resource, *audio_snare_resource, *audio_tom_resource, *audio_ride_resource;
void *audio_crash_resource, *audio_closed_hihat_resource, *audio_open_hihat_resource;
legacy_u16 legacy_closed_hihat_offset;

static void hash_word(legacy_u16 word)
{
	trace_hash = (trace_hash ^ word) * 16777619UL;
}
static void hash_bytes(const void *data, legacy_u32 length)
{
	const legacy_u8 *bytes = data;
	for (legacy_u32 index = 0; index < length; index++) {
		hash_word(bytes[index]);
	}
}
static void finish_callback(legacy_s16 channel)
{
	hash_word(90);
	hash_word(channel);
}
static void *pointer_at(legacy_u16 segment, legacy_u16 offset)
{
	if (segment == 0 && offset == 0) {
		return 0;
	}
	if (segment == 0x7000) {
		return (void *)finish_callback;
	}
	if (segment >= 0x8000) {
		assert((legacy_u32)(segment - 0x8000) < external_count);
		return (legacy_u8 *)external_pointers[segment - 0x8000] + offset;
	}
	assert(segment == 0x1000 || segment == 0x2000);
	return memory_bytes + (segment == 0x1000 ? 0 : 65536) + offset;
}
void *dos_memory_make_pointer(legacy_u16 segment, legacy_u16 offset)
{
	return pointer_at(segment, offset);
}
legacy_u16 dos_memory_pointer_segment(const void *pointer)
{
	uintptr_t address = (uintptr_t)pointer;
	if (pointer == 0) {
		return 0;
	}
	if (pointer == (void *)finish_callback) {
		return 0x7000;
	}
	if (address >= (uintptr_t)memory_bytes && address < (uintptr_t)(memory_bytes + 131072)) {
		return address - (uintptr_t)memory_bytes < 65536 ? 0x1000 : 0x2000;
	}
	for (legacy_u32 index = 0; index < external_count; index++) {
		if (external_pointers[index] == pointer) {
			return 0x8000 + index;
		}
	}
	assert(external_count < 128);
	external_pointers[external_count] = pointer;
	return 0x8000 + external_count++;
}
legacy_u16 dos_memory_pointer_offset(const void *pointer)
{
	uintptr_t address = (uintptr_t)pointer;
	if (address >= (uintptr_t)memory_bytes && address < (uintptr_t)(memory_bytes + 131072)) {
		return (legacy_u16)(address - (uintptr_t)memory_bytes);
	}
	return 0;
}
void *audio_read_far_pointer(const legacy_u8 *source)
{
	return pointer_at(LEGACY_READ_U16_LE(source + 2), LEGACY_READ_U16_LE(source));
}
void audio_write_far_pointer(legacy_u8 *destination, const void *pointer)
{
	legacy_u16 offset = dos_memory_pointer_offset(pointer);
	legacy_u16 segment = dos_memory_pointer_segment(pointer);
	destination[0] = offset;
	destination[1] = offset >> 8;
	destination[2] = segment;
	destination[3] = segment >> 8;
}
legacy_s16 dos_data_stack_segments_match(void)
{
	hash_word(91);
	return segments_match;
}
void audio_release_channel_range(legacy_s16 first, legacy_s16 last)
{
	hash_word(1);
	hash_word(first);
	hash_word(last);
}
void audio_init_channel_range(legacy_s16 first, legacy_s16 last, void *resource, legacy_u16 offset,
							  legacy_u16 rate, legacy_u8 priority)
{
	hash_word(2);
	hash_word(first);
	hash_word(last);
	hash_word(dos_memory_pointer_offset(resource));
	hash_word(offset);
	hash_word(rate);
	hash_word(priority);
}
void dos_audio_set_channel_volume(legacy_s16 channel, legacy_s16 volume)
{
	hash_word(3);
	hash_word(channel);
	hash_word(volume);
	audio_channels[channel].volume = volume;
}
void dos_audio_driver_prepare_context(legacy_s16 channel, struct AUDIO_CONTEXT *context,
									  legacy_u8 *timer, void *resource)
{
	hash_word(4);
	hash_word(channel);
	hash_word(context != 0);
	hash_word((struct AUDIO_CHANNEL *)timer - audio_channels);
	hash_word(dos_memory_pointer_offset(resource));
}
void dos_audio_driver_set_control(legacy_s16 channel, struct AUDIO_CONTEXT *context,
								  legacy_u16 control, legacy_u16 value)
{
	hash_word(5);
	hash_word(channel);
	hash_word(context != 0);
	hash_word(control);
	hash_word(value);
}
void dos_audio_driver_set_pitch(legacy_u8 *timer, legacy_s16 pitch, legacy_s16 channel)
{
	hash_word(6);
	hash_word((struct AUDIO_CHANNEL *)timer - audio_channels);
	hash_word(pitch);
	hash_word(channel);
}
void dos_audio_driver_send_data(legacy_u16 length, legacy_u8 *data)
{
	hash_word(7);
	hash_word(length);
	hash_bytes(data, 256);
}
static void reset_audio_fixture(void)
{
	memset(memory_bytes, 0, sizeof(memory_bytes));
	memset(audio_channels, 0, sizeof(audio_channels));
	memset(dos_audio_contexts, 0, sizeof(dos_audio_contexts));
	memset(audio_channel_reserved, 0, sizeof(audio_channel_reserved));
	memset(dos_audio_driver_data, 0x5a, sizeof(dos_audio_driver_data));
	external_count = 0;
	audio_update_lock = 0;
	audio_closed_hihat_resource = 0;
	legacy_closed_hihat_offset = 0;
	segments_match = 1;
	nested_timer = 0;
	dos_audio_driver_binary = memory_bytes;
	dos_audio_uses_direct_channels = 0;
	dos_audio_context_count = 4;
	audio_music_enabled = audio_effects_enabled = 1;
	audio_suspended = audio_music_active = 0;
	audio_sequence_elapsed_ticks = 0;
	audio_sequence_tick_period = 128;
	audio_music_channel_count = 2;
	audio_effect_rate = audio_music_rate = 127;
}

#endif
