#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../c/fileio.h"
#include "../c/memmgr.h"
#include "../c/platform.h"
#include "../c/fatal.h"
#include "../c/resource.h"

legacy_u32 file_decomp_vle(legacy_u8 huge *src, legacy_u8 huge *dst, legacy_u16 paragraphs);
legacy_u32 file_decomp_rle(legacy_u8 huge *src, legacy_u8 huge *dst, legacy_u16 paragraphs);
legacy_u32 file_decomp_rle_seq(legacy_u8 huge *src, legacy_u8 huge *dst, legacy_u32 length,
							   legacy_u8 escape);
legacy_u32 file_decomp_rle_single(legacy_u8 huge *src, legacy_u8 huge *dst, legacy_u32 length,
								  legacy_u8 *escapes);

static legacy_u8 memory[0x100000];
static legacy_u8 packed[200000];
static legacy_u8 expected[70000];
static legacy_u8 stage[200000];
static legacy_u8 file_bytes[200000];
static legacy_u32 file_length, file_position;
static legacy_u32 packed_open_calls;
static legacy_u32 open_calls, close_calls, read_calls, resize_calls, copy_calls, fatal_calls;
static legacy_u32 fail_open, fail_read, cached;
static legacy_u32 allocated_paragraphs, resized_paragraphs;
static legacy_u32 lookup_calls, release_calls, resource_file_kind, fail_read_after;
static legacy_u64 trace_hash = UINT64_C(1469598103934665603);

static void trace_word(legacy_u32 value)
{
	trace_hash = (trace_hash ^ (value & 255U)) * UINT64_C(1099511628211);
	trace_hash = (trace_hash ^ ((value >> 8) & 255U)) * UINT64_C(1099511628211);
}
static void trace_bytes(const legacy_u8 *bytes, legacy_u32 count)
{
	for (legacy_u32 i = 0; i < count; i++) {
		trace_hash = (trace_hash ^ bytes[i]) * UINT64_C(1099511628211);
	}
}
static void check_hash(const legacy_char *name, legacy_u64 expected_hash)
{
#ifdef FILE_RECORD_BASELINE
	printf("%s %016" LEGACY_PRIx64 "\n", name, trace_hash);
	(void)expected_hash;
#else
	if (trace_hash != expected_hash) {
		fprintf(stderr, "%s: got %016" LEGACY_PRIx64 " expected %016" LEGACY_PRIx64 "\n", name,
				trace_hash, expected_hash);
		assert(trace_hash == expected_hash);
	}
#endif
	trace_hash = UINT64_C(1469598103934665603);
}

