#include "cshig.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t cshig_magic[8] = {'C', 'S', 'H', 'I', 'G', 0x1a, '\r', '\n'};

static int cshig_fail(char *error, size_t size, const char *format, ...)
{
	if (error != NULL && size != 0U) {
		va_list args;
		va_start(args, format);
		vsnprintf(error, size, format, args);
		va_end(args);
	}
	return 0;
}

static void cshig_success(char *error, size_t size)
{
	if (error != NULL && size != 0U) {
		error[0] = '\0';
	}
}

static uint16_t cshig_read16(const uint8_t *data)
{
	return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

static uint32_t cshig_read32(const uint8_t *data)
{
	return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) | ((uint32_t)data[2] << 16U) |
		   ((uint32_t)data[3] << 24U);
}

static uint64_t cshig_read64(const uint8_t *data)
{
	return (uint64_t)cshig_read32(data) | ((uint64_t)cshig_read32(data + 4U) << 32U);
}

static void cshig_write16(uint8_t *data, uint16_t value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8U);
}

static void cshig_write32(uint8_t *data, uint32_t value)
{
	for (unsigned index = 0; index < 4U; index++) {
		data[index] = (uint8_t)(value >> (index * 8U));
	}
}

static void cshig_write64(uint8_t *data, uint64_t value)
{
	cshig_write32(data, (uint32_t)value);
	cshig_write32(data + 4U, (uint32_t)(value >> 32U));
}

void cshig_init(struct CSHIG_TABLE *table)
{
	if (table != NULL) {
		memset(table, 0, sizeof(*table));
		for (unsigned index = 0; index < CSHIG_ENTRY_COUNT; index++) {
			table->entries[index].ticks = CSHIG_EMPTY_TICKS;
		}
	}
}

void cshig_free(struct CSHIG_TABLE *table)
{
	if (table != NULL) {
		free(table->track_data);
		cshig_init(table);
	}
}

static int cshig_compare_objects(const void *left, const void *right)
{
	return memcmp(left, right, CSTRK_OBJECT_SIZE);
}

static int cshig_canonical_track(const struct CSTRK_TRACK *track, uint8_t **data, size_t *size,
								 char *error, size_t error_size)
{
	if (track == NULL) {
		return cshig_fail(error, error_size, "A track is required for its highscore table");
	}
	struct CSTRK_TRACK gameplay = *track;
	gameplay.notes_length = 0;
	gameplay.notes[0] = '\0';
	uint8_t *encoded = NULL;
	size_t encoded_size = 0;
	if (!cstrk_encode(&gameplay, &encoded, &encoded_size, error, error_size)) {
		return 0;
	}
	size_t objects_offset = CSTRK_HEADER_SIZE +
							((size_t)track->width + 1U) * ((size_t)track->height + 1U) +
							(size_t)track->width * track->height * 2U;
	if (track->object_count > 1U) {
		qsort(encoded + objects_offset, track->object_count, CSTRK_OBJECT_SIZE,
			  cshig_compare_objects);
	}
	*data = encoded;
	*size = encoded_size;
	return 1;
}

static int cshig_canonical_bytes(const uint8_t *data, size_t size, char *error, size_t error_size)
{
	if (data == NULL || size < CSTRK_HEADER_SIZE || size > CSHIG_MAX_TRACK_SIZE) {
		return cshig_fail(error, error_size, "Highscore track identity has an invalid size");
	}
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	if (!cstrk_decode(data, size, &track, error, error_size)) {
		return 0;
	}
	uint8_t *canonical = NULL;
	size_t canonical_size = 0;
	int valid = cshig_canonical_track(&track, &canonical, &canonical_size, error, error_size);
	cstrk_free(&track);
	if (!valid) {
		return 0;
	}
	int same = canonical_size == size && memcmp(data, canonical, size) == 0;
	free(canonical);
	if (!same) {
		return cshig_fail(error, error_size, "Highscore track identity is not canonical");
	}
	return 1;
}

static int cshig_text_valid(const uint8_t *text, size_t size)
{
	int terminated = 0;
	for (size_t index = 0; index < size; index++) {
		uint8_t value = text[index];
		if (value == 0U) {
			terminated = 1;
		} else if (value < 32U || value == 127U) {
			return 0;
		}
	}
	return terminated;
}

