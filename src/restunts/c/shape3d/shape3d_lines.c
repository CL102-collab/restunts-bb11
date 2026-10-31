#include "../externs.h"
#include "../legacy.h"
#include "../shape2d/shape2d.h"
#include "../shape2d/shape2d_internal.h"
#include "shape3d.h"
#include "shape3d_internal.h"
#include "shape3d_line_internal.h"

#define DRAW_LINE_STEP_TABLE_LIMIT 50U

legacy_u16 line_prepare(legacy_u16 start_x, legacy_u16 start_y, legacy_u16 end_x, legacy_u16 end_y,
						legacy_u16 *line, legacy_u16 skip_clipping);

/* Walk the line's far end forward by `steps` and carry the same correction
 * into the span counter the caller names. */
static void draw_line_advance_end(legacy_u16 *line, legacy_u16 steps, legacy_u16 span_index)
{
	legacy_u32 value32 = ((legacy_u32)line[DRAW_LINE_START_Y_INDEX] << LEGACY_WORD_BITS) |
						 line[DRAW_LINE_START_Y_FRACTION_INDEX];
	value32 += (legacy_u32)steps * line[DRAW_LINE_STEP_INDEX];
	value32 += DRAW_LINE_FIXED_ROUNDING;
	legacy_u16 old_value = line[DRAW_LINE_END_Y_INDEX];
	line[DRAW_LINE_END_Y_INDEX] = (legacy_u16)(value32 >> LEGACY_WORD_BITS);
	line[span_index] = (legacy_u16)(line[span_index] + old_value - line[DRAW_LINE_END_Y_INDEX]);
}

static legacy_u16 draw_line_round_div(legacy_u32 numerator, legacy_u16 divisor)
{
	legacy_u32 quotient = LEGACY_U32_DIV_OR_ZERO(numerator, divisor);
	legacy_u16 remainder = divisor == 0U ? 0U : (legacy_u16)(numerator % divisor);
	if ((legacy_u16)(divisor >> 1) < remainder) {
		quotient++;
	}
	return (legacy_u16)quotient;
}

legacy_u16 draw_line_step(legacy_u16 minor, legacy_u16 major)
{
	/* The original uses truncated table entries below 50, then rounds its
	 * division result to the nearest integer (ties down). Preserve both paths:
	 * a one-unit step difference can move a raster sample across a pixel edge. */
	if (major < DRAW_LINE_MIN_MAJOR_LENGTH) {
		return DRAW_LINE_DEGENERATE_STEP;
	}
	if (major < DRAW_LINE_STEP_TABLE_LIMIT) {
		return (legacy_u16)LEGACY_U32_DIV_OR_ZERO((legacy_u32)minor << LEGACY_WORD_BITS, major);
	}
	return draw_line_round_div((legacy_u32)minor << LEGACY_WORD_BITS, major);
}

legacy_u16 draw_line_reject(legacy_u16 *line, legacy_u16 reject)
{
	legacy_u16 clip = reject & DRAW_LINE_MODE_MASK;
	line[DRAW_LINE_MODE_AND_CLIP_INDEX] =
		(legacy_u16)((line[DRAW_LINE_MODE_AND_CLIP_INDEX] & DRAW_LINE_MODE_MASK) |
					 (clip << DRAW_LINE_CLIP_SHIFT));
	line[DRAW_LINE_PIXEL_COUNT_INDEX] = 0;
	if (clip & DRAW_LINE_CLIP_TOP) {
		line[DRAW_LINE_START_Y_INDEX] = drawing_sprite.sprite_top;
		line[DRAW_LINE_START_Y_FRACTION_INDEX] = 0;
		line[DRAW_LINE_END_Y_INDEX] = (legacy_u16)(drawing_sprite.sprite_top - 1);
		return clip;
	}
	if (clip & DRAW_LINE_CLIP_BOTTOM) {
		line[DRAW_LINE_START_Y_INDEX] = drawing_sprite.sprite_bottom;
		line[DRAW_LINE_START_Y_FRACTION_INDEX] = 0;
		return clip;
	}
	legacy_u16 bottom = line[DRAW_LINE_END_Y_INDEX];
	if (LEGACY_S16_FROM_BITS(bottom) >= LEGACY_S16_FROM_BITS(drawing_sprite.sprite_bottom)) {
		bottom = (legacy_u16)(drawing_sprite.sprite_bottom - 1);
	}
	legacy_u32 value32 =
		(legacy_u32)line[DRAW_LINE_START_Y_FRACTION_INDEX] + DRAW_LINE_FIXED_ROUNDING;
	legacy_u16 top =
		(legacy_u16)(line[DRAW_LINE_START_Y_INDEX] + (legacy_u16)(value32 >> LEGACY_WORD_BITS));
	if (LEGACY_S16_FROM_BITS(top) < LEGACY_S16_FROM_BITS(drawing_sprite.sprite_top)) {
		top = drawing_sprite.sprite_top;
	}
	line[DRAW_LINE_START_Y_INDEX] = top;
	line[DRAW_LINE_START_Y_FRACTION_INDEX] = 0;
	bottom = (legacy_u16)(bottom - top + 1);
	line[DRAW_LINE_END_Y_INDEX] = (legacy_u16)(top - 1);
	if (clip & DRAW_LINE_CLIP_LEFT) {
		line[DRAW_LINE_END_LEFT_CLIP_COUNT_INDEX] =
			(legacy_u16)(line[DRAW_LINE_END_LEFT_CLIP_COUNT_INDEX] + bottom);
	} else {
		line[DRAW_LINE_END_RIGHT_CLIP_COUNT_INDEX] =
			(legacy_u16)(line[DRAW_LINE_END_RIGHT_CLIP_COUNT_INDEX] + bottom);
	}
	return clip;
}

