#include "ui_dialog_private.h"

const legacy_u8 far g_ascii_props[256] = {
	32,	 32, 32,  32,  32,	32,	 32,  32,  32,	40,	 40,  40,  40,	40,	 32,  32,  32,	32,	 32,
	32,	 32, 32,  32,  32,	32,	 32,  32,  32,	32,	 32,  32,  32,	72,	 16,  16,  16,	16,	 16,
	16,	 16, 16,  16,  16,	16,	 16,  16,  16,	16,	 132, 132, 132, 132, 132, 132, 132, 132, 132,
	132, 16, 16,  16,  16,	16,	 16,  16,  129, 129, 129, 129, 129, 129, 1,	  1,   1,	1,	 1,
	1,	 1,	 1,	  1,   1,	1,	 1,	  1,   1,	1,	 1,	  1,   1,	1,	 1,	  16,  16,	16,	 16,
	16,	 16, 130, 130, 130, 130, 130, 130, 2,	2,	 2,	  2,   2,	2,	 2,	  2,   2,	2,	 2,
	2,	 2,	 2,	  2,   2,	2,	 2,	  2,   2,	16,	 16,  16,  16,	32,	 0,	  0,   0,	0,	 0,
	0,	 0,	 0,	  0,   0,	0,	 0,	  0,   0,	0,	 0,	  0,   0,	0,	 0,	  0,   0,	0,	 0,
	0,	 0,	 0,	  0,   0,	0,	 0,	  0,   0,	0,	 0,	  0,   0,	0,	 0,	  0,   0,	0,	 0,
	0,	 0,	 0,	  0,   0,	0,	 0,	  0,   0,	0,	 0,	  0,   0,	0,	 0,	  0,   0,	0,	 0,
	0,	 0,	 0,	  0,   0,	0,	 0,	  0,   0,	0,	 0,	  0,   0,	0,	 0,	  0,   0,	0,	 0,
	0,	 0,	 0,	  0,   0,	0,	 0,	  0,   0,	0,	 0,	  0,   0,	0,	 0,	  0,   0,	0,	 0,
	0,	 0,	 0,	  0,   0,	0,	 0,	  0,   0,	0,	 0,	  0,   0,	0,	 0,	  0,   0,	0,	 0,
	0,	 0,	 0,	  0,   0,	0,	 0,	  0,   0};

void sprite_xor_rect_outline(legacy_s16 left, legacy_s16 top, legacy_s16 right, legacy_s16 bottom,
							 legacy_s16 color)
{
	legacy_s16 x = LEGACY_S16_FROM_BITS(left);
	legacy_s16 y = LEGACY_S16_FROM_BITS(top);
	legacy_s16 width = LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_SUB(right, left), 1);
	legacy_s16 height = LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_SUB(bottom, top), 1);
	if (width > 0) {
		sprite_xor_rect_clipped(x, y, width, 1, color);
		sprite_xor_rect_clipped(x, LEGACY_S16_FROM_BITS(bottom), width, 1, color);
	}
	if (height > 0) {
		y = LEGACY_S16_WRAP_ADD(y, 1);
		sprite_xor_rect_clipped(x, y, 1, height, color);
		sprite_xor_rect_clipped(LEGACY_S16_FROM_BITS(right), y, 1, height, color);
	}
}

legacy_u16 dialog_ascii_lower(legacy_u16 character)
{
	if (character < 256U && (g_ascii_props[character] & RST_ASC_CHAR_UPPER) != 0) {
		character = LEGACY_U16_WRAP_ADD(character, ASCII_CASE_OFFSET);
	}
	return character;
}

static legacy_u16 dialog_finish(legacy_s16 result, legacy_s16 save_background)
{
	if (save_background != 0) {
		sprite_pop_background();
	}
	return (legacy_u16)result;
}

static legacy_s16 dialog_advance_height(legacy_s16 dialog_height, legacy_s16 line_height,
										legacy_u8 separator, legacy_s16 paragraph_height)
{
	if (separator == ']') {
		return LEGACY_S16_WRAP_ADD(dialog_height, line_height);
	}
	return LEGACY_S16_WRAP_ADD(dialog_height, paragraph_height);
}

