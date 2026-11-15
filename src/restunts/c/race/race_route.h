#ifndef RESTUNTS_RACE_ROUTE_H
#define RESTUNTS_RACE_ROUTE_H

#include "../legacy.h"

struct VECTORLONG;
struct CARSTATE;

/* Native route recognition state, independent of either packed car/game state.
 * Each observer owns its footprint cache and confirmation history. */
struct RACE_PENALTY_STATE {
	legacy_s16 confirmed_route, previous_route;
	legacy_s16 last_skipped;
	legacy_s16 minimum_column, maximum_column, minimum_row, maximum_row;
	legacy_s8 status, confirmations, laps;
};

legacy_s16 race_penalty_detect(struct RACE_PENALTY_STATE *rules, const struct VECTORLONG *position,
							   legacy_s16 *current_track, legacy_s16 *penalty_count);
/* Returns the committed positive skipped-piece count; callers own timing,
 * presentation and finish accounting. No player or opponent globals mutate. */
legacy_s16 race_penalty_step(struct RACE_PENALTY_STATE *rules, const struct VECTORLONG *position);

void race_penalty_recover_direction(struct RACE_PENALTY_STATE *rules, const struct CARSTATE *car);

void configure_dual_way_switching(legacy_s16 argc, legacy_s8 *argv[]);
legacy_u16 dual_way_switching_is_enabled(void);
legacy_s16 detect_penalty(legacy_s16 *current_track, legacy_s16 *penalty_count);
void update_penalty_display(void);
void update_player_route_penalty(void);
void update_player_route_guidance(legacy_s8 previous_route_status);

#endif