void far *dos_memory_make_pointer(legacy_u16 segment, legacy_u16 offset)
{
	legacy_u32 address = (legacy_u32)segment * 16U + offset;
	assert(address < sizeof(memory));
	return memory + address;
}
legacy_u16 dos_memory_pointer_segment(const void far *pointer)
{
	if (pointer == 0) {
		return 0;
	}
	assert((const legacy_u8 *)pointer >= memory &&
		   (const legacy_u8 *)pointer < memory + sizeof(memory));
	return ((const legacy_u8 *)pointer - memory) >> 4;
}
legacy_u16 dos_memory_pointer_offset(const void far *pointer)
{
	assert((const legacy_u8 *)pointer >= memory &&
		   (const legacy_u8 *)pointer < memory + sizeof(memory));
	return ((const legacy_u8 *)pointer - memory) & 15U;
}
void copy_paras_reverse(legacy_u16 source, legacy_u16 destination, legacy_s16 paragraphs)
{
	trace_word(1);
	trace_word(source);
	trace_word(destination);
	trace_word(paragraphs);
	copy_calls++;
	assert(paragraphs >= 0);
	memmove(memory + (legacy_u32)destination * 16U, memory + (legacy_u32)source * 16U,
			(legacy_u32)paragraphs * 16U);
}
void far *mmgr_get_chunk_by_name(const legacy_s8 *name)
{
	(void)name;
	trace_word(2);
	lookup_calls++;
	return cached ? memory + 0x20000 : 0;
}
void far *mmgr_alloc_pages(const legacy_s8 *name, legacy_u16 paragraphs)
{
	(void)name;
	trace_word(3);
	trace_word(paragraphs);
	allocated_paragraphs = paragraphs;
	return memory + 0x20000;
}
void mmgr_release(void far *pointer)
{
	assert(pointer == memory + 0x20000);
	release_calls++;
}
legacy_u16 mmgr_resize_memory(legacy_u16 offset, legacy_u16 segment, legacy_u16 paragraphs)
{
	trace_word(4);
	trace_word(offset);
	trace_word(segment);
	trace_word(paragraphs);
	resize_calls++;
	resized_paragraphs = paragraphs;
	return paragraphs;
}
legacy_u16 dos_file_open(const legacy_s8 *name, legacy_s16 create)
{
	(void)name;
	trace_word(5);
	trace_word(create);
	open_calls++;
	if (strstr((const legacy_char *)name, ".pre") != 0) {
		packed_open_calls++;
	}
	file_position = 0;
	if (resource_file_kind == 1 && strstr((const legacy_char *)name, ".res") != 0) {
		return 0;
	}
	if (resource_file_kind == 2 && strstr((const legacy_char *)name, ".pre") != 0) {
		return 0;
	}
	return fail_open ? 0 : 1;
}
legacy_s16 dos_file_close(legacy_u16 handle)
{
	trace_word(6);
	trace_word(handle);
	close_calls++;
	return 0;
}
legacy_u16 dos_file_read(legacy_u16 handle, void far *destination, legacy_u16 length)
{
	legacy_u32 count = length;
	trace_word(7);
	trace_word(handle);
	trace_word(length);
	read_calls++;
	if (count > file_length - file_position) {
		count = file_length - file_position;
	}
	memcpy(destination, file_bytes + file_position, count);
	file_position += count;
	return count;
}
legacy_s16 dos_file_seek(legacy_u16 handle, legacy_s32 offset, legacy_s16 origin)
{
	trace_word(8);
	trace_word(handle);
	trace_word(offset);
	trace_word(origin);
	assert((origin == DOS_FILE_SEEK_END || origin == DOS_FILE_SEEK_BEGIN) && offset == 0);
	file_position = origin == DOS_FILE_SEEK_END ? file_length : 0;
	return 0;
}
legacy_s32 dos_file_tell(legacy_u16 handle)
{
	trace_word(9);
	trace_word(handle);
	return file_position;
}
legacy_s16 dos_file_error(void)
{
	trace_word(10);
	return fail_read || (fail_read_after && read_calls >= fail_read_after);
}
void fatal_error(const legacy_s8 *format, ...)
{
	(void)format;
	trace_word(11);
	fatal_calls++;
}

static void write_size(legacy_u8 *bytes, legacy_u32 length)
{
	bytes[0] = length;
	bytes[1] = length >> 8;
	bytes[2] = length >> 16;
}

/* Encode canonical prefix codes independently of the production lookup tables. */
static legacy_u32 make_vle(legacy_u8 *destination, const legacy_u8 *counts, legacy_u32 depth,
						   legacy_u32 length, legacy_u32 additive, legacy_u32 scenario)
{
	destination[0] = 2;
	write_size(destination + 1, length);
	destination[4] = depth | (additive ? 128 : 0);
	legacy_u8 alphabet[256];
	legacy_u32 codes[256];
	legacy_u32 widths[256];
	legacy_u32 alphabet_length = 0;
	legacy_u32 code = 0;
	for (legacy_u32 width = 1; width <= depth; width++) {
		destination[4 + width] = counts[width - 1];
		for (legacy_u32 i = 0; i < counts[width - 1]; i++) {
			codes[alphabet_length] = code++;
			widths[alphabet_length] = width;
			alphabet[alphabet_length] = (legacy_u8)(alphabet_length * 19U + scenario * 17U + 123U);
			alphabet_length++;
		}
		code *= 2;
	}
	memcpy(destination + 5 + depth, alphabet, alphabet_length);
	legacy_u32 data_offset = 5 + depth + alphabet_length;
	memset(destination + data_offset, 0, (length + 1) * 2 + 4);
	legacy_u8 value = 0;
	legacy_u32 bit_count = 0;
	for (legacy_u32 i = 0; i <= length; i++) {
		legacy_u32 symbol = (i * 47U + scenario) % alphabet_length;
		if (additive) {
			value = (legacy_u8)(value + alphabet[symbol]);
		} else {
			value = alphabet[symbol];
		}
		expected[i] = value;
		for (legacy_u32 j = widths[symbol]; j > 0; j--) {
			legacy_u32 bit = (codes[symbol] >> (j - 1)) & 1U;
			destination[data_offset + bit_count / 8] |= bit << (7 - bit_count % 8);
			bit_count++;
		}
	}
	return data_offset + (bit_count + 7) / 8 + 4;
}

