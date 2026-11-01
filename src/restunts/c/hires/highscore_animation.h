#ifndef RESTUNTS_HIRES_HIGHSCORE_ANIMATION_H
#define RESTUNTS_HIRES_HIGHSCORE_ANIMATION_H

#include "../legacy.h"

#ifdef RESTUNTS_SDL3
struct HIRES_HIGHSCORE_ANIMATION {
	legacy_s16 width, height;
	legacy_u8 drawn;
};

struct END_SCREEN_STATE;
struct SPRITE;
typedef void (*HIRES_HIGHSCORE_DRAW_FRAME)(legacy_s8 far *resource, legacy_u8 far *sequence,
										   legacy_u8 frame, legacy_s16 x, legacy_s16 y,
										   struct SPRITE far *sprite, legacy_u8 direct_copy);

void hires_highscore_reset_animation(struct END_SCREEN_STATE *screen);
void hires_highscore_load_animation(const struct END_SCREEN_STATE *screen);
void hires_highscore_show_animation(struct END_SCREEN_STATE *screen, legacy_s16 animation_width,
									legacy_s16 animation_height);
void hires_highscore_update_animation(struct END_SCREEN_STATE *screen,
									  HIRES_HIGHSCORE_DRAW_FRAME draw_frame);
legacy_u8 hires_highscore_display_input(legacy_u16 input);
#endif

#endif
