#include "timing.h"
#include "platform.h"
#include "shape2d.h"
#include "ui_input.h"
#ifdef RESTUNTS_SDL3
#include "../platform/sdl3/controller_game.h"
#endif
#include "ui_text.h"
#include "game_input.h"
#include "resource_bytes.h"
#include "externs.h"
#include "keyboard.h"

#ifdef __ANDROID__
#include "../platform/sdl3/sdl3.h"
#endif

#define SPRITE_BLIT_IMMEDIATE_MODE 65534U
#define READ_LINE_CLEAR_TEXT 1U
#define READ_LINE_CURSOR_AT_START 2U
#define READ_LINE_RETAIN_INITIAL_TEXT 4U
#define READ_LINE_IGNORE_DOWN_KEY 8U
#define READ_LINE_IGNORE_TAB_KEY 16U
#define TEXT_EDIT_MINIMUM_CHARACTER 32
#define TEXT_EDIT_MAXIMUM_CHARACTER 122
#define TEXT_EDIT_CHARACTER_WIDTH 9U
#define TEXT_EDIT_INSERT_MARGIN 2U
#define TEXT_EDIT_NARROW_CURSOR_WIDTH 1U
#define TEXT_EDIT_WIDE_CURSOR_WIDTH 8U
#define TEXT_EDIT_CURSOR_BLINK_TICKS 4UL
#define SPRITE_BLIT_PHASE_COUNT 4U
#define FONT_DEFINITION_HEIGHT_OFFSET 18U
#define FONT_DEFINITION_BACKGROUND_COLOR_OFFSET 2U

static legacy_u16 text_edit_cursor_width;
static legacy_u16 text_edit_x;
static legacy_u16 text_edit_y;
static legacy_u16 text_edit_cursor_visible;
static legacy_s8 *text_edit_buffer;
static legacy_u16 text_edit_max_pixels;
static legacy_u16 text_edit_cursor;

legacy_s16 call_read_line(legacy_s8 *text, legacy_s16 max_characters, legacy_s16 x, legacy_s16 y,
						  legacy_u32 timeout)
{
	mouse_draw_opaque_check();
	legacy_u16 max_pixels = LEGACY_U16_WRAP_ADD(
		LEGACY_U16_WRAP_MUL(max_characters, TEXT_EDIT_CHARACTER_WIDTH), TEXT_EDIT_CHARACTER_WIDTH);
	legacy_s16 result = read_line(READ_LINE_CURSOR_AT_START, text, 0, max_characters, max_pixels, x,
								  y, &dos_kb_clear_numlock, timeout);
	mouse_draw_transparent_check();

	legacy_u16 length = (legacy_u16)strlen(text);
	while (length != 0 && text[length - 1U] == ' ') {
		length--;
	}
	text[length] = 0;
	return result;
}

legacy_s16 sprite_blit_to_video(struct SPRITE far *sprite, legacy_s16 mode)
{
	sprite_select_screen_compat();
	mouse_draw_opaque_check();
	if ((legacy_u16)mode == SPRITE_BLIT_IMMEDIATE_MODE) {
		sprite_putimage(sprite->sprite_bitmapptr);
		mouse_draw_transparent_check();
		return 0;
	}

	legacy_s16 result = 0;
	for (legacy_u16 phase = 0; phase < SPRITE_BLIT_PHASE_COUNT; ++phase) {
		result = input_do_checking((legacy_s16)timer_get_delta_alt());
		if (result != 0) {
			break;
		}
		sprite_draw_dissolve_phase(sprite->sprite_bitmapptr, phase);
	}
	if (result != 0) {
		sprite_select_screen_compat();
		sprite_putimage(sprite->sprite_bitmapptr);
	}
	mouse_draw_transparent_check();
	return result;
}

static void read_line_delete_character(legacy_s8 *text, legacy_s16 max_characters)
{
	legacy_u16 index = (legacy_u16)text_edit_cursor;
	while (LEGACY_S16_FROM_BITS(index) < LEGACY_S16_FROM_BITS(max_characters)) {
		text[index] = text[LEGACY_U16_WRAP_ADD(index, 1U)];
		index = LEGACY_U16_WRAP_ADD(index, 1U);
	}
	text[LEGACY_U16_WRAP_SUB(max_characters, 1U)] = ' ';
	text_edit_redraw();
}

