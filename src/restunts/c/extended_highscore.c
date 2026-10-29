#include "extended_highscore.h"
#include "fileio.h"
#include "platform.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXTENDED_HIGHSCORE_PATH_SIZE 1024U
#define EXTENDED_HIGHSCORE_IO_CHUNK 16384U
#define EXTENDED_HIGHSCORE_MAX_TRACK_BYTES                                                         \
	(CSTRK_HEADER_SIZE + 91U * 91U + 2U * 90U * 90U + CSTRK_OBJECT_SIZE * CSTRK_MAX_OBJECTS +      \
	 CSTRK_MAX_NOTES)
#define EXTENDED_HIGHSCORE_MAX_BYTES CSHIG_MAX_FILE_SIZE
#define EXTENDED_HIGHSCORE_MINUTE_FIELD_WIDTH 2
#define EXTENDED_HIGHSCORE_TIME_BUFFER_SIZE 32U

enum SCORE_READ_RESULT { SCORE_READ_OK, SCORE_READ_FAILED, SCORE_READ_MISSING };

static struct CSHIG_TABLE loaded_scores;
static int scores_available;
static char score_error[192];

const legacy_s8 *extended_highscore_error(void)
{
	return (const legacy_s8 *)score_error;
}

static int score_fail(const char *message)
{
	snprintf(score_error, sizeof(score_error), "%s", message);
	return 1;
}

static int score_path(const legacy_s8 *directory, const legacy_s8 *name, const legacy_s8 *extension,
					  legacy_s8 path[EXTENDED_HIGHSCORE_PATH_SIZE])
{
	if (name == NULL || name[0] == 0 || strlen((const char *)name) > 8U) {
		return score_fail("Invalid track name for the high score table.");
	}
	for (const legacy_s8 *character = name; *character; character++) {
		if ((unsigned char)*character < 32U || strchr("/\\:.\"<>|*?", *character) != NULL) {
			return score_fail("Invalid track name for the high score table.");
		}
	}
	size_t directory_size = directory != NULL ? strlen((const char *)directory) : 0;
	if (directory_size + strlen((const char *)name) + strlen((const char *)extension) + 2U >
		EXTENDED_HIGHSCORE_PATH_SIZE) {
		return score_fail("The high score directory path is too long.");
	}
	file_build_path(directory, name, extension, path);
	return 0;
}

static enum SCORE_READ_RESULT score_read(const legacy_s8 *path, size_t maximum, uint8_t **data,
										 size_t *size)
{
	legacy_u16 handle = dos_file_open(path, DOS_FILE_OPEN_EXISTING);
	if (handle == 0) {
		if (sdl3_file_exists(path) == 0) {
			return SCORE_READ_MISSING;
		}
		score_fail("Unable to open the high score or track file.");
		return SCORE_READ_FAILED;
	}
	legacy_s32 length = -1;
	if (dos_file_seek(handle, 0, DOS_FILE_SEEK_END) == 0) {
		length = dos_file_tell(handle);
	}
	if (length <= 0 || (size_t)length > maximum ||
		dos_file_seek(handle, 0, DOS_FILE_SEEK_BEGIN) != 0) {
		dos_file_close(handle);
		score_fail("Invalid high score or track file size; the file was retained.");
		return SCORE_READ_FAILED;
	}
	uint8_t *bytes = malloc((size_t)length);
	if (bytes == NULL) {
		dos_file_close(handle);
		score_fail("Not enough memory to read the high score table.");
		return SCORE_READ_FAILED;
	}
	size_t offset = 0;
	while (offset < (size_t)length) {
		legacy_u16 count = (legacy_u16)(((size_t)length - offset > EXTENDED_HIGHSCORE_IO_CHUNK)
											? EXTENDED_HIGHSCORE_IO_CHUNK
											: (size_t)length - offset);
		if (dos_file_read(handle, bytes + offset, count) != count) {
			break;
		}
		offset += count;
	}
	int close_failed = dos_file_close(handle) != 0;
	if (offset != (size_t)length || close_failed) {
		free(bytes);
		score_fail("The high score or track file could not be read completely.");
		return SCORE_READ_FAILED;
	}
	*data = bytes;
	*size = (size_t)length;
	return SCORE_READ_OK;
}

