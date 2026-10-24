#ifndef RESTUNTS_RACE_ROUTE_H
#define RESTUNTS_RACE_ROUTE_H

#include "legacy.h"

void configure_dual_way_switching(legacy_s16 argc, legacy_s8 *argv[]);
legacy_s16 detect_penalty(legacy_s16 *current_track, legacy_s16 *penalty_count);
void update_penalty_display(void);
void update_player_route_penalty(void);
void update_player_route_guidance(legacy_s8 previous_route_status);

#endif