legacy_u16 draw_line_horizontal(legacy_u16 y, legacy_u16 start_x, legacy_u16 end_x,
								legacy_u16 *line, legacy_u16 unclipped)
{
	line[DRAW_LINE_MODE_AND_CLIP_INDEX] =
		(legacy_u16)((line[DRAW_LINE_MODE_AND_CLIP_INDEX] & LEGACY_U16_HIGH_BYTE_MASK) |
					 DRAW_LINE_MODE_HORIZONTAL);
	if (start_x == end_x) {
		line[DRAW_LINE_MODE_AND_CLIP_INDEX] =
			(legacy_u16)((line[DRAW_LINE_MODE_AND_CLIP_INDEX] & LEGACY_U16_HIGH_BYTE_MASK) |
						 DRAW_LINE_MODE_POINT);
	}
	if (LEGACY_S16_FROM_BITS(start_x) > LEGACY_S16_FROM_BITS(end_x)) {
		line[DRAW_LINE_MODE_AND_CLIP_INDEX] &= LEGACY_U16_HIGH_BYTE_MASK;
		line[DRAW_LINE_START_X_INDEX] = end_x;
		line[DRAW_LINE_END_X_INDEX] = start_x;
		legacy_u16 temporary = start_x;
		start_x = end_x;
		end_x = temporary;
	}
	if (unclipped != 0) {
		line[DRAW_LINE_PIXEL_COUNT_INDEX] = (legacy_u16)(end_x - start_x + 1);
		return 0;
	}
	legacy_u16 reject;
	if (LEGACY_S16_FROM_BITS(y) < LEGACY_S16_FROM_BITS(drawing_sprite.sprite_top)) {
		line[DRAW_LINE_START_Y_INDEX] = drawing_sprite.sprite_top;
		line[DRAW_LINE_END_Y_INDEX] = drawing_sprite.sprite_top;
		reject = DRAW_LINE_CLIP_TOP;
	} else if (LEGACY_S16_FROM_BITS(y) >= LEGACY_S16_FROM_BITS(drawing_sprite.sprite_bottom)) {
		line[DRAW_LINE_START_Y_INDEX] = drawing_sprite.sprite_bottom;
		line[DRAW_LINE_END_Y_INDEX] = drawing_sprite.sprite_bottom;
		reject = DRAW_LINE_CLIP_BOTTOM;
	} else {
		line[DRAW_LINE_PIXEL_COUNT_INDEX] = (legacy_u16)(end_x - start_x + 1);
		if (LEGACY_S16_FROM_BITS(end_x) < LEGACY_S16_FROM_BITS(drawing_sprite.sprite_raster_left)) {
			line[DRAW_LINE_END_Y_INDEX] = (legacy_u16)(line[DRAW_LINE_END_Y_INDEX] - 1);
			line[DRAW_LINE_END_LEFT_CLIP_COUNT_INDEX] = 1;
			reject = DRAW_LINE_CLIP_LEFT;
		} else if (LEGACY_S16_FROM_BITS(start_x) >=
				   LEGACY_S16_FROM_BITS(drawing_sprite.sprite_raster_right)) {
			line[DRAW_LINE_END_Y_INDEX] = (legacy_u16)(line[DRAW_LINE_END_Y_INDEX] - 1);
			line[DRAW_LINE_END_RIGHT_CLIP_COUNT_INDEX] = 1;
			reject = DRAW_LINE_CLIP_RIGHT;
		} else {
			legacy_u16 amount = (legacy_u16)(drawing_sprite.sprite_raster_left - start_x);
			if (LEGACY_S16_FROM_BITS(amount) > 0) {
				line[DRAW_LINE_START_X_INDEX] = drawing_sprite.sprite_raster_left;
				line[DRAW_LINE_PIXEL_COUNT_INDEX] =
					(legacy_u16)(line[DRAW_LINE_PIXEL_COUNT_INDEX] - amount);
			}
			amount = (legacy_u16)(end_x - (drawing_sprite.sprite_raster_right - 1));
			if (LEGACY_S16_FROM_BITS(amount) > 0) {
				line[DRAW_LINE_PIXEL_COUNT_INDEX] =
					(legacy_u16)(line[DRAW_LINE_PIXEL_COUNT_INDEX] - amount);
				line[DRAW_LINE_END_X_INDEX] = (legacy_u16)(drawing_sprite.sprite_raster_right - 1);
			}
			return 0;
		}
	}
	line[DRAW_LINE_MODE_AND_CLIP_INDEX] =
		(legacy_u16)((line[DRAW_LINE_MODE_AND_CLIP_INDEX] & DRAW_LINE_MODE_MASK) |
					 ((reject & DRAW_LINE_MODE_MASK) << DRAW_LINE_CLIP_SHIFT));
	line[DRAW_LINE_PIXEL_COUNT_INDEX] = 0;
	return reject & DRAW_LINE_MODE_MASK;
}