static void dialog_measure(struct DIALOG_CONTENT *dialog, void far *text_resource)
{
	dialog->line_height = LEGACY_S16_WRAP_ADD(font_glyph_height, DIALOG_LINE_HEIGHT_PADDING);
	dialog->dialog_height = 0;
	dialog->dialog_width = DIALOG_DEFAULT_WIDTH;
	mouse_draw_opaque_check();

	dialog->cursor = (legacy_s8 far *)text_resource;
	dialog->line_length = 0;
	legacy_u8 character;
	while ((character = (legacy_u8)*dialog->cursor) != 0) {
		if (character == ']' || character == '}') {
			dialog->line_buffer[dialog->line_length] = 0;
			legacy_s16 measured_width = (legacy_s16)font_text_width(dialog->line_buffer);
			if (measured_width > dialog->dialog_width) {
				dialog->dialog_width = measured_width;
			}
			dialog->line_length = 0;
			dialog->dialog_height = dialog_advance_height(
				dialog->dialog_height, dialog->line_height, character, DIALOG_PARAGRAPH_HEIGHT);
		} else {
			dialog->line_buffer[dialog->line_length++] = (legacy_s8)character;
		}
		dialog->cursor++;
	}

	dialog->dialog_width = LEGACY_S16_FROM_BITS(
		LEGACY_U16_WRAP_ADD((legacy_u16)dialog->dialog_width, DIALOG_WIDTH_PADDING) &
		DIALOG_ALIGNMENT_MASK);
}

static legacy_s16 dialog_draw_frame(struct DIALOG_CONTENT *dialog, legacy_u16 x_argument,
									legacy_u16 y_argument, legacy_s16 save_background,
									legacy_s16 border_color)
{
	dialog->x = LEGACY_S16_FROM_BITS(x_argument);
	dialog->y = LEGACY_S16_FROM_BITS(y_argument);
	if (dialog->x == -1) {
		dialog->x = LEGACY_S16_DIV_OR_ZERO(
			LEGACY_S16_WRAP_SUB(DIALOG_SCREEN_WIDTH, dialog->dialog_width), DIALOG_CENTER_DIVISOR);
		dialog->x = LEGACY_S16_FROM_BITS((legacy_u16)dialog->x & DIALOG_ALIGNMENT_MASK);
	}
	if (dialog->y == -1) {
		dialog->y =
			LEGACY_S16_DIV_OR_ZERO(LEGACY_S16_WRAP_SUB(DIALOG_SCREEN_HEIGHT, dialog->dialog_height),
								   DIALOG_CENTER_DIVISOR);
	}

	legacy_s16 left = dialog->x;
	legacy_s16 right = LEGACY_S16_WRAP_ADD(dialog->x, dialog->dialog_width);
	legacy_s16 top = LEGACY_S16_WRAP_SUB(dialog->y, DIALOG_FRAME_PADDING);
	legacy_s16 bottom = LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_ADD(dialog->y, dialog->dialog_height),
											DIALOG_FRAME_PADDING);
	dialog->x = LEGACY_S16_WRAP_ADD(dialog->x, DIALOG_FRAME_PADDING);
	dialog->dialog_width =
		LEGACY_S16_WRAP_SUB(dialog->dialog_width, DIALOG_CONTENT_WIDTH_REDUCTION);
	if (save_background != 0 && sprite_push_background(left, right, top, bottom) == 0) {
		return 0;
	}

	sprite_select_screen();
	sprite_set_target_clip_bounds(left, right, top, bottom);
	sprite_clear_target(0);
	sprite_draw_rect_outline(
		LEGACY_S16_WRAP_SUB(dialog->x, 4), LEGACY_S16_WRAP_SUB(dialog->y, 4),
		LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_ADD(dialog->x, dialog->dialog_width), 4),
		LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_ADD(dialog->y, dialog->dialog_height), 4),
		border_color);
	font_set_colors(dialog_fnt_colour, 0);
	dialog_background_color = 0;
	font_set_colors(dialog_fnt_colour, 0);

	return 1;
}

