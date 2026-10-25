#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__linux__) && !defined(__ANDROID__)
#include <dirent.h>
#endif
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#endif
#include "../platform/sdl3/sdl3.h"
#include "../c/fileio.h"
#include "../c/highscore.h"
#include "../c/platform.h"
#include "../c/memmgr.h"
#include "../c/shape2d.h"

#define TEST_GUARD_BYTES 19U
#define TEST_GUARD_VALUE 0xa7U
#define TEST_LARGE_FILE_BYTES 98341U
#ifdef _WIN32
#define TEST_WINDOWS_NUL_LENGTH (-1)
#define TEST_WINDOWS_PATH_CAPACITY 1024U
#endif

static void fill_pattern(legacy_u8 *data, size_t length)
{
	for (size_t index = 0; index < length; index++) {
		data[index] = (legacy_u8)(index * 37U + (index >> 9U) + (index >> 16U));
	}
}

static void check_file_contents(const legacy_char *path, const legacy_u8 *expected, size_t length)
{
	FILE *file = fopen(path, "rb");
	assert(file != NULL);
	legacy_u8 buffer[2048];
	while (length != 0) {
		size_t count = length < sizeof(buffer) ? length : sizeof(buffer);
		assert(fread(buffer, 1, count, file) == count);
		assert(memcmp(buffer, expected, count) == 0);
		expected += count;
		length -= count;
	}
	assert(fgetc(file) == EOF);
	assert(!ferror(file));
	assert(fclose(file) == 0);
}

static void check_guards(const legacy_u8 *allocation, size_t length)
{
	for (size_t index = 0; index < TEST_GUARD_BYTES; index++) {
		assert(allocation[index] == TEST_GUARD_VALUE);
		assert(allocation[TEST_GUARD_BYTES + length + index] == TEST_GUARD_VALUE);
	}
}

static void test_highscore_stack_buffers(void)
{
	struct HIGHSCORE_ENTRY scores[HIGHSCORE_ENTRY_COUNT];
	struct {
		legacy_u8 before[TEST_GUARD_BYTES];
		struct HIGHSCORE_ENTRY scores[HIGHSCORE_ENTRY_COUNT];
		legacy_u8 after[TEST_GUARD_BYTES];
	} loaded;
	fill_pattern((legacy_u8 *)scores, sizeof(scores));
	memset(&loaded, TEST_GUARD_VALUE, sizeof(loaded));
	assert(sizeof(scores) == HIGHSCORE_TABLE_SIZE_BYTES);
	assert(dos_memory_make_pointer(dos_memory_pointer_segment(scores),
								   dos_memory_pointer_offset(scores)) == scores);
	assert(file_write_fatal((const legacy_s8 *)"casedir/ScOrE.HiG", scores, sizeof(scores)) == 0);
	check_file_contents("CaSeDir/ScOrE.HiG", (const legacy_u8 *)scores, sizeof(scores));
	const legacy_s8 *found = file_find((const legacy_s8 *)"CASEDIR/*.hig");
	assert(found != NULL);
	assert(file_read_nofatal(found, loaded.scores) == loaded.scores);
	assert(memcmp(scores, loaded.scores, sizeof(scores)) == 0);
	for (size_t index = 0; index < TEST_GUARD_BYTES; index++) {
		assert(loaded.before[index] == TEST_GUARD_VALUE);
		assert(loaded.after[index] == TEST_GUARD_VALUE);
	}
	assert(file_find_next() == NULL);
	assert(dos_file_remove((const legacy_s8 *)"CaSeDiR\\score.hig") == 0);
}