static int cshig_entry_valid(const struct CSHIG_ENTRY *entry, char *error, size_t error_size)
{
	if (entry == NULL || !cshig_text_valid(entry->player_name, sizeof(entry->player_name)) ||
		!cshig_text_valid(entry->car_name, sizeof(entry->car_name)) ||
		!cshig_text_valid(entry->opponent, sizeof(entry->opponent))) {
		return cshig_fail(error, error_size,
						  "Highscore names contain invalid or unterminated text");
	}
	if (entry->ticks == 0U) {
		return cshig_fail(error, error_size, "Highscore times must be positive or empty");
	}
	return 1;
}

static int cshig_entries_valid(const struct CSHIG_TABLE *table, char *error, size_t error_size)
{
	if (table == NULL) {
		return cshig_fail(error, error_size, "A highscore table is required");
	}
	uint64_t previous = 0;
	for (unsigned index = 0; index < CSHIG_ENTRY_COUNT; index++) {
		if (!cshig_entry_valid(&table->entries[index], error, error_size)) {
			return 0;
		}
		if (table->entries[index].ticks < previous) {
			return cshig_fail(error, error_size,
							  "Highscore rows must be ordered with empty rows last");
		}
		previous = table->entries[index].ticks;
	}
	return 1;
}

int cshig_create(struct CSHIG_TABLE *table, const struct CSTRK_TRACK *track, char *error,
				 size_t error_size)
{
	if (table == NULL) {
		return cshig_fail(error, error_size, "A highscore table is required");
	}
	struct CSHIG_TABLE next;
	cshig_init(&next);
	if (!cshig_canonical_track(track, &next.track_data, &next.track_size, error, error_size)) {
		return 0;
	}
	cshig_free(table);
	*table = next;
	cshig_success(error, error_size);
	return 1;
}

int cshig_validate(const struct CSHIG_TABLE *table, char *error, size_t error_size)
{
	if (!cshig_entries_valid(table, error, error_size) ||
		!cshig_canonical_bytes(table->track_data, table->track_size, error, error_size)) {
		return 0;
	}
	cshig_success(error, error_size);
	return 1;
}

int cshig_decode(const uint8_t *data, size_t size, struct CSHIG_TABLE *table, char *error,
				 size_t error_size)
{
	if (table == NULL || data == NULL || size < CSHIG_HEADER_SIZE || size > CSHIG_MAX_FILE_SIZE) {
		return cshig_fail(error, error_size, "Highscore file is missing, truncated, or too large");
	}
	if (memcmp(data, cshig_magic, sizeof(cshig_magic)) != 0 ||
		cshig_read16(data + 8U) != CSHIG_VERSION || cshig_read16(data + 10U) != CSHIG_HEADER_SIZE ||
		cshig_read32(data + 12U) != 0U || cshig_read16(data + 20U) != CSHIG_ENTRY_COUNT ||
		cshig_read16(data + 22U) != CSHIG_ENTRY_SIZE || cshig_read32(data + 28U) != 0U) {
		return cshig_fail(error, error_size, "Unsupported Chocolate Stunts highscore header");
	}
	size_t track_size = cshig_read32(data + 16U);
	size_t records_size = CSHIG_ENTRY_COUNT * CSHIG_ENTRY_SIZE;
	if (track_size > CSHIG_MAX_TRACK_SIZE ||
		cshig_read32(data + 24U) != track_size + records_size ||
		size != CSHIG_HEADER_SIZE + track_size + records_size) {
		return cshig_fail(error, error_size, "Highscore payload length does not match its header");
	}
	if (!cshig_canonical_bytes(data + CSHIG_HEADER_SIZE, track_size, error, error_size)) {
		return 0;
	}
	struct CSHIG_TABLE next;
	cshig_init(&next);
	next.track_data = malloc(track_size);
	if (next.track_data == NULL) {
		return cshig_fail(error, error_size, "Cannot allocate highscore track identity");
	}
	memcpy(next.track_data, data + CSHIG_HEADER_SIZE, track_size);
	next.track_size = track_size;
	const uint8_t *record = data + CSHIG_HEADER_SIZE + track_size;
	for (unsigned index = 0; index < CSHIG_ENTRY_COUNT; index++, record += CSHIG_ENTRY_SIZE) {
		struct CSHIG_ENTRY *entry = &next.entries[index];
		memcpy(entry->player_name, record, CSHIG_PLAYER_NAME_BYTES);
		memcpy(entry->car_name, record + 17U, CSHIG_CAR_NAME_BYTES);
		entry->car_flag = record[41];
		memcpy(entry->opponent, record + 42U, CSHIG_OPPONENT_BYTES);
		entry->ticks = cshig_read64(record + 50U);
	}
	if (!cshig_entries_valid(&next, error, error_size)) {
		cshig_free(&next);
		return 0;
	}
	cshig_free(table);
	*table = next;
	cshig_success(error, error_size);
	return 1;
}

