/* Check native pointer marshaling and resource traversal independently of host
 * word size and 64 KiB registration-page boundaries. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../c/audio/audio_engine.c"
#include "../c/audio/audio_channels.c"
#include "../c/audio/audio_context.c"
#include "../c/audio/audio_resources.c"

struct AUDIO_CHANNEL audio_channels[AUDIO_CHANNEL_COUNT];

static const legacy_s8 native_ids[AUDIO_ENGINE_RESOURCE_COUNT][5] = {
	"ENGI", "ENGI", "STAR", "STOP", "BLOW", "CRAS", "SKID", "SKI2", "BUMP", "SCRA"};

static void check_native_engine_definition(void)
{
	struct FULL_AUDIO_ENGINE_DEFINITION native = {500, {16, 39, 40, 35}, 0, 0, {0}};
	for (legacy_u16 index = 0; index < AUDIO_ENGINE_RESOURCE_COUNT; ++index) {
		native.resource_ids[index] = native_ids[index];
	}
	struct AUDIO_ENGINE_DEFINITION packed;
	memset(&packed, 0xcd, sizeof(packed));
	audio_copy_engine_definition(&packed, &native);
	assert(packed.sample_count == native.sample_count);
	assert(memcmp(packed.reserved_parameters, native.reserved_parameters, 4) == 0);
	assert(packed.initialized == 0 && packed.reserved_initialization_byte == 0);
	for (legacy_u16 index = 0; index < AUDIO_ENGINE_RESOURCE_COUNT; ++index) {
		assert(dos_memory_make_pointer(packed.resources[index].segment,
									   packed.resources[index].offset) == native_ids[index]);
	}
}

static void check_finish_callback(void)
{
	memset(audio_channels, 0xa7, sizeof(audio_channels));
	audio_set_finish_callback(1, (void *)native_ids[3]);
	assert(audio_read_far_pointer((legacy_u8 *)&audio_channels[1].finish_callback) ==
		   native_ids[3]);
	legacy_u8 *start = (legacy_u8 *)audio_channels;
	legacy_u8 *field = (legacy_u8 *)&audio_channels[1].finish_callback;
	for (size_t index = 0; index < sizeof(audio_channels); ++index) {
		if (start + index < field || start + index >= field + sizeof(struct AUDIO_FAR_POINTER)) {
			assert(start[index] == 0xa7);
		}
	}
	audio_set_finish_callback(1, NULL);
	assert(audio_channels[1].finish_callback.offset == 0);
	assert(audio_channels[1].finish_callback.segment == 0);
	assert(audio_channels[2].cursor.offset == 0xa7a7);
}

static void check_resource_boundaries(void)
{
	/* Two full host pages let identifiers, source bytes and destination bytes
	 * cross the actual memory registry's boundary deterministically. */
	legacy_u8 *allocation = malloc(3UL * 65536UL);
	assert(allocation != NULL);
	memset(allocation, 'Z', 3UL * 65536UL);
	legacy_u8 *page = (legacy_u8 *)(((uintptr_t)allocation + 65535U) & ~(uintptr_t)65535U);
	legacy_s8 *crossing = (legacy_s8 *)(page + 65534U);
	memcpy(crossing, "BASD", 4);
	assert(dos_memory_pointer_offset(crossing) == 65534U);
	assert(dos_memory_make_pointer(dos_memory_pointer_segment(crossing),
								   dos_memory_pointer_offset(crossing)) == crossing);
	assert(audioresource_compare_chunknames(1, crossing, "BASD", 4) == 1);
	assert(audioresource_compare_chunknames(1, "BASD", crossing, 4) == 1);
	assert(audioresource_compare_chunknames(0, crossing, "basd", 4) == 1);
	assert(audioresource_compare_chunknames(1, crossing, "BASS", 4) == 0);

	legacy_u8 *names = page + 65530U;
	memcpy(names, "NOPEBASD", 8);
	assert(audioresource_get_chunk_index(0, 2, "BASD", names) == 1);
	assert(audioresource_get_chunk_index(0, 2, "SNAR", names) == -1);
	memcpy(names, "NOPE--BASD--", 12);
	assert(audioresource_get_chunk_index(2, 2, "BASD", names) == 1);

	/* Move the bank so the header, name table and offset table each cross a
	 * host-page edge. Lookup must return the same chunk in all three cases. */
	for (legacy_u32 shift = 4; shift <= 20; shift += 4) {
		legacy_u8 *bank = page + 65536U - shift;
		LEGACY_WRITE_U32_LE(bank, 30);
		LEGACY_WRITE_U16_LE(bank + 4, 2);
		memcpy(bank + 6, "NOPEBASD", 8);
		LEGACY_WRITE_U32_LE(bank + 14, 0);
		LEGACY_WRITE_U32_LE(bank + 18, 4);
		memcpy(bank + 22, "bad!good", 8);
		assert(audioresource_find(bank, "BASD") == bank + 26);
		assert(audioresource_find(bank, "NOPE") == bank + 22);
		assert(audioresource_find(bank, "SNAR") == NULL);
	}

	legacy_u8 copy[6] = {0x5a, 0, 0, 0, 0, 0x5a};
	memcpy(crossing, "BASD", 4);
	audioresource_copy_n_bytes((legacy_u8 *)crossing, copy + 1, 4);
	assert(copy[0] == 0x5a && copy[5] == 0x5a);
	assert(memcmp(copy + 1, "BASD", 4) == 0);
	audioresource_copy_n_bytes((const legacy_u8 *)"SNAR", (legacy_u8 *)crossing, 4);
	assert(memcmp(crossing, "SNAR", 4) == 0);
	free(allocation);
}

legacy_int main(void)
{
	check_native_engine_definition();
	check_finish_callback();
	check_resource_boundaries();
	puts("Native audio pointers, packed callbacks and resource page boundaries passed");
	return 0;
}
