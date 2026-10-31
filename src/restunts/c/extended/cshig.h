#ifndef RESTUNTS_CSHIG_H
#define RESTUNTS_CSHIG_H

#include <stddef.h>
#include <stdint.h>
#include "cstrk.h"

#define CSHIG_VERSION 1U
#define CSHIG_HEADER_SIZE 32U
#define CSHIG_ENTRY_COUNT 7U
#define CSHIG_ENTRY_SIZE 58U
#define CSHIG_PLAYER_NAME_BYTES 17U
#define CSHIG_CAR_NAME_BYTES 24U
#define CSHIG_OPPONENT_BYTES 8U
#define CSHIG_EMPTY_TICKS UINT64_MAX
#define CSHIG_MAX_TRACK_SIZE                                                                       \
	(CSTRK_HEADER_SIZE + (CSTRK_MAX_DIMENSION + 1U) * (CSTRK_MAX_DIMENSION + 1U) +                 \
	 CSTRK_MAX_DIMENSION * CSTRK_MAX_DIMENSION * 2U + CSTRK_MAX_OBJECTS * CSTRK_OBJECT_SIZE)
#define CSHIG_MAX_FILE_SIZE                                                                        \
	(CSHIG_HEADER_SIZE + CSHIG_MAX_TRACK_SIZE + CSHIG_ENTRY_COUNT * CSHIG_ENTRY_SIZE)

/* In-memory layout only. Strings contain at least one NUL within each field;
 * other bytes may be printable game-font bytes 32..255, excluding DEL. */
struct CSHIG_ENTRY {
	uint8_t player_name[CSHIG_PLAYER_NAME_BYTES];
	uint8_t car_name[CSHIG_CAR_NAME_BYTES];
	uint8_t car_flag;
	uint8_t opponent[CSHIG_OPPONENT_BYTES];
	uint64_t ticks; /* Twentieths of a second; UINT64_MAX marks an empty row. */
};

struct CSHIG_TABLE {
	uint8_t *track_data; /* Owned canonical CSTRK: no notes, sorted object records. */
	size_t track_size;
	struct CSHIG_ENTRY entries[CSHIG_ENTRY_COUNT];
};

/* Initialize before first create/decode. Free is idempotent. Mutations and
 * encode output arguments remain unchanged on failure. All int results are
 * 1 for success and 0 for failure, except rank (index or -1). Encode allocates
 * with malloc, and its caller owns that buffer. */
void cshig_init(struct CSHIG_TABLE *table);
void cshig_free(struct CSHIG_TABLE *table);
int cshig_create(struct CSHIG_TABLE *table, const struct CSTRK_TRACK *track, char *error,
				 size_t error_size);
int cshig_validate(const struct CSHIG_TABLE *table, char *error, size_t error_size);
int cshig_decode(const uint8_t *data, size_t size, struct CSHIG_TABLE *table, char *error,
				 size_t error_size);
int cshig_encode(const struct CSHIG_TABLE *table, uint8_t **data, size_t *size, char *error,
				 size_t error_size);
int cshig_matches_track(const struct CSHIG_TABLE *table, const struct CSTRK_TRACK *track);
/* Ties follow existing equal times. Empty/nonqualifying times return -1.
 * Insert copies entry first, so a table row may be passed as its input. */
int cshig_rank(const struct CSHIG_TABLE *table, uint64_t ticks);
int cshig_insert(struct CSHIG_TABLE *table, const struct CSHIG_ENTRY *entry, char *error,
				 size_t error_size);
int cshig_time_normalize(uint32_t frames, uint16_t frames_per_second, uint64_t *ticks);

#endif
