#include "opponent_animation.h"
#include "../hires/opponent_artwork.h"

void opponent_animation_load(legacy_u8 opponent, legacy_u8 won)
{
	hires_opponent_animation_load(opponent, won);
}

void opponent_animation_unload(void)
{
	hires_opponent_animation_unload();
}

legacy_s32 opponent_animation_draw(const struct SPRITE *target, legacy_s16 x, legacy_s16 y,
								   legacy_s16 width, legacy_s16 height)
{
	return hires_opponent_animation_draw(target, x, y, width, height);
}