static void test_heap_buffers(void)
{
	static const size_t lengths[] = {0,		1,	   16383, 16384,
									 16385, 65535, 65536, TEST_LARGE_FILE_BYTES};
	legacy_u8 *source_allocation = malloc(TEST_LARGE_FILE_BYTES + TEST_GUARD_BYTES * 2U);
	legacy_u8 *destination_allocation = malloc(TEST_LARGE_FILE_BYTES + TEST_GUARD_BYTES * 2U);
	assert(source_allocation != NULL && destination_allocation != NULL);
	legacy_u8 *source = source_allocation + TEST_GUARD_BYTES;
	legacy_u8 *destination = destination_allocation + TEST_GUARD_BYTES;
	for (size_t index = 0; index < sizeof(lengths) / sizeof(lengths[0]); index++) {
		size_t length = lengths[index];
		memset(source_allocation, TEST_GUARD_VALUE, length + TEST_GUARD_BYTES * 2U);
		memset(destination_allocation, TEST_GUARD_VALUE, length + TEST_GUARD_BYTES * 2U);
		fill_pattern(source, length);
		assert(file_write_fatal((const legacy_s8 *)"casedir\\BuFfEr.BiN", source,
								(legacy_u32)length) == 0);
		check_file_contents("CaSeDir/BuFfEr.BiN", source, length);
		assert(file_read_nofatal((const legacy_s8 *)"CASEDIR/buffer.bin", destination) ==
			   destination);
		assert(memcmp(source, destination, length) == 0);
		check_guards(source_allocation, length);
		check_guards(destination_allocation, length);
		assert(dos_file_remove((const legacy_s8 *)"casedir/BUFFER.BIN") == 0);
	}
	free(source_allocation);
	free(destination_allocation);
}

static void test_external_car_loaders(void)
{
	static const legacy_char *const filenames[] = {
		"CaSeDir/cars/CARZ9Z9.RES", "CaSeDir/cars/STZ9Z9.3SH", "CaSeDir/cars/STDAZ9Z9.VSH",
		"CaSeDir/cars/STDBZ9Z9.VSH"};
	legacy_char directory[FILE_CAR_PATH_SIZE];
#ifdef _WIN32
	WCHAR wide_directory[TEST_WINDOWS_PATH_CAPACITY];
	DWORD path_length = GetCurrentDirectoryW(TEST_WINDOWS_PATH_CAPACITY, wide_directory);
	assert(path_length != 0 && path_length < TEST_WINDOWS_PATH_CAPACITY);
	assert(WideCharToMultiByte(CP_UTF8, 0, wide_directory, TEST_WINDOWS_NUL_LENGTH, directory,
							   sizeof(directory), NULL, NULL) != 0);
#else
	assert(getcwd(directory, sizeof(directory)) != NULL);
#endif
	assert(strlen(directory) + sizeof("/CaSeDir") <= sizeof(directory));
	strcat(directory, "/CaSeDir");
#ifdef _WIN32
	assert(_mkdir("CaSeDir/cars") == 0);
#else
	assert(mkdir("CaSeDir/cars", 0700) == 0);
#endif
	legacy_u8 expected[TEST_GUARD_BYTES];
	fill_pattern(expected, sizeof(expected));
	for (legacy_u16 index = 0; index < sizeof(filenames) / sizeof(filenames[0]); index++) {
		assert(file_write_fatal((const legacy_s8 *)filenames[index], expected, sizeof(expected)) ==
			   0);
	}
	file_car_set_base((const legacy_s8 *)directory);
	legacy_s8 ids[LEGACY_U8_MAX][FILE_CAR_ID_BUFFER_SIZE];
	legacy_u16 count = file_find_car_ids(ids, LEGACY_U8_MAX);
	legacy_u16 matches = 0;
	for (legacy_u16 index = 0; index < count; index++) {
		matches += strcmp((const legacy_char *)ids[index], "Z9Z9") == 0;
	}
	assert(matches == 1);
	mmgr_init_conventional_arena();
	void *resource = file_load_resfile((const legacy_s8 *)"carz9z9");
	assert(resource != NULL && memcmp(resource, expected, sizeof(expected)) == 0);
	mmgr_release(resource);
	resource = file_load_resfile_with_tail((const legacy_s8 *)"carz9z9", TEST_GUARD_BYTES);
	assert(resource != NULL && memcmp(resource, expected, sizeof(expected)) == 0);
	mmgr_release(resource);
	resource = file_load_3dres((const legacy_s8 *)"stz9z9");
	assert(resource != NULL && memcmp(resource, expected, sizeof(expected)) == 0);
	mmgr_release(resource);
	resource = file_load_shape2d_fatal((const legacy_s8 *)"stdaz9z9");
	assert(resource != NULL && memcmp(resource, expected, sizeof(expected)) == 0);
	mmgr_release(resource);
	resource = file_load_shape2d_fatal((const legacy_s8 *)"stdbz9z9");
	assert(resource != NULL && memcmp(resource, expected, sizeof(expected)) == 0);
	mmgr_release(resource);
	file_car_set_base(NULL);
	for (legacy_u16 index = 0; index < sizeof(filenames) / sizeof(filenames[0]); index++) {
		assert(remove(filenames[index]) == 0);
	}
	assert(rmdir("CaSeDir/cars") == 0);
}