static void test_vle(void)
{
	static const legacy_u32 lengths[] = {0, 1, 7, 15, 31, 257, 65537};
	legacy_u8 counts[16];
	for (legacy_u32 layout = 0; layout < 5; layout++) {
		memset(counts, 0, sizeof(counts));
		if (layout == 0) {
			counts[0] = 2;
		}
		legacy_u32 depth = 1;
		if (layout == 1) {
			depth = 4;
			counts[0] = 1;
			counts[1] = 1;
			counts[2] = 1;
			counts[3] = 2;
		}
		if (layout == 2) {
			depth = 9;
			for (legacy_u32 i = 0; i < 8; i++) {
				counts[i] = 1;
			}
			counts[8] = 2;
		}
		if (layout == 3) {
			depth = 16;
			for (legacy_u32 i = 0; i < 16; i++) {
				counts[i] = 1;
			}
		}
		if (layout == 4) {
			depth = 9;
			counts[7] = 254;
			counts[8] = 2;
		}
		for (legacy_u32 l = 0; l < sizeof(lengths) / sizeof(lengths[0]); l++) {
			for (legacy_u32 additive = 0; additive < 2; additive++) {
				for (legacy_u32 scenario = 0; scenario < 3; scenario++) {
					trace_word(layout);
					trace_word(lengths[l]);
					trace_word(lengths[l] >> 16);
					trace_word(additive);
					trace_word(scenario);
					legacy_u32 size =
						make_vle(packed, counts, depth, lengths[l], additive, scenario);
					memset(memory, 0xa5, sizeof(memory));
					memcpy(memory + 0x5fffb, packed, size);
					legacy_u32 result = file_decomp_vle(memory + 0x5fffb, memory + 0x1fffd, 0xffff);
					assert(result == lengths[l]);
					assert(memcmp(memory + 0x1fffd, expected, lengths[l] + 1) == 0);
					assert(memory[0x1fffc] == 0xa5 && memory[0x1fffd + lengths[l] + 1] == 0xa5);
					trace_bytes(memory + 0x1fffc, lengths[l] + 3);
				}
			}
		}
	}
	check_hash("VLE", UINT64_C(0xf04d3106ec426067));
}

static legacy_u32 make_rle_literals(legacy_u8 *destination, const legacy_u8 *source,
									legacy_u32 length)
{
	destination[0] = 1;
	write_size(destination + 1, length);
	destination[7] = 0;
	destination[8] = 129;
	destination[9] = 0xe0;
	legacy_u32 cursor = 10;
	for (legacy_u32 i = 0; i < length; i++) {
		if (source[i] == 0xe0) {
			destination[cursor++] = 0xe0;
			destination[cursor++] = 1;
		}
		destination[cursor++] = source[i];
	}
	write_size(destination + 4, cursor - 10);
	return cursor;
}

static void reset_file(void)
{
	memset(memory, 0xa5, sizeof(memory));
	open_calls = 0;
	close_calls = 0;
	read_calls = 0;
	resize_calls = 0;
	copy_calls = 0;
	fatal_calls = 0;
	fail_open = 0;
	fail_read = 0;
	cached = 0;
	allocated_paragraphs = 0;
	resized_paragraphs = 0;
	lookup_calls = 0;
	release_calls = 0;
	resource_file_kind = 0;
	fail_read_after = 0;
}

