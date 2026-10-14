#include "fileio.h"
#include "legacy.h"
#include "memmgr.h"
#include "menu_internal.h"
#include "platform.h"
#include "replay.h"
#include "resource.h"
#include "shape2d.h"
#include "ui_text.h"
#include "timing.h"
#include "ui_input.h"
#include "game_input.h"
#include "ui_dialog.h"
#include "race_stats.h"
#include "audio_control.h"
#include "car_resources.h"
#include "highscore.h"
#include "menu_common.h"
#include "externs.h"
#include "keyboard.h"
#ifdef RESTUNTS_SDL3
#include "hires.h"
#include "opponent_animation.h"
#include "shape2d_internal.h"
#endif

#define HIGHSCORE_READ_RETRY_CANCEL_RESULT 2

#define HIGHSCORE_NO_HIGHLIGHT LEGACY_U8_MAX
#define HIGHSCORE_EMPTY_TIME LEGACY_U16_MAX
#define HIGHSCORE_FORMAT_BUFFER_SIZE 18
#define HIGHSCORE_TEXT_FIELD_COUNT 4
#define HIGHSCORE_NAME_MAX_CHARACTERS 16
#define HIGHSCORE_NAME_INPUT_TIMEOUT 30000UL
#define HIGHSCORE_LOW_FRAME_RATE_TIME_SCALE 2U

#define HIGHSCORE_TITLE_Y 5
#define HIGHSCORE_HEADING_Y 15
#define HIGHSCORE_FIRST_ROW_Y 25
#define HIGHSCORE_ROW_HEIGHT 10U
#define HIGHSCORE_PLAYER_COLUMN_X 16
#define HIGHSCORE_CAR_COLUMN_X 120
#define HIGHSCORE_OPPONENT_COLUMN_X 224
#define HIGHSCORE_TIME_COLUMN_X 272

#define END_SCREEN_WIDTH 320U
#define END_SCREEN_HEIGHT 200U
#define END_SCREEN_COLOR 15U
#define END_SCREEN_TOP_HEIGHT 100
#define END_SCREEN_BOTTOM_Y 101
#define END_SCREEN_BOTTOM_HEIGHT 99
#define END_SCREEN_TEXT_START_Y 107
#define END_SCREEN_ANIMATION_WIDTH 200U
#define END_SCREEN_ANIMATION_HEIGHT 100U
#define END_SCREEN_ANIMATION_RIGHT 312
#define END_SCREEN_ANIMATION_BOTTOM 99
#define END_SCREEN_ANIMATION_CENTER_SHIFT 1U
#define END_SCREEN_ANIMATION_BORDER_INSET 3
#define END_SCREEN_ANIMATION_BORDER_GROWTH 5
#define END_SCREEN_ANIMATION_FRAME_TICKS 30

#define END_SCREEN_SPEED_FRACTION_BITS 8U
#define END_SCREEN_NUMBER_WIDTH 3

#define END_SCREEN_TEXT_VARIANT_COUNT 3U
#define END_SCREEN_WIN_VARIANT_COUNT 2
#define END_SCREEN_OUTCOME_VARIANT_COUNT 4
#define END_SCREEN_FINISHED_VARIANT_OFFSET 2
#define END_SCREEN_BINARY_RANDOM_MASK 1U
#define END_SCREEN_FOUR_WAY_RANDOM_MASK 3U

#define END_SCREEN_TEXT_WORD_CAPACITY 32
#define END_SCREEN_TEXT_ID_SIZE 4
#define END_SCREEN_TEXT_LEFT 8
#define END_SCREEN_TEXT_TOP 8
#define END_SCREEN_TEXT_LINE_HEIGHT 8
#define END_SCREEN_TEXT_RIGHT_MARGIN 16
#define END_SCREEN_TEXT_OUTPUT_CAPACITY 80U

#define END_SCREEN_TRACK_RESOURCE_INDEX 4
#define END_SCREEN_TRACK_VALIDATION_BYTES 901U
#define END_SCREEN_MENU_AREA_COUNT 5U
#define END_SCREEN_BUTTON_COUNT 4U
#define END_SCREEN_REDUCED_BUTTON_COUNT 3
#define END_SCREEN_MENU_OFFSET_WITHOUT_FIRST_BUTTON (-36)
#define END_SCREEN_LAST_BUTTON_INDEX 3U
#define END_SCREEN_EVALUATION_BUTTON_X 129
#define END_SCREEN_BUTTON_Y 175
#define END_SCREEN_BUTTON_WIDTH 70
#define END_SCREEN_BUTTON_HEIGHT 21
#define END_SCREEN_NO_SCORE_MESSAGE_Y 50

enum HIGHSCORE_READ_OPERATION {
	HIGHSCORE_READ_RETRY_OPERATION = 9,
	HIGHSCORE_READ_ONCE_OPERATION = 10
};

enum END_SCREEN_OUTCOME {
	END_SCREEN_OUTCOME_LOSS = 0,
	END_SCREEN_OUTCOME_WIN = 1,
	END_SCREEN_OUTCOME_NONE = 2
};

static legacy_u8 ranking_highlight;
legacy_s16 ranking_entry_order[HIGHSCORE_ENTRY_COUNT];

legacy_s16 get_super_random(void);

struct RECTANGLE *hiscore_draw_text(legacy_s8 *text, legacy_s16 x, legacy_s16 y, legacy_s16 color,
									legacy_s16 shadow_color)
{
	highscore_text_bounds.left = LEGACY_S16_WRAP_SUB(x, 1);
	highscore_text_bounds.right =
		LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_ADD(x, font_text_width(text)), 1);
	highscore_text_bounds.top = LEGACY_S16_WRAP_SUB(y, 1);
	highscore_text_bounds.bottom =
		LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_ADD(y, font_glyph_height), 1);
	font_set_colors(shadow_color, 0);
	font_draw_text(text, LEGACY_S16_WRAP_ADD(x, 1), LEGACY_S16_WRAP_ADD(y, 1));
	font_draw_text(text, LEGACY_S16_WRAP_SUB(x, 1), LEGACY_S16_WRAP_ADD(y, 1));
	font_draw_text(text, LEGACY_S16_WRAP_ADD(x, 1), LEGACY_S16_WRAP_SUB(y, 1));
	font_draw_text(text, LEGACY_S16_WRAP_SUB(x, 1), LEGACY_S16_WRAP_SUB(y, 1));
	font_set_colors(color, 0);
	font_draw_text(text, LEGACY_S16_FROM_BITS((legacy_u16)x), LEGACY_S16_FROM_BITS((legacy_u16)y));
	return &highscore_text_bounds;
}

void far *highscore_read_with_retry(legacy_s16 operation, const legacy_s8 *filename,
									void far *destination)
{
	if (operation == HIGHSCORE_READ_ONCE_OPERATION) {
		return file_read_nofatal(filename, destination);
	}
	if (operation != HIGHSCORE_READ_RETRY_OPERATION) {
		return 0;
	}
	do {
		void far *result = file_read_nofatal(filename, destination);
		if (result != 0) {
			return result;
		}
	} while (show_disk_error_dialog() != HIGHSCORE_READ_RETRY_CANCEL_RESULT);
	return 0;
}