static void run_file_io_tests(void)
{
#ifdef _WIN32
	legacy_s32 result = _mkdir("CaSeDir");
#else
	legacy_s32 result = mkdir("CaSeDir", 0700);
#endif
	assert(result == 0 || errno == EEXIST);
	test_highscore_stack_buffers();
	test_heap_buffers();
	test_external_car_loaders();
	assert(rmdir("CaSeDir") == 0);
}

legacy_int main(void)
{
#if defined(__linux__) && !defined(__ANDROID__)
	/* Overlay filesystems can return 64-bit directory cookies for tiny files.
	 * Keep the Linux file ABI wide even when CI's filesystem has small cookies. */
	assert(sizeof(off_t) >= 8);
	assert(sizeof(((struct dirent *)0)->d_off) >= 8);
#endif
	sdl3_batch_mode = 1;
	run_file_io_tests();
#ifdef _WIN32
	/* ANSI MinGW dirent returns no entries in a directory outside the ACP,
	 * even for opendir("."). Repeat real core reads/writes, mixed-case lookup,
	 * wildcard enumeration and absolute external-car paths in such a directory. */
	static const WCHAR unicode_directory[] = L"Restunts-\u0151\u0171-\u6771\u4eac";
	WCHAR original[TEST_WINDOWS_PATH_CAPACITY];
	DWORD count = GetCurrentDirectoryW(TEST_WINDOWS_PATH_CAPACITY, original);
	assert(count != 0 && count < TEST_WINDOWS_PATH_CAPACITY);
	assert(CreateDirectoryW(unicode_directory, NULL));
	assert(SetCurrentDirectoryW(unicode_directory));
	run_file_io_tests();
	legacy_char absolute[FILE_CAR_PATH_SIZE];
	WCHAR wide_directory[TEST_WINDOWS_PATH_CAPACITY];
	count = GetCurrentDirectoryW(TEST_WINDOWS_PATH_CAPACITY, wide_directory);
	assert(count != 0 && count < TEST_WINDOWS_PATH_CAPACITY);
	assert(WideCharToMultiByte(CP_UTF8, 0, wide_directory, TEST_WINDOWS_NUL_LENGTH, absolute,
							   sizeof(absolute), NULL, NULL) != 0);
	static const legacy_char filename[] = "/REMOVE.BIN";
	assert(strlen(absolute) + sizeof(filename) <= sizeof(absolute));
	strcat(absolute, filename);
	legacy_u16 handle = dos_file_open((const legacy_s8 *)absolute, DOS_FILE_CREATE);
	assert(handle != 0);
	assert(dos_file_close(handle) == 0);
	assert(dos_file_remove((const legacy_s8 *)absolute) == 0);
	assert(SetCurrentDirectoryW(original));
	assert(RemoveDirectoryW(unicode_directory));
#endif
	puts("SDL3 core file I/O stack, heap, page-boundary and path regressions passed");
	return 0;
}
