/* Regression fingerprints captured from the original audio routines before extraction. */
#include "audio-test-support.h"
#ifndef AUDIO_RESOURCES_SOURCE
#define AUDIO_RESOURCES_SOURCE "../c/audio_resources.c"
#endif
#include AUDIO_RESOURCES_SOURCE
#undef memcpy
#undef strlen
#undef printf
static legacy_u32 load_count, driver_variant;
static legacy_u8 driver_channels;
void dos_audio_shutdown(void)
{
	hash_word(20);
}
void add_exit_handler(void(far *handler)(void))
{
	hash_word(21);
	assert(handler == dos_audio_shutdown);
}
void *file_load_binary_nofatal(const legacy_s8 *filename)
{
	hash_word(22);
	hash_bytes(filename, strlen((const legacy_char *)filename));
	load_count++;
	if (load_count == 1) {
		return driver_variant & 1 ? 0 : memory_bytes + 100;
	}
	return driver_variant & 2 ? 0 : memory_bytes + 200;
}
void fatal_error(const legacy_s8 *message, ...)
{
	hash_word(23);
	hash_bytes(message, strlen((const legacy_char *)message));
}
legacy_u8 dos_audio_driver_initialize(void)
{
	hash_word(24);
	return driver_channels;
}
void audio_reset_channels(void)
{
	hash_word(25);
}
void audio_sequence_timer(void)
{
}
void timer_reg_callback(void(far *callback)(void))
{
	hash_word(26);
	assert(callback == audio_sequence_timer);
}
void dos_audio_driver_load_bank(void *bank)
{
	hash_word(27);
	hash_word(dos_memory_pointer_offset(bank));
}
void mmgr_release(void *resource)
{
	hash_word(28);
	hash_word(dos_memory_pointer_offset(resource));
}
void dos_audio_driver_set_master_state(legacy_s16 operation, void *state)
{
	hash_word(29);
	hash_word(operation);
	assert(state == dos_audio_master_state);
}
void audio_stop_music(void)
{
	hash_word(30);
}
void dos_audio_driver_reset(void)
{
	hash_word(31);
}
legacy_s16 audio_sequence_command_has_byte_argument(legacy_u8 command)
{
	return command == 3 || command == 4 || command == 5 || command == 7 || command == 8 ||
		   command == 9 || command == 11 || command == 16 || command == 17;
}
static legacy_u32 driver_fingerprint(void)
{
	trace_hash = 2166136261UL;
	static const legacy_u8 counts[] = {0, 1, 16, 127, 128, 254, 255};
	static const char *names[] = {"adlib", "c:adlib", "c:\\snd\\mt32.drv", "pc", "xx\\snd"};
	for (legacy_u32 variant = 0; variant < 32; variant++) {
		for (legacy_u32 name = 0; name < 5; name++) {
			for (legacy_u32 count = 0; count < 7; count++) {
				reset_audio_fixture();
				load_count = 0;
				driver_variant = variant;
				driver_channels = counts[count];
				dos_audio_special_mode = variant & 4 ? 1 : 0;
				dos_audio_uses_direct_channels = variant & 8 ? 1 : 0;
				dos_audio_master_volume = 42;
				if (variant & 16) {
					dos_audio_driver_binary = 0;
				}
				hash_word(audio_load_dos_driver((const legacy_s8 *)names[name], 0,
												count & 1 ? AUDIO_SPECIAL_DRIVER_MODE : 0));
				hash_word(dos_audio_context_count);
				hash_word(dos_audio_uses_direct_channels);
				hash_word(dos_audio_special_mode);
				hash_word(dos_audio_driver_binary != 0);
				hash_word(audio_music_rate);
				hash_word(audio_effect_rate);
				hash_word(dos_audio_master_volume);
				hash_word(audio_music_enabled);
				hash_word(audio_effects_enabled);
				hash_word(audio_music_active);
				hash_word(audio_suspended);
				hash_bytes(audio_driver_prefix, 3);
			}
		}
	}
	return trace_hash;
}
static void put_byte(legacy_u16 *offset, legacy_u8 value)
{
	memory_bytes[*offset] = value;
	*offset = (legacy_u16)(*offset + 1);
}
static void put_bytes(legacy_u16 offset, const void *source, legacy_u32 length)
{
	const legacy_u8 *bytes = source;
	for (legacy_u32 index = 0; index < length; index++) {
		put_byte(&offset, bytes[index]);
	}
}
static void put_word(legacy_u16 offset, legacy_u16 value)
{
	put_byte(&offset, value);
	put_byte(&offset, value >> 8);
}
static void put_length(legacy_u16 offset, legacy_u16 value)
{
	put_word(offset, value);
	put_word((legacy_u16)(offset + 2), 0);
}
static legacy_u32 sequence_bytes(legacy_u8 *buffer, legacy_u32 command, legacy_u32 variant)
{
	legacy_u32 size = 0;
	if (variant & 1) {
		buffer[size++] = 0x81;
	}
	buffer[size++] = 0;
	buffer[size++] = command;
	if (command < AUDIO_SEQUENCE_COMMAND_BASE || command > AUDIO_SEQUENCE_COMMAND_LAST) {
		if (command >= AUDIO_SEQUENCE_STATUS_BIT) {
			buffer[size++] = 64;
		}
		buffer[size++] = 0x81;
		buffer[size++] = 1;
		return size;
	}
	if (audio_sequence_command_has_byte_argument(command - AUDIO_SEQUENCE_COMMAND_BASE)) {
		buffer[size++] = 23;
		return size;
	}
	switch (command - AUDIO_SEQUENCE_COMMAND_BASE) {
		case AUDIO_SEQUENCE_COMMAND_SET_CONTROL:
		case AUDIO_SEQUENCE_COMMAND_SET_PITCH:
			buffer[size++] = 64;
			buffer[size++] = 127;
			break;
		case AUDIO_SEQUENCE_COMMAND_CALL:
			buffer[size++] = 1;
			memcpy(buffer + size, variant & 2 ? "MISS" : "trk2", 4);
			size += 4;
			break;
		case AUDIO_SEQUENCE_COMMAND_SKIP_PAYLOAD:
		case AUDIO_SEQUENCE_COMMAND_SEND_DRIVER_DATA:
			buffer[size++] = variant & 3;
			for (legacy_u32 index = 0; index < (variant & 3); index++) {
				buffer[size++] = index + 17;
			}
			break;
	}
	return size;
}
static void build_song(legacy_u16 base, legacy_u32 command, legacy_u32 variant)
{
	put_word((legacy_u16)(base + 4), 3);
	put_bytes((legacy_u16)(base + 6), variant & 4 ? "NONEtrk1trk2" : "hdr1trk1trk2", 12);
	put_length((legacy_u16)(base + 18), 256);
	put_length((legacy_u16)(base + 22), 768);
	put_length((legacy_u16)(base + 26), 1536);
	legacy_u16 header = (legacy_u16)(base + 30 + 256);
	put_length(header, 32);
	legacy_u16 offset = (legacy_u16)(header + 6);
	put_byte(&offset, variant % 3);
	for (legacy_u32 index = 0; index < variant % 3; index++) {
		put_bytes(offset, "SNAR", 4);
		offset = (legacy_u16)(offset + 4);
	}
	put_byte(&offset, 2);
	put_bytes(offset, "trk1", 4);
	offset = (legacy_u16)(offset + 5);
	put_bytes(offset, "MISS", 4);
	if (variant & 4) {
		put_length(header, 4);
	}
	legacy_u16 track = (legacy_u16)(base + 30 + 768);
	legacy_u8 bytes[32];
	legacy_u32 length = sequence_bytes(bytes, command, variant);
	put_length(track, 4 + length);
	put_bytes((legacy_u16)(track + 4), bytes, length);
	legacy_u16 end = (legacy_u16)(base + 30 + 1536);
	put_length(end, 6);
	put_word((legacy_u16)(end + 4), (AUDIO_SEQUENCE_COMMAND_BASE + AUDIO_SEQUENCE_COMMAND_STOP)
										<< 8);
}
static legacy_u32 mapping_fingerprint(void)
{
	trace_hash = 2166136261UL;
	static const legacy_u16 offsets[] = {0, 1, 32768, 65500};
	for (legacy_u32 command = 0; command < 256; command++) {
		for (legacy_u32 variant = 0; variant < 8; variant++) {
			reset_audio_fixture();
			build_song(offsets[variant & 3], command, variant);
			audio_map_song_tracks(memory_bytes + offsets[variant & 3]);
			hash_bytes(memory_bytes, 65536);
		}
	}
	return trace_hash;
}
static legacy_u32 finalize_fingerprint(void)
{
	trace_hash = 2166136261UL;
	for (legacy_u32 sample = 0; sample < 256; sample++) {
		reset_audio_fixture();
		memory_bytes[4] = sample & 1;
		memory_bytes[5] = (sample >> 1) & 1;
		memory_bytes[6] = (sample >> 2) & 3;
		memory_bytes[7 + memory_bytes[6] * 4] = sample >> 4;
		load_audio_finalize(sample == 0 ? 0 : memory_bytes);
		hash_word(audio_update_lock);
		hash_word(audio_sequence_elapsed_ticks);
		hash_word(audio_sequence_tick_period);
		hash_word(audio_music_channel_count);
		hash_word(audio_music_active);
	}
	return trace_hash;
}
static void test_song_reference_mapping(void)
{
	reset_audio_fixture();
	legacy_u16 base = 65500;
	build_song(base, AUDIO_SEQUENCE_COMMAND_BASE + AUDIO_SEQUENCE_COMMAND_CALL, 0);
	legacy_u16 header = (legacy_u16)(base + 30 + 256);
	legacy_u16 track = (legacy_u16)(base + 30 + 768);
	legacy_u16 target = (legacy_u16)(base + 30 + 1536);
	audio_map_song_tracks(memory_bytes + base);
	assert(LEGACY_READ_U16_LE(memory_bytes + header + 8) == track);
	assert(LEGACY_READ_U16_LE(memory_bytes + header + 10) == 0x1000);
	assert(memcmp(memory_bytes + header + 13, "MISS", 4) == 0);
	assert(LEGACY_READ_U16_LE(memory_bytes + track + 7) == target);
	assert(LEGACY_READ_U16_LE(memory_bytes + track + 9) == 0x1000);
}

