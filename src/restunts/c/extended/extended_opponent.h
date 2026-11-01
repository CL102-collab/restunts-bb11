#ifndef RESTUNTS_EXTENDED_OPPONENT_H
#define RESTUNTS_EXTENDED_OPPONENT_H

#include "../legacy.h"
#include "../track/track_types.h"

/* Build a least-cost nonempty lap in the extended route table. Costs are the
 * original opponent's resource entries. Does not modify the table on failure. */
int extended_opponent_build_route(const legacy_u8 costs[256]);

/* Format-specific route adaptation, with no-op legacy fallbacks. */
legacy_s16 extended_opponent_average(legacy_s16 first, legacy_s16 second, legacy_s16 *average);
legacy_s16 extended_opponent_passing_allowed(void);
legacy_s16 extended_opponent_place_points(legacy_s16 track_index, struct VECTOR *first,
										  struct VECTOR *second);
const struct TRKOBJINFO *extended_opponent_route_info(legacy_s16 track_index, legacy_u16 id,
													  const struct TRKOBJINFO *legacy_info);
typedef legacy_u8 (*EXTENDED_OPPONENT_SPEED_LOOKUP)(legacy_u16 index);
void extended_opponent_route_speed(legacy_s16 track_index, legacy_u16 id, legacy_u8 subtype,
								   const struct TRKOBJINFO *info, const struct TRACKOBJECT *object,
								   legacy_s8 *speed, EXTENDED_OPPONENT_SPEED_LOOKUP lookup);
legacy_s16 extended_opponent_load_route(const legacy_u8 far *costs, void far *resource);
#endif
