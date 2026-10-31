#include "../fileio.h"
#include "../legacy.h"
#include "../memmgr.h"
#include "../platform.h"
#include "../shape2d/shape2d.h"
#include "../shape3d/shape3d.h"
#include "../timing.h"
#include "../game_input.h"
#include "../ui/ui_text.h"
#include "menu_common.h"
#include "../externs.h"

#define MENU_ANIMATION_PERIOD 60
#define MENU_ANIMATION_SECOND_STATE_START 30
#define BUTTON_TEXT_LINE_BUFFER_SIZE 86U
#define BUTTON_FONT_LINE_HEIGHT 8U
#define BUTTON_CENTER_DIVISOR 2
#define BUTTON_TEXT_VERTICAL_ADJUSTMENT 1

static legacy_s16 menu_animation_counter;
static legacy_s16 menu_highlight_color;
legacy_s16 menu_idle_counter;

void menu_reset_animation_timers(void)
{
	menu_animation_counter = 0;
	menu_highlight_color = 0;
	menu_idle_counter = 0;
}

void menu_update_idle_counter(legacy_u16 elapsed, legacy_s16 limit)
{
	menu_idle_counter = LEGACY_U16_WRAP_ADD(menu_idle_counter, elapsed);
	if (LEGACY_S16_FROM_BITS((legacy_u16)menu_idle_counter) > limit) {
		menu_idle_counter = 0;
		idle_expired = (legacy_u8)(idle_expired + 1U);
	}
}

legacy_s16 menu_animate_button_highlight(legacy_s16 item_index, const struct BUTTON_AREA *buttons,
										 legacy_s16 second_color, legacy_s16 first_color)
{
	legacy_u16 delta = (legacy_u16)timer_get_delta_alt();
	legacy_u16 animation_counter = LEGACY_U16_WRAP_ADD(menu_animation_counter, delta);
	while (LEGACY_S16_FROM_BITS(animation_counter) > MENU_ANIMATION_PERIOD) {
		animation_counter = LEGACY_U16_WRAP_SUB(animation_counter, MENU_ANIMATION_PERIOD);
	}
	menu_animation_counter = animation_counter;
	legacy_s16 selected_color =
		LEGACY_S16_FROM_BITS(animation_counter) > MENU_ANIMATION_SECOND_STATE_START
			? LEGACY_S16_FROM_BITS((legacy_u16)second_color)
			: LEGACY_S16_FROM_BITS((legacy_u16)first_color);
	if (menu_highlight_color != selected_color) {
		menu_highlight_color = selected_color;
		mouse_draw_opaque_check();
		sprite_draw_rect_outline(buttons[item_index].x1, buttons[item_index].y1,
								 buttons[item_index].x2, buttons[item_index].y2, selected_color);
		mouse_draw_transparent_check();
	}
	return LEGACY_S16_FROM_BITS(delta);
}

void draw_button(legacy_s8 far *text, legacy_s16 x, legacy_s16 y, legacy_s16 width,
				 legacy_s16 height, legacy_s16 top_color, legacy_s16 bottom_color,
				 legacy_s16 fill_color, legacy_s16 font_color)
{
	sprite_fill_rect(x, y, width, height, fill_color);
	draw_beveled_border(x, y, width, height, top_color, top_color, bottom_color, bottom_color);

	if (text == 0) {
		return;
	}

	font_set_colors(font_color, 0);
	legacy_s8 *copied_text = &resID_byte1;
	copy_string(copied_text, text);
	legacy_u16 length = (legacy_u16)strlen(copied_text);
	legacy_u16 line_count = 1;
	for (legacy_u16 source_index = 0; source_index < length; source_index++) {
		if (copied_text[source_index] == ']') {
			line_count++;
		}
	}

	legacy_s16 remaining =
		LEGACY_S16_WRAP_SUB(height, LEGACY_U16_WRAP_MUL(line_count, BUTTON_FONT_LINE_HEIGHT));
	legacy_s16 vertical_offset = LEGACY_S16_WRAP_ADD(
		LEGACY_S16_DIV_OR_ZERO(remaining, BUTTON_CENTER_DIVISOR), BUTTON_TEXT_VERTICAL_ADJUSTMENT);
	legacy_s8 line[BUTTON_TEXT_LINE_BUFFER_SIZE];
	legacy_u16 destination_index = 0;
	legacy_u16 line_index = 0;
	for (legacy_u16 source_index = 0; source_index <= length; source_index++) {
		legacy_s8 character = copied_text[source_index];

		if (character != ']' && character != 0) {
			line[destination_index++] = character;
			continue;
		}

		line[destination_index] = 0;
		remaining = LEGACY_S16_WRAP_SUB(width, font_text_width(line));
		legacy_s16 horizontal_offset = LEGACY_S16_DIV_OR_ZERO(remaining, BUTTON_CENTER_DIVISOR);
		font_draw_text(
			line, LEGACY_S16_WRAP_ADD(x, horizontal_offset),
			LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_ADD(y, vertical_offset),
								LEGACY_U16_WRAP_MUL(line_index, BUTTON_FONT_LINE_HEIGHT)));
		line_index++;
		destination_index = 0;
	}
}
