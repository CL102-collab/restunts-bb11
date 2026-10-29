#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <direct.h>
#endif
#ifdef __linux__
#include <signal.h>
#include <sys/resource.h>
#endif
#include "../c/extended_highscore.h"
#include "../c/platform.h"

#define SCORE_DIRECTORY "ExtendedHighscore"
#define TRACK_PATH SCORE_DIRECTORY "/TWIN.CSTRK"
#define SCORE_PATH SCORE_DIRECTORY "/TWIN.cshig"
#define LEGACY_SCORE_PATH SCORE_DIRECTORY "/TWIN.HIG"
#define LEGACY_TRACK_PATH SCORE_DIRECTORY "/TWIN.TRK"

static void write_bytes(const char *path, const uint8_t *bytes, size_t size)
{
	FILE *file = fopen(path, "wb");
	assert(file != NULL);
	assert(fwrite(bytes, 1, size, file) == size);
	assert(fclose(file) == 0);
}

static uint8_t *read_bytes(const char *path, size_t *size)
{
	FILE *file = fopen(path, "rb");
	assert(file != NULL);
	assert(fseek(file, 0, SEEK_END) == 0);
	long length = ftell(file);
	assert(length >= 0);
	assert(fseek(file, 0, SEEK_SET) == 0);
	uint8_t *bytes = malloc((size_t)length + 1U);
	assert(bytes != NULL);
	assert(fread(bytes, 1, (size_t)length, file) == (size_t)length);
	assert(fclose(file) == 0);
	*size = (size_t)length;
	return bytes;
}

static void check_bytes(const char *path, const uint8_t *expected, size_t expected_size)
{
	size_t size;
	uint8_t *bytes = read_bytes(path, &size);
	assert(size == expected_size);
	assert(memcmp(bytes, expected, size) == 0);
	free(bytes);
}

static void write_track(const struct CSTRK_TRACK *track)
{
	char error[192];
	uint8_t *bytes = NULL;
	size_t size = 0;
	assert(cstrk_encode(track, &bytes, &size, error, sizeof(error)));
	write_bytes(TRACK_PATH, bytes, size);
	free(bytes);
}

static struct CSHIG_ENTRY score_entry(const char *name, uint64_t ticks)
{
	struct CSHIG_ENTRY entry;
	memset(&entry, 0, sizeof(entry));
	assert(strlen(name) < sizeof(entry.player_name));
	memcpy(entry.player_name, name, strlen(name) + 1U);
	memcpy(entry.car_name, "Porsche Carrera", sizeof("Porsche Carrera"));
	memcpy(entry.opponent, " ", sizeof(" "));
	entry.ticks = ticks;
	return entry;
}

static void fixture(struct CSTRK_TRACK *track)
{
	char error[192];
	cstrk_init(track);
	assert(cstrk_create(track, 3U, 2U, error, sizeof(error)));
	struct CSTRK_OBJECT road;
	memset(&road, 0, sizeof(road));
	road.legacy_id = 4;
	road.width = road.height = road.occupied_mask = 1;
	assert(cstrk_add_object(track, &road, error, sizeof(error)));
	road.x = 2;
	assert(cstrk_add_object(track, &road, error, sizeof(error)));
	assert(cstrk_set_notes(track, "First revision", 14U, error, sizeof(error)));
}

static void check_loaded_score(unsigned index, const char *name, uint64_t ticks)
{
	const struct CSHIG_ENTRY *entry = extended_highscore_entry(index);
	assert(entry != NULL);
	assert(strcmp((const char *)entry->player_name, name) == 0);
	assert(entry->ticks == ticks);
}

