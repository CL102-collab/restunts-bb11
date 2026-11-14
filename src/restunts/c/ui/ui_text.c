#include "ui_text.h"
#include "../resource_bytes.h"
#include "../externs.h"
#include "../video/video_color.h"

legacy_u8 far *active_font_definition;

#define UI_SCREEN_WIDTH 320
#define FONT_FIXED_GLYPH_WIDTH_OFFSET 16U
#define FONT_GLYPH_WIDTHS_FLAG_OFFSET 20U
#define FONT_GLYPH_OFFSET_TABLE_OFFSET 22U
#define FONT_GLYPH_OFFSET_ENTRY_SIZE 2U
#define UI_HORIZONTAL_CENTER_DIVISOR 2
#define LEGACY_S16_DECIMAL_DIGIT_COUNT 5U
#define UI_NUMBER_SCRATCH_SIZE 18U
#define UI_DECIMAL_RADIX 10U
#define SECONDS_PER_MINUTE 60U
#define HUNDREDTHS_PER_SECOND 100U
#define TIME_FIELD_WIDTH 2

legacy_s16 font_centered_text_x(const legacy_s8 *text)
{
	legacy_s16 centered =
		LEGACY_S16_WRAP_NEGATE(LEGACY_S16_WRAP_SUB(font_text_width(text), UI_SCREEN_WIDTH));
	return LEGACY_S16_DIV_OR_ZERO(centered, UI_HORIZONTAL_CENTER_DIVISOR);
}

legacy_u16 legacy_near_string_length(const legacy_s8 *text)
{
	legacy_u16 length = 0;
	while (*text++ != 0) {
		length = LEGACY_U16_WRAP_ADD(length, 1U);
	}
	return length;
}

void format_integer(legacy_s8 *destination, legacy_s16 value, legacy_s16 zero_pad, legacy_s16 width)
{
	legacy_s16 signed_value = LEGACY_S16_FROM_BITS((legacy_u16)value);
	legacy_u16 magnitude =
		signed_value < 0 ? (legacy_u16)(0U - (legacy_u16)signed_value) : (legacy_u16)signed_value;
	legacy_s8 digits[LEGACY_S16_DECIMAL_DIGIT_COUNT];
	legacy_u16 digit_count = 0;
	do {
		digits[digit_count++] = (legacy_s8)('0' + magnitude % UI_DECIMAL_RADIX);
		magnitude = LEGACY_U16_DIV_OR_ZERO(magnitude, UI_DECIMAL_RADIX);
	} while (magnitude != 0);

	legacy_u16 index = 0;
	if (signed_value < 0) {
		destination[index++] = '-';
	}
	while (digit_count != 0) {
		destination[index++] = digits[--digit_count];
	}
	destination[index] = 0;
	legacy_u16 length = index;

	if (width != 0) {
		while (LEGACY_S16_FROM_BITS((legacy_u16)width) < LEGACY_S16_FROM_BITS(length)) {
			for (index = 0; index < length; index++) {
				destination[index] = destination[index + 1U];
			}
			length--;
		}
		while (LEGACY_S16_FROM_BITS((legacy_u16)width) > LEGACY_S16_FROM_BITS(length)) {
			index = length;
			do {
				destination[index + 1U] = destination[index];
			} while (index-- != 0);
			destination[0] = ' ';
			length++;
		}
	}
	if (zero_pad != 0) {
		index = 0;
		while (destination[index] == ' ') {
			destination[index++] = '0';
		}
	}
}

static legacy_s8 *legacy_near_string_copy(legacy_s8 *destination, const legacy_s8 *source)
{
	while ((*destination = *source) != 0) {
		destination++;
		source++;
	}
	return destination;
}

void format_frame_as_string(legacy_s8 *destination, legacy_s16 frame_count,
							legacy_s16 include_hundredths)
{
	legacy_u16 frames = (legacy_u16)frame_count;
	legacy_u16 frame_rate = (legacy_u16)framespersec;
	legacy_u16 frames_per_minute = LEGACY_U16_WRAP_MUL(SECONDS_PER_MINUTE, frame_rate);
	legacy_u16 minutes = LEGACY_U16_DIV_OR_ZERO(frames, frames_per_minute);
	frames = LEGACY_U16_WRAP_SUB(frames, LEGACY_U16_WRAP_MUL(frames_per_minute, minutes));
	legacy_u16 seconds = LEGACY_U16_DIV_OR_ZERO(frames, frame_rate);
	frames = LEGACY_U16_WRAP_SUB(frames, LEGACY_U16_WRAP_MUL(frame_rate, seconds));

	legacy_s8 number[UI_NUMBER_SCRATCH_SIZE];
	format_integer(number, minutes, 0, TIME_FIELD_WIDTH);
	legacy_s8 *output = legacy_near_string_copy(destination, number);
	*output++ = ':';
	format_integer(number, seconds, 1, TIME_FIELD_WIDTH);
	output = legacy_near_string_copy(output, number);
	if (include_hundredths != 0) {
		*output++ = '.';
		legacy_u16 hundredths =
			LEGACY_U16_WRAP_MUL(LEGACY_U16_DIV_OR_ZERO(HUNDREDTHS_PER_SECOND, frame_rate), frames);
		format_integer(number, hundredths, 1, TIME_FIELD_WIDTH);
		legacy_near_string_copy(output, number);
	}
}

