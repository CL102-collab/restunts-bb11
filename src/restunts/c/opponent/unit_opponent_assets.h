#ifndef RESTUNTS_UNIT_OPPONENT_ASSETS_H
#define RESTUNTS_UNIT_OPPONENT_ASSETS_H

#include "../legacy.h"

struct SHAPE2D;

/* Original resource arrays contain only Clock and the six original opponents. */
struct SHAPE2D *unit_opponent_menu_portrait(legacy_u8 opponent, legacy_s8 **originals);
struct SHAPE2D *unit_opponent_result_portrait(void);
void unit_opponent_portrait_release(void);
legacy_u16 unit_opponent_draw_biography(legacy_u16 page, legacy_u8 show_hint);
const legacy_s8 *unit_opponent_background(void);

#endif
