#ifndef RESTUNTS_MENU_COMMON_H
#define RESTUNTS_MENU_COMMON_H

#include "../game_input.h"

enum MENU_BLIT_MODE { MENU_BLIT_MODE_REFRESH = 254, MENU_BLIT_MODE_INITIAL = 255 };
extern legacy_s16 button_top_color;
extern legacy_s16 button_bottom_color;
extern legacy_s16 button_fill_color;
extern legacy_s16 menu_highlight_second_color;
extern legacy_s16 menu_highlight_first_color;
extern legacy_s16 menu_idle_counter;

void draw_button(legacy_s8 far *text, legacy_s16 x, legacy_s16 y, legacy_s16 width,
				 legacy_s16 height, legacy_s16 top_color, legacy_s16 bottom_color,
				 legacy_s16 fill_color, legacy_s16 font_color);

void menu_reset_animation_timers(void);

void menu_update_idle_counter(legacy_u16 elapsed, legacy_s16 limit);

legacy_s16 menu_animate_button_highlight(legacy_s16 item_index, const struct BUTTON_AREA *buttons,
										 legacy_s16 second_color, legacy_s16 first_color);

#endif
