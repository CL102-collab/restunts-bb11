#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../c/fileio.h"
#include "../c/platform.h"

#define TEST_DIRECTORY_MODE 0700
#define TEST_TRACK_CAPACITY 16U
#define TEST_TRACK_NAME_SIZE 32U

static legacy_char original_directory[FILE_CAR_PATH_SIZE];
static legacy_char temporary_directory[] = "/tmp/restunts-tracks-XXXXXX";
static const legacy_char *const fixture_files[] = {"Loose.TRK",
												   "CONFLICT.TRK",
												   "../App/Base.TRK",
												   "../App/CONFLICT.TRK",
												   "../App/ORDER.TRK",
												   "TrAcKs/DataOnly.TrK",
												   "TrAcKs/dataonly.trk",
												   "TrAcKs/CONFLICT.TRK",
												   "TrAcKs/order.trk",
												   "TrAcKs/FOLDER.TRK",
												   "../App/tracks/AppOnly.TRK",
												   "../App/tracks/CONFLICT.TRK",
												   "../App/tracks/ORDER.TRK",
												   "../App/tracks/FOLDER.TRK",
												   "TrAcKs/Longer-track.cstrk",
												   "TrAcKs/FOLDER.HIG",
												   "TrAcKs/Longer-track.cshig",
												   "TrAcKs/IGNORE.RES",
												   "TrAcKs/nested/HIDDEN.TRK",
												   "Other/ISOLATED.TRK"};
static const legacy_char *const expected_tracks[] = {"LOOSE",	 "CONFLICT", "BASE",   "ORDER",
													 "DATAONLY", "FOLDER",	 "APPONLY"};

void fatal_error(const legacy_s8 *format, ...)
{
	fprintf(stderr, "Unexpected fatal error: %s\n", (const legacy_char *)format);
	abort();
}

void dos_process_exit(legacy_s16 status)
{
	exit(status);
}

static void make_directory(const legacy_char *name)
{
	assert(mkdir(name, TEST_DIRECTORY_MODE) == 0);
}

static void write_marker(const legacy_char *name, legacy_u8 marker)
{
	FILE *file = fopen(name, "wb");
	assert(file != NULL);
	assert(fwrite(&marker, sizeof(marker), 1, file) == 1);
	assert(fclose(file) == 0);
}

static void assert_marker(const legacy_char *name, legacy_u8 expected)
{
	legacy_u8 marker = 0;
	if (file_read_nofatal((const legacy_s8 *)name, &marker) != &marker) {
		fprintf(stderr, "Could not read %s\n", name);
		abort();
	}
	assert(marker == expected);
}

static void assert_discovery(const legacy_char *query)
{
	legacy_char names[TEST_TRACK_CAPACITY][TEST_TRACK_NAME_SIZE];
	legacy_u16 count = 0;
	const legacy_s8 *path = file_find((const legacy_s8 *)query);
	while (path != NULL) {
		assert(count < TEST_TRACK_CAPACITY);
		const legacy_char *name = (const legacy_char *)path;
		for (const legacy_char *cursor = name; *cursor != 0; cursor++) {
			if (*cursor == '/' || *cursor == '\\' || *cursor == ':') {
				name = cursor + 1;
			}
		}
		legacy_u16 index = 0;
		while (name[index] != 0 && name[index] != '.') {
			assert(index + 1U < TEST_TRACK_NAME_SIZE);
			names[count][index] =
				name[index] >= 'a' && name[index] <= 'z' ? name[index] - ('a' - 'A') : name[index];
			index++;
		}
		names[count++][index] = 0;
		path = file_find_next_alt();
	}
	assert(count == sizeof(expected_tracks) / sizeof(expected_tracks[0]));
	for (legacy_u16 expected = 0; expected < count; expected++) {
		legacy_u16 matches = 0;
		for (legacy_u16 index = 0; index < count; index++) {
			matches += strcmp(names[index], expected_tracks[expected]) == 0;
		}
		assert(matches == 1);
	}
	assert(file_find_next() == NULL);
}