legacy_s16 highscore_load_or_create(legacy_s16 create_default)
{
	ranking_highlight = HIGHSCORE_NO_HIGHLIGHT;
	for (legacy_u16 entry = 0; entry < HIGHSCORE_ENTRY_COUNT; entry++) {
		ranking_entry_order[entry] = entry;
	}
	file_build_path(track_directory, gameconfig.game_trackname, ".hig", g_path_buf);
	if (create_default == 0) {
		g_is_busy = 1;
		void far *read_result = highscore_read_with_retry(HIGHSCORE_READ_ONCE_OPERATION, g_path_buf,
														  track_highscore_table);
		g_is_busy = 0;
		return read_result == 0 ? 1 : 0;
	}

	struct HIGHSCORE_ENTRY record;
	legacy_u8 *record_bytes = (legacy_u8 *)&record;
	for (legacy_u16 offset = 0; offset < HIGHSCORE_COMBINED_NAME_TEXT_BYTES; offset++) {
		record_bytes[offset] = '.';
	}
	record_bytes[HIGHSCORE_COMBINED_NAME_TEXT_BYTES] = 0;
	record.car_flag = 0;
	record.opponent[0] = '.';
	record.opponent[1] = '.';
	record.opponent[2] = '/';
	for (legacy_u16 offset = 3U; offset < HIGHSCORE_OPPONENT_TEXT_BYTES; offset++) {
		record.opponent[offset] = '.';
	}
	record.opponent[HIGHSCORE_OPPONENT_TEXT_BYTES] = 0;
	highscore_entry_set_time(&record, HIGHSCORE_EMPTY_TIME);
	struct HIGHSCORE_ENTRY far *scores = (struct HIGHSCORE_ENTRY far *)track_highscore_table;
	for (legacy_u16 entry = 0; entry < HIGHSCORE_ENTRY_COUNT; entry++) {
		scores[entry] = record;
	}
	return file_write_fatal(g_path_buf, track_highscore_table, HIGHSCORE_TABLE_SIZE_BYTES) != 0;
}

void highscore_save_sorted(void)
{
	struct HIGHSCORE_ENTRY far *scores = (struct HIGHSCORE_ENTRY far *)track_highscore_table;
	struct HIGHSCORE_ENTRY ordered_scores[HIGHSCORE_ENTRY_COUNT];
	for (legacy_u16 entry = 0; entry < HIGHSCORE_ENTRY_COUNT; entry++) {
		legacy_u16 source_entry = (legacy_u16)ranking_entry_order[entry];
		ordered_scores[entry] = scores[source_entry];
	}
	file_build_path(track_directory, gameconfig.game_trackname, ".hig", g_path_buf);
	g_is_busy = 1;
	(void)file_write_fatal(g_path_buf, ordered_scores, HIGHSCORE_TABLE_SIZE_BYTES);
	g_is_busy = 0;
}

void print_highscore_entry(legacy_s16 entry, legacy_u8 *text_offsets)
{
	struct HIGHSCORE_ENTRY far *scores = (struct HIGHSCORE_ENTRY far *)track_highscore_table;
	struct HIGHSCORE_ENTRY record = scores[ranking_entry_order[entry]];

	text_offsets[0] = 0;
	strcpy(&resID_byte1, record.player_name);
	legacy_u16 output_offset = (legacy_u16)strlen(&resID_byte1) + 1U;
	text_offsets[1] = (legacy_u8)output_offset;
	strcpy(&resID_byte1 + output_offset, record.car_name);
	output_offset =
		LEGACY_U16_WRAP_ADD(output_offset, (legacy_u16)strlen(&resID_byte1 + output_offset) + 1U);
	text_offsets[2] = (legacy_u8)output_offset;

	legacy_s8 *output = &resID_byte1 + output_offset;
	*output = 0;
	if (record.car_flag == 1) {
		strcat(output, "(");
	}
	strcat(output, record.opponent);
	if (record.car_flag == 1) {
		strcat(output, ")");
	}
	output_offset = LEGACY_U16_WRAP_ADD(output_offset, (legacy_u16)strlen(output) + 1U);

	legacy_s16 saved_frame_rate = framespersec;
	framespersec = GAME_FRAME_RATE_NORMAL;
	legacy_s16 frame_count = LEGACY_S16_FROM_BITS(highscore_entry_time(&record));
	legacy_s8 formatted_time[HIGHSCORE_FORMAT_BUFFER_SIZE];
	format_frame_as_string(formatted_time, frame_count == -1 ? 0 : frame_count, 1);
	text_offsets[3] = (legacy_u8)output_offset;
	strcpy(&resID_byte1 + output_offset, formatted_time);
	framespersec = saved_frame_rate;
}

void highscore_draw_table(void)
{
	sprite_select_render_window();
	copy_string(&resID_byte1, locate_text_res(mainresptr, "hs1"));
	strcat(&resID_byte1, " '");
	strcat(&resID_byte1, gameconfig.game_trackname);
	strcat(&resID_byte1, "'");
	hiscore_draw_text(&resID_byte1, font_centered_text_x(&resID_byte1), HIGHSCORE_TITLE_Y,
					  dialog_fnt_colour, 0);

	legacy_s8 far *text = locate_text_res(mainresptr, "hs2");
	copy_string(&resID_byte1, text);
	hiscore_draw_text(&resID_byte1, HIGHSCORE_PLAYER_COLUMN_X, HIGHSCORE_HEADING_Y,
					  dialog_fnt_colour, 0);
	text = locate_text_res(mainresptr, "hs3");
	copy_string(&resID_byte1, text);
	hiscore_draw_text(&resID_byte1, HIGHSCORE_CAR_COLUMN_X, HIGHSCORE_HEADING_Y, dialog_fnt_colour,
					  0);
	text = locate_text_res(mainresptr, "hs5");
	copy_string(&resID_byte1, text);
	hiscore_draw_text(&resID_byte1, HIGHSCORE_OPPONENT_COLUMN_X, HIGHSCORE_HEADING_Y,
					  dialog_fnt_colour, 0);
	text = locate_text_res(mainresptr, "hs4");
	copy_string(&resID_byte1, text);
	hiscore_draw_text(&resID_byte1, HIGHSCORE_TIME_COLUMN_X, HIGHSCORE_HEADING_Y, dialog_fnt_colour,
					  0);

	font_set_fontdef2(fontnptr);
	legacy_u8 text_offsets[HIGHSCORE_TEXT_FIELD_COUNT];
	for (legacy_u16 entry = 0; entry < HIGHSCORE_ENTRY_COUNT; entry++) {
		print_highscore_entry(entry, text_offsets);
		legacy_s16 row = LEGACY_S16_WRAP_ADD(LEGACY_U16_WRAP_MUL(entry, HIGHSCORE_ROW_HEIGHT),
											 HIGHSCORE_FIRST_ROW_Y);
		legacy_s16 color = entry == (legacy_u8)ranking_highlight ? dialog_border_color : 0;
		font_set_colors(color, 0);
		font_draw_text(&resID_byte1 + text_offsets[0], HIGHSCORE_PLAYER_COLUMN_X, row);
		font_draw_text(&resID_byte1 + text_offsets[1], HIGHSCORE_CAR_COLUMN_X, row);
		font_draw_text(&resID_byte1 + text_offsets[2], HIGHSCORE_OPPONENT_COLUMN_X, row);
		font_draw_text(&resID_byte1 + text_offsets[3], HIGHSCORE_TIME_COLUMN_X, row);
	}
	font_set_fontdef();
}