static void draw_line_clip_top_x_major(legacy_u16 *line, legacy_u16 mode,
									   legacy_u16 top_bound_or_clip_count,
									   legacy_u16 original_start_y)
{
	line[DRAW_LINE_START_Y_INDEX] = original_start_y;
	legacy_u16 advance = draw_line_round_div(
		(legacy_u32)top_bound_or_clip_count << LEGACY_WORD_BITS, line[DRAW_LINE_STEP_INDEX]);
	if (mode == DRAW_LINE_MODE_X_MAJOR_LEFT) {
		line[DRAW_LINE_START_X_INDEX] = (legacy_u16)(line[DRAW_LINE_START_X_INDEX] - advance);
	} else {
		line[DRAW_LINE_START_X_INDEX] = (legacy_u16)(line[DRAW_LINE_START_X_INDEX] + advance);
	}
	line[DRAW_LINE_PIXEL_COUNT_INDEX] = (legacy_u16)(line[DRAW_LINE_PIXEL_COUNT_INDEX] - advance);
	if (LEGACY_S16_FROM_BITS(line[DRAW_LINE_PIXEL_COUNT_INDEX]) <= 0) {
		line[DRAW_LINE_PIXEL_COUNT_INDEX] = 1;
		line[DRAW_LINE_START_Y_INDEX] = drawing_sprite.sprite_top;
		line[DRAW_LINE_START_X_INDEX] = line[DRAW_LINE_END_X_INDEX];
	} else {
		legacy_u32 product = (legacy_u32)advance * line[DRAW_LINE_STEP_INDEX];
		legacy_u32 value32 = ((legacy_u32)line[DRAW_LINE_START_Y_INDEX] << LEGACY_WORD_BITS) |
							 line[DRAW_LINE_START_Y_FRACTION_INDEX];
		value32 += product;
		line[DRAW_LINE_START_Y_FRACTION_INDEX] = (legacy_u16)value32;
		line[DRAW_LINE_START_Y_INDEX] = (legacy_u16)(value32 >> LEGACY_WORD_BITS);
	}
}

legacy_u8 draw_line_clip_top(legacy_u16 *line)
{
	legacy_u16 original_start_y = line[DRAW_LINE_START_Y_INDEX];
	legacy_u16 top_bound_or_clip_count = drawing_sprite.sprite_top;
	line[DRAW_LINE_START_Y_INDEX] = top_bound_or_clip_count;
	top_bound_or_clip_count = (legacy_u16)(top_bound_or_clip_count - original_start_y);
	legacy_u16 mode = line[DRAW_LINE_MODE_AND_CLIP_INDEX] & DRAW_LINE_MODE_MASK;
	legacy_u32 product;
	legacy_u32 value32;
	switch (mode) {
		case DRAW_LINE_MODE_VERTICAL:
		case DRAW_LINE_MODE_DIAGONAL_LEFT:
		case DRAW_LINE_MODE_DIAGONAL_RIGHT:
			if (mode == DRAW_LINE_MODE_DIAGONAL_LEFT) {
				line[DRAW_LINE_START_X_INDEX] =
					(legacy_u16)(line[DRAW_LINE_START_X_INDEX] - top_bound_or_clip_count);
			} else if (mode == DRAW_LINE_MODE_DIAGONAL_RIGHT) {
				line[DRAW_LINE_START_X_INDEX] =
					(legacy_u16)(line[DRAW_LINE_START_X_INDEX] + top_bound_or_clip_count);
			}
			line[DRAW_LINE_PIXEL_COUNT_INDEX] =
				(legacy_u16)(line[DRAW_LINE_PIXEL_COUNT_INDEX] - top_bound_or_clip_count);
			break;
		case DRAW_LINE_MODE_Y_MAJOR_LEFT:
		case DRAW_LINE_MODE_Y_MAJOR_RIGHT:
			product = (legacy_u32)line[DRAW_LINE_STEP_INDEX] * top_bound_or_clip_count;
			value32 = ((legacy_u32)line[DRAW_LINE_START_X_INDEX] << LEGACY_WORD_BITS) |
					  line[DRAW_LINE_START_X_FRACTION_INDEX];
			if (mode == DRAW_LINE_MODE_Y_MAJOR_LEFT) {
				value32 -= product;
			} else {
				value32 += product;
			}
			line[DRAW_LINE_START_X_FRACTION_INDEX] = (legacy_u16)value32;
			line[DRAW_LINE_START_X_INDEX] = (legacy_u16)(value32 >> LEGACY_WORD_BITS);
			line[DRAW_LINE_PIXEL_COUNT_INDEX] =
				(legacy_u16)(line[DRAW_LINE_PIXEL_COUNT_INDEX] - top_bound_or_clip_count);
			break;
		case DRAW_LINE_MODE_X_MAJOR_LEFT:
		case DRAW_LINE_MODE_X_MAJOR_RIGHT:
			draw_line_clip_top_x_major(line, mode, top_bound_or_clip_count, original_start_y);
			break;
		default:
			return 0;
	}
	return 1;
}