static void read_line_erase_character(legacy_s8 *text, legacy_s16 max_characters,
									  legacy_s16 move_left)
{
	text_edit_toggle_cursor();
	if (move_left != 0) {
		text_edit_cursor = LEGACY_U16_WRAP_SUB(text_edit_cursor, 1U);
	}
	read_line_delete_character(text, max_characters);
	text_edit_toggle_cursor();
}

struct READ_LINE_STATE {
	legacy_u8 input_flags;
	legacy_s8 *text;
	legacy_s16 max_characters;
	legacy_s16 insert_mode;
	legacy_s16 first_key;
};

static void read_line_initialize(struct READ_LINE_STATE *state, legacy_s16 max_pixels, legacy_s16 x,
								 legacy_s16 y)
{
	sprite_select_screen();
	text_edit_x = (legacy_u16)x;
	text_edit_y = (legacy_u16)y;
	text_edit_buffer = state->text;
	text_edit_max_pixels = (legacy_u16)max_pixels;
	state->text[(legacy_u16)state->max_characters] = 0;
	if ((state->input_flags & READ_LINE_CLEAR_TEXT) != 0) {
		state->text[0] = 0;
	}
	if ((state->input_flags & READ_LINE_CURSOR_AT_START) != 0) {
		text_edit_cursor = 0;
	} else {
		text_edit_cursor = (legacy_u16)strlen(state->text);
	}
	legacy_u16 length = (legacy_u16)strlen(state->text);
	while (LEGACY_S16_FROM_BITS(length) < LEGACY_S16_FROM_BITS(state->max_characters)) {
		state->text[length] = ' ';
		length = LEGACY_U16_WRAP_ADD(length, 1U);
	}
	text_edit_redraw();
	text_edit_cursor_width = TEXT_EDIT_NARROW_CURSOR_WIDTH;
	text_edit_cursor_visible = 1;
	state->insert_mode = 0;
	text_edit_toggle_cursor();
}

static legacy_u16 read_line_wait_key(legacy_s16 *initial_key, void(far *callback)(void))
{
	legacy_u16 key;
	if ((legacy_u16)*initial_key != 0) {
		key = (legacy_u16)*initial_key;
		*initial_key = 0;
		return key;
	}
	do {
		callback();
		key = (legacy_u16)kb_call_readchar_callback();
		if (key != 0) {
			break;
		}
	} while (slow_timer_deadline_reached() == 0);
	return key;
}

static legacy_s16 read_line_blink_cursor(legacy_u32 timeout)
{
	slow_timer_set_deadline(TEXT_EDIT_CURSOR_BLINK_TICKS);
	legacy_u16 old_cursor_state = (legacy_u16)text_edit_cursor_visible;
	text_edit_cursor_visible = 1;
	text_edit_toggle_cursor();
	text_edit_cursor_visible = old_cursor_state != 0 ? 0 : 1;
	if (timeout != 0 && timer_deadline_reached()) {
		text_edit_toggle_cursor();
		return 1;
	}
	return 0;
}

static legacy_s16 read_line_is_finished(legacy_u16 key, legacy_u8 input_flags)
{
	return key == KEY_ENTER || key == KEY_ESCAPE || key == KEY_UP ||
		   (key == KEY_DOWN && (input_flags & READ_LINE_IGNORE_DOWN_KEY) == 0) ||
		   (key == KEY_TAB && (input_flags & READ_LINE_IGNORE_TAB_KEY) == 0);
}

static void read_line_move_cursor(struct READ_LINE_STATE *state, legacy_u16 key)
{
	text_edit_toggle_cursor();
	switch (key) {
		case KEY_RIGHT:
			if (LEGACY_S16_FROM_BITS(state->max_characters) >
				LEGACY_S16_FROM_BITS(text_edit_cursor)) {
				text_edit_cursor = LEGACY_U16_WRAP_ADD(text_edit_cursor, 1U);
			}
			break;
		case KEY_LEFT:
			if (text_edit_cursor != 0) {
				text_edit_cursor = LEGACY_U16_WRAP_SUB(text_edit_cursor, 1U);
			}
			break;
		case KEY_HOME:
			text_edit_cursor = 0;
			break;
		case KEY_END:
			text_edit_cursor = (legacy_u16)strlen(state->text);
			break;
		case KEY_INSERT:
			state->insert_mode = !state->insert_mode;
			text_edit_cursor_width =
				state->insert_mode ? TEXT_EDIT_WIDE_CURSOR_WIDTH : TEXT_EDIT_NARROW_CURSOR_WIDTH;
			break;
	}
	text_edit_toggle_cursor();
}