void enter_hiscore(legacy_s16 frame_count, void far *prompt, legacy_u8 car_flag)
{
	legacy_u16 time_bits = (legacy_u16)frame_count;
	if (framespersec == GAME_FRAME_RATE_LOW) {
		time_bits = LEGACY_U16_WRAP_MUL(time_bits, HIGHSCORE_LOW_FRAME_RATE_TIME_SCALE);
	}
	struct HIGHSCORE_ENTRY far *scores = (struct HIGHSCORE_ENTRY far *)track_highscore_table;
	if (highscore_entry_time(&scores[HIGHSCORE_LAST_ENTRY_INDEX]) <= time_bits) {
		highscore_draw_table();
		return;
	}

	legacy_u16 entry = 0;
	while (highscore_entry_time(&scores[entry]) <= time_bits) {
		if (entry >= HIGHSCORE_ENTRY_COUNT) {
			break;
		}
		ranking_entry_order[entry] = (legacy_s16)entry;
		entry++;
	}
	legacy_u16 rank = entry;
	ranking_highlight = (legacy_u8)rank;
	while (entry < HIGHSCORE_LAST_ENTRY_INDEX) {
		ranking_entry_order[entry + 1U] = (legacy_s16)entry;
		entry++;
	}
	ranking_entry_order[rank] = HIGHSCORE_LAST_ENTRY_INDEX;

	struct HIGHSCORE_ENTRY record;
	legacy_u8 *record_bytes = (legacy_u8 *)&record;
	for (legacy_u16 copied = 0; copied < sizeof(record); copied++) {
		record_bytes[copied] = 0;
	}
	strcpy(record.car_name, gnam_string);
	record.car_flag = car_flag;
	if (gameconfig.game_opponenttype != 0) {
		strcpy(record.opponent, opponent_highscore_name);
		record.opponent[2] = '/';
		strcpy(&record.opponent[3], gsna_string);
	} else {
		strcpy(record.opponent, " ");
	}
	highscore_entry_set_time(&record, time_bits);
	scores[HIGHSCORE_LAST_ENTRY_INDEX] = record;

	sprite_select_render_window();
	highscore_draw_table();
	sprite_blit_to_video(render_window_sprite, -1);
	legacy_s16 positions[2];
	show_text_input_dialog(DIALOG_NO_BACKGROUND_SAVE, prompt, DIALOG_AUTO_POSITION,
						   DIALOG_AUTO_POSITION, dialog_border_color, positions);
	check_input();
	call_read_line(highscore_player_name_input, HIGHSCORE_NAME_MAX_CHARACTERS, positions[0],
				   positions[1], HIGHSCORE_NAME_INPUT_TIMEOUT);
	strcpy(record.player_name, highscore_player_name_input);
	scores[HIGHSCORE_LAST_ENTRY_INDEX] = record;

	sprite_select_render_window();
	highscore_draw_table();
	sprite_blit_to_video(render_window_sprite, -1);
	highscore_save_sorted();
	highscore_draw_table();
}

static void end_hiscore_set_text(legacy_s8 far *resource, legacy_s8 *text_id)
{
	copy_string(&resID_byte1, locate_text_res(resource, text_id));
}

static void end_hiscore_append_text(legacy_s8 far *resource, legacy_s8 *text_id)
{
	copy_string(&resID_byte1 + strlen(&resID_byte1), locate_text_res(resource, text_id));
}

static void end_hiscore_draw_current_text(legacy_s16 *y)
{
	hiscore_draw_text(&resID_byte1, font_centered_text_x(&resID_byte1), *y, dialog_fnt_colour, 0);
	*y = LEGACY_S16_WRAP_ADD(*y, HIGHSCORE_ROW_HEIGHT);
}

static void end_hiscore_draw_animation_frame(legacy_s8 far *animation_resource,
											 legacy_u8 far *frame_sequence, legacy_u8 frame_index,
											 legacy_s16 animation_x, legacy_s16 animation_y,
											 struct SPRITE far *animation_sprite,
											 legacy_u8 draw_direct_copy)
{
	opponent_animation_frame_id[3] = (legacy_s8)(frame_sequence[frame_index] + '0');
	struct SHAPE2D far *frame_shape =
		(struct SHAPE2D far *)locate_shape_fatal(animation_resource, opponent_animation_frame_id);
	mouse_draw_opaque_check();
	if (video_uses_page_flipping != 0) {
		sprite_select_target(animation_sprite);
		shape2d_rle_copy(frame_shape, 0, 0);
		sprite_select_screen_compat();
		/* Preserve resource access order independently of argument evaluation. */
		legacy_u16 height = shape2d_get_height(frame_shape);
		legacy_u16 width = shape2d_get_width(frame_shape);
		sprite_set_target_clip_bounds(
			animation_x,
			LEGACY_S16_WRAP_ADD(animation_x, LEGACY_S16_WRAP_MUL(width, video_shape_width_scale)),
			animation_y, LEGACY_S16_WRAP_ADD(animation_y, height));
		sprite_copy_image_at(animation_sprite->sprite_bitmapptr, animation_x, animation_y);
		sprite_select_screen_compat();
	} else {
		shape2d_rle_copy(frame_shape, animation_x, animation_y);
	}
	if (draw_direct_copy != 0) {
		shape2d_rle_copy(frame_shape, animation_x, animation_y);
	}
	mouse_draw_transparent_check();
}

static void end_hiscore_advance_animation(legacy_s16 delta, legacy_s16 *timer, legacy_u8 *frame,
										  const legacy_u8 far *frame_sequence)
{
	*timer = LEGACY_S16_WRAP_ADD(*timer, delta);
	if (*timer >= END_SCREEN_ANIMATION_FRAME_TICKS) {
		*timer = LEGACY_S16_WRAP_SUB(*timer, END_SCREEN_ANIMATION_FRAME_TICKS);
		(*frame)++;
		if (frame_sequence[*frame] == 0) {
			*frame = 0;
		}
	}
}

static void end_hiscore_update_animation(
	legacy_s16 delta, legacy_s16 *timer, legacy_u8 *frame, legacy_u8 *previous_frame,
	legacy_s8 far *animation_resource, legacy_u8 far *frame_sequence, legacy_s16 animation_x,
	legacy_s16 animation_y, struct SPRITE far *animation_sprite, legacy_u8 draw_direct_copy)
{
	end_hiscore_advance_animation(delta, timer, frame, frame_sequence);
	if (*previous_frame != *frame) {
		*previous_frame = *frame;
		end_hiscore_draw_animation_frame(animation_resource, frame_sequence, *frame, animation_x,
										 animation_y, animation_sprite, draw_direct_copy);
	}
}