static int score_load(const legacy_s8 *path, const struct CSTRK_TRACK *track, int allow_missing,
					  struct CSHIG_TABLE *table)
{
	uint8_t *bytes = NULL;
	size_t size = 0;
	enum SCORE_READ_RESULT result = score_read(path, EXTENDED_HIGHSCORE_MAX_BYTES, &bytes, &size);
	if (result == SCORE_READ_FAILED) {
		return 1;
	}
	if (result == SCORE_READ_MISSING) {
		if (!allow_missing) {
			return score_fail("No Chocolate Stunts high score table exists yet.");
		}
		return cshig_create(table, track, score_error, sizeof(score_error)) ? 0 : 1;
	}
	int valid = cshig_decode(bytes, size, table, score_error, sizeof(score_error));
	free(bytes);
	if (!valid) {
		return 1;
	}
	/* Canonicalization can fail independently of a revision mismatch. Do not
	 * interpret an allocation failure as permission to discard old scores. */
	struct CSHIG_TABLE identity;
	cshig_init(&identity);
	if (!cshig_create(&identity, track, score_error, sizeof(score_error))) {
		return 1;
	}
	if (table->track_size != identity.track_size ||
		memcmp(table->track_data, identity.track_data, identity.track_size) != 0) {
		cshig_free(table);
		*table = identity;
		cshig_init(&identity);
	}
	cshig_free(&identity);
	return 0;
}

static void score_commit_table(struct CSHIG_TABLE *table)
{
	cshig_free(&loaded_scores);
	loaded_scores = *table;
	cshig_init(table);
	scores_available = 1;
}

int extended_highscore_load_or_create(const legacy_s8 *directory, const legacy_s8 *name,
									  const struct CSTRK_TRACK *track, int create_default)
{
	score_error[0] = 0;
	scores_available = 0;
	legacy_s8 path[EXTENDED_HIGHSCORE_PATH_SIZE];
	if (score_path(directory, name, ".cshig", path) != 0) {
		return 1;
	}
	struct CSHIG_TABLE table;
	cshig_init(&table);
	int failed = score_load(path, track, create_default != 0, &table);
	if (!failed) {
		score_commit_table(&table);
	}
	cshig_free(&table);
	return failed;
}

const struct CSHIG_ENTRY *extended_highscore_entry(unsigned index)
{
	return scores_available && index < CSHIG_ENTRY_COUNT ? &loaded_scores.entries[index] : NULL;
}

int extended_highscore_validate_track(const legacy_s8 *directory, const legacy_s8 *name,
									  const struct CSTRK_TRACK *track)
{
	score_error[0] = 0;
	legacy_s8 path[EXTENDED_HIGHSCORE_PATH_SIZE];
	if (score_path(directory, name, ".cstrk", path) != 0) {
		return 1;
	}
	uint8_t *bytes = NULL;
	size_t size = 0;
	enum SCORE_READ_RESULT result =
		score_read(path, EXTENDED_HIGHSCORE_MAX_TRACK_BYTES, &bytes, &size);
	if (result == SCORE_READ_MISSING) {
		return score_fail("The raced Chocolate Stunts track is missing from disk.");
	}
	if (result != SCORE_READ_OK) {
		return 1;
	}
	struct CSTRK_TRACK disk_track;
	struct CSHIG_TABLE identity;
	cstrk_init(&disk_track);
	cshig_init(&identity);
	int valid = cstrk_decode(bytes, size, &disk_track, score_error, sizeof(score_error));
	free(bytes);
	if (valid) {
		valid = cshig_create(&identity, &disk_track, score_error, sizeof(score_error));
	}
	if (valid && !cshig_matches_track(&identity, track)) {
		score_fail("The track on disk has changed since this race; the score was not saved.");
		valid = 0;
	}
	cshig_free(&identity);
	cstrk_free(&disk_track);
	return valid ? 0 : 1;
}

uint64_t extended_highscore_ticks(uint32_t frames, uint16_t frames_per_second)
{
	uint64_t ticks;
	return cshig_time_normalize(frames, frames_per_second, &ticks) ? ticks : CSHIG_EMPTY_TICKS;
}

int extended_highscore_qualifies(uint32_t frames, uint16_t frames_per_second)
{
	uint64_t ticks = extended_highscore_ticks(frames, frames_per_second);
	return scores_available && ticks != 0 && ticks != CSHIG_EMPTY_TICKS &&
		   cshig_rank(&loaded_scores, ticks) >= 0;
}

