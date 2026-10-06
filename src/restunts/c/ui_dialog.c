#include "fileio.h"
#include "legacy.h"
#include "memmgr.h"
#include "platform.h"
#include "resource.h"
#include "shape2d.h"
#include "ui_dialog.h"
#include "ui_text.h"
#include "timing.h"
#include "ui_input.h"
#include "game_input.h"
#include "ui_dialog_internal.h"
#include "externs.h"
#include "keyboard.h"
#include "shape3d.h"
#ifdef RESTUNTS_SDL3
#include "../platform/sdl3/controller_game.h"
#endif

#define RST_ASC_CHAR_UPPER 1
#define RST_ASC_CHAR_LOWER 2
#define RST_ASC_CONTROL 32
#define ASCII_CASE_OFFSET 32U
#define DIALOG_DEFAULT_WIDTH 32
#define DIALOG_WIDTH_PADDING 24U
#define DIALOG_ALIGNMENT_MASK 65528U
#define DIALOG_SCREEN_WIDTH 320
#define DIALOG_SCREEN_HEIGHT 200
#define DIALOG_CENTER_DIVISOR 2
#define DIALOG_CONTENT_WIDTH_REDUCTION 16
#define DIALOG_NO_SELECTION 255U
#define FILE_DIALOG_SEPARATOR_WIDTH 171
#define FILE_DIALOG_LIST_WIDTH 162
#define FILE_DIALOG_DIRECTORY_MAX_LENGTH 18
#define DIALOG_INPUT_TIMEOUT 30000UL
#define SECURITY_DIALOG_Y 120U
#define DIALOG_LINE_BUFFER_CAPACITY 128
#define DIALOG_CHOICE_BUFFER_CAPACITY 80
#define DIALOG_CHOICE_CAPACITY 20
#define DIALOG_LINE_HEIGHT_PADDING 2
#define DIALOG_PARAGRAPH_HEIGHT 4
#define DIALOG_CHOICE_PARAGRAPH_HEIGHT 3
#define DIALOG_PLACEHOLDER_POSITION_STRIDE 2U
#define DIALOG_DELAY_TICKS 8UL

static const legacy_u8 far quiz_question_suffixes[20] = {'0', '1', '2', '3', '4', '5', '6',
														 '7', '8', '9', 'a', 'b', 'c', 'd',
														 'e', 'f', 'g', 'h', 'i', 'j'};