static void test_creation_and_wide_times(struct CSTRK_TRACK *track)
{
	assert(extended_highscore_load_or_create(SCORE_DIRECTORY, "TWIN", track, 0) != 0);
	assert(extended_highscore_entry(0) == NULL);
	assert(extended_highscore_load_or_create("extendedhighscore", "twin", track, 1) == 0);
	assert(sdl3_file_exists(SCORE_PATH) == 0);
	assert(extended_highscore_entry(0)->ticks == CSHIG_EMPTY_TICKS);
	assert(extended_highscore_entry(CSHIG_ENTRY_COUNT) == NULL);
	assert(!extended_highscore_qualifies(0U, 20U));
	assert(!extended_highscore_qualifies(100U, 30U));
	assert(extended_highscore_qualifies(70000U, 20U));
	assert(extended_highscore_ticks(UINT32_MAX, 10U) == UINT64_C(8589934590));
	assert(extended_highscore_ticks(100U, 30U) == CSHIG_EMPTY_TICKS);

	unsigned rank = UINT_MAX;
	struct CSHIG_ENTRY entry = score_entry("Long race", extended_highscore_ticks(70000U, 20U));
	assert(extended_highscore_validate_track(SCORE_DIRECTORY, "TWIN", track) == 0);
	assert(extended_highscore_record(SCORE_DIRECTORY, "TWIN", track, &entry, &rank) == 0);
	assert(rank == 0U);
	check_loaded_score(0U, "Long race", 70000U);
	assert(extended_highscore_load_or_create(SCORE_DIRECTORY, "TWIN", track, 0) == 0);
	check_loaded_score(0U, "Long race", 70000U);

	entry = score_entry("10 Hz", extended_highscore_ticks(70000U, 10U));
	assert(extended_highscore_record(SCORE_DIRECTORY, "TWIN", track, &entry, &rank) == 0);
	assert(rank == 1U);
	check_loaded_score(1U, "10 Hz", 140000U);
	entry = score_entry("Maximum frames", extended_highscore_ticks(UINT32_MAX, 10U));
	assert(extended_highscore_record(SCORE_DIRECTORY, "TWIN", track, &entry, &rank) == 0);
	assert(rank == 2U);
	check_loaded_score(2U, "Maximum frames", UINT64_C(8589934590));

	legacy_s8 formatted[32];
	assert(extended_highscore_format_time(formatted, sizeof(formatted), 1U) == 0);
	assert(strcmp((const char *)formatted, " 0:00.05") == 0);
	assert(extended_highscore_format_time(formatted, sizeof(formatted), 11999U) == 0);
	assert(strcmp((const char *)formatted, " 9:59.95") == 0);
	assert(extended_highscore_format_time(formatted, sizeof(formatted), 12000U) == 0);
	assert(strcmp((const char *)formatted, "10:00.00") == 0);
	assert(extended_highscore_format_time(formatted, sizeof(formatted), 70000U) == 0);
	assert(strcmp((const char *)formatted, "58:20.00") == 0);
	assert(extended_highscore_format_time(formatted, sizeof(formatted), UINT64_C(8589934590)) == 0);
	assert(strcmp((const char *)formatted, "7158278:49.50") == 0);
	assert(extended_highscore_format_time(formatted, sizeof(formatted), UINT64_MAX - 1U) == 0);
	assert(strcmp((const char *)formatted, "15372286728091293:00.70") == 0);
	assert(extended_highscore_format_time(formatted, sizeof(formatted), CSHIG_EMPTY_TICKS) == 0);
	assert(strcmp((const char *)formatted, " 0:00.00") == 0);
	strcpy((char *)formatted, "preserved");
	assert(extended_highscore_format_time(formatted, strlen(" 0:00.00"), 1U) != 0);
	assert(strcmp((const char *)formatted, "preserved") == 0);
	assert(extended_highscore_format_time(formatted, sizeof(" 0:00.00"), 1U) == 0);
	assert(strcmp((const char *)formatted, " 0:00.05") == 0);
	assert(extended_highscore_format_time(NULL, sizeof(formatted), 1U) != 0);
	assert(extended_highscore_format_time(formatted, 0U, 1U) != 0);
	assert(strcmp((const char *)formatted, " 0:00.05") == 0);
}

