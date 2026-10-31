#include "highscore_internal.h"

void end_format_time(legacy_s8 *out, legacy_u32 frame)
{
#ifdef RESTUNTS_SDL3
	if (extended_highscore_format_runtime_time(out, frame)) {
		return;
	}
#endif
	format_frame_as_string(out, (legacy_u16)frame, 1);
}

void end_hiscore_set_text(legacy_s8 far *resource, legacy_s8 *text_id)
{
	copy_string(&resID_byte1, locate_text_res(resource, text_id));
}

void end_hiscore_append_text(legacy_s8 far *resource, legacy_s8 *text_id)
{
	copy_string(&resID_byte1 + strlen(&resID_byte1), locate_text_res(resource, text_id));
}

void end_hiscore_draw_current_text(legacy_s16 *y)
{
	hiscore_draw_text(&resID_byte1, font_centered_text_x(&resID_byte1), *y, dialog_fnt_colour, 0);
	*y = LEGACY_S16_WRAP_ADD(*y, HIGHSCORE_ROW_HEIGHT);
}

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

void end_hiscore_draw_opponent_text(legacy_s8 far *opponent_resource, legacy_u8 outcome,
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