static void test_file_passes(void)
{
	for (legacy_u32 passes = 1; passes <= 3; passes++) {
		for (legacy_u32 scenario = 0; scenario < 8; scenario++) {
			reset_file();
			legacy_u32 result_size = scenario % 2 ? 33 : 16;
			for (legacy_u32 i = 0; i < result_size; i++) {
				expected[i] = (legacy_u8)(scenario + i * 7U);
			}
			legacy_u32 size = make_rle_literals(packed, expected, result_size);
			for (legacy_u32 i = 1; i < passes; i++) {
				memcpy(stage, packed, size);
				size = make_rle_literals(packed, stage, size);
			}
			if (passes > 1) {
				file_bytes[0] = 128 | passes;
				write_size(file_bytes + 1, result_size);
				memcpy(file_bytes + 4, packed, size);
				file_length = size + 4;
			} else {
				memcpy(file_bytes, packed, size);
				file_length = size;
			}
			if (scenario == 2) {
				cached = 1;
			}
			if (scenario == 3) {
				fail_open = 1;
			}
			if (scenario == 4) {
				fail_read = 1;
			}
			if (scenario == 5) {
				file_bytes[passes > 1 ? 4 : 0] = 3;
			}
			if (scenario == 6) {
				file_bytes[0] = 128;
			}
			if (scenario == 7) {
				write_size(file_bytes + 1, 0);
			}
			trace_word(passes);
			trace_word(scenario);
			void *result = file_decomp("TEST.PVS", scenario % 2);
			if (scenario < 2) {
				assert(result == memory + 0x20000);
				assert(memcmp(result, expected, result_size) == 0);
				assert(resize_calls == 1 && resized_paragraphs == (result_size + 15) / 16);
				assert(copy_calls == passes - 1);
			}
			if (cached) {
				assert(open_calls == 0 && result == memory + 0x20000);
			}
			if (scenario == 3 || scenario == 4 || scenario == 5 || scenario == 7) {
				assert(result == 0);
			}
			trace_word(result != 0);
			trace_word(open_calls);
			trace_word(close_calls);
			trace_word(read_calls);
			trace_word(resize_calls);
			trace_word(copy_calls);
			trace_word(fatal_calls);
			trace_word(allocated_paragraphs);
			trace_word(resized_paragraphs);
			trace_bytes(memory + 0x20000, 128);
		}
	}
	check_hash("file passes", UINT64_C(0x30b187c9d0cfa23c));
}

static void test_file_vle(void)
{
	legacy_u8 counts[1] = {2};
	static const legacy_u32 lengths[] = {1, 15, 257, 65537};
	for (legacy_u32 i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
		for (legacy_u32 additive = 0; additive < 2; additive++) {
			reset_file();
			file_length = make_vle(file_bytes, counts, 1, lengths[i], additive, i);
			trace_word(lengths[i]);
			trace_word(lengths[i] >> 16);
			trace_word(additive);
			void *result = file_decomp("VLE.PVS", 0);
			assert(result == memory + 0x20000);
			assert(memcmp(result, expected, lengths[i] + 1) == 0);
			assert(resize_calls == 1 && resized_paragraphs == (lengths[i] + 15) / 16);
			trace_bytes(result, lengths[i] + 2);
		}
	}
	check_hash("VLE files", UINT64_C(0x91345b9f2b17be5c));
}

static legacy_u32 make_vle_bytes(legacy_u8 *destination, const legacy_u8 *source, legacy_u32 length,
								 legacy_u32 additive)
{
	legacy_u8 deltas[256];
	assert(length <= sizeof(deltas));
	legacy_u32 j;
	legacy_u8 alphabet[256];
	legacy_u8 previous = 0;
	legacy_u32 alphabet_length = 0;
	for (legacy_u32 i = 0; i < length; i++) {
		legacy_u8 value = additive ? (legacy_u8)(source[i] - previous) : source[i];
		previous = source[i];
		for (j = 0; j < alphabet_length && alphabet[j] != value; j++) {
		}
		if (j == alphabet_length) {
			alphabet[alphabet_length++] = value;
		}
		deltas[i] = j;
	}
	assert(alphabet_length < 256);
	destination[0] = 2;
	write_size(destination + 1, length - 1);
	destination[4] = 8 | (additive ? 128 : 0);
	memset(destination + 5, 0, 8);
	destination[12] = alphabet_length;
	memcpy(destination + 13, alphabet, alphabet_length);
	memcpy(destination + 13 + alphabet_length, deltas, length);
	memset(destination + 13 + alphabet_length + length, 0, 4);
	return 17 + alphabet_length + length;
}

