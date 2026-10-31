#ifndef RESTUNTS_OPPONENT_PORTRAIT_H
#define RESTUNTS_OPPONENT_PORTRAIT_H

#include "../legacy.h"

struct SPRITE;
struct SHAPE2D;

void opponent_portrait_draw(const struct SPRITE *target, const struct SHAPE2D *original,
							legacy_u8 opponent);
/* Overlay enhanced artwork at an explicit tile position, retaining the original digit mask. */
void opponent_portrait_draw_at(const struct SPRITE *target, const struct SHAPE2D *original,
							   legacy_u8 opponent, legacy_s16 x, legacy_s16 y);
void opponent_portrait_unload(void);

#endif