static void dialog_draw_message(struct DIALOG_CONTENT *dialog, void far *text_resource,
								legacy_s16 dialog_type, legacy_s16 *disabled_choices)
{
	dialog->cursor = (legacy_s8 far *)text_resource;
	dialog->line_length = 0;
	dialog->placeholder_index = 0;
	dialog->dialog_height = 1;
	legacy_u8 character;
	while ((character = (legacy_u8)*dialog->cursor) != 0 && character != '[') {
		if (character == ']' || character == '}') {
			dialog->line_buffer[dialog->line_length] = 0;
			font_draw_text_opaque(dialog->line_buffer, dialog->x,
								  LEGACY_S16_WRAP_ADD(dialog->y, dialog->dialog_height));
			dialog->line_length = 0;
			dialog->dialog_height = dialog_advance_height(
				dialog->dialog_height, dialog->line_height, character, DIALOG_PARAGRAPH_HEIGHT);
		} else if (character == '@') {
			if (dialog_type == DIALOG_TYPE_PLACEHOLDERS) {
				dialog->line_buffer[dialog->line_length] = 0;
				disabled_choices[dialog->placeholder_index] = LEGACY_S16_WRAP_ADD(
					dialog->x, (legacy_s16)font_text_width(dialog->line_buffer));
				disabled_choices[dialog->placeholder_index + 1U] =
					LEGACY_S16_WRAP_ADD(dialog->y, dialog->dialog_height);
				dialog->placeholder_index =
					(legacy_u8)(dialog->placeholder_index + DIALOG_PLACEHOLDER_POSITION_STRIDE);
			}
			dialog->line_buffer[dialog->line_length++] = ' ';
		} else {
			dialog->line_buffer[dialog->line_length++] = (legacy_s8)character;
		}
		dialog->cursor++;
	}
}

static void dialog_measure_choices(struct DIALOG_CONTENT *dialog)
{
	dialog->choice_count = 0;
	legacy_u8 character;
	while ((legacy_u8)*dialog->cursor == '[') {
		dialog->cursor++;
		dialog->choice_texts[dialog->choice_count] = dialog->cursor;
		dialog->line_buffer[dialog->line_length] = 0;
		dialog->choices[dialog->choice_count].x1 =
			LEGACY_S16_WRAP_ADD(dialog->x, (legacy_s16)font_text_width(dialog->line_buffer));
		dialog->choices[dialog->choice_count].y1 =
			LEGACY_S16_WRAP_ADD(dialog->y, dialog->dialog_height);
		dialog->choices[dialog->choice_count].y2 =
			LEGACY_S16_WRAP_ADD(dialog->choices[dialog->choice_count].y1, dialog->line_height);
		dialog->line_buffer[dialog->line_length++] = ' ';
		legacy_u16 character_count = 0;
		legacy_u16 choice_width = 0;
		while ((character = (legacy_u8)*dialog->cursor) != 0 && character != '[') {
			if (character == ']' || character == '}') {
				dialog->line_buffer[dialog->line_length] = 0;
				choice_width = (legacy_u16)font_text_width(dialog->line_buffer);
				dialog->line_length = 0;
				dialog->dialog_height =
					dialog_advance_height(dialog->dialog_height, dialog->line_height, character,
										  DIALOG_CHOICE_PARAGRAPH_HEIGHT);
			} else {
				dialog->line_buffer[dialog->line_length++] = (legacy_s8)character;
				character_count++;
			}
			dialog->cursor++;
		}
		dialog->choice_lengths[dialog->choice_count] = (legacy_u8)character_count;
		dialog->line_buffer[dialog->line_length] = 0;
		if (choice_width == 0) {
			choice_width = (legacy_u16)font_text_width(dialog->line_buffer);
		}
		dialog->choices[dialog->choice_count].x2 =
			LEGACY_S16_WRAP_ADD(dialog->choices[dialog->choice_count].x1, (legacy_s16)choice_width);
		dialog->choice_count++;
	}

	if (dialog->choice_count > 2U && dialog->choices[0].x1 == dialog->choices[1].x1 &&
		dialog->choices[1].x1 == dialog->choices[2].x1) {
		for (legacy_u16 index = 0; index < dialog->choice_count; index++) {
			dialog->choices[index].x2 =
				LEGACY_S16_WRAP_ADD(dialog->choices[index].x1, dialog->dialog_width);
		}
	}
	mouse_draw_transparent_check();
}