legacy_u8 draw_line_clip_bottom(legacy_u16 *line)
{
	legacy_u16 end_y_or_clip_count = line[DRAW_LINE_END_Y_INDEX];
	legacy_u16 clip_bottom = (legacy_u16)(drawing_sprite.sprite_bottom - 1);
	line[DRAW_LINE_END_Y_INDEX] = clip_bottom;
	end_y_or_clip_count = (legacy_u16)(end_y_or_clip_count - clip_bottom);
	legacy_u16 mode = line[DRAW_LINE_MODE_AND_CLIP_INDEX] & DRAW_LINE_MODE_MASK;
	legacy_u32 product;
	legacy_u16 advance;
	legacy_u32 value32;
	switch (mode) {
		case DRAW_LINE_MODE_VERTICAL:
		case DRAW_LINE_MODE_DIAGONAL_LEFT:
		case DRAW_LINE_MODE_DIAGONAL_RIGHT:
			if (mode == DRAW_LINE_MODE_DIAGONAL_LEFT) {
				line[DRAW_LINE_END_X_INDEX] =
					(legacy_u16)(line[DRAW_LINE_END_X_INDEX] + end_y_or_clip_count);
			} else if (mode == DRAW_LINE_MODE_DIAGONAL_RIGHT) {
				line[DRAW_LINE_END_X_INDEX] =
					(legacy_u16)(line[DRAW_LINE_END_X_INDEX] - end_y_or_clip_count);
			}
			line[DRAW_LINE_PIXEL_COUNT_INDEX] =
				(legacy_u16)(line[DRAW_LINE_PIXEL_COUNT_INDEX] - end_y_or_clip_count);
			break;
		case DRAW_LINE_MODE_Y_MAJOR_LEFT:
		case DRAW_LINE_MODE_Y_MAJOR_RIGHT:
			line[DRAW_LINE_PIXEL_COUNT_INDEX] =
				(legacy_u16)(line[DRAW_LINE_PIXEL_COUNT_INDEX] - end_y_or_clip_count);
			advance = (legacy_u16)(line[DRAW_LINE_PIXEL_COUNT_INDEX] - 1);
			product = (legacy_u32)line[DRAW_LINE_STEP_INDEX] * advance;
			value32 = ((legacy_u32)line[DRAW_LINE_START_X_INDEX] << LEGACY_WORD_BITS) |
					  line[DRAW_LINE_START_X_FRACTION_INDEX];
			if (mode == DRAW_LINE_MODE_Y_MAJOR_LEFT) {
				value32 -= product;
			} else {
				value32 += product;
			}
			value32 += DRAW_LINE_FIXED_ROUNDING;
			line[DRAW_LINE_END_X_INDEX] = (legacy_u16)(value32 >> LEGACY_WORD_BITS);
			break;
		case DRAW_LINE_MODE_X_MAJOR_LEFT:
		case DRAW_LINE_MODE_X_MAJOR_RIGHT:
			value32 = ((legacy_u32)clip_bottom << LEGACY_WORD_BITS) -
					  (((legacy_u32)line[DRAW_LINE_START_Y_INDEX] << LEGACY_WORD_BITS) |
					   line[DRAW_LINE_START_Y_FRACTION_INDEX]);
			if ((value32 & LEGACY_U32_SIGN_BIT) != 0) {
				advance = 0;
			} else {
				advance = draw_line_round_div(value32, line[DRAW_LINE_STEP_INDEX]);
			}
			if (mode == DRAW_LINE_MODE_X_MAJOR_LEFT) {
				line[DRAW_LINE_END_X_INDEX] = (legacy_u16)(line[DRAW_LINE_START_X_INDEX] - advance);
			} else {
				line[DRAW_LINE_END_X_INDEX] = (legacy_u16)(line[DRAW_LINE_START_X_INDEX] + advance);
			}
			line[DRAW_LINE_PIXEL_COUNT_INDEX] = (legacy_u16)(advance + 1);
			break;
		default:
			return 0;
	}
	return 1;
}

static void draw_line_advance_secondary(legacy_u16 *line, legacy_u16 advance,
										legacy_u16 counter_index)
{
	legacy_u32 value32 = ((legacy_u32)line[DRAW_LINE_START_Y_INDEX] << LEGACY_WORD_BITS) |
						 line[DRAW_LINE_START_Y_FRACTION_INDEX];
	legacy_u16 old_value = (legacy_u16)((value32 + DRAW_LINE_FIXED_ROUNDING) >> LEGACY_WORD_BITS);
	value32 += (legacy_u32)advance * line[DRAW_LINE_STEP_INDEX];
	line[DRAW_LINE_START_Y_FRACTION_INDEX] = (legacy_u16)value32;
	line[DRAW_LINE_START_Y_INDEX] = (legacy_u16)(value32 >> LEGACY_WORD_BITS);
	legacy_u16 new_value = (legacy_u16)((value32 + DRAW_LINE_FIXED_ROUNDING) >> LEGACY_WORD_BITS);
	line[counter_index] = (legacy_u16)(line[counter_index] + new_value - old_value);
}