void parse_filepath_separators(legacy_s8 *destination, const legacy_s8 *path)
{
	legacy_u16 path_index = legacy_near_string_length(path);
	legacy_s8 current;
	while (path_index != 0) {
		current = path[path_index - 1U];
		if (current == '\\' || current == ':') {
			break;
		}
		path_index--;
	}
	legacy_u16 output_index = 0;
	do {
		current = path[path_index++];
		destination[output_index++] = current;
	} while (current != '.');
	destination[output_index - 1U] = 0;
}

void font_set_colors(legacy_s16 color, legacy_s16 background_color)
{
	legacy_u8 far *font_definition = active_font_definition;
	font_definition[0] = (legacy_u8)color;
#if defined(RESTUNTS_SDL3) && defined(RESTUNTS_FULL) && !defined(RESTUNTS_HEADLESS) &&             \
	!defined(RESTUNTS_TEMPLEOS)
	font_definition[1] = (legacy_u16)VIDEO_COLOR_VALUE(color) != (legacy_u16)color
							 ? (legacy_u8)((legacy_u16)color >> LEGACY_BYTE_BITS)
							 : 0;
#else
	font_definition[1] = 0;
#endif
	font_definition[2] = (legacy_u8)background_color;
#if defined(RESTUNTS_SDL3) && defined(RESTUNTS_FULL) && !defined(RESTUNTS_HEADLESS) &&             \
	!defined(RESTUNTS_TEMPLEOS)
	font_definition[3] =
		(legacy_u16)VIDEO_COLOR_VALUE(background_color) != (legacy_u16)background_color
			? (legacy_u8)((legacy_u16)background_color >> LEGACY_BYTE_BITS)
			: 0;
#else
	font_definition[3] = 0;
#endif
}

struct RECTANGLE *intro_draw_text(legacy_s8 *text, legacy_s16 x, legacy_s16 y, legacy_s16 color,
								  legacy_s16 shadow_color)
{
	intro_text_bounds.left = LEGACY_S16_FROM_BITS((legacy_u16)x);
	intro_text_bounds.right = LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_ADD(x, font_text_width(text)), 1);
	intro_text_bounds.top = LEGACY_S16_FROM_BITS((legacy_u16)y);
	intro_text_bounds.bottom = LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_ADD(y, font_glyph_height), 1);
	font_set_colors(shadow_color, 0);
	font_draw_text(text, LEGACY_S16_WRAP_ADD(x, 1), LEGACY_S16_WRAP_ADD(y, 1));
	font_set_colors(color, 0);
	font_draw_text(text, LEGACY_S16_FROM_BITS((legacy_u16)x), LEGACY_S16_FROM_BITS((legacy_u16)y));
	return &intro_text_bounds;
}

static legacy_s16 font_measure(const legacy_s8 *text, legacy_u16 remaining, legacy_s16 bounded)
{
	if (bounded != 0 && remaining == 0) {
		return 0;
	}
	legacy_u8 far *font_definition = active_font_definition;
	legacy_u8 has_glyph_widths = font_definition[FONT_GLYPH_WIDTHS_FLAG_OFFSET];
	legacy_u16 glyph_width = resource_read_u16le(font_definition + FONT_FIXED_GLYPH_WIDTH_OFFSET);
	legacy_u8 character;
	legacy_u16 total_width = 0;
	while ((character = (legacy_u8)*text++) != 0) {
		legacy_u16 glyph_offset =
			resource_read_u16le(font_definition + FONT_GLYPH_OFFSET_TABLE_OFFSET +
								(legacy_u16)character * FONT_GLYPH_OFFSET_ENTRY_SIZE);
		if (glyph_offset == 0) {
			continue;
		}
		if (has_glyph_widths != 0) {
			glyph_width = font_definition[glyph_offset];
		}
		total_width = LEGACY_U16_WRAP_ADD(total_width, glyph_width);
		remaining--;
		if (remaining == 0) {
			break;
		}
	}
	return LEGACY_S16_FROM_BITS(total_width);
}

legacy_s16 font_prefix_width(const legacy_s8 *text, legacy_s16 glyph_count)
{
	return font_measure(text, (legacy_u16)glyph_count, 1);
}

legacy_s16 font_text_width(const legacy_s8 *text)
{
	return font_measure(text, 0, 0);
}