struct END_SCREEN_TEXT_LINE {
	legacy_s8 word[END_SCREEN_TEXT_WORD_CAPACITY];
	legacy_u16 word_length, output_length;
	legacy_s16 line_width, line_y;
};

static legacy_s8 far *end_hiscore_result_text(legacy_s8 far *opponent_resource, legacy_u8 outcome,
											  legacy_u8 text_prefix, legacy_u16 resource_index)
{
	legacy_s8 text_id[END_SCREEN_TEXT_ID_SIZE];
	if (outcome == END_SCREEN_OUTCOME_NONE) {
		return locate_text_res(opponent_resource, opponent_neutral_result_text_id);
	} else {
		text_id[0] = (legacy_s8)text_prefix;
		text_id[1] = (legacy_s8)('1' + resource_index);
		legacy_s16 selector;
		if (resource_index == 0) {
			selector = end_opening_variant;
		} else if (resource_index == 1) {
			selector = end_outcome_variant;
		} else {
			selector = end_closing_variant;
		}
		text_id[2] = (legacy_s8)('a' + selector);
		text_id[3] = 0;
		return locate_text_res(opponent_resource, text_id);
	}
}

static void end_hiscore_append_text_word(struct END_SCREEN_TEXT_LINE *line, legacy_s16 animation_x)
{
	line->word[line->word_length] = 0;
	legacy_s16 word_width = (legacy_s16)font_text_width(line->word);
	if (LEGACY_S16_WRAP_ADD(word_width, line->line_width) <
			LEGACY_S16_WRAP_SUB(animation_x, END_SCREEN_TEXT_RIGHT_MARGIN) &&
		LEGACY_U16_WRAP_ADD(line->output_length, line->word_length) <
			END_SCREEN_TEXT_OUTPUT_CAPACITY) {
		for (legacy_u16 copy_index = 0; copy_index < line->word_length; copy_index++) {
			(&resID_byte1)[line->output_length++] = line->word[copy_index];
		}
		line->line_width = LEGACY_S16_WRAP_ADD(line->line_width, word_width);
	} else {
		(&resID_byte1)[line->output_length] = 0;
		font_draw_text(&resID_byte1, END_SCREEN_TEXT_LEFT, line->line_y);
		line->line_y = LEGACY_S16_WRAP_ADD(line->line_y, END_SCREEN_TEXT_LINE_HEIGHT);
		legacy_u16 first_character = line->word[0] == ' ' ? 1U : 0U;
		line->output_length = 0;
		for (legacy_u16 copy_index = first_character; copy_index < line->word_length;
			 copy_index++) {
			(&resID_byte1)[line->output_length++] = line->word[copy_index];
		}
		(&resID_byte1)[line->output_length] = 0;
		line->line_width = (legacy_s16)font_text_width(&resID_byte1);
	}
}

static void end_hiscore_consume_text(struct END_SCREEN_TEXT_LINE *line, legacy_s8 far *text,
									 legacy_s16 animation_x)
{
	for (;;) {
		legacy_u8 character = (legacy_u8)*text++;
		if (character != ' ' && character != 0) {
			line->word[line->word_length++] = (legacy_s8)character;
			continue;
		}

		end_hiscore_append_text_word(line, animation_x);
		line->word_length = 1;
		line->word[0] = ' ';
		if (character == 0) {
			break;
		}
	}
}

static void end_hiscore_draw_opponent_text(legacy_s8 far *opponent_resource, legacy_u8 outcome,
										   legacy_u8 text_prefix, legacy_s16 animation_x)
{
	struct END_SCREEN_TEXT_LINE line;
	line.line_y = END_SCREEN_TEXT_TOP;
	line.output_length = 0;
	line.line_width = 0;
	line.word_length = 0;
	legacy_u16 resource_count =
		outcome == END_SCREEN_OUTCOME_NONE ? 1U : END_SCREEN_TEXT_VARIANT_COUNT;
	for (legacy_u16 resource_index = 0; resource_index < resource_count; resource_index++) {
		legacy_s8 far *text =
			end_hiscore_result_text(opponent_resource, outcome, text_prefix, resource_index);
		font_set_fontdef2(fontnptr);
		end_hiscore_consume_text(&line, text, animation_x);
		font_set_fontdef();
	}
	if (line.output_length != 0) {
		font_set_fontdef2(fontnptr);
		(&resID_byte1)[line.output_length] = 0;
		font_draw_text(&resID_byte1, END_SCREEN_TEXT_LEFT, line.line_y);
		font_set_fontdef();
	}
}

struct END_SCREEN_STATE {
	legacy_s8 far *misc_resource;
	legacy_s8 far *opponent_resource;
	legacy_s8 far *animation_resource;
	legacy_u8 far *animation_sequence;
	struct SPRITE far *animation_sprite;
	struct BUTTON_AREA button_areas[END_SCREEN_BUTTON_COUNT];
	legacy_s8 score_status;
	legacy_u8 outcome, opponent_active, evaluation_screen;
	legacy_u8 selected, previous_selection, blit_mode;
	legacy_u8 animation_frame, previous_animation_frame, text_prefix;
	legacy_s16 finish_time, animation_x, animation_y, animation_timer, text_y;
#ifdef RESTUNTS_SDL3
	legacy_s16 animation_width, animation_height;
	legacy_u8 external_animation_drawn;
#endif
};

#ifdef RESTUNTS_SDL3
static void end_hiscore_update_external_animation(struct END_SCREEN_STATE *screen)
{
	if (screen->outcome == END_SCREEN_OUTCOME_NONE || !hires_enabled()) {
		screen->external_animation_drawn = 0;
		return;
	}
	sprite_select_screen_compat();
	mouse_draw_opaque_check();
	legacy_u8 drawn =
		opponent_animation_draw(&drawing_sprite, screen->animation_x, screen->animation_y,
								screen->animation_width, screen->animation_height) != 0;
	mouse_draw_transparent_check();
	if (screen->external_animation_drawn != 0 && drawn == 0) {
		/* A later decode error must retire the old full-color frame immediately. */
		end_hiscore_draw_animation_frame(screen->animation_resource, screen->animation_sequence,
										 screen->animation_frame, screen->animation_x,
										 screen->animation_y, screen->animation_sprite, 0);
	}
	screen->external_animation_drawn = drawn;
}

static legacy_u8 end_hiscore_handle_display_input(legacy_u16 input)
{
	if (input != (legacy_u16)KEY_F12 && input != (legacy_u16)KEY_SHIFT_F12) {
		return 0;
	}
	(void)handle_ingame_kb_shortcuts(LEGACY_S16_FROM_BITS(input));
	return 1;
}
#endif