static void build_resource_reference(legacy_u16 base, const char *name, legacy_u16 target)
{
	put_word((legacy_u16)(base + 4), 1);
	put_bytes((legacy_u16)(base + 6), name, 4);
	put_length((legacy_u16)(base + 10), (legacy_u16)(target - base - 14));
}

static void test_closed_hihat_offset_mapping(void)
{
	static const legacy_u16 offsets[] = {0, 1, 0x1234, 0x8000, 0xabcd, 0xffff};
	for (legacy_u32 index = 0; index < sizeof(offsets) / sizeof(offsets[0]); index++) {
		reset_audio_fixture();
		build_song(16384, AUDIO_SEQUENCE_COMMAND_BASE + AUDIO_SEQUENCE_COMMAND_STOP, 0);
		build_resource_reference(4096, "CHHT", offsets[index]);
		legacy_closed_hihat_offset = (legacy_u16)~offsets[index];
		audio_map_song_instruments(memory_bytes + 16384, memory_bytes + 4096);
		/* An offset of zero can still identify a non-null segmented pointer. */
		assert(audio_closed_hihat_resource == memory_bytes + offsets[index]);
		assert(legacy_closed_hihat_offset == offsets[index]);
	}
}

static void test_closed_hihat_offset_lifetime(void)
{
	reset_audio_fixture();
	build_song(16384, AUDIO_SEQUENCE_COMMAND_BASE + AUDIO_SEQUENCE_COMMAND_STOP, 0);
	build_resource_reference(8192, "song", 16384);
	build_resource_reference(4096, "CHHT", 0x8123);
	void *header = init_audio_resources(memory_bytes + 8192, memory_bytes + 4096, "song");
	assert(header == memory_bytes + 16384 + 30 + 256);
	assert(audio_closed_hihat_resource == memory_bytes + 0x8123);
	assert(legacy_closed_hihat_offset == 0x8123);

	/* Cached songs restore percussion without changing the historical physics offset. */
	build_resource_reference(4096, "CHHT", 0x9234);
	assert(init_audio_resources(memory_bytes + 8192, memory_bytes + 4096, "song") == header);
	assert(audio_closed_hihat_resource == memory_bytes + 0x9234);
	assert(legacy_closed_hihat_offset == 0x8123);

	/* A missing song header leaves the previous instrument mapping intact. */
	put_bytes(16384 + 6, "NONE", 4);
	audio_map_song_instruments(memory_bytes + 16384, memory_bytes + 4096);
	assert(audio_closed_hihat_resource == memory_bytes + 0x9234);
	assert(legacy_closed_hihat_offset == 0x8123);
	assert(init_audio_resources(memory_bytes + 8192, memory_bytes + 4096, "song") == 0);
	assert(legacy_closed_hihat_offset == 0x8123);

	/* A valid song without CHHT replaces both the pointer and its saved offset. */
	put_bytes(16384 + 6, "hdr1", 4);
	build_resource_reference(4096, "MISS", 0x9234);
	audio_map_song_instruments(memory_bytes + 16384, memory_bytes + 4096);
	assert(audio_closed_hihat_resource == 0);
	assert(legacy_closed_hihat_offset == 0);

	build_resource_reference(4096, "CHHT", 0x9234);
	audio_map_song_instruments(memory_bytes + 16384, memory_bytes + 4096);
	assert(audio_closed_hihat_resource == memory_bytes + 0x9234);
	assert(legacy_closed_hihat_offset == 0x9234);
}