static void test_mixed_passes(void)
{
	for (legacy_u32 order = 0; order < 2; order++) {
		for (legacy_u32 additive = 0; additive < 2; additive++) {
			reset_file();
			for (legacy_u32 i = 0; i < 16; i++) {
				expected[i] = (legacy_u8)(i * 17 + additive * 13);
			}
			legacy_u32 size;
			if (order == 0) {
				size = make_rle_literals(stage, expected, 16);
				size = make_vle_bytes(packed, stage, size, additive);
			} else {
				size = make_vle_bytes(stage, expected, 16, additive);
				size = make_rle_literals(packed, stage, size);
			}
			file_bytes[0] = 130;
			write_size(file_bytes + 1, 16 - order);
			memcpy(file_bytes + 4, packed, size);
			file_length = size + 4;
			trace_word(order);
			trace_word(additive);
			void *result = file_decomp("MIXED.PVS", 0);
			assert(result == memory + 0x20000);
			assert(memcmp(result, expected, 16) == 0);
			assert(copy_calls == 1 && resize_calls == 1 && resized_paragraphs == 1);
			trace_bytes(memory + 0x20000, 80);
		}
	}
	check_hash("mixed passes", UINT64_C(0x00951c7e63501a81));
}

static void test_rle_passes(void)
{
	static const legacy_u8 escape_flags[] = {2, 3, 128, 129, 131};
	for (legacy_u32 i = 0; i < sizeof(escape_flags); i++) {
		for (legacy_u32 scenario = 0; scenario < 3; scenario++) {
			reset_file();
			memset(packed, 0, sizeof(packed));
			packed[0] = 1;
			legacy_u32 length = 5;
			write_size(packed + 1, length);
			packed[8] = escape_flags[i];
			packed[9] = 0xe0;
			packed[10] = 0xe1;
			packed[11] = 0xe2;
			if (escape_flags[i] == 128) {
				/* Zero declared escapes still enables the sequence pass. */
				packed[9] = 'A';
				packed[10] = 0xe1;
				packed[11] = 'B';
				packed[12] = 0xe1;
				packed[13] = 4;
				write_size(packed + 4, 5);
			} else if (escape_flags[i] <= 3) {
				legacy_u32 start = 9 + escape_flags[i];
				packed[start] = 0xe1;
				packed[start + 1] = 'A';
				packed[start + 2] = 0xe1;
				packed[start + 3] = 5;
				write_size(packed + 4, 4);
			} else {
				legacy_u32 start = 9 + (escape_flags[i] & 127);
				packed[start] = 0xe0;
				packed[start + 1] = 5 + scenario;
				packed[start + 2] = 'Z';
				write_size(packed + 4, 3);
			}
			memcpy(memory + 0x5fff9, packed, 32);
			legacy_u32 result = file_decomp_rle(memory + 0x5fff9, memory + 0x20000, 16);
			assert(result == length);
			trace_word(escape_flags[i]);
			trace_word(scenario);
			trace_word(result);
			trace_bytes(memory + 0x20000, 32);
		}
	}
	check_hash("RLE passes", UINT64_C(0x861032445c2d38b5));
}

/* Model the original workspace positions independently of the decoder. The
 * loaded snapshot retains these bytes even when the extra storage is larger
 * than the original four workspace paragraphs. */
static legacy_u32 prepare_tail_model(legacy_u32 result_size, legacy_u32 tail_bytes)
{
	legacy_u32 rounded_size = (result_size + 15) / 16 * 16;
	legacy_u32 original_workspace = rounded_size + 64;
	legacy_u32 retained_size = rounded_size + (tail_bytes + 15) / 16 * 16;
	legacy_u32 source = original_workspace - (file_length + 15) / 16 * 16;
	memset(stage, 0, retained_size);
	memcpy(stage + source, file_bytes, file_length);
	return retained_size;
}