int cshig_encode(const struct CSHIG_TABLE *table, uint8_t **data, size_t *size, char *error,
				 size_t error_size)
{
	if (data == NULL || size == NULL) {
		return cshig_fail(error, error_size, "Highscore output arguments are required");
	}
	if (!cshig_validate(table, error, error_size)) {
		return 0;
	}
	size_t payload = table->track_size + CSHIG_ENTRY_COUNT * CSHIG_ENTRY_SIZE;
	size_t encoded_size = CSHIG_HEADER_SIZE + payload;
	uint8_t *encoded = calloc(encoded_size, 1U);
	if (encoded == NULL) {
		return cshig_fail(error, error_size, "Cannot allocate highscore file");
	}
	memcpy(encoded, cshig_magic, sizeof(cshig_magic));
	cshig_write16(encoded + 8U, CSHIG_VERSION);
	cshig_write16(encoded + 10U, CSHIG_HEADER_SIZE);
	cshig_write32(encoded + 16U, (uint32_t)table->track_size);
	cshig_write16(encoded + 20U, CSHIG_ENTRY_COUNT);
	cshig_write16(encoded + 22U, CSHIG_ENTRY_SIZE);
	cshig_write32(encoded + 24U, (uint32_t)payload);
	memcpy(encoded + CSHIG_HEADER_SIZE, table->track_data, table->track_size);
	uint8_t *record = encoded + CSHIG_HEADER_SIZE + table->track_size;
	for (unsigned index = 0; index < CSHIG_ENTRY_COUNT; index++, record += CSHIG_ENTRY_SIZE) {
		const struct CSHIG_ENTRY *entry = &table->entries[index];
		memcpy(record, entry->player_name, CSHIG_PLAYER_NAME_BYTES);
		memcpy(record + 17U, entry->car_name, CSHIG_CAR_NAME_BYTES);
		record[41] = entry->car_flag;
		memcpy(record + 42U, entry->opponent, CSHIG_OPPONENT_BYTES);
		cshig_write64(record + 50U, entry->ticks);
	}
	*data = encoded;
	*size = encoded_size;
	cshig_success(error, error_size);
	return 1;
}

int cshig_matches_track(const struct CSHIG_TABLE *table, const struct CSTRK_TRACK *track)
{
	if (table == NULL || table->track_data == NULL || table->track_size > CSHIG_MAX_TRACK_SIZE) {
		return 0;
	}
	uint8_t *canonical = NULL;
	size_t size = 0;
	if (!cshig_canonical_track(track, &canonical, &size, NULL, 0)) {
		return 0;
	}
	int same = size == table->track_size && memcmp(canonical, table->track_data, size) == 0;
	free(canonical);
	return same;
}

int cshig_rank(const struct CSHIG_TABLE *table, uint64_t ticks)
{
	if (ticks == 0U || ticks == CSHIG_EMPTY_TICKS || !cshig_entries_valid(table, NULL, 0)) {
		return -1;
	}
	for (unsigned index = 0; index < CSHIG_ENTRY_COUNT; index++) {
		if (ticks < table->entries[index].ticks) {
			return (int)index;
		}
	}
	return -1;
}

int cshig_insert(struct CSHIG_TABLE *table, const struct CSHIG_ENTRY *entry, char *error,
				 size_t error_size)
{
	if (!cshig_entry_valid(entry, error, error_size) || !cshig_validate(table, error, error_size)) {
		return 0;
	}
	int rank = cshig_rank(table, entry->ticks);
	if (rank < 0) {
		return cshig_fail(error, error_size, "The time does not qualify for this highscore table");
	}
	struct CSHIG_ENTRY record = *entry;
	for (unsigned index = CSHIG_ENTRY_COUNT - 1U; index > (unsigned)rank; index--) {
		table->entries[index] = table->entries[index - 1U];
	}
	table->entries[rank] = record;
	cshig_success(error, error_size);
	return 1;
}

int cshig_time_normalize(uint32_t frames, uint16_t frames_per_second, uint64_t *ticks)
{
	if (ticks == NULL || (frames_per_second != 10U && frames_per_second != 20U)) {
		return 0;
	}
	*ticks = (uint64_t)frames * (frames_per_second == 10U ? 2U : 1U);
	return 1;
}