static legacy_s16 draw_line_clip_left_y_major(legacy_u16 *line, legacy_u16 mode)
{
	legacy_u32 product;
	legacy_s32 difference;
	legacy_u16 advance;
	legacy_u16 original_count;
	legacy_u32 value32;
	switch (mode) {
		case DRAW_LINE_MODE_Y_MAJOR_LEFT:
			value32 = ((legacy_u32)line[DRAW_LINE_START_X_INDEX] << LEGACY_WORD_BITS) |
					  line[DRAW_LINE_START_X_FRACTION_INDEX];
			line[DRAW_LINE_END_X_INDEX] = drawing_sprite.sprite_raster_left;
			difference = (legacy_s32)LEGACY_S16_FROM_BITS(line[DRAW_LINE_START_X_INDEX]) -
						 (legacy_s32)LEGACY_S16_FROM_BITS(drawing_sprite.sprite_raster_left);
			if (difference < 0) {
				advance = 1;
			} else {
				advance = draw_line_round_div(
					value32 - ((legacy_u32)drawing_sprite.sprite_raster_left << LEGACY_WORD_BITS),
					line[DRAW_LINE_STEP_INDEX]);
				advance = (legacy_u16)(advance + 1);
			}
			original_count = line[DRAW_LINE_PIXEL_COUNT_INDEX];
			line[DRAW_LINE_PIXEL_COUNT_INDEX] = advance;
			line[DRAW_LINE_END_LEFT_CLIP_COUNT_INDEX] =
				(legacy_u16)(line[DRAW_LINE_END_LEFT_CLIP_COUNT_INDEX] + original_count - advance);
			line[DRAW_LINE_END_Y_INDEX] = (legacy_u16)(line[DRAW_LINE_START_Y_INDEX] + advance - 1);
			break;
		case DRAW_LINE_MODE_Y_MAJOR_RIGHT:
			value32 = ((legacy_u32)drawing_sprite.sprite_raster_left << LEGACY_WORD_BITS) -
					  (((legacy_u32)line[DRAW_LINE_START_X_INDEX] << LEGACY_WORD_BITS) |
					   line[DRAW_LINE_START_X_FRACTION_INDEX]);
			if ((value32 & LEGACY_U32_SIGN_BIT) != 0) {
				return DRAW_LINE_CLIP_LEFT;
			}
			advance = draw_line_round_div(value32, line[DRAW_LINE_STEP_INDEX]);
			line[DRAW_LINE_START_Y_INDEX] = (legacy_u16)(line[DRAW_LINE_START_Y_INDEX] + advance);
			line[DRAW_LINE_START_LEFT_CLIP_COUNT_INDEX] =
				(legacy_u16)(line[DRAW_LINE_START_LEFT_CLIP_COUNT_INDEX] + advance);
			line[DRAW_LINE_PIXEL_COUNT_INDEX] =
				(legacy_u16)(line[DRAW_LINE_PIXEL_COUNT_INDEX] - advance);
			if (LEGACY_S16_FROM_BITS(line[DRAW_LINE_PIXEL_COUNT_INDEX]) <= 0) {
				return DRAW_LINE_CLIP_LEFT;
			}
			product = (legacy_u32)advance * line[DRAW_LINE_STEP_INDEX];
			value32 = ((legacy_u32)line[DRAW_LINE_START_X_INDEX] << LEGACY_WORD_BITS) |
					  line[DRAW_LINE_START_X_FRACTION_INDEX];
			value32 += product;
			line[DRAW_LINE_START_X_FRACTION_INDEX] = (legacy_u16)value32;
			line[DRAW_LINE_START_X_INDEX] = (legacy_u16)(value32 >> LEGACY_WORD_BITS);
			break;
	}
	return 1;
}

legacy_s16 draw_line_clip_left(legacy_u16 *line)
{
	legacy_u16 mode = line[DRAW_LINE_MODE_AND_CLIP_INDEX] & DRAW_LINE_MODE_MASK;
	legacy_u16 advance;
	legacy_u16 x_or_remaining_span;
	legacy_u16 left_bound_or_clip_count;
	legacy_u16 clip_left;
	legacy_u16 old_value;
	switch (mode) {
		case DRAW_LINE_MODE_VERTICAL:
			return DRAW_LINE_CLIP_LEFT;
		case DRAW_LINE_MODE_DIAGONAL_LEFT:
			left_bound_or_clip_count = drawing_sprite.sprite_raster_left;
			x_or_remaining_span = line[DRAW_LINE_END_X_INDEX];
			line[DRAW_LINE_END_X_INDEX] = left_bound_or_clip_count;
			left_bound_or_clip_count = (legacy_u16)(left_bound_or_clip_count - x_or_remaining_span);
			line[DRAW_LINE_END_LEFT_CLIP_COUNT_INDEX] =
				(legacy_u16)(line[DRAW_LINE_END_LEFT_CLIP_COUNT_INDEX] + left_bound_or_clip_count);
			line[DRAW_LINE_PIXEL_COUNT_INDEX] =
				(legacy_u16)(line[DRAW_LINE_PIXEL_COUNT_INDEX] - left_bound_or_clip_count);
			line[DRAW_LINE_END_Y_INDEX] =
				(legacy_u16)(line[DRAW_LINE_END_Y_INDEX] - left_bound_or_clip_count);
			break;
		case DRAW_LINE_MODE_DIAGONAL_RIGHT:
			x_or_remaining_span = line[DRAW_LINE_START_X_INDEX];
			left_bound_or_clip_count = drawing_sprite.sprite_raster_left;
			line[DRAW_LINE_START_X_INDEX] = left_bound_or_clip_count;
			left_bound_or_clip_count = (legacy_u16)(left_bound_or_clip_count - x_or_remaining_span);
			line[DRAW_LINE_START_LEFT_CLIP_COUNT_INDEX] =
				(legacy_u16)(line[DRAW_LINE_START_LEFT_CLIP_COUNT_INDEX] +
							 left_bound_or_clip_count);
			line[DRAW_LINE_START_Y_INDEX] =
				(legacy_u16)(line[DRAW_LINE_START_Y_INDEX] + left_bound_or_clip_count);
			line[DRAW_LINE_PIXEL_COUNT_INDEX] =
				(legacy_u16)(line[DRAW_LINE_PIXEL_COUNT_INDEX] - left_bound_or_clip_count);
			break;
		case DRAW_LINE_MODE_Y_MAJOR_LEFT:
		case DRAW_LINE_MODE_Y_MAJOR_RIGHT:
			return draw_line_clip_left_y_major(line, mode);

		case DRAW_LINE_MODE_X_MAJOR_LEFT:
			x_or_remaining_span = line[DRAW_LINE_START_X_INDEX];
			clip_left = drawing_sprite.sprite_raster_left;
			line[DRAW_LINE_END_X_INDEX] = clip_left;
			x_or_remaining_span = (legacy_u16)(x_or_remaining_span - clip_left);
			advance = (legacy_u16)(x_or_remaining_span + 1);
			line[DRAW_LINE_PIXEL_COUNT_INDEX] = advance;
			draw_line_advance_end(line, x_or_remaining_span, DRAW_LINE_END_LEFT_CLIP_COUNT_INDEX);
			break;
		case DRAW_LINE_MODE_X_MAJOR_RIGHT:
			old_value = line[DRAW_LINE_START_X_INDEX];
			line[DRAW_LINE_START_X_INDEX] = drawing_sprite.sprite_raster_left;
			advance = (legacy_u16)(line[DRAW_LINE_START_X_INDEX] - old_value);
			line[DRAW_LINE_PIXEL_COUNT_INDEX] =
				(legacy_u16)(line[DRAW_LINE_PIXEL_COUNT_INDEX] - advance);
			draw_line_advance_secondary(line, advance, DRAW_LINE_START_LEFT_CLIP_COUNT_INDEX);
			break;
		default:
			return 0;
	}
	return 1;
}

