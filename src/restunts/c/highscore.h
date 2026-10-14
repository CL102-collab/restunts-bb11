#ifndef RESTUNTS_HIGHSCORE_H
#define RESTUNTS_HIGHSCORE_H

#include <stddef.h>
#include "legacy.h"

#define HIGHSCORE_ENTRY_COUNT 7U
#define HIGHSCORE_LAST_ENTRY_INDEX (HIGHSCORE_ENTRY_COUNT - 1U)
#define HIGHSCORE_PLAYER_NAME_BYTES 17U
#define HIGHSCORE_CAR_NAME_BYTES 24U
#define HIGHSCORE_OPPONENT_BYTES 8U
#define HIGHSCORE_COMBINED_NAME_TEXT_BYTES                                                         \
	(HIGHSCORE_PLAYER_NAME_BYTES + HIGHSCORE_CAR_NAME_BYTES - 1U)
#define HIGHSCORE_OPPONENT_TEXT_BYTES (HIGHSCORE_OPPONENT_BYTES - 1U)
#define HIGHSCORE_ENTRY_SIZE_BYTES 52U
#define HIGHSCORE_CAR_NAME_OFFSET 17U
#define HIGHSCORE_CAR_FLAG_OFFSET 41U
#define HIGHSCORE_OPPONENT_OFFSET 42U
#define HIGHSCORE_TIME_OFFSET 50U
#define HIGHSCORE_TABLE_SIZE_BYTES (HIGHSCORE_ENTRY_COUNT * HIGHSCORE_ENTRY_SIZE_BYTES)

#pragma pack(push, 1)

/* One line of a track's .HIG table. The file is the raw array, so the
 * layout is fixed by the on-disk format. The two name areas each hold a
 * pair of strings laid end to end. */
struct HIGHSCORE_ENTRY {
	legacy_s8 player_name[HIGHSCORE_PLAYER_NAME_BYTES];
	legacy_s8 car_name[HIGHSCORE_CAR_NAME_BYTES];
	legacy_u8 car_flag;
	legacy_s8 opponent[HIGHSCORE_OPPONENT_BYTES];
	legacy_u16 time;
};

#pragma pack(pop)

typedef char highscore_entry_must_have_expected_size
	[(sizeof(struct HIGHSCORE_ENTRY) == HIGHSCORE_ENTRY_SIZE_BYTES) ? 1 : -1];
typedef char highscore_entry_car_name_must_have_expected_offset
	[(offsetof(struct HIGHSCORE_ENTRY, car_name) == HIGHSCORE_CAR_NAME_OFFSET) ? 1 : -1];
typedef char highscore_entry_car_flag_must_have_expected_offset
	[(offsetof(struct HIGHSCORE_ENTRY, car_flag) == HIGHSCORE_CAR_FLAG_OFFSET) ? 1 : -1];
typedef char highscore_entry_opponent_must_have_expected_offset
	[(offsetof(struct HIGHSCORE_ENTRY, opponent) == HIGHSCORE_OPPONENT_OFFSET) ? 1 : -1];
typedef char highscore_entry_time_must_have_expected_offset
	[(offsetof(struct HIGHSCORE_ENTRY, time) == HIGHSCORE_TIME_OFFSET) ? 1 : -1];

/* The table stays in its DOS file representation even on big-endian hosts. */
static inline legacy_u16 highscore_entry_time(const struct HIGHSCORE_ENTRY far *entry)
{
	return LEGACY_READ_U16_LE((const legacy_u8 far *)entry + HIGHSCORE_TIME_OFFSET);
}

static inline void highscore_entry_set_time(struct HIGHSCORE_ENTRY far *entry, legacy_u16 time)
{
	LEGACY_WRITE_U16_LE((legacy_u8 far *)entry + HIGHSCORE_TIME_OFFSET, time);
}

legacy_s16 highscore_load_or_create(legacy_s16 create_default);

void print_highscore_entry(legacy_s16 entry, legacy_u8 *text_offsets);
extern legacy_s16 ranking_entry_order[HIGHSCORE_ENTRY_COUNT];
extern legacy_s8 highscore_player_name_input[];

#endif