static void check_tail_snapshot(legacy_u32 retained_size)
{
	resource_file_kind = 1;
	cached = 1;
	legacy_u32 prior_lookups = lookup_calls;
	legacy_u8 *result = file_load_resfile_with_tail("SNAPSHOT", 256);
	assert(result == memory + 0x20000);
	assert(lookup_calls == prior_lookups);
	assert(allocated_paragraphs * 16 == retained_size);
	assert(resized_paragraphs * 16 == retained_size);
	assert(memcmp(result, stage, retained_size) == 0);
	assert(result[retained_size] == 0xa5);
	mmgr_release(result);
	assert(release_calls == 1);
}

static void test_vle_resource_tail(void)
{
	legacy_u8 counts[1] = {2};
	static const legacy_u32 lengths[] = {65, 65537};
	for (legacy_u32 i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
		reset_file();
		legacy_u32 length = lengths[i];
		file_length = make_vle(file_bytes, counts, 1, length, 0, 1);
		legacy_u32 retained_size = prepare_tail_model(length, 256);
		memcpy(stage, expected, length + 1);
		/* Leave a trimmed ordinary resource in the cache first. */
		assert(file_decomp("SNAPSHOT.pre", 0) == memory + 0x20000);
		assert(resized_paragraphs == (length + 15) / 16);
		memset(memory + 0x20000, 0xa5, retained_size + 1);
		check_tail_snapshot(retained_size);
		assert(memory[0x20000 + length] == expected[length]);
		assert(memory[0x20000 + length] != 0);
		/* A second load must not inherit poison from the previous allocation. */
		memset(memory + 0x20000, 0x3c, retained_size);
		release_calls = 0;
		check_tail_snapshot(retained_size);
	}
}

static void test_rle_resource_tail(void)
{
	reset_file();
	static const legacy_u8 encoded[] = {1, 5, 0, 0, 3, 0, 0, 0, 129, 0xe0, 0xe0, 7, 'Z'};
	memcpy(file_bytes, encoded, sizeof(encoded));
	file_length = sizeof(encoded);
	legacy_u32 retained_size = prepare_tail_model(5, 256);
	/* A final run deliberately writes beyond the declared output size. */
	memset(stage, 'Z', 7);
	check_tail_snapshot(retained_size);

	reset_file();
	static const legacy_u8 sequence[] = {1, 5, 0, 0, 4, 0, 0, 0, 2, 0xe0, 0xe1, 0xe1, 'A', 0xe1, 5};
	memcpy(file_bytes, sequence, sizeof(sequence));
	file_length = sizeof(sequence);
	retained_size = prepare_tail_model(5, 256);
	memset(stage, 'A', 5);
	memmove(stage + 64, stage, 16);
	check_tail_snapshot(retained_size);
	assert(copy_calls == 1);
}

static void test_multipass_resource_tail(void)
{
	reset_file();
	for (legacy_u32 i = 0; i < 16; i++) {
		expected[i] = (legacy_u8)(31 + i * 7);
	}
	legacy_u32 inner_size = make_rle_literals(packed, expected, 16);
	file_bytes[0] = 130;
	write_size(file_bytes + 1, 16);
	file_length = 4 + make_rle_literals(file_bytes + 4, packed, inner_size);
	legacy_u32 retained_size = prepare_tail_model(16, 256);
	memcpy(stage, packed, inner_size);
	memmove(stage + 48, stage, 32);
	memcpy(stage, expected, 16);
	check_tail_snapshot(retained_size);
	assert(copy_calls == 1);
}

static void test_binary_resource_tail(void)
{
	reset_file();
	resource_file_kind = 2;
	cached = 1;
	file_length = 17;
	for (legacy_u32 i = 0; i < file_length; i++) {
		file_bytes[i] = (legacy_u8)(i * 3);
	}
	legacy_u8 *result = file_load_resfile_with_tail("SNAPSHOT", 256);
	assert(result == memory + 0x20000);
	assert(lookup_calls == 0 && allocated_paragraphs == 18);
	assert(resize_calls == 0 && copy_calls == 0);
	assert(memcmp(result, file_bytes, file_length) == 0);
	for (legacy_u32 i = file_length; i < 288; i++) {
		assert(result[i] == 0);
	}
	assert(result[288] == 0xa5);
	mmgr_release(result);
	assert(release_calls == 1);
}