static legacy_u16 draw_line_clip_right_y_major(legacy_u16 *line, legacy_u16 mode)
{
	legacy_u32 value32;
	legacy_u32 product;
	legacy_u16 advance;
	legacy_u16 original_count;
	switch (mode) {
		case DRAW_LINE_MODE_Y_MAJOR_LEFT:
			value32 = ((legacy_u32)line[DRAW_LINE_START_X_INDEX] << LEGACY_WORD_BITS) |
					  line[DRAW_LINE_START_X_FRACTION_INDEX];
			value32 -= (legacy_u32)(drawing_sprite.sprite_raster_right - 1) << LEGACY_WORD_BITS;
			advance = draw_line_round_div(value32, line[DRAW_LINE_STEP_INDEX]);
			line[DRAW_LINE_PIXEL_COUNT_INDEX] =
				(legacy_u16)(line[DRAW_LINE_PIXEL_COUNT_INDEX] - advance);
			if (LEGACY_S16_FROM_BITS(line[DRAW_LINE_PIXEL_COUNT_INDEX]) <= 0) {
				return draw_line_reject(line, DRAW_LINE_CLIP_RIGHT);
			}
			line[DRAW_LINE_START_Y_INDEX] = (legacy_u16)(line[DRAW_LINE_START_Y_INDEX] + advance);
			line[DRAW_LINE_START_RIGHT_CLIP_COUNT_INDEX] =
				(legacy_u16)(line[DRAW_LINE_START_RIGHT_CLIP_COUNT_INDEX] + advance);
			product = (legacy_u32)advance * line[DRAW_LINE_STEP_INDEX];
			value32 = ((legacy_u32)line[DRAW_LINE_START_X_INDEX] << LEGACY_WORD_BITS) |
					  line[DRAW_LINE_START_X_FRACTION_INDEX];
			value32 -= product;
			line[DRAW_LINE_START_X_FRACTION_INDEX] = (legacy_u16)value32;
			line[DRAW_LINE_START_X_INDEX] = (legacy_u16)(value32 >> LEGACY_WORD_BITS);
			return 0;
		case DRAW_LINE_MODE_Y_MAJOR_RIGHT:
			line[DRAW_LINE_END_X_INDEX] = (legacy_u16)(drawing_sprite.sprite_raster_right - 1);
			value32 = ((legacy_u32)line[DRAW_LINE_END_X_INDEX] << LEGACY_WORD_BITS) -
					  (((legacy_u32)line[DRAW_LINE_START_X_INDEX] << LEGACY_WORD_BITS) |
					   line[DRAW_LINE_START_X_FRACTION_INDEX]);
			if ((value32 & LEGACY_U32_SIGN_BIT) != 0) {
				return draw_line_reject(line, DRAW_LINE_CLIP_RIGHT);
			}
			advance = (legacy_u16)(draw_line_round_div(value32, line[DRAW_LINE_STEP_INDEX]) + 1);
			original_count = line[DRAW_LINE_PIXEL_COUNT_INDEX];
			line[DRAW_LINE_PIXEL_COUNT_INDEX] = advance;
			line[DRAW_LINE_END_RIGHT_CLIP_COUNT_INDEX] =
				(legacy_u16)(line[DRAW_LINE_END_RIGHT_CLIP_COUNT_INDEX] + original_count - advance);
			line[DRAW_LINE_END_Y_INDEX] = (legacy_u16)(line[DRAW_LINE_START_Y_INDEX] + advance - 1);
			return 0;
	}
	return 0;
}

