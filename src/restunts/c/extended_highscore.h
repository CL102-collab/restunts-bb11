#ifndef RESTUNTS_EXTENDED_HIGHSCORE_H
#define RESTUNTS_EXTENDED_HIGHSCORE_H

#include "cshig.h"
#include "legacy.h"

/* SDL3 score I/O uses zero for success and one for failure. Missing tables
 * are created in memory only; a completed score commits the .cshig file.
 * Valid tables for a different course revision appear empty. Corrupt or
 * unreadable files are never replaced. These routines never access .hig. */
int extended_highscore_load_or_create(const legacy_s8 *directory, const legacy_s8 *name,
									  const struct CSTRK_TRACK *track, int create_default);
const struct CSHIG_ENTRY *extended_highscore_entry(unsigned index);
int extended_highscore_validate_track(const legacy_s8 *directory, const legacy_s8 *name,
									  const struct CSTRK_TRACK *track);

/* Queries return one for qualification, zero otherwise. Invalid frame rates
 * normalize to CSHIG_EMPTY_TICKS. Finishing in zero frames does not qualify. */
uint64_t extended_highscore_ticks(uint32_t frames, uint16_t frames_per_second);
int extended_highscore_qualifies(uint32_t frames, uint16_t frames_per_second);

/* Reloads the latest table, rechecks the on-disk track, and atomically inserts
 * a score after existing equal times. The table stays unchanged on failure.
 * A nonqualifying score succeeds with *rank = UINT_MAX and performs no write.
 * The caller checks recording eligibility before requesting a player name. */
int extended_highscore_record(const legacy_s8 *directory, const legacy_s8 *name,
							  const struct CSTRK_TRACK *track, const struct CSHIG_ENTRY *entry,
							  unsigned *rank);

/* Formats normalized 20 Hz ticks as minutes:ss.hh; zero on success.
 * Minutes are space-padded to at least two characters, matching legacy scores.
 * The output buffer stays unchanged on failure. Empty entries show " 0:00.00". */
int extended_highscore_format_time(legacy_s8 *out, size_t size, uint64_t ticks);
const legacy_s8 *extended_highscore_error(void);

#endif