static void test_stale_table_merge(struct CSTRK_TRACK *track)
{
	char error[192];
	struct CSHIG_TABLE external;
	cshig_init(&external);
	size_t size;
	uint8_t *bytes = read_bytes(SCORE_PATH, &size);
	assert(cshig_decode(bytes, size, &external, error, sizeof(error)));
	free(bytes);
	struct CSHIG_ENTRY entry = score_entry("Other process", 500U);
	assert(cshig_insert(&external, &entry, error, sizeof(error)));
	assert(cshig_encode(&external, &bytes, &size, error, sizeof(error)));
	write_bytes(SCORE_PATH, bytes, size);
	free(bytes);
	cshig_free(&external);
	/* The UI still holds the old first score, but saving must merge disk rows. */
	check_loaded_score(0U, "Long race", 70000U);
	entry = score_entry("New score", 900U);
	unsigned rank;
	assert(extended_highscore_record(SCORE_DIRECTORY, "TWIN", track, &entry, &rank) == 0);
	assert(rank == 1U);
	check_loaded_score(0U, "Other process", 500U);
	check_loaded_score(1U, "New score", 900U);
	check_loaded_score(2U, "Long race", 70000U);

	entry = score_entry("Equal time", 900U);
	assert(extended_highscore_record(SCORE_DIRECTORY, "TWIN", track, &entry, &rank) == 0);
	assert(rank == 2U);
	check_loaded_score(1U, "New score", 900U);
	check_loaded_score(2U, "Equal time", 900U);
}

static void test_track_identity(struct CSTRK_TRACK *track)
{
	char error[192];
	assert(cstrk_set_notes(track, "Notes are editable", 18U, error, sizeof(error)));
	struct CSTRK_OBJECT temporary = track->objects[0];
	track->objects[0] = track->objects[1];
	track->objects[1] = temporary;
	write_track(track);
	/* Keep the raced model's older notes/order while the on-disk editorial
	 * revision changes. Neither difference may disqualify the finish. */
	track->objects[1] = track->objects[0];
	track->objects[0] = temporary;
	assert(cstrk_set_notes(track, "First revision", 14U, error, sizeof(error)));
	assert(extended_highscore_validate_track(SCORE_DIRECTORY, "TWIN", track) == 0);
	assert(extended_highscore_load_or_create(SCORE_DIRECTORY, "TWIN", track, 0) == 0);
	check_loaded_score(0U, "Other process", 500U);

	size_t prior_size;
	uint8_t *prior = read_bytes(SCORE_PATH, &prior_size);
	track->corner_heights[11] = 1;
	write_track(track);
	track->corner_heights[11] = 0;
	assert(extended_highscore_validate_track(SCORE_DIRECTORY, "TWIN", track) != 0);
	struct CSHIG_ENTRY entry = score_entry("Changed disk", 100U);
	unsigned rank = 123U;
	assert(extended_highscore_record(SCORE_DIRECTORY, "TWIN", track, &entry, &rank) != 0);
	assert(rank == 123U);
	check_loaded_score(0U, "Other process", 500U);
	check_bytes(SCORE_PATH, prior, prior_size);

	/* Loading an intentionally edited revision shows an empty table without
	 * changing the previous revision's file until a new score is completed. */
	track->corner_heights[11] = 1;
	assert(extended_highscore_load_or_create(SCORE_DIRECTORY, "TWIN", track, 0) == 0);
	assert(extended_highscore_entry(0)->ticks == CSHIG_EMPTY_TICKS);
	check_bytes(SCORE_PATH, prior, prior_size);
	assert(extended_highscore_record(SCORE_DIRECTORY, "TWIN", track, &entry, &rank) == 0);
	assert(rank == 0U);
	check_loaded_score(0U, "Changed disk", 100U);
	free(prior);
}

