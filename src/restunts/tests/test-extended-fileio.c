#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include "../c/platform.h"

static void create_file(const char *path)
{
	FILE *file = fopen(path, "wb");
	assert(file != NULL);
	assert(fputc(42, file) == 42);
	assert(fclose(file) == 0);
}

static void expect_matches(const legacy_s8 *query, const char *const *expected, size_t count)
{
	size_t index = 0;
	for (const legacy_s8 *name = dos_file_find_first(query); name != NULL;
		 name = dos_file_find_next()) {
		assert(index < count);
		assert(strcmp((const char *)name, expected[index++]) == 0);
	}
	assert(index == count);
}

int main(void)
{
#ifdef _WIN32
	assert(_mkdir("ExtendedFileIo") == 0);
#else
	assert(mkdir("ExtendedFileIo", 0700) == 0);
#endif
	static const char *const paths[] = {
		"ExtendedFileIo/12345678.CSTRK", "ExtendedFileIo/12345678.TRK",
		"ExtendedFileIo/12345678.CSRPL", "ExtendedFileIo/12345678.RPL",
		"ExtendedFileIo/MiXeD.CsTrK",	 "ExtendedFileIo/MiXeD.CsRpL",
		"ExtendedFileIo/123456789.TRK",	 "ExtendedFileIo/123456789.RPL"};
	for (size_t index = 0; index < sizeof(paths) / sizeof(paths[0]); index++) {
		create_file(paths[index]);
	}
	static const char *const tracks[] = {"12345678.CSTRK", "MiXeD.CsTrK"};
	static const char *const replays[] = {"12345678.CSRPL", "MiXeD.CsRpL"};
	static const char *const old_track[] = {"12345678.TRK"};
	static const char *const old_replay[] = {"12345678.RPL"};
	expect_matches("extendedfileio/*.cstrk", tracks, 2);
	expect_matches("EXTENDEDFILEIO\\*.csrpl", replays, 2);
	expect_matches("ExtendedFileIo/*.trk", old_track, 1);
	expect_matches("ExtendedFileIo/*.rpl", old_replay, 1);
	/* A completed sibling file replaces the destination in one operation. */
	FILE *temporary = fopen("ExtendedFileIo/_CTEST.tmp", "wb");
	assert(temporary != NULL);
	assert(fputc(99, temporary) == 99);
	assert(fclose(temporary) == 0);
	assert(sdl3_file_replace("ExtendedFileIo/_CTEST.tmp", "ExtendedFileIo/12345678.CSRPL") == 0);
	assert(fopen("ExtendedFileIo/_CTEST.tmp", "rb") == NULL);
	FILE *replaced = fopen("ExtendedFileIo/12345678.CSRPL", "rb");
	assert(replaced != NULL && fgetc(replaced) == 99);
	assert(fclose(replaced) == 0);
	/* A failed replacement must leave an existing replay untouched. */
	assert(sdl3_file_replace("ExtendedFileIo/missing.tmp", "ExtendedFileIo/12345678.CSRPL") != 0);
	replaced = fopen("ExtendedFileIo/12345678.CSRPL", "rb");
	assert(replaced != NULL && fgetc(replaced) == 99);
	assert(fclose(replaced) == 0);
	for (size_t index = 0; index < sizeof(paths) / sizeof(paths[0]); index++) {
		assert(remove(paths[index]) == 0);
	}
	assert(rmdir("ExtendedFileIo") == 0);
	puts("SDL3 enumeration preserves eight-character names and five-character format extensions.");
	return 0;
}