static void end_hiscore_initialize(struct END_SCREEN_STATE *screen)
{
	ensure_file_exists(END_SCREEN_TRACK_RESOURCE_INDEX);
	screen->misc_resource = (legacy_s8 far *)file_load_resfile(results_misc_resource_name);
	screen->opponent_resource = 0;
	if (gameconfig.game_opponenttype != 0) {
		opponent_resource_name[3] = (legacy_s8)((legacy_u8)gameconfig.game_opponenttype + '0');
		screen->opponent_resource = (legacy_s8 far *)file_load_resfile(opponent_resource_name);
	}

	render_window_sprite = sprite_make_wnd(END_SCREEN_WIDTH, END_SCREEN_HEIGHT, END_SCREEN_COLOR);
	screen->animation_sprite = 0;
	if (video_uses_page_flipping != 0) {
		screen->animation_sprite = sprite_make_wnd(END_SCREEN_ANIMATION_WIDTH,
												   END_SCREEN_ANIMATION_HEIGHT, END_SCREEN_COLOR);
	}
	screen->blit_mode = MENU_BLIT_MODE_INITIAL;
	sprite_select_render_window_and_clear();
	draw_button(0, 0, 0, END_SCREEN_WIDTH, END_SCREEN_TOP_HEIGHT, button_top_color,
				button_bottom_color, button_fill_color, 0);
	draw_button(0, 0, END_SCREEN_BOTTOM_Y, END_SCREEN_WIDTH, END_SCREEN_BOTTOM_HEIGHT,
				button_top_color, button_bottom_color, button_fill_color, 0);
}

static void end_hiscore_draw_player_time(struct END_SCREEN_STATE *screen)
{
	screen->text_y = END_SCREEN_TEXT_START_Y;
	end_hiscore_set_text(screen->misc_resource, elapsed_time_label_id);
	legacy_s8 number[HIGHSCORE_FORMAT_BUFFER_SIZE];
	if (gState_total_finish_time != 0) {
		format_frame_as_string(number,
							   LEGACY_S16_WRAP_SUB(gState_total_finish_time, gState_penalty), 1);
		strcat(&resID_byte1, number);
		if (((legacy_u8)replay_recording_flags & REPLAY_RECORDING_MODIFIED_FLAG) != 0) {
			end_hiscore_append_text(screen->misc_resource, continued_race_label_id);
		}
		end_hiscore_draw_current_text(&screen->text_y);
		if (gState_penalty != 0) {
			end_hiscore_set_text(screen->misc_resource, penalty_time_label_id);
			format_frame_as_string(number, gState_penalty, 1);
			strcat(&resID_byte1, number);
			end_hiscore_draw_current_text(&screen->text_y);
		}
	} else {
		end_hiscore_append_text(screen->misc_resource, player_did_not_finish_label_id);
		end_hiscore_draw_current_text(&screen->text_y);
	}
}

static void end_hiscore_draw_opponent_time(struct END_SCREEN_STATE *screen)
{
	screen->outcome = END_SCREEN_OUTCOME_NONE;
	legacy_s8 number[HIGHSCORE_FORMAT_BUFFER_SIZE];
	if (gameconfig.game_opponenttype != 0) {
		if (gState_opponent_finish_time == 0) {
			end_hiscore_set_text(screen->misc_resource, opponent_unfinished_time_label_id);
			end_hiscore_append_text(screen->misc_resource, opponent_did_not_finish_label_id);
			if (gState_total_finish_time != 0) {
				screen->outcome = END_SCREEN_OUTCOME_LOSS;
			}
		} else if (gState_total_finish_time == 0 ||
				   (legacy_u16)gState_opponent_finish_time < (legacy_u16)gState_total_finish_time) {
			end_hiscore_set_text(screen->misc_resource, opponent_win_time_label_id);
			format_frame_as_string(number, gState_opponent_finish_time, 1);
			strcat(&resID_byte1, number);
			screen->outcome = END_SCREEN_OUTCOME_WIN;
		} else {
			end_hiscore_set_text(screen->misc_resource, opponent_loss_time_label_id);
			format_frame_as_string(number, gState_opponent_finish_time, 1);
			strcat(&resID_byte1, number);
			screen->outcome = END_SCREEN_OUTCOME_LOSS;
		}
		end_hiscore_draw_current_text(&screen->text_y);
	}
}

static void end_hiscore_load_music(struct END_SCREEN_STATE *screen)
{
	if (screen->outcome == END_SCREEN_OUTCOME_LOSS) {
		file_load_audiores(victory_music_resource_name, victory_instrument_resource_name,
						   victory_song_id);
	} else {
		file_load_audiores(race_end_music_resource_name, race_end_instrument_resource_name,
						   race_end_song_id);
	}

	screen->opponent_active = (legacy_u8)gameconfig.game_opponenttype;
	if (screen->outcome == END_SCREEN_OUTCOME_NONE && gState_pEndFrame != gState_oEndFrame) {
		screen->opponent_active = 0;
	}
}

static void end_hiscore_draw_statistics(struct END_SCREEN_STATE *screen)
{
	end_hiscore_set_text(screen->misc_resource, average_speed_label_id);
	legacy_u16 duration = LEGACY_U16_WRAP_ADD(gState_pEndFrame, elapsed_time1);
	legacy_u16 average_speed;
	if (duration != 0) {
		average_speed = (legacy_u16)(LEGACY_U32_DIV_OR_ZERO((legacy_u32)gState_travDist,
															(legacy_u32)duration) >>
									 END_SCREEN_SPEED_FRACTION_BITS);
	} else {
		average_speed = 0;
	}
	legacy_s8 number[HIGHSCORE_FORMAT_BUFFER_SIZE];
	format_integer(number, average_speed, 0, END_SCREEN_NUMBER_WIDTH);
	strcat(&resID_byte1, number);
	end_hiscore_append_text(screen->misc_resource, average_speed_units_id);
	end_hiscore_draw_current_text(&screen->text_y);

	if (gState_impactSpeed != 0) {
		end_hiscore_set_text(screen->misc_resource, impact_speed_label_id);
		format_integer(number, (legacy_u16)gState_impactSpeed >> END_SCREEN_SPEED_FRACTION_BITS, 0,
					   END_SCREEN_NUMBER_WIDTH);
		strcat(&resID_byte1, number);
		end_hiscore_append_text(screen->misc_resource, impact_speed_units_id);
		end_hiscore_draw_current_text(&screen->text_y);
	}

	end_hiscore_set_text(screen->misc_resource, top_speed_label_id);
	format_integer(number, (legacy_u16)gState_topSpeed >> END_SCREEN_SPEED_FRACTION_BITS, 0,
				   END_SCREEN_NUMBER_WIDTH);
	strcat(&resID_byte1, number);
	end_hiscore_append_text(screen->misc_resource, top_speed_units_id);
	end_hiscore_draw_current_text(&screen->text_y);
	if (gState_jumpCount != 0) {
		end_hiscore_set_text(screen->misc_resource, jump_count_label_id);
		format_integer(number, gState_jumpCount, 0, END_SCREEN_NUMBER_WIDTH);
		strcat(&resID_byte1, number);
		hiscore_draw_text(&resID_byte1, font_centered_text_x(&resID_byte1), screen->text_y,
						  dialog_fnt_colour, 0);
	}
}

