#ifndef RESTUNTS_UI_INPUT_H
#define RESTUNTS_UI_INPUT_H

#include "../legacy.h"
#include "../shape2d/shape2d.h"

legacy_s16 call_read_line(legacy_s8 *text, legacy_s16 max_characters, legacy_s16 x, legacy_s16 y,
						  legacy_u32 timeout);
legacy_s16 sprite_blit_to_video(struct SPRITE far *sprite, legacy_s16 mode);
legacy_s16 read_line(legacy_s16 flags, legacy_s8 *text, legacy_s16 initial_key,
					 legacy_s16 max_characters, legacy_s16 max_pixels, legacy_s16 x, legacy_s16 y,
					 void(far *callback)(void), legacy_u32 timeout);
void text_edit_toggle_cursor(void);
void text_edit_redraw(void);

#endif