legacy_int main(void)
{
	assert(getcwd(original_directory, sizeof(original_directory)) != NULL);
	assert(mkdtemp(temporary_directory) != NULL);
	assert(chdir(temporary_directory) == 0);
	make_directory("Data");
	make_directory("App");
	make_directory("Data/TrAcKs");
	make_directory("App/tracks");
	make_directory("Data/TrAcKs/nested");
	make_directory("Data/Other");
	assert(chdir("Data") == 0);
	for (legacy_u16 index = 0; index < sizeof(fixture_files) / sizeof(fixture_files[0]); index++) {
		write_marker(fixture_files[index], (legacy_u8)(index + 1U));
	}
	assert(chdir("../App") == 0);
	legacy_char executable_directory[FILE_CAR_PATH_SIZE];
	assert(getcwd(executable_directory, sizeof(executable_directory)) != NULL);
	file_car_set_base((const legacy_s8 *)executable_directory);
	assert(chdir("../Data") == 0);

	assert_discovery("*.trk");
	assert_discovery(".\\*.TrK");
	assert_discovery("./*.trk");
	assert_marker("loose.trk", 1);
	assert_marker("base.trk", 3);
	assert_marker("apponly.trk", 11);
	assert_marker("conflict.trk", 2);
	assert_marker("order.trk", 5);
	assert_marker("folder.trk", 10);
	assert_marker("./folder.trk", 10);
	assert_marker(".\\folder.trk", 10);
	assert_marker("folder.hig", 16);
	assert_marker("longer-track.cstrk", 15);
	assert_marker("longer-track.cshig", 17);
	assert(file_find((const legacy_s8 *)"*.cstrk") != NULL);
	assert(file_find_next() == NULL);
	assert(file_find((const legacy_s8 *)"*.csrpl") == NULL);
	assert(file_find((const legacy_s8 *)"hidden.trk") == NULL);
	assert(file_open_resource((const legacy_s8 *)"ignore.res") == 0);
	assert(file_open_resource((const legacy_s8 *)"missing/folder.trk") == 0);
	assert(file_find((const legacy_s8 *)"missing/*.trk") == NULL);
	const legacy_s8 *isolated = file_find((const legacy_s8 *)"Other/*.trk");
	assert(isolated != NULL);
	assert_marker((const legacy_char *)isolated, 20);
	assert(file_find_next() == NULL);

	/* Saves create a loose override without changing the imported track. */
	legacy_u8 replacement = 42;
	assert(file_write_fatal((const legacy_s8 *)"folder.trk", &replacement, sizeof(replacement)) ==
		   0);
	assert_marker("folder.trk", replacement);
	assert_marker("TrAcKs/FOLDER.TRK", 10);
	assert_discovery("*.trk");
	assert(remove("folder.trk") == 0);
	for (legacy_u16 index = 0; index < sizeof(fixture_files) / sizeof(fixture_files[0]); index++) {
		assert(remove(fixture_files[index]) == 0);
	}
	assert(file_find((const legacy_s8 *)"*.trk") == NULL);
	assert(file_find_next() == NULL);
	assert(rmdir("TrAcKs/nested") == 0);
	assert(rmdir("TrAcKs") == 0);
	assert(rmdir("Other") == 0);
	assert(rmdir("../App/tracks") == 0);
	write_marker("loose.trk", 1);
	assert_marker("loose.trk", 1);
	assert(file_find((const legacy_s8 *)"*.trk") != NULL);
	assert(file_find_next() == NULL);
	assert(remove("loose.trk") == 0);
	assert(chdir("..") == 0);
	assert(rmdir("Data") == 0);
	assert(rmdir("App") == 0);
	file_car_set_base(NULL);
	assert(chdir(original_directory) == 0);
	assert(rmdir(temporary_directory) == 0);
	puts("Track folder discovery, duplicate precedence, explicit paths and loose saves passed");
	return 0;
}