static legacy_s16 read_line_apply_edit_key(struct READ_LINE_STATE *state, legacy_u16 key)
{
	switch (key) {
		case KEY_RIGHT:
		case KEY_LEFT:
		case KEY_HOME:
		case KEY_END:
		case KEY_INSERT:
			read_line_move_cursor(state, key);
			return 1;
		case KEY_DELETE:
			if (LEGACY_S16_FROM_BITS(state->max_characters) >
					LEGACY_S16_FROM_BITS(text_edit_cursor) &&
				state->text[(legacy_u16)text_edit_cursor] != 0) {
				read_line_erase_character(state->text, state->max_characters, 0);
			}
			return 1;
		case KEY_BACKSPACE:
			if (text_edit_cursor != 0) {
				read_line_erase_character(state->text, state->max_characters, 1);
			}
			return 1;
	}
	return 0;
}

static void read_line_insert_space(struct READ_LINE_STATE *state)
{
	legacy_u16 move_index = LEGACY_U16_WRAP_SUB(state->max_characters, TEXT_EDIT_INSERT_MARGIN);
	while (LEGACY_S16_FROM_BITS(move_index) >= LEGACY_S16_FROM_BITS(text_edit_cursor)) {
		state->text[LEGACY_U16_WRAP_ADD(move_index, 1U)] = state->text[move_index];
		move_index = LEGACY_U16_WRAP_SUB(move_index, 1U);
	}
}

static void read_line_type_character(struct READ_LINE_STATE *state, legacy_u16 key)
{
	if (LEGACY_S16_FROM_BITS(key) < TEXT_EDIT_MINIMUM_CHARACTER ||
		LEGACY_S16_FROM_BITS(key) > TEXT_EDIT_MAXIMUM_CHARACTER ||
		LEGACY_S16_FROM_BITS(state->max_characters) <= LEGACY_S16_FROM_BITS(text_edit_cursor)) {
		return;
	}
	text_edit_toggle_cursor();
	legacy_u16 index;
	if (state->first_key && (state->input_flags & READ_LINE_RETAIN_INITIAL_TEXT) == 0) {
		text_edit_cursor = 0;
		for (index = 0; LEGACY_S16_FROM_BITS(index) < LEGACY_S16_FROM_BITS(state->max_characters);
			 index = LEGACY_U16_WRAP_ADD(index, 1U)) {
			state->text[index] = ' ';
		}
	}
	index = (legacy_u16)text_edit_cursor;
	if (state->text[index] == 0) {
		state->text[LEGACY_U16_WRAP_ADD(index, 1U)] = 0;
	}
	if (state->insert_mode) {
		read_line_insert_space(state);
	}
	state->text[index] = (legacy_s8)(legacy_u8)key;
	if (LEGACY_S16_FROM_BITS(state->max_characters) > LEGACY_S16_FROM_BITS(text_edit_cursor)) {
		text_edit_cursor = LEGACY_U16_WRAP_ADD(text_edit_cursor, 1U);
	}
	text_edit_redraw();
	text_edit_toggle_cursor();
}

legacy_s16 read_line(legacy_s16 flags, legacy_s8 *text, legacy_s16 initial_key,
					 legacy_s16 max_characters, legacy_s16 max_pixels, legacy_s16 x, legacy_s16 y,
					 void(far *callback)(void), legacy_u32 timeout)
{
#ifdef RESTUNTS_SDL3
	sdl3_input_push_modal();
#endif
	struct READ_LINE_STATE state;
	state.input_flags = (legacy_u8)flags;
	state.text = text;
	state.max_characters = max_characters;
	read_line_initialize(&state, max_pixels, x, y);
#ifdef __ANDROID__
	SDL_StartTextInput(sdl3_video_window());
#endif
	timer_set_deadline(timeout);
	slow_timer_set_deadline(TEXT_EDIT_CURSOR_BLINK_TICKS);
	state.first_key = 1;
	for (;;) {
		legacy_u16 key = read_line_wait_key(&initial_key, callback);
		if (key == 0) {
			if (read_line_blink_cursor(timeout)) {
#ifdef __ANDROID__
				SDL_StopTextInput(sdl3_video_window());
#endif
#ifdef RESTUNTS_SDL3
				sdl3_input_pop_modal();
#endif
				return 0;
			}
			continue;
		}
		timer_set_deadline(timeout);
		if (read_line_is_finished(key, state.input_flags)) {
			text_edit_toggle_cursor();
#ifdef __ANDROID__
			SDL_StopTextInput(sdl3_video_window());
#endif
#ifdef RESTUNTS_SDL3
			sdl3_input_pop_modal();
#endif
			return key;
		}
		if (read_line_apply_edit_key(&state, key)) {
			state.first_key = 0;
			continue;
		}
		read_line_type_character(&state, key);
		state.first_key = 0;
	}
}