legacy_u16 draw_line_clip_right(legacy_u16 *line)
{
	legacy_u16 mode = line[DRAW_LINE_MODE_AND_CLIP_INDEX] & DRAW_LINE_MODE_MASK;
	legacy_u16 advance;
	legacy_u16 x_or_span_count;
	legacy_u16 right_bound_or_clip_count;
	switch (mode) {
		case DRAW_LINE_MODE_VERTICAL:
			return draw_line_reject(line, DRAW_LINE_CLIP_RIGHT);
		case DRAW_LINE_MODE_DIAGONAL_LEFT:
			right_bound_or_clip_count = line[DRAW_LINE_START_X_INDEX];
			x_or_span_count = (legacy_u16)(drawing_sprite.sprite_raster_right - 1);
			line[DRAW_LINE_START_X_INDEX] = x_or_span_count;
			right_bound_or_clip_count = (legacy_u16)(right_bound_or_clip_count - x_or_span_count);
			line[DRAW_LINE_START_Y_INDEX] =
				(legacy_u16)(line[DRAW_LINE_START_Y_INDEX] + right_bound_or_clip_count);
			line[DRAW_LINE_PIXEL_COUNT_INDEX] =
				(legacy_u16)(line[DRAW_LINE_PIXEL_COUNT_INDEX] - right_bound_or_clip_count);
			line[DRAW_LINE_START_RIGHT_CLIP_COUNT_INDEX] =
				(legacy_u16)(line[DRAW_LINE_START_RIGHT_CLIP_COUNT_INDEX] +
							 right_bound_or_clip_count);
			return 0;
		case DRAW_LINE_MODE_DIAGONAL_RIGHT:
			right_bound_or_clip_count = (legacy_u16)(drawing_sprite.sprite_raster_right - 1);
			x_or_span_count = line[DRAW_LINE_END_X_INDEX];
			line[DRAW_LINE_END_X_INDEX] = right_bound_or_clip_count;
			x_or_span_count = (legacy_u16)(x_or_span_count - right_bound_or_clip_count);
			line[DRAW_LINE_END_RIGHT_CLIP_COUNT_INDEX] =
				(legacy_u16)(line[DRAW_LINE_END_RIGHT_CLIP_COUNT_INDEX] + x_or_span_count);
			line[DRAW_LINE_PIXEL_COUNT_INDEX] =
				(legacy_u16)(line[DRAW_LINE_PIXEL_COUNT_INDEX] - x_or_span_count);
			line[DRAW_LINE_END_Y_INDEX] =
				(legacy_u16)(line[DRAW_LINE_END_Y_INDEX] - x_or_span_count);
			return 0;
		case DRAW_LINE_MODE_Y_MAJOR_LEFT:
		case DRAW_LINE_MODE_Y_MAJOR_RIGHT:
			return draw_line_clip_right_y_major(line, mode);

		case DRAW_LINE_MODE_X_MAJOR_LEFT:
			x_or_span_count = line[DRAW_LINE_START_X_INDEX];
			right_bound_or_clip_count = (legacy_u16)(drawing_sprite.sprite_raster_right - 1);
			x_or_span_count = (legacy_u16)(x_or_span_count - right_bound_or_clip_count);
			line[DRAW_LINE_START_X_INDEX] = right_bound_or_clip_count;
			line[DRAW_LINE_PIXEL_COUNT_INDEX] =
				(legacy_u16)(line[DRAW_LINE_PIXEL_COUNT_INDEX] - x_or_span_count);
			draw_line_advance_secondary(line, x_or_span_count,
										DRAW_LINE_START_RIGHT_CLIP_COUNT_INDEX);
			return 0;
		case DRAW_LINE_MODE_X_MAJOR_RIGHT:
			x_or_span_count = drawing_sprite.sprite_raster_right;
			right_bound_or_clip_count = (legacy_u16)(x_or_span_count - 1);
			line[DRAW_LINE_END_X_INDEX] = right_bound_or_clip_count;
			x_or_span_count = (legacy_u16)(x_or_span_count - line[DRAW_LINE_START_X_INDEX]);
			line[DRAW_LINE_PIXEL_COUNT_INDEX] = x_or_span_count;
			advance = (legacy_u16)(x_or_span_count - 1);
			draw_line_advance_end(line, advance, DRAW_LINE_END_RIGHT_CLIP_COUNT_INDEX);
			return 0;
		default:
			return 0;
	}
}

static void draw_line_subdivide_advance_start(legacy_u16 *line, legacy_u16 vertical_step,
											  legacy_u16 horizontal_step)
{
	legacy_u16 start_y_or_visible_step =
		(legacy_u16)(line[DRAW_LINE_START_Y_INDEX] + vertical_step);
	line[DRAW_LINE_START_Y_INDEX] = start_y_or_visible_step;
	legacy_u16 start_y_or_bottom_overflow = start_y_or_visible_step;
	start_y_or_visible_step = (legacy_u16)(start_y_or_visible_step - drawing_sprite.sprite_top);
	if (LEGACY_S16_FROM_BITS(start_y_or_visible_step) > 0) {
		if (LEGACY_S16_FROM_BITS(start_y_or_visible_step) > LEGACY_S16_FROM_BITS(vertical_step)) {
			start_y_or_visible_step = vertical_step;
		}
		start_y_or_bottom_overflow =
			(legacy_u16)(start_y_or_bottom_overflow - drawing_sprite.sprite_bottom);
		legacy_u8 update_counter = 1;
		if (LEGACY_S16_FROM_BITS(start_y_or_bottom_overflow) > 0) {
			start_y_or_visible_step =
				(legacy_u16)(start_y_or_visible_step - start_y_or_bottom_overflow);
			if (LEGACY_S16_FROM_BITS(start_y_or_visible_step) <= 0) {
				update_counter = 0;
			}
		}
		if (update_counter != 0) {
			if (LEGACY_S16_FROM_BITS(line[DRAW_LINE_START_X_INDEX]) <
				LEGACY_S16_FROM_BITS(drawing_sprite.sprite_raster_left)) {
				line[DRAW_LINE_START_LEFT_CLIP_COUNT_INDEX] =
					(legacy_u16)(line[DRAW_LINE_START_LEFT_CLIP_COUNT_INDEX] +
								 start_y_or_visible_step);
			} else {
				line[DRAW_LINE_START_RIGHT_CLIP_COUNT_INDEX] =
					(legacy_u16)(line[DRAW_LINE_START_RIGHT_CLIP_COUNT_INDEX] +
								 start_y_or_visible_step);
			}
		}
	}
	line[DRAW_LINE_START_X_INDEX] = (legacy_u16)(line[DRAW_LINE_START_X_INDEX] + horizontal_step);
}

