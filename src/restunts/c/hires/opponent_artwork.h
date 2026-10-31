#ifndef RESTUNTS_HIRES_OPPONENT_ARTWORK_H
#define RESTUNTS_HIRES_OPPONENT_ARTWORK_H

#include "../legacy.h"

struct SPRITE;
struct SHAPE2D;

void hires_opponent_portrait_draw(const struct SPRITE *target, const struct SHAPE2D *original,
								  legacy_u8 opponent);
void hires_opponent_portrait_draw_at(const struct SPRITE *target, const struct SHAPE2D *original,
									 legacy_u8 opponent, legacy_s16 x, legacy_s16 y);
void hires_opponent_portrait_unload(void);
void hires_opponent_animation_load(legacy_u8 opponent, legacy_u8 won);
void hires_opponent_animation_unload(void);
legacy_s32 hires_opponent_animation_draw(const struct SPRITE *target, legacy_s16 x, legacy_s16 y,
										 legacy_s16 width, legacy_s16 height);

#endif