void text_edit_toggle_cursor(void)
{
	if (text_edit_cursor_visible == 0) {
		return;
	}
	legacy_u16 length = legacy_near_string_length(text_edit_buffer);
	legacy_u16 cursor = (legacy_u16)text_edit_cursor;
	if (LEGACY_S16_FROM_BITS(length) < LEGACY_S16_FROM_BITS(cursor)) {
		cursor = length;
		text_edit_cursor = cursor;
	}
	legacy_u16 cursor_width = (legacy_u16)font_prefix_width(text_edit_buffer + cursor, 1);
	static const legacy_s8 space[] = " ";
	if (cursor_width == 0) {
		cursor_width = (legacy_u16)font_text_width(space);
	}
	legacy_u16 x = LEGACY_U16_WRAP_ADD(font_prefix_width(text_edit_buffer, cursor), text_edit_x);
	legacy_u8 far *font_definition = active_font_definition;
	legacy_u16 y = LEGACY_U16_WRAP_ADD(
		resource_read_u16le(font_definition + FONT_DEFINITION_HEIGHT_OFFSET), text_edit_y);
	y = LEGACY_U16_WRAP_SUB(y, text_edit_cursor_width);
	legacy_u16 color = resource_read_u16le(font_definition);
	sprite_xor_rect_clipped(
		LEGACY_S16_FROM_BITS(x), LEGACY_S16_FROM_BITS(y), LEGACY_S16_FROM_BITS(cursor_width),
		LEGACY_S16_FROM_BITS(text_edit_cursor_width), LEGACY_S16_FROM_BITS(color));
}

void text_edit_redraw(void)
{
	legacy_u16 length;
	if (text_edit_max_pixels != 0) {
		while (LEGACY_S16_FROM_BITS(font_text_width(text_edit_buffer)) >
			   LEGACY_S16_FROM_BITS(text_edit_max_pixels)) {
			length = legacy_near_string_length(text_edit_buffer);
			if (length == 0) {
				break;
			}
			text_edit_buffer[length - 1U] = 0;
		}
	}
	length = legacy_near_string_length(text_edit_buffer);
	if (LEGACY_S16_FROM_BITS(length) < LEGACY_S16_FROM_BITS(text_edit_cursor)) {
		text_edit_cursor = length;
	}
	font_draw_text_opaque(text_edit_buffer, LEGACY_S16_FROM_BITS(text_edit_x),
						  LEGACY_S16_FROM_BITS(text_edit_y));
	if (text_edit_max_pixels == 0) {
		return;
	}

	legacy_u16 text_width = (legacy_u16)font_text_width(text_edit_buffer);
	legacy_u16 remaining_width = LEGACY_U16_WRAP_SUB(text_edit_max_pixels, text_width);
	if (LEGACY_S16_FROM_BITS(remaining_width) <= 0) {
		return;
	}
	legacy_u8 far *font_definition = active_font_definition;
	sprite_fill_rect_clipped(
		LEGACY_S16_FROM_BITS(LEGACY_U16_WRAP_ADD(text_width, text_edit_x)),
		LEGACY_S16_FROM_BITS(text_edit_y), LEGACY_S16_FROM_BITS(remaining_width),
		LEGACY_S16_FROM_BITS(resource_read_u16le(font_definition + FONT_DEFINITION_HEIGHT_OFFSET)),
		LEGACY_S16_FROM_BITS(
			resource_read_u16le(font_definition + FONT_DEFINITION_BACKGROUND_COLOR_OFFSET)));
}