static void dialog_draw_choices(struct DIALOG_CONTENT *dialog, legacy_u8 selected,
								legacy_s16 *disabled_choices)
{
	mouse_draw_opaque_check();
	legacy_u16 copied;
	legacy_s8 choice_buffer[DIALOG_CHOICE_BUFFER_CAPACITY];
	for (legacy_u16 index = 0; index < dialog->choice_count; index++) {
		if (selected == (legacy_u8)index) {
			font_set_colors(dialog_background_color, dialog_fnt_colour);
		} else {
			font_set_colors(dialog_fnt_colour, dialog_background_color);
		}
		if (disabled_choices != 0 && disabled_choices[index] != 0) {
			font_set_colors(performGraphColor, dialog_background_color);
		}
		for (copied = 0; copied < dialog->choice_lengths[index]; copied++) {
			choice_buffer[copied] = dialog->choice_texts[index][copied];
		}
		choice_buffer[copied] = 0;
		font_draw_text_opaque(choice_buffer, dialog->choices[index].x1, dialog->choices[index].y1);
	}
	mouse_draw_transparent_check();
}

static legacy_u16 dialog_choice_hotkey(legacy_s8 far *cursor)
{
	legacy_u16 hotkey;
	do {
		hotkey = (legacy_u8)*cursor++;
	} while (hotkey == ' ');
	return dialog_ascii_lower(hotkey);
}

static legacy_u16 dialog_apply_hotkey(legacy_u16 input, legacy_u16 first_hotkey,
									  legacy_u16 second_hotkey, legacy_u8 *selected)
{
	input = dialog_ascii_lower(input);
	if (input == first_hotkey) {
		*selected = 0;
		return KEY_ENTER;
	}
	if (input == second_hotkey) {
		*selected = 1;
		return KEY_ENTER;
	}
	return input;
}

static void dialog_select_previous(legacy_u8 *selected, legacy_u8 choice_count,
								   legacy_s16 *disabled_choices)
{
	do {
		*selected = *selected == 0 ? (legacy_u8)(choice_count - 1U) : (legacy_u8)(*selected - 1U);
	} while (disabled_choices != 0 && disabled_choices[*selected] != 0);
}

static void dialog_select_next(legacy_u8 *selected, legacy_u8 choice_count,
							   legacy_s16 *disabled_choices)
{
	do {
		*selected = (legacy_u8)(*selected + 1U);
		if (*selected >= choice_count) {
			*selected = 0;
		}
	} while (disabled_choices != 0 && disabled_choices[*selected] != 0);
}

static legacy_s16 dialog_apply_menu_input(legacy_u16 input, legacy_u8 *selected,
										  legacy_u8 choice_count, legacy_s16 *disabled_choices)
{
	if (input == KEY_SPACE || input == KEY_ENTER) {
		check_input();
		return 1;
	}
	if (input == KEY_ESCAPE) {
		*selected = DIALOG_NO_SELECTION;
		check_input();
		return 1;
	}
	if (input == KEY_UP || input == KEY_LEFT) {
		dialog_select_previous(selected, choice_count, disabled_choices);
		return 0;
	}
	if (input == KEY_RIGHT || input == KEY_DOWN) {
		dialog_select_next(selected, choice_count, disabled_choices);
	}
	return 0;
}