static void build_percussion_bank(legacy_u16 base)
{
	static const char names[] = "BASDSNARTOMMRIDECRSHCHHTOHHT";
	put_word((legacy_u16)(base + 4), 7);
	put_bytes((legacy_u16)(base + 6), names, 28);
	for (legacy_u32 index = 0; index < 7; index++) {
		put_length((legacy_u16)(base + 34 + index * 4), index * 64);
	}
}

static void assert_percussion_bank(legacy_u16 base)
{
	assert(audio_bass_drum_resource == memory_bytes + base + 62);
	assert(audio_snare_resource == memory_bytes + base + 126);
	assert(audio_tom_resource == memory_bytes + base + 190);
	assert(audio_ride_resource == memory_bytes + base + 254);
	assert(audio_crash_resource == memory_bytes + base + 318);
	assert(audio_closed_hihat_resource == memory_bytes + base + 382);
	assert(audio_open_hihat_resource == memory_bytes + base + 446);
}

static void test_cached_music_after_sound_effects(void)
{
	reset_audio_fixture();
	build_song(16384, AUDIO_SEQUENCE_COMMAND_BASE + AUDIO_SEQUENCE_COMMAND_CALL, 1);
	build_resource_reference(8192, "song", 16384);
	build_percussion_bank(4096);
	void *header = init_audio_resources(memory_bytes + 8192, memory_bytes + 4096, "song");
	assert(header != 0);
	assert_percussion_bank(4096);
	assert(legacy_closed_hihat_offset == 4478);
	legacy_u8 mapped_song[2048];
	memcpy(mapped_song, memory_bytes + 16384, sizeof(mapped_song));

	/* Car sounds use the same mapper but have no percussion instruments. */
	build_song(32768, AUDIO_SEQUENCE_COMMAND_BASE + AUDIO_SEQUENCE_COMMAND_STOP, 0);
	build_resource_reference(12288, "sfx", 32768);
	build_resource_reference(2048, "ENGI", 3072);
	for (legacy_u32 replay = 0; replay < 3; replay++) {
		assert(init_audio_resources(memory_bytes + 12288, memory_bytes + 2048, "sfx") != 0);
		assert(audio_bass_drum_resource == 0);
		assert(audio_snare_resource == 0);
		assert(audio_tom_resource == 0);
		assert(audio_ride_resource == 0);
		assert(audio_crash_resource == 0);
		assert(audio_closed_hihat_resource == 0);
		assert(audio_open_hihat_resource == 0);

		/* Returning to cached music must restore every percussion instrument,
		 * while keeping the already resolved melodic and track pointers intact. */
		assert(init_audio_resources(memory_bytes + 8192, memory_bytes + 4096, "song") == header);
		assert_percussion_bank(4096);
		assert(legacy_closed_hihat_offset == 0);
		assert(memcmp(mapped_song, memory_bytes + 16384, sizeof(mapped_song)) == 0);
	}
}

int main(void)
{
	legacy_u32 driver = driver_fingerprint();
	legacy_u32 mapping = mapping_fingerprint();
	legacy_u32 finalize = finalize_fingerprint();
	test_song_reference_mapping();
	test_closed_hihat_offset_mapping();
	test_closed_hihat_offset_lifetime();
	test_cached_music_after_sound_effects();
#ifdef AUDIO_RESOURCES_BASELINE
	printf("%08" LEGACY_PRIx32 " %08" LEGACY_PRIx32 " %08" LEGACY_PRIx32 "\n", driver, mapping,
		   finalize);
#else
	assert(driver == 0x1bed120dUL);
	assert(mapping == 0xe9f082d5UL);
	assert(finalize == 0x8530e745UL);
#endif
	return 0;
}