static void end_hiscore_choose_text_variants(struct END_SCREEN_STATE *screen)
{
	previous_end_opening_variant = end_opening_variant;
	previous_end_outcome_variant = end_outcome_variant;
	previous_end_closing_variant = end_closing_variant;
	legacy_s16 random_value = (legacy_s16)get_super_random();
	end_opening_variant = (legacy_s16)(random_value % END_SCREEN_TEXT_VARIANT_COUNT);
	if (end_opening_variant == previous_end_opening_variant) {
		end_opening_variant = end_text_alternate_variant[(legacy_u16)end_opening_variant];
	}
	random_value = (legacy_s16)get_super_random();
	end_closing_variant = (legacy_s16)(random_value % END_SCREEN_TEXT_VARIANT_COUNT);
	if (end_closing_variant == previous_end_closing_variant) {
		end_closing_variant = end_text_alternate_variant[(legacy_u16)end_closing_variant];
	}

	random_value = (legacy_s16)get_super_random();
	if (screen->outcome == END_SCREEN_OUTCOME_WIN) {
		end_outcome_variant = (legacy_s16)(random_value % END_SCREEN_WIN_VARIANT_COUNT);
		if (gState_total_finish_time != 0) {
			end_outcome_variant =
				LEGACY_S16_WRAP_ADD(end_outcome_variant, END_SCREEN_FINISHED_VARIANT_OFFSET);
		}
	} else {
		end_outcome_variant = (legacy_s16)(random_value % END_SCREEN_OUTCOME_VARIANT_COUNT);
	}
	if (end_outcome_variant == previous_end_outcome_variant) {
		end_outcome_variant = end_outcome_alternate_variant[(legacy_u16)end_outcome_variant];
	}
}

static void end_hiscore_prepare_animation(struct END_SCREEN_STATE *screen)
{
	screen->animation_resource = 0;
	screen->animation_sequence = 0;
	screen->text_prefix = 0;
#ifdef RESTUNTS_SDL3
	screen->external_animation_drawn = 0;
#endif
	if (screen->opponent_active != 0) {
		if (((legacy_u8)replay_recording_flags & REPLAY_RECORDING_RESTARTABLE_FLAG) == 0) {
			end_hiscore_choose_text_variants(screen);
		}

		if (screen->outcome == END_SCREEN_OUTCOME_WIN) {
			opponent_win_animation_name[3] = (legacy_s8)(screen->opponent_active + '0');
			screen->animation_resource = (legacy_s8 far *)file_load_resource(
				FILE_RESOURCE_SHAPE2D_COLLECTION, opponent_win_animation_name);
			screen->animation_sequence =
				(legacy_u8 far *)locate_shape_alt(screen->opponent_resource, opponent_win_text_id);
			end_outcome_variant =
				(legacy_s16)(LEGACY_U16_WRAP_ADD(get_kevinrandom(), gState_frame) &
							 END_SCREEN_BINARY_RANDOM_MASK);
			if (gState_total_finish_time != 0) {
				end_outcome_variant =
					LEGACY_S16_WRAP_ADD(end_outcome_variant, END_SCREEN_FINISHED_VARIANT_OFFSET);
			}
			screen->text_prefix = 'v';
		} else {
			opponent_loss_animation_name[3] = (legacy_s8)(screen->opponent_active + '0');
			screen->animation_resource = (legacy_s8 far *)file_load_resource(
				FILE_RESOURCE_SHAPE2D_COLLECTION, opponent_loss_animation_name);
			screen->animation_sequence =
				(legacy_u8 far *)locate_shape_alt(screen->opponent_resource, opponent_loss_text_id);
			end_outcome_variant =
				(legacy_s16)(LEGACY_U16_WRAP_ADD(get_kevinrandom(), gState_frame) &
							 END_SCREEN_FOUR_WAY_RANDOM_MASK);
			screen->text_prefix = 'd';
		}
#ifdef RESTUNTS_SDL3
		if (screen->outcome != END_SCREEN_OUTCOME_NONE) {
			opponent_animation_load(screen->opponent_active,
									screen->outcome == END_SCREEN_OUTCOME_WIN);
		}
#endif
	}
}

static void end_hiscore_validate_track(struct END_SCREEN_STATE *screen)
{
	screen->score_status = 0;
	file_build_path(track_directory, gameconfig.game_trackname, track_file_extension, g_path_buf);
	legacy_u8 far *track_resource =
		(legacy_u8 far *)file_load_resource(FILE_RESOURCE_BINARY_OPTIONAL, g_path_buf);
	if (track_resource == 0) {
		legacy_u16 result =
			show_dialog(DIALOG_TYPE_ACKNOWLEDGEMENT, DIALOG_SAVE_BACKGROUND,
						locate_text_res(mainresptr, highscore_track_disk_prompt_id),
						DIALOG_AUTO_POSITION, DIALOG_AUTO_POSITION, dialog_border_color, 0, 0);
		if (result != 0) {
			track_resource =
				(legacy_u8 far *)file_load_resource(FILE_RESOURCE_BINARY_OPTIONAL, g_path_buf);
		}
	}
	if (track_resource != 0) {
		for (legacy_u16 i = 0; i < END_SCREEN_TRACK_VALIDATION_BYTES; i++) {
			if (track_resource[i] != track_element_map[i]) {
				screen->score_status = -1;
				break;
			}
		}
		mmgr_release((legacy_s8 far *)track_resource);
	} else {
		screen->score_status = -1;
	}
}

static void end_hiscore_check_score(struct END_SCREEN_STATE *screen)
{
	end_hiscore_validate_track(screen);
	if (screen->score_status == 0 && highscore_load_or_create(0) != 0) {
		if (highscore_load_or_create(1) != 0) {
			screen->score_status = -1;
		}
	}
	screen->finish_time = 0;
	struct HIGHSCORE_ENTRY far *scores;
	if (screen->score_status == 0 && gState_total_finish_time != 0) {
		screen->finish_time = gState_total_finish_time;
		scores = (struct HIGHSCORE_ENTRY far *)track_highscore_table;
		if (((legacy_u8)replay_recording_flags & REPLAY_RECORDING_HIGHSCORE_INELIGIBLE_FLAGS) ==
				0 &&
			highscore_entry_time(&scores[HIGHSCORE_LAST_ENTRY_INDEX]) > (legacy_u16)screen->finish_time) {
			screen->score_status = 1;
		}
	}
}