static const legacy_u8 far g_ascii_props[256] = {
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

static legacy_u16 dialog_ascii_lower(legacy_u16 character)
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

struct DIALOG_CONTENT {
	legacy_s8 line_buffer[DIALOG_LINE_BUFFER_CAPACITY];
	legacy_s8 far *choice_texts[DIALOG_CHOICE_CAPACITY];
	legacy_u8 choice_lengths[DIALOG_CHOICE_CAPACITY];
	struct BUTTON_AREA choices[DIALOG_CHOICE_CAPACITY];
	legacy_s8 far *cursor;
	legacy_s16 line_height;
	legacy_s16 dialog_width;
	legacy_s16 dialog_height;
	legacy_s16 x;
	legacy_s16 y;
	legacy_u16 line_length;
	legacy_u8 choice_count;
	legacy_u8 placeholder_index;
};

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

struct FILE_DIALOG {
	legacy_s16 positions[40];
	struct BUTTON_AREA hit_areas[10];
	legacy_s8 filenames[128][13];
	legacy_s8 selected;
	legacy_s8 scroll;
	legacy_s8 previous_selected;
	legacy_s8 previous_scroll;
	legacy_u8 file_count;
	legacy_u8 search_again;
};

static void file_dialog_draw_frame(struct FILE_DIALOG *dialog, const legacy_s8 *directory,
								   legacy_s8 far *prompt)
{
	preRender_line(dialog->positions[4] - 4, dialog->positions[5] + 4,
				   dialog->positions[4] + FILE_DIALOG_SEPARATOR_WIDTH, dialog->positions[5] + 4,
				   dialog_border_color);
	font_set_colors(dialog_fnt_colour, dialog_background_color);
	copy_string(&resID_byte1, prompt);
	font_draw_text_opaque(&resID_byte1, dialog->positions[0], dialog->positions[1]);
	for (legacy_u16 index = 0; index < 10U; index++) {
		dialog->hit_areas[index].x1 = dialog->positions[2];
		dialog->hit_areas[index].x2 = dialog->positions[2] + FILE_DIALOG_LIST_WIDTH;
		if (index == 9U) {
			dialog->hit_areas[index].y1 = dialog->hit_areas[index - 1U].y1 + 10;
		} else {
			dialog->hit_areas[index].y1 = dialog->positions[3U + index * 2U];
		}
		dialog->hit_areas[index].y2 = dialog->hit_areas[index].y1 + 10;
	}
	font_set_colors(dialog_fnt_colour, dialog_background_color);
	font_draw_text_opaque(directory, dialog->positions[2], dialog->positions[3]);
}

static legacy_u16 file_dialog_edit_directory(struct FILE_DIALOG *dialog, legacy_s8 *directory)
{
	font_set_colors(dialog_fnt_colour, dialog_background_color);
	return (legacy_u16)call_read_line(directory, FILE_DIALOG_DIRECTORY_MAX_LENGTH,
									  dialog->positions[2], dialog->positions[3],
									  DIALOG_INPUT_TIMEOUT);
}

static void file_dialog_collect_names(struct FILE_DIALOG *dialog, const legacy_s8 *found_path)
{
	parse_filepath_separators(dialog->filenames[dialog->file_count++], found_path);
	while (dialog->file_count < 128U && (found_path = file_find_next_alt()) != 0) {
		parse_filepath_separators(dialog->filenames[dialog->file_count++], found_path);
	}
	for (legacy_u16 index = 0; index + 1U < dialog->file_count; index++) {
		for (legacy_u16 compare_index = index + 1U; compare_index < dialog->file_count;
			 compare_index++) {
			if (strcmp(dialog->filenames[index], dialog->filenames[compare_index]) > 0) {
				strcpy(&resID_byte1, dialog->filenames[index]);
				strcpy(dialog->filenames[index], dialog->filenames[compare_index]);
				strcpy(dialog->filenames[compare_index], &resID_byte1);
			}
		}
	}
}

static void file_dialog_draw_scroll_labels(struct FILE_DIALOG *dialog)
{
	if (dialog->file_count > 7U) {
		copy_string(&resID_byte1, locate_text_res(mainresptr, file_scroll_up_label_id));
		font_draw_text_opaque(&resID_byte1, font_centered_text_x(&resID_byte1),
							  dialog->hit_areas[1].y1);
		copy_string(&resID_byte1, locate_text_res(mainresptr, file_scroll_down_label_id));
		font_draw_text_opaque(&resID_byte1, font_centered_text_x(&resID_byte1),
							  dialog->hit_areas[9].y1 - 1);
	}
}

static void file_dialog_draw_rows(struct FILE_DIALOG *dialog)
{
	dialog->previous_selected = dialog->selected;
	dialog->previous_scroll = dialog->scroll;
	mouse_draw_opaque_check();
	for (legacy_u16 visible_row = 0; visible_row < 7U; visible_row++) {
		legacy_s16 candidate = (legacy_s16)(dialog->scroll + (legacy_s16)visible_row);
		if (candidate == dialog->selected) {
			font_set_colors(dialog_background_color, dialog_fnt_colour);
		} else {
			font_set_colors(dialog_fnt_colour, dialog_background_color);
		}
		if (candidate < (legacy_s16)dialog->file_count) {
			strcpy(&resID_byte1, dialog->filenames[(legacy_u8)candidate]);
			font_draw_text_opaque(&resID_byte1, dialog->positions[2],
								  dialog->hit_areas[visible_row + 2U].y1);
		} else {
			font_draw_text_opaque("        ", dialog->positions[2],
								  dialog->hit_areas[visible_row + 2U].y1);
		}
		legacy_u16 text_width = (legacy_u16)font_text_width(&resID_byte1);
		sprite_fill_rect(dialog->positions[2] + text_width, dialog->hit_areas[visible_row + 2U].y1,
						 dialog->positions[2] + FILE_DIALOG_LIST_WIDTH - text_width -
							 dialog->positions[2],
						 8, dialog_background_color);
	}
	mouse_draw_transparent_check();
}

static legacy_u16 file_dialog_apply_mouse(struct FILE_DIALOG *dialog, legacy_s16 hit,
										  legacy_u16 key)
{
	if (hit == -1) {
		return key;
	}
	if (hit != 0 && hit != 1 && hit != 9) {
		legacy_s16 candidate = (legacy_s16)(dialog->scroll + hit - 2);
		if (candidate < (legacy_s16)dialog->file_count) {
			dialog->selected = (legacy_s8)candidate;
		}
		return key;
	}
	if ((mouse_butstate & 3U) == 0) {
		return key;
	}
	if (hit == 0) {
		dialog->selected = 0;
		dialog->scroll = -1;
	} else if (hit == 1) {
		if ((legacy_s16)dialog->selected + dialog->scroll != 0) {
			dialog->selected--;
		}
		if (dialog->selected < dialog->scroll) {
			dialog->scroll = dialog->selected;
		}
	} else {
		if (dialog->selected != (legacy_s8)(dialog->file_count - 1U)) {
			dialog->selected++;
		}
	}
	return 0;
}

static legacy_s8 file_dialog_apply_key(struct FILE_DIALOG *dialog, legacy_u16 key)
{
	if (key == KEY_ENTER || key == KEY_SPACE) {
		return 1;
	}
	if (key == KEY_ESCAPE) {
		return -1;
	}
	if (key == KEY_UP) {
		dialog->selected--;
	} else if (key == KEY_DOWN) {
		if (dialog->selected != (legacy_s8)(dialog->file_count - 1U)) {
			dialog->selected++;
		}
	} else if (key < 256U &&
			   (g_ascii_props[key] & (RST_ASC_CHAR_UPPER | RST_ASC_CHAR_LOWER)) != 0) {
		legacy_u8 character = (legacy_u8)dialog_ascii_lower(key);
		for (legacy_u16 index = 0; index < dialog->file_count; index++) {
			if ((legacy_u8)dialog_ascii_lower((legacy_u8)dialog->filenames[index][0]) ==
				character) {
				dialog->selected = (legacy_s8)index;
				break;
			}
		}
	}
	return 0;
}

static legacy_s8 file_dialog_choose(struct FILE_DIALOG *dialog, legacy_s8 *directory,
									legacy_s8 *filename)
{
	dialog->selected = 0;
	dialog->scroll = 0;
	dialog->previous_selected = -1;
	dialog->previous_scroll = -1;
	(void)timer_get_delta_alt();
	dialog->search_again = 0;
	legacy_s8 result = 0;
	for (;;) {
		if (dialog->selected != dialog->previous_selected ||
			dialog->scroll != dialog->previous_scroll) {
			file_dialog_draw_rows(dialog);
		}
		legacy_u16 key = (legacy_u16)input_checking((legacy_s16)timer_get_delta_alt());
		legacy_s16 hit = (legacy_s16)mouse_multi_hittest(10, dialog->hit_areas);
		key = file_dialog_apply_mouse(dialog, hit, key);
		result = file_dialog_apply_key(dialog, key);
		if (dialog->selected < dialog->scroll) {
			dialog->scroll = dialog->selected;
		}
		if (dialog->scroll < 0) {
			key = file_dialog_edit_directory(dialog, directory);
			if (key == KEY_ESCAPE) {
				result = 0;
			} else {
				dialog->search_again = 1;
			}
			break;
		}
		while ((legacy_s16)(dialog->scroll + 6) < dialog->selected) {
			dialog->scroll++;
		}
		if (result == 0) {
			continue;
		}
		if (result < 0) {
			result = 0;
			break;
		}
		strcpy(filename, dialog->filenames[(legacy_u8)dialog->selected]);
		result = 1;
		break;
	}
	return result;
}

static legacy_s8 do_fileselect_dialog_impl(legacy_s8 *directory, legacy_s8 *filename,
										   legacy_s8 *extension, legacy_s8 far *prompt)
{
	struct FILE_DIALOG dialog;
	legacy_s16 dialog_result = LEGACY_S16_FROM_BITS(show_text_input_dialog(
		DIALOG_SAVE_BACKGROUND, locate_text_res(mainresptr, file_load_dialog_id),
		DIALOG_AUTO_POSITION, DIALOG_AUTO_POSITION, dialog_border_color, dialog.positions));
	if (dialog_result < 0) {
		return 0;
	}
	legacy_u8 saved_busy = g_is_busy;
	g_is_busy = 1;
	file_dialog_draw_frame(&dialog, directory, prompt);
	legacy_s8 result;
	for (;;) {
		mouse_draw_transparent_check();
		dialog.file_count = 0;
		const legacy_s8 *found_path = file_combine_and_find(directory, "*", extension);
		if (found_path == 0) {
			if (file_dialog_edit_directory(&dialog, directory) == KEY_ESCAPE) {
				result = 0;
				break;
			}
			continue;
		}
		file_dialog_collect_names(&dialog, found_path);
		file_dialog_draw_scroll_labels(&dialog);
		result = file_dialog_choose(&dialog, directory, filename);
		if (dialog.search_again == 0) {
			break;
		}
	}
	sprite_pop_background();
	g_is_busy = saved_busy;
	return result;
}

legacy_s8 do_fileselect_dialog(legacy_s8 *directory, legacy_s8 *filename, legacy_s8 *extension,
							   legacy_s8 far *prompt)
{
#ifdef RESTUNTS_SDL3
	sdl3_input_push_modal();
#endif
	legacy_s8 result = do_fileselect_dialog_impl(directory, filename, extension, prompt);
#ifdef RESTUNTS_SDL3
	sdl3_input_pop_modal();
#endif
	return result;
}

void ensure_file_exists(legacy_s16 file_index)
{
	static legacy_s8 *const message_ids[] = {missing_disk1_message_id, missing_disk2_message_id,
											 missing_disk3_message_id, missing_disk4_message_id};

	legacy_s8 *message_id = message_ids[file_index - 1];
	while (file_find(findfilenames[file_index]) == 0) {
		show_dialog(DIALOG_TYPE_ACKNOWLEDGEMENT, DIALOG_SAVE_BACKGROUND,
					locate_text_res(mainresptr, message_id), -1, -1, dialog_border_color, 0, 0);
		mouse_draw_opaque_check();
		kbormouse = 0;
	}
}

void show_waiting(void)
{
	show_dialog(DIALOG_TYPE_MESSAGE, DIALOG_NO_BACKGROUND_SAVE,
				locate_text_res(mainresptr, waiting_message_id), -1, waitflag, dialog_border_color,
				0, 0);
	mouse_draw_opaque_check();
}

static legacy_s16 do_savefile_dialog_impl(legacy_s8 *primary, legacy_s8 *secondary,
										  legacy_s8 far *prompt)
{
	legacy_s16 positions[6];
	legacy_s16 result = LEGACY_S16_FROM_BITS(show_text_input_dialog(
		DIALOG_SAVE_BACKGROUND, locate_text_res(mainresptr, file_save_dialog_id),
		DIALOG_AUTO_POSITION, DIALOG_AUTO_POSITION, dialog_border_color, positions));
	if (result < 0) {
		return 0;
	}

	font_set_colors(dialog_fnt_colour, dialog_background_color);
	copy_string(&resID_byte1, prompt);
	font_draw_text_opaque(&resID_byte1, positions[0], positions[1]);
	font_set_colors(dialog_fnt_colour, dialog_background_color);
	font_draw_text_opaque(primary, positions[2], positions[3]);
	font_draw_text_opaque(secondary, positions[4], positions[5]);
	mouse_draw_transparent_check();

	result = 0;
	for (;;) {
		legacy_s16 key = LEGACY_S16_FROM_BITS(
			call_read_line(secondary, 8, positions[4], positions[5], DIALOG_INPUT_TIMEOUT));
		for (legacy_s16 character_index = 0; secondary[character_index] != 0; character_index++) {
			if (secondary[character_index] == ' ') {
				secondary[character_index] = '_';
			}
		}
		if (key == KEY_ESCAPE) {
			break;
		}
		if (key == KEY_ENTER) {
			result = 1;
			break;
		}
		key =
			LEGACY_S16_FROM_BITS(call_read_line(primary, FILE_DIALOG_DIRECTORY_MAX_LENGTH,
												positions[2], positions[3], DIALOG_INPUT_TIMEOUT));
		if (key == KEY_ESCAPE) {
			break;
		}
	}

	sprite_pop_background();
	return result;
}

legacy_s16 do_savefile_dialog(legacy_s8 *primary, legacy_s8 *secondary, legacy_s8 far *prompt)
{
#ifdef RESTUNTS_SDL3
	sdl3_input_push_modal();
#endif
	legacy_s16 result = do_savefile_dialog_impl(primary, secondary, prompt);
#ifdef RESTUNTS_SDL3
	sdl3_input_pop_modal();
#endif
	return result;
}

legacy_s16 show_disk_error_dialog(void)
{
	input_push_status();
	legacy_s16 result;
	if (g_is_busy != 0) {
		result = show_dialog(DIALOG_TYPE_MENU, DIALOG_SAVE_BACKGROUND,
							 locate_text_res(mainresptr, disk_retry_dialog_id), -1, -1,
							 dialog_border_color, 0, 0) == 0;
	} else {
		show_dialog(DIALOG_TYPE_ACKNOWLEDGEMENT, DIALOG_SAVE_BACKGROUND,
					locate_text_res(mainresptr, disk_error_dialog_id), -1, -1, dialog_border_color,
					0, 0);
		result = 1;
	}
	input_pop_status();
	return result;
}

void security_check(legacy_s16 question_index)
{
	legacy_s8 question_id[4] = "q00";
	question_id[2] = quiz_question_suffixes[(legacy_u16)question_index];
	legacy_s8 answer_id[4] = "a00";
	answer_id[2] = question_id[2];
	void far *resource = file_load_resfile("misc");
	legacy_s8 question_text[1024];
	copy_string(question_text, locate_text_res(resource, "cop"));
	copy_string(&resID_byte1, locate_text_res(resource, question_id));
	strcat(question_text, resource_text_payload);
	legacy_u8 question_parts[6];
	for (legacy_u16 i = 0; i < 6U; i++) {
		question_parts[i] = (legacy_u8)(&resID_byte1)[i];
	}

	legacy_s16 positions[8];
	show_text_input_dialog(DIALOG_SAVE_BACKGROUND, (void far *)question_text, DIALOG_AUTO_POSITION,
						   SECURITY_DIALOG_Y, performGraphColor, positions);
	(&resID_byte1)[2] = 0;
	(&resID_byte1)[0] = question_parts[0];
	(&resID_byte1)[1] = question_parts[1];
	font_draw_text(&resID_byte1, positions[0], positions[1]);
	(&resID_byte1)[0] = question_parts[2];
	(&resID_byte1)[1] = question_parts[3];
	font_draw_text(&resID_byte1, positions[2], positions[3]);
	(&resID_byte1)[0] = question_parts[4];
	(&resID_byte1)[1] = question_parts[5];
	font_draw_text(&resID_byte1, positions[4], positions[5]);

	copy_string(&resID_byte1, locate_text_res(resource, answer_id));
	legacy_u16 answer_length = (legacy_u16)strlen(&resID_byte1);
	legacy_s8 answer[22];
	answer[0] = 0;
	legacy_u16 attempts = 0;
	for (;;) {
		call_read_line(answer, answer_length, positions[6], positions[7], DIALOG_INPUT_TIMEOUT);
		for (legacy_u16 i = 0; answer[i] != 0; i++) {
			legacy_u8 character = (legacy_u8)answer[i];

			if ((g_ascii_props[character] & RST_ASC_CHAR_UPPER) != 0) {
				answer[i] = (legacy_s8)(character + ASCII_CASE_OFFSET);
			}
		}
		if (strcmp(answer, &resID_byte1) == 0) {
			passed_security = 1;
			break;
		}
		attempts++;
		if (passed_security != 0 || attempts == 3U) {
			break;
		}
	}

	sprite_pop_background();
	mouse_draw_transparent_check();
	unload_resource(resource);
}