static legacy_s16 dialog_run_menu(struct DIALOG_CONTENT *dialog, legacy_s16 *disabled_choices,
								  legacy_s16 initial_choice)
{
	legacy_u8 selected = (legacy_u8)initial_choice;
	legacy_u8 previous = DIALOG_NO_SELECTION;
	(void)timer_get_delta_alt();
	mouse_draw_opaque_check();
	legacy_u16 second_hotkey = 0;
	legacy_u16 first_hotkey = 0;
	if (dialog->choice_count == 2U) {
		first_hotkey = dialog_choice_hotkey(dialog->choice_texts[0]);
		second_hotkey = dialog_choice_hotkey(dialog->choice_texts[1]);
	}
	for (;;) {
		if (selected != previous) {
			dialog_draw_choices(dialog, selected, disabled_choices);
			if (previous == DIALOG_NO_SELECTION) {
				check_input();
			}
			previous = selected;
		}
		legacy_u16 input = (legacy_u16)input_checking((legacy_s16)timer_get_delta_alt());
		legacy_s16 hit = (legacy_s16)mouse_multi_hittest(dialog->choice_count, dialog->choices);
		if (hit != -1 && (disabled_choices == 0 || disabled_choices[hit] == 0)) {
			selected = (legacy_u8)hit;
		}
		if (dialog->choice_count == 2U && input != 0) {
			input = dialog_apply_hotkey(input, first_hotkey, second_hotkey, &selected);
		}
		if (input != 0 &&
			dialog_apply_menu_input(input, &selected, dialog->choice_count, disabled_choices)) {
			break;
		}
	}
	return LEGACY_S8_FROM_BITS(selected);
}

static legacy_u16 show_dialog_impl(legacy_s16 dialog_type, legacy_s16 save_background,
								   void far *text_resource, legacy_u16 x_argument,
								   legacy_u16 y_argument, legacy_s16 border_color,
								   legacy_s16 *disabled_choices, legacy_s16 initial_choice)
{
	struct DIALOG_CONTENT dialog;
	dialog_measure(&dialog, text_resource);
	if (!dialog_draw_frame(&dialog, x_argument, y_argument, save_background, border_color)) {
		return DIALOG_FAILURE_RESULT;
	}
	dialog_draw_message(&dialog, text_resource, dialog_type, disabled_choices);
	dialog_measure_choices(&dialog);
#ifdef RESTUNTS_AMIGA
	/* These paths do not poll keyboard or mouse, so present before returning
	 * to resource loading or waiting on a timer that only advances time. */
	if (dialog_type == DIALOG_TYPE_MESSAGE || dialog_type == DIALOG_TYPE_DELAY) {
		amiga_video_present();
	}
#endif
	if (dialog_type == DIALOG_TYPE_MESSAGE) {
		return 0;
	}
	legacy_u16 input;
	legacy_s16 result = 1;
	if (dialog_type == DIALOG_TYPE_ACKNOWLEDGEMENT) {
		do {
			input = (legacy_u16)input_checking((legacy_s16)timer_get_delta_alt());
		} while (input == 0);
		if (input == KEY_ESCAPE) {
			result = 0;
		}
		check_input();
		return dialog_finish(result, save_background);
	}
	if (dialog_type == DIALOG_TYPE_PLACEHOLDERS) {
		return LEGACY_U16_DIV_OR_ZERO(dialog.placeholder_index, DIALOG_PLACEHOLDER_POSITION_STRIDE);
	}
	if (dialog_type == DIALOG_TYPE_DELAY) {
		(void)slow_timer_wait_ticks(DIALOG_DELAY_TICKS);
		return dialog_finish(result, save_background);
	}
	if (dialog_type != DIALOG_TYPE_MENU) {
		return dialog_finish(result, save_background);
	}

	result = dialog_run_menu(&dialog, disabled_choices, initial_choice);
	return dialog_finish(result, save_background);
}

legacy_u16 show_dialog(legacy_s16 dialog_type, legacy_s16 save_background, void far *text_resource,
					   legacy_u16 x_argument, legacy_u16 y_argument, legacy_s16 border_color,
					   legacy_s16 *disabled_choices, legacy_s16 initial_choice)
{
#ifdef RESTUNTS_SDL3
	sdl3_input_push_modal();
#endif
	legacy_u16 result =
		show_dialog_impl(dialog_type, save_background, text_resource, x_argument, y_argument,
						 border_color, disabled_choices, initial_choice);
#ifdef RESTUNTS_SDL3
	sdl3_input_pop_modal();
#endif
	return result;
}