static void test_failure_preserves_scores(struct CSTRK_TRACK *track)
{
	size_t prior_size;
	uint8_t *prior = read_bytes(SCORE_PATH, &prior_size);
	const uint8_t corrupt[] = "invalid score file";
	write_bytes(SCORE_PATH, corrupt, sizeof(corrupt));
	struct CSHIG_ENTRY entry = score_entry("Do not clobber", 50U);
	unsigned rank = 123U;
	assert(extended_highscore_record(SCORE_DIRECTORY, "TWIN", track, &entry, &rank) != 0);
	assert(rank == 123U);
	check_loaded_score(0U, "Changed disk", 100U);
	check_bytes(SCORE_PATH, corrupt, sizeof(corrupt));
	assert(extended_highscore_load_or_create(SCORE_DIRECTORY, "TWIN", track, 1) != 0);
	check_bytes(SCORE_PATH, corrupt, sizeof(corrupt));
	write_bytes(SCORE_PATH, prior, prior_size);
	assert(extended_highscore_load_or_create(SCORE_DIRECTORY, "TWIN", track, 0) == 0);

	/* A zero-length existing file also must never be treated as absent. */
	write_bytes(SCORE_PATH, corrupt, 0);
	assert(extended_highscore_load_or_create(SCORE_DIRECTORY, "TWIN", track, 1) != 0);
	check_bytes(SCORE_PATH, corrupt, 0);
	write_bytes(SCORE_PATH, prior, prior_size);
	assert(extended_highscore_load_or_create(SCORE_DIRECTORY, "TWIN", track, 0) == 0);

	/* An existing table that cannot currently be opened is not a missing file. */
	legacy_u16 handles[64];
	size_t count = 0;
	for (;;) {
		legacy_u16 handle = dos_file_open(SCORE_PATH, DOS_FILE_OPEN_EXISTING);
		if (handle == 0) {
			break;
		}
		assert(count < sizeof(handles) / sizeof(handles[0]));
		handles[count++] = handle;
	}
	assert(count != 0);
	assert(extended_highscore_load_or_create(SCORE_DIRECTORY, "TWIN", track, 1) != 0);
	while (count != 0) {
		assert(dos_file_close(handles[--count]) == 0);
	}
	check_bytes(SCORE_PATH, prior, prior_size);
	assert(extended_highscore_load_or_create(SCORE_DIRECTORY, "TWIN", track, 0) == 0);

#ifdef __linux__
	/* Exercise a real short-write/close failure even when tests run as root.
	 * The completed destination and the in-memory table must both survive. */
	struct rlimit original;
	assert(getrlimit(RLIMIT_FSIZE, &original) == 0);
	struct rlimit limited = original;
	limited.rlim_cur = 0;
	void (*previous_handler)(int) = signal(SIGXFSZ, SIG_IGN);
	assert(previous_handler != SIG_ERR);
	assert(setrlimit(RLIMIT_FSIZE, &limited) == 0);
	int failed = extended_highscore_record(SCORE_DIRECTORY, "TWIN", track, &entry, &rank);
	assert(setrlimit(RLIMIT_FSIZE, &original) == 0);
	assert(signal(SIGXFSZ, previous_handler) != SIG_ERR);
	assert(failed != 0);
	assert(rank == 123U);
	check_loaded_score(0U, "Changed disk", 100U);
	check_bytes(SCORE_PATH, prior, prior_size);
	assert(dos_file_find_first(SCORE_DIRECTORY "/_H*.tmp") == NULL);

	/* A dangling link is an existing file entry, not a missing score table. */
	assert(remove(SCORE_PATH) == 0);
	assert(symlink("missing-score-target", SCORE_PATH) == 0);
	assert(extended_highscore_load_or_create(SCORE_DIRECTORY, "TWIN", track, 1) != 0);
	assert(extended_highscore_record(SCORE_DIRECTORY, "TWIN", track, &entry, &rank) != 0);
	struct stat info;
	assert(lstat(SCORE_PATH, &info) == 0 && S_ISLNK(info.st_mode));
	assert(remove(SCORE_PATH) == 0);
	write_bytes(SCORE_PATH, prior, prior_size);
#endif
	free(prior);
	assert(extended_highscore_load_or_create(SCORE_DIRECTORY, "TWIN", track, 0) == 0);
	entry.ticks = 0;
	assert(extended_highscore_record(SCORE_DIRECTORY, "TWIN", track, &entry, &rank) != 0);
	assert(extended_highscore_load_or_create(SCORE_DIRECTORY, "../TWIN", track, 1) != 0);
}