static void end_hiscore_show_opponent(struct END_SCREEN_STATE *screen)
{
	opponent_animation_frame_id[3] = '1';
	struct SHAPE2D far *frame_shape = (struct SHAPE2D far *)locate_shape_fatal(
		screen->animation_resource, opponent_animation_frame_id);
	legacy_s16 animation_width =
		LEGACY_S16_WRAP_MUL(shape2d_get_width(frame_shape), video_shape_width_scale);
	screen->animation_x = LEGACY_S16_WRAP_SUB(END_SCREEN_ANIMATION_RIGHT, animation_width);
	legacy_s16 animation_height = (legacy_s16)shape2d_get_height(frame_shape);
	screen->animation_y = LEGACY_S16_WRAP_SUB(END_SCREEN_ANIMATION_BOTTOM, animation_height);
	screen->animation_y = LEGACY_S16_FROM_BITS(
		LEGACY_U16_SAR((legacy_u16)screen->animation_y, END_SCREEN_ANIMATION_CENTER_SHIFT));
	draw_three_color_beveled_border(
		LEGACY_S16_WRAP_SUB(screen->animation_x, END_SCREEN_ANIMATION_BORDER_INSET),
		LEGACY_S16_WRAP_SUB(screen->animation_y, END_SCREEN_ANIMATION_BORDER_INSET),
		LEGACY_S16_WRAP_ADD(animation_width, END_SCREEN_ANIMATION_BORDER_GROWTH),
		LEGACY_S16_WRAP_ADD(shape2d_get_height(frame_shape), END_SCREEN_ANIMATION_BORDER_GROWTH),
		dialog_fnt_colour, 0, end_animation_border_shadow_color);
	opponent_animation_frame_id[3] =
		(legacy_s8)(screen->animation_sequence[screen->animation_frame] + '0');
	shape2d_rle_copy((struct SHAPE2D far *)locate_shape_fatal(screen->animation_resource,
															  opponent_animation_frame_id),
					 screen->animation_x, screen->animation_y);
	screen->previous_animation_frame = screen->animation_frame;
#ifdef RESTUNTS_SDL3
	screen->animation_width = animation_width;
	screen->animation_height = animation_height;
	if (screen->outcome != END_SCREEN_OUTCOME_NONE) {
		screen->external_animation_drawn =
			opponent_animation_draw(&drawing_sprite, screen->animation_x, screen->animation_y,
									animation_width, animation_height) != 0;
	}
#endif
	font_set_colors(0, 0);
	end_hiscore_draw_opponent_text(screen->opponent_resource, screen->outcome, screen->text_prefix,
								   screen->animation_x);
	screen->evaluation_screen = 0;
}

static void end_hiscore_enter_opponent_score(struct END_SCREEN_STATE *screen)
{
	screen->score_status = 0;
	screen->evaluation_screen = 1;
	draw_button(locate_text_res(screen->misc_resource, result_continue_button_id),
				END_SCREEN_EVALUATION_BUTTON_X, END_SCREEN_BUTTON_Y, END_SCREEN_BUTTON_WIDTH,
				END_SCREEN_BUTTON_HEIGHT, button_top_color, button_bottom_color, button_fill_color,
				0);
	(void)sprite_blit_to_video(render_window_sprite, LEGACY_S8_FROM_BITS(screen->blit_mode));
	screen->blit_mode = MENU_BLIT_MODE_REFRESH;
	menu_reset_animation_timers();
	check_input();
	sprite_select_screen_compat();
	struct BUTTON_AREA menu_areas[END_SCREEN_MENU_AREA_COUNT];
	for (legacy_u16 i = 0; i < END_SCREEN_MENU_AREA_COUNT; i++) {
		menu_areas[i].x1 = result_button_left[i];
		menu_areas[i].x2 = result_button_right[i];
		menu_areas[i].y1 = hiscore_buttons_y1[i];
		menu_areas[i].y2 = hiscore_buttons_y2[i];
	}
	legacy_u16 text_resource_count =
		screen->outcome == END_SCREEN_OUTCOME_NONE ? 1U : END_SCREEN_TEXT_VARIANT_COUNT;
	for (;;) {
		legacy_s16 delta = (legacy_s16)menu_animate_button_highlight(
			END_SCREEN_BUTTON_COUNT, menu_areas, menu_highlight_second_color,
			menu_highlight_first_color);
		end_hiscore_update_animation(delta, &screen->animation_timer, &screen->animation_frame,
									 &screen->previous_animation_frame, screen->animation_resource,
									 screen->animation_sequence, screen->animation_x,
									 screen->animation_y, screen->animation_sprite, 0);
#ifdef RESTUNTS_SDL3
		end_hiscore_update_external_animation(screen);
#endif
		legacy_u16 input = (legacy_u16)input_checking((legacy_s16)text_resource_count);
#ifdef RESTUNTS_SDL3
		if (end_hiscore_handle_display_input(input)) {
			continue;
		}
#endif
		if (input == KEY_ENTER || input == KEY_SPACE || input == KEY_ESCAPE) {
			break;
		}
	}

	sprite_select_render_window();
	draw_button(0, 0, 0, END_SCREEN_WIDTH, END_SCREEN_TOP_HEIGHT, button_top_color,
				button_bottom_color, button_fill_color, 0);
	sprite_set_target_clip_bounds(END_SCREEN_TEXT_LEFT, END_SCREEN_ANIMATION_RIGHT,
								  hiscore_buttons_y1[0],
								  LEGACY_S16_WRAP_ADD(hiscore_buttons_y2[0], 1));
	sprite_clear_target(button_fill_color);
	mouse_draw_opaque_check();
	enter_hiscore(screen->finish_time,
				  locate_text_res(screen->misc_resource, opponent_highscore_prompt_id),
				  screen->outcome);
}

static void end_hiscore_draw_evaluation(struct END_SCREEN_STATE *screen)
{
	if (screen->opponent_active != 0 && screen->score_status == 2) {
		screen->score_status = 0;
		sprite_select_render_window();
		highscore_draw_table();
		screen->selected = 1;
		screen->evaluation_screen = 1;
		return;
	}

	if (screen->opponent_active == 0) {
		if (screen->score_status > 0) {
			check_input();
			mouse_draw_opaque_check();
			enter_hiscore(screen->finish_time,
						  locate_text_res(screen->misc_resource, solo_highscore_prompt_id), 0);
			screen->score_status = 0;
			screen->blit_mode = MENU_BLIT_MODE_REFRESH;
		} else {
			mouse_draw_opaque_check();
			if (screen->score_status == -1) {
				end_hiscore_set_text(screen->misc_resource, highscore_unavailable_message_id);
				hiscore_draw_text(&resID_byte1, font_centered_text_x(&resID_byte1),
								  END_SCREEN_NO_SCORE_MESSAGE_Y, dialog_fnt_colour, 0);
			} else {
				highscore_draw_table();
			}
		}
		return;
	}

	end_hiscore_show_opponent(screen);
	if (screen->score_status > 0) {
		end_hiscore_enter_opponent_score(screen);
	}
}