static void draw_line_subdivide_advance_end(legacy_u16 *line, legacy_u16 vertical_step,
											legacy_u16 horizontal_step)
{
	legacy_u16 end_y_or_visible_step = (legacy_u16)(line[DRAW_LINE_END_Y_INDEX] - vertical_step);
	line[DRAW_LINE_END_Y_INDEX] = end_y_or_visible_step;
	legacy_u16 end_y_or_top_overflow = end_y_or_visible_step;
	end_y_or_visible_step = (legacy_u16)(end_y_or_visible_step - drawing_sprite.sprite_bottom + 1);
	if (LEGACY_S16_FROM_BITS(end_y_or_visible_step) < 0) {
		end_y_or_visible_step = (legacy_u16)(0U - end_y_or_visible_step);
		if (LEGACY_S16_FROM_BITS(end_y_or_visible_step) > LEGACY_S16_FROM_BITS(vertical_step)) {
			end_y_or_visible_step = vertical_step;
		}
		end_y_or_top_overflow = (legacy_u16)(end_y_or_top_overflow - drawing_sprite.sprite_top + 1);
		legacy_u8 update_counter = 1;
		if (LEGACY_S16_FROM_BITS(end_y_or_top_overflow) < 0) {
			end_y_or_visible_step = (legacy_u16)(end_y_or_visible_step + end_y_or_top_overflow);
			if (LEGACY_S16_FROM_BITS(end_y_or_visible_step) <= 0) {
				update_counter = 0;
			}
		}
		if (update_counter != 0) {
			if (LEGACY_S16_FROM_BITS(line[DRAW_LINE_END_X_INDEX]) <
				LEGACY_S16_FROM_BITS(drawing_sprite.sprite_raster_left)) {
				line[DRAW_LINE_END_LEFT_CLIP_COUNT_INDEX] =
					(legacy_u16)(line[DRAW_LINE_END_LEFT_CLIP_COUNT_INDEX] + end_y_or_visible_step);
			} else {
				line[DRAW_LINE_END_RIGHT_CLIP_COUNT_INDEX] =
					(legacy_u16)(line[DRAW_LINE_END_RIGHT_CLIP_COUNT_INDEX] +
								 end_y_or_visible_step);
			}
		}
	}
	line[DRAW_LINE_END_X_INDEX] = (legacy_u16)(line[DRAW_LINE_END_X_INDEX] - horizontal_step);
}

void draw_line_subdivide(legacy_u16 *line)
{
	legacy_u16 vertical_step = sar1_word(line[DRAW_LINE_END_Y_INDEX]);
	legacy_u16 half_start_coordinate = sar1_word(line[DRAW_LINE_START_Y_INDEX]);
	vertical_step = sar1_word((legacy_u16)(vertical_step - half_start_coordinate));
	legacy_u16 horizontal_step = sar1_word(line[DRAW_LINE_END_X_INDEX]);
	half_start_coordinate = sar1_word(line[DRAW_LINE_START_X_INDEX]);
	horizontal_step = sar1_word((legacy_u16)(horizontal_step - half_start_coordinate));

	for (;;) {
		while (LEGACY_S16_FROM_BITS(line[DRAW_LINE_START_Y_INDEX]) <= DRAW_LINE_SUBDIVIDE_MIN) {
			draw_line_subdivide_advance_start(line, vertical_step, horizontal_step);
		}

		if (LEGACY_S16_FROM_BITS(line[DRAW_LINE_END_Y_INDEX]) >= DRAW_LINE_SUBDIVIDE_MAX) {
			draw_line_subdivide_advance_end(line, vertical_step, horizontal_step);
			continue;
		}
		if (LEGACY_S16_FROM_BITS(line[DRAW_LINE_START_X_INDEX]) <= DRAW_LINE_SUBDIVIDE_MIN ||
			LEGACY_S16_FROM_BITS(line[DRAW_LINE_START_X_INDEX]) >= DRAW_LINE_SUBDIVIDE_MAX) {
			draw_line_subdivide_advance_start(line, vertical_step, horizontal_step);
			continue;
		}
		if (LEGACY_S16_FROM_BITS(line[DRAW_LINE_END_X_INDEX]) <= DRAW_LINE_SUBDIVIDE_MIN ||
			LEGACY_S16_FROM_BITS(line[DRAW_LINE_END_X_INDEX]) >= DRAW_LINE_SUBDIVIDE_MAX) {
			draw_line_subdivide_advance_end(line, vertical_step, horizontal_step);
			continue;
		}
		return;
	}
}
