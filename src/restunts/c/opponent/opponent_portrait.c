#include "opponent_portrait.h"
#include "../hires/opponent_artwork.h"

void opponent_portrait_draw(const struct SPRITE *target, const struct SHAPE2D *original,
							legacy_u8 opponent)
{
	hires_opponent_portrait_draw(target, original, opponent);
}

void opponent_portrait_draw_at(const struct SPRITE *target, const struct SHAPE2D *original,
							   legacy_u8 opponent, legacy_s16 x, legacy_s16 y)
{
	hires_opponent_portrait_draw_at(target, original, opponent, x, y);
}

void opponent_portrait_unload(void)
{
	hires_opponent_portrait_unload();
}