static void test_resource_tail_errors(void)
{
	reset_file();
	resource_file_kind = 1;
	file_length = 16;
	memset(file_bytes, 0, file_length);
	file_bytes[0] = 3;
	write_size(file_bytes + 1, 16);
	assert(file_load_resfile_with_tail("INVALID", 256) == 0);
	assert(release_calls == 1 && fatal_calls == 1);

	for (legacy_u32 kind = 1; kind <= 2; kind++) {
		reset_file();
		resource_file_kind = kind;
		file_length = 16;
		memset(file_bytes, 0, file_length);
		write_size(file_bytes + 1, 16);
		/* Compressed files read the header before allocating their workspace. */
		fail_read_after = kind == 1 ? 2 : 1;
		assert(file_load_resfile_with_tail("UNREAD", 256) == 0);
		assert(release_calls == 1 && fatal_calls == 1);
	}
}

#define TEST_GAME_VERSION_DATA_OFFSET                                                              \
	(RESOURCE_FILE_DIRECTORY_OFFSET + RESOURCE_FILE_IDENTIFIER_SIZE + RESOURCE_FILE_OFFSET_SIZE)
#define TEST_PACKED_HEADER_SIZE 4U

static legacy_u32 make_game_version_resource(legacy_u8 *destination)
{
	static const legacy_s8 version[] = "Version 1.1 (Feb 12 1991)";
	legacy_u32 size = TEST_GAME_VERSION_DATA_OFFSET + sizeof(version);
	memset(destination, 0, size);
	LEGACY_WRITE_U32_LE(destination + RESOURCE_FILE_SIZE_OFFSET, size);
	LEGACY_WRITE_U16_LE(destination + RESOURCE_FILE_COUNT_OFFSET, 1);
	memcpy(destination + RESOURCE_FILE_DIRECTORY_OFFSET, "gver", RESOURCE_FILE_IDENTIFIER_SIZE);
	memcpy(destination + TEST_GAME_VERSION_DATA_OFFSET, version, sizeof(version));
	return size;
}

static void test_game_version(void)
{
	reset_file();
	file_length = make_game_version_resource(file_bytes);
	assert(file_game_version_matches() != 0);
	assert(release_calls == 1 && fatal_calls == 0);

	/* An unpacked override is authoritative, even when a packed file exists. */
	reset_file();
	packed_open_calls = 0;
	file_length = make_game_version_resource(file_bytes);
	file_bytes[TEST_GAME_VERSION_DATA_OFFSET] = 'X';
	assert(file_game_version_matches() == 0);
	assert(packed_open_calls == 0 && release_calls == 1 && fatal_calls == 0);

	reset_file();
	file_length = make_game_version_resource(file_bytes) - 1U;
	assert(file_game_version_matches() == 0);
	assert(release_calls == 1 && fatal_calls == 0);

	reset_file();
	file_length = make_game_version_resource(file_bytes);
	fail_read = 1;
	assert(file_game_version_matches() == 0);
	assert(allocated_paragraphs == 0 && fatal_calls == 0);

	reset_file();
	fail_open = 1;
	assert(file_game_version_matches() == 0);
	assert(fatal_calls == 0);

	reset_file();
	packed_open_calls = 0;
	file_length = 0;
	assert(file_game_version_matches() == 0);
	assert(packed_open_calls == 0 && fatal_calls == 0);

	reset_file();
	resource_file_kind = 1;
	legacy_u32 length = make_game_version_resource(expected);
	file_length = make_rle_literals(file_bytes, expected, length);
	assert(file_game_version_matches() != 0);
	assert(release_calls == 1 && fatal_calls == 0);

	reset_file();
	resource_file_kind = 1;
	file_length = TEST_PACKED_HEADER_SIZE - 1U;
	assert(file_game_version_matches() == 0);
	assert(allocated_paragraphs == 0 && fatal_calls == 0);
}

int main(void)
{
	test_vle();
	test_file_passes();
	test_file_vle();
	test_mixed_passes();
	test_rle_passes();
	test_vle_resource_tail();
	test_rle_resource_tail();
	test_multipass_resource_tail();
	test_binary_resource_tail();
	test_resource_tail_errors();
	test_game_version();
	puts("File decompression regression checks passed.");
	return 0;
}