static void test_full_table(struct CSTRK_TRACK *track)
{
	unsigned rank;
	for (unsigned index = 0; index < CSHIG_ENTRY_COUNT - 1U; index++) {
		struct CSHIG_ENTRY entry = score_entry("Fill table", 200U + index);
		assert(extended_highscore_record(SCORE_DIRECTORY, "TWIN", track, &entry, &rank) == 0);
	}
	assert(!extended_highscore_qualifies(206U, 20U));
	assert(extended_highscore_qualifies(204U, 20U));
	size_t prior_size;
	uint8_t *prior = read_bytes(SCORE_PATH, &prior_size);
	struct CSHIG_ENTRY entry = score_entry("Too slow", 207U);
	assert(extended_highscore_record(SCORE_DIRECTORY, "TWIN", track, &entry, &rank) == 0);
	assert(rank == UINT_MAX);
	check_bytes(SCORE_PATH, prior, prior_size);
	free(prior);
}

static void remove_test_directory(void)
{
	if (sdl3_file_exists(SCORE_DIRECTORY) == 0) {
		return;
	}
	/* Only remove this fixture's known files. Unexpected contents deliberately
	 * prevent rmdir so a reused working directory cannot lose unrelated data. */
	remove(TRACK_PATH);
	remove(SCORE_PATH);
	remove(LEGACY_SCORE_PATH);
	remove(LEGACY_TRACK_PATH);
	for (const legacy_s8 *name = dos_file_find_first(SCORE_DIRECTORY "/_H*.tmp"); name != NULL;
		 name = dos_file_find_next()) {
		char path[128];
		int length = snprintf(path, sizeof(path), SCORE_DIRECTORY "/%s", (const char *)name);
		assert(length > 0 && (size_t)length < sizeof(path));
		assert(remove(path) == 0);
	}
	assert(rmdir(SCORE_DIRECTORY) == 0);
}

int main(void)
{
	remove_test_directory();
#ifdef _WIN32
	assert(_mkdir(SCORE_DIRECTORY) == 0);
#else
	assert(mkdir(SCORE_DIRECTORY, 0700) == 0);
#endif
	uint8_t legacy_scores[364];
	uint8_t legacy_track[1802];
	for (size_t index = 0; index < sizeof(legacy_scores); index++) {
		legacy_scores[index] = (uint8_t)(index * 13U);
	}
	for (size_t index = 0; index < sizeof(legacy_track); index++) {
		legacy_track[index] = (uint8_t)(index * 19U);
	}
	write_bytes(LEGACY_SCORE_PATH, legacy_scores, sizeof(legacy_scores));
	write_bytes(LEGACY_TRACK_PATH, legacy_track, sizeof(legacy_track));
	struct CSTRK_TRACK track;
	fixture(&track);
	write_track(&track);
	test_creation_and_wide_times(&track);
	test_stale_table_merge(&track);
	test_track_identity(&track);
	test_failure_preserves_scores(&track);
	test_full_table(&track);
	check_bytes(LEGACY_SCORE_PATH, legacy_scores, sizeof(legacy_scores));
	check_bytes(LEGACY_TRACK_PATH, legacy_track, sizeof(legacy_track));
	cstrk_free(&track);
	remove_test_directory();
	puts("Extended high scores preserve wide times, track identity, existing scores and legacy "
		 "files.");
	return 0;
}