static int score_write(const legacy_s8 *path, const struct CSHIG_TABLE *table)
{
	uint8_t *bytes = NULL;
	size_t size = 0;
	if (!cshig_encode(table, &bytes, &size, score_error, sizeof(score_error))) {
		return 1;
	}
	legacy_s8 temporary[EXTENDED_HIGHSCORE_PATH_SIZE];
	size_t prefix = 0;
	for (size_t index = 0; path[index]; index++) {
		if (path[index] == '/' || path[index] == '\\' || path[index] == ':') {
			prefix = index + 1U;
		}
	}
	if (prefix + 13U > sizeof(temporary)) {
		free(bytes);
		return score_fail("The high score directory path is too long.");
	}
	memcpy(temporary, path, prefix);
	static unsigned temporary_serial;
	int available = 0;
	for (unsigned attempt = 0; attempt < 65536U; attempt++) {
		snprintf((char *)temporary + prefix, sizeof(temporary) - prefix, "_H%06X.tmp",
				 ++temporary_serial & 0xffffffU);
		legacy_s16 existing = sdl3_file_exists(temporary);
		if (existing == 0) {
			available = 1;
			break;
		} else if (existing < 0) {
			break;
		}
	}
	legacy_u16 handle = available ? dos_file_open(temporary, DOS_FILE_CREATE) : 0;
	if (handle == 0) {
		free(bytes);
		return score_fail("Unable to create the temporary high score file.");
	}
	size_t offset = 0;
	while (offset < size) {
		legacy_u16 count =
			(legacy_u16)((size - offset > EXTENDED_HIGHSCORE_IO_CHUNK) ? EXTENDED_HIGHSCORE_IO_CHUNK
																	   : size - offset);
		if (dos_file_write(handle, bytes + offset, count) != count) {
			break;
		}
		offset += count;
	}
	int close_failed = dos_file_close(handle) != 0;
	free(bytes);
	if (offset != size || close_failed) {
		dos_file_remove(temporary);
		return score_fail("The high score file could not be saved completely.");
	}
	if (sdl3_file_replace(temporary, path) != 0) {
		dos_file_remove(temporary);
		return score_fail("Unable to replace the high score file; the previous file was retained.");
	}
	return 0;
}

int extended_highscore_record(const legacy_s8 *directory, const legacy_s8 *name,
							  const struct CSTRK_TRACK *track, const struct CSHIG_ENTRY *entry,
							  unsigned *rank)
{
	score_error[0] = 0;
	if (entry == NULL || entry->ticks == 0 || entry->ticks == CSHIG_EMPTY_TICKS) {
		return score_fail("A completed race is required to save a high score.");
	}
	if (extended_highscore_validate_track(directory, name, track) != 0) {
		return 1;
	}
	legacy_s8 path[EXTENDED_HIGHSCORE_PATH_SIZE];
	if (score_path(directory, name, ".cshig", path) != 0) {
		return 1;
	}
	struct CSHIG_TABLE table;
	cshig_init(&table);
	int failed = score_load(path, track, 1, &table);
	int position = -1;
	if (!failed) {
		position = cshig_rank(&table, entry->ticks);
		if (position >= 0) {
			failed = !cshig_insert(&table, entry, score_error, sizeof(score_error));
			if (!failed) {
				failed = score_write(path, &table);
			}
		}
	}
	if (!failed) {
		score_commit_table(&table);
		if (rank != NULL) {
			*rank = position >= 0 ? (unsigned)position : UINT_MAX;
		}
	}
	cshig_free(&table);
	return failed;
}

int extended_highscore_format_time(legacy_s8 *out, size_t size, uint64_t ticks)
{
	if (out == NULL || size == 0) {
		return 1;
	}
	if (ticks == CSHIG_EMPTY_TICKS) {
		ticks = 0;
	}
	legacy_u64 seconds = ticks / 20U;
	legacy_s8 text[EXTENDED_HIGHSCORE_TIME_BUFFER_SIZE];
	legacy_s32 length = snprintf((char *)text, sizeof(text),
								 "%*" LEGACY_PRIu64 ":%02" LEGACY_PRIu32 ".%02" LEGACY_PRIu32,
								 EXTENDED_HIGHSCORE_MINUTE_FIELD_WIDTH, seconds / 60U,
								 (legacy_u32)(seconds % 60U), (legacy_u32)(ticks % 20U) * 5U);
	if (length < 0 || (size_t)length >= size || (size_t)length >= sizeof(text)) {
		return 1;
	}
	memcpy(out, text, (size_t)length + 1U);
	return 0;
}