static void end_hiscore_draw_menu(struct END_SCREEN_STATE *screen)
{
	screen->selected = 1;
	screen->previous_selection = 1;
	menu_reset_animation_timers();
	sprite_select_render_window();
	legacy_s16 menu_offset;
	if (screen->opponent_active == 0 || screen->score_status == -1) {
		menu_offset = END_SCREEN_MENU_OFFSET_WITHOUT_FIRST_BUTTON;
	} else {
		menu_offset = 0;
		draw_button(locate_text_res(screen->misc_resource, screen->evaluation_screen != 0
															   ? result_evaluation_button_id
															   : result_highscore_button_id),
					LEGACY_S16_WRAP_ADD(result_button_left[0], 1), END_SCREEN_BUTTON_Y,
					END_SCREEN_BUTTON_WIDTH, END_SCREEN_BUTTON_HEIGHT, button_top_color,
					button_bottom_color, button_fill_color, 0);
	}
	draw_button(locate_text_res(screen->misc_resource, result_replay_button_id),
				LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_ADD(result_button_left[1], menu_offset), 1),
				END_SCREEN_BUTTON_Y, END_SCREEN_BUTTON_WIDTH, END_SCREEN_BUTTON_HEIGHT,
				button_top_color, button_bottom_color, button_fill_color, 0);
	draw_button(locate_text_res(screen->misc_resource, screen->opponent_active != 0
														   ? result_race_button_id
														   : result_drive_button_id),
				LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_ADD(result_button_left[2], menu_offset), 1),
				END_SCREEN_BUTTON_Y, END_SCREEN_BUTTON_WIDTH, END_SCREEN_BUTTON_HEIGHT,
				button_top_color, button_bottom_color, button_fill_color, 0);
	draw_button(locate_text_res(screen->misc_resource, result_main_menu_button_data),
				LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_ADD(result_button_left[3], menu_offset), 1),
				END_SCREEN_BUTTON_Y, END_SCREEN_BUTTON_WIDTH, END_SCREEN_BUTTON_HEIGHT,
				button_top_color, button_bottom_color, button_fill_color, 0);
	for (legacy_u16 i = 0; i < END_SCREEN_BUTTON_COUNT; i++) {
		screen->button_areas[i].x1 = LEGACY_S16_WRAP_ADD(result_button_left[i], menu_offset);
		screen->button_areas[i].x2 = LEGACY_S16_WRAP_ADD(result_button_right[i], menu_offset);
		screen->button_areas[i].y1 = hiscore_buttons_y1[i];
		screen->button_areas[i].y2 = hiscore_buttons_y2[i];
	}
	check_input();
	(void)sprite_blit_to_video(render_window_sprite, LEGACY_S8_FROM_BITS(screen->blit_mode));
	screen->blit_mode = MENU_BLIT_MODE_REFRESH;
	sprite_select_screen_compat();
}

static void end_hiscore_redraw_selection(struct END_SCREEN_STATE *screen)
{
	if (screen->previous_selection != screen->selected) {
		screen->previous_selection = screen->selected;
		sprite_select_screen_compat();
		sprite_set_target_clip_bounds(0, END_SCREEN_WIDTH, hiscore_buttons_y1[0],
									  LEGACY_S16_WRAP_ADD(hiscore_buttons_y2[0], 1));
		mouse_draw_opaque_check();
		sprite_putimage(render_window_sprite->sprite_bitmapptr);
		mouse_draw_transparent_check();
		(void)timer_get_delta_alt();
		menu_reset_animation_timers();
	}
}

static void end_hiscore_select_mouse(struct END_SCREEN_STATE *screen)
{
	legacy_s16 hit;
	if (screen->opponent_active == 0 || screen->score_status == -1) {
		hit = (legacy_s16)mouse_multi_hittest(END_SCREEN_REDUCED_BUTTON_COUNT,
											  &screen->button_areas[1]);
		if (hit != -1) {
			screen->selected = (legacy_u8)(hit + 1);
		}
	} else {
		hit = (legacy_s16)mouse_multi_hittest(END_SCREEN_BUTTON_COUNT, screen->button_areas);
		if (hit != -1) {
			screen->selected = (legacy_u8)hit;
		}
	}
}

static legacy_s16 end_hiscore_navigate_menu(struct END_SCREEN_STATE *screen, legacy_u16 input)
{
	if (input == KEY_LEFT) {
		if (screen->opponent_active == 0 || screen->score_status == -1) {
			screen->selected = screen->selected <= 1 ? END_SCREEN_LAST_BUTTON_INDEX
													 : (legacy_u8)(screen->selected - 1U);
		} else {
			screen->selected = screen->selected == 0 ? END_SCREEN_LAST_BUTTON_INDEX
													 : (legacy_u8)(screen->selected - 1U);
		}
		return 1;
	}
	if (input == KEY_RIGHT) {
		if (screen->selected < END_SCREEN_LAST_BUTTON_INDEX) {
			screen->selected++;
		} else {
			screen->selected =
				(screen->opponent_active == 0 || screen->score_status == -1) ? 1U : 0U;
		}
		return 1;
	}
	return 0;
}

static legacy_s16 end_hiscore_handle_menu_input(struct END_SCREEN_STATE *screen, legacy_u16 input)
{
#ifdef RESTUNTS_SDL3
	if (end_hiscore_handle_display_input(input)) {
		return 0;
	}
#endif
	if (input == 0) {
		return 0;
	}
	if (end_hiscore_navigate_menu(screen, input)) {
		return 0;
	}
	if (input != KEY_ENTER && input != KEY_SPACE) {
		return 0;
	}

	if (screen->selected == 0) {
		sprite_select_render_window();
		draw_button(0, 0, 0, END_SCREEN_WIDTH, END_SCREEN_TOP_HEIGHT, button_top_color,
					button_bottom_color, button_fill_color, 0);
		screen->score_status = screen->evaluation_screen != 0 ? 0 : 2;
		return -1;
	}
	return 1;
}

static legacy_s16 end_hiscore_run_menu(struct END_SCREEN_STATE *screen)
{
	for (;;) {
		end_hiscore_redraw_selection(screen);
		legacy_s16 delta = (legacy_s16)menu_animate_button_highlight(
			screen->selected, screen->button_areas, menu_highlight_second_color,
			menu_highlight_first_color);
		if (screen->evaluation_screen == 0 && screen->outcome != END_SCREEN_OUTCOME_NONE) {
			end_hiscore_update_animation(delta, &screen->animation_timer, &screen->animation_frame,
										 &screen->previous_animation_frame,
										 screen->animation_resource, screen->animation_sequence,
										 screen->animation_x, screen->animation_y,
										 screen->animation_sprite, 1);
#ifdef RESTUNTS_SDL3
			end_hiscore_update_external_animation(screen);
#endif
		}
		legacy_u16 input = (legacy_u16)input_checking(delta);
		end_hiscore_select_mouse(screen);
		legacy_s16 result = end_hiscore_handle_menu_input(screen, input);
		if (result != 0) {
			return result;
		}
	}
}

static legacy_u16 end_hiscore_release(struct END_SCREEN_STATE *screen)
{
#ifdef RESTUNTS_SDL3
	opponent_animation_unload();
#endif
	audio_unload();
	if (screen->opponent_active != 0) {
		mmgr_release(screen->animation_resource);
	}
	if (video_uses_page_flipping != 0) {
		sprite_free_wnd(screen->animation_sprite);
	}
	sprite_free_wnd(render_window_sprite);
	if (gameconfig.game_opponenttype != 0) {
		unload_resource(screen->opponent_resource);
	}
	unload_resource(screen->misc_resource);
	return (legacy_u16)(screen->selected - 1U);
}

legacy_u16 end_hiscore(void)
{
	struct END_SCREEN_STATE screen;
	end_hiscore_initialize(&screen);
	end_hiscore_draw_player_time(&screen);
	end_hiscore_draw_opponent_time(&screen);
	end_hiscore_load_music(&screen);
	end_hiscore_draw_statistics(&screen);
	end_hiscore_prepare_animation(&screen);
	end_hiscore_check_score(&screen);
	screen.animation_frame = 0;
	screen.animation_timer = END_SCREEN_ANIMATION_FRAME_TICKS;
	screen.evaluation_screen = 1;
	for (;;) {
		end_hiscore_draw_evaluation(&screen);
		end_hiscore_draw_menu(&screen);
		if (end_hiscore_run_menu(&screen) > 0) {
			return end_hiscore_release(&screen);
		}
	}
}
