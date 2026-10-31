#ifndef RESTUNTS_OPPONENT_ANIMATION_H
#define RESTUNTS_OPPONENT_ANIMATION_H

#include "../legacy.h"

struct SPRITE;

/* Optional silent WebM/VP8 clips. Outcomes are relative to the opponent.
 * Keep the original animation underneath for disabled HyperVision or failures. */
void opponent_animation_load(legacy_u8 opponent, legacy_u8 won);
void opponent_animation_unload(void);
/* Draw at logical VGA coordinates. Playback starts on the first visible draw,
 * follows WebM timestamps, and loops. Returns zero when the original is needed. */
legacy_s32 opponent_animation_draw(const struct SPRITE *target, legacy_s16 x, legacy_s16 y,
								   legacy_s16 width, legacy_s16 height);

#endif
