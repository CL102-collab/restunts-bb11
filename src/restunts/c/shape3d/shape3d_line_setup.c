#include "../externs.h"
#include "../legacy.h"
#include "../shape2d/shape2d.h"
#include "../shape2d/shape2d_internal.h"
#include "shape3d.h"
#include "shape3d_internal.h"

#include "shape3d_line_internal.h"

legacy_u16 line_prepare_clipped(legacy_u16 start_x, legacy_u16 start_y, legacy_u16 end_x,
								legacy_u16 end_y, legacy_u16 *line)
{
	return line_prepare(start_x, start_y, end_x, end_y, line, 0);
}

legacy_u16 line_prepare_unclipped(legacy_u16 start_x, legacy_u16 start_y, legacy_u16 end_x,
								  legacy_u16 end_y, legacy_u16 *line)
{
	return line_prepare(start_x, start_y, end_x, end_y, line, 1);
}

static legacy_u16 draw_line_find_initial_clip(legacy_u16 *line, legacy_u16 *clip)
{
	legacy_u16 coordinate = line[DRAW_LINE_START_Y_INDEX];
	legacy_u16 boundary_or_swap = drawing_sprite.sprite_top;
	legacy_u16 extent_or_major_delta = drawing_sprite.sprite_bottom;
	legacy_u16 clip_or_minor_delta = 0;
	if (LEGACY_S16_FROM_BITS(coordinate) >= LEGACY_S16_FROM_BITS(extent_or_major_delta)) {
		clip_or_minor_delta = DRAW_LINE_CLIP_BOTTOM;
		return draw_line_reject(line, clip_or_minor_delta);
	}
	if (LEGACY_S16_FROM_BITS(coordinate) < LEGACY_S16_FROM_BITS(boundary_or_swap)) {
		clip_or_minor_delta |= DRAW_LINE_CLIP_TOP << DRAW_LINE_CLIP_SHIFT;
	}

	coordinate = line[DRAW_LINE_END_Y_INDEX];
	if (LEGACY_S16_FROM_BITS(coordinate) < LEGACY_S16_FROM_BITS(boundary_or_swap)) {
		clip_or_minor_delta = DRAW_LINE_CLIP_TOP;
		return draw_line_reject(line, clip_or_minor_delta);
	}
	if (LEGACY_S16_FROM_BITS(coordinate) >= LEGACY_S16_FROM_BITS(extent_or_major_delta)) {
		clip_or_minor_delta |= DRAW_LINE_CLIP_BOTTOM;
	}

	boundary_or_swap = drawing_sprite.sprite_raster_left;
	extent_or_major_delta = drawing_sprite.sprite_raster_right;
	coordinate = line[DRAW_LINE_START_X_INDEX];
	if (LEGACY_S16_FROM_BITS(coordinate) < LEGACY_S16_FROM_BITS(boundary_or_swap)) {
		clip_or_minor_delta |= DRAW_LINE_CLIP_LEFT << DRAW_LINE_CLIP_SHIFT;
	}
	if (LEGACY_S16_FROM_BITS(coordinate) >= LEGACY_S16_FROM_BITS(extent_or_major_delta)) {
		clip_or_minor_delta |= DRAW_LINE_CLIP_RIGHT << DRAW_LINE_CLIP_SHIFT;
	}
	coordinate = line[DRAW_LINE_END_X_INDEX];
	if (LEGACY_S16_FROM_BITS(coordinate) < LEGACY_S16_FROM_BITS(boundary_or_swap)) {
		clip_or_minor_delta |= DRAW_LINE_CLIP_LEFT;
	}
	if (LEGACY_S16_FROM_BITS(coordinate) >= LEGACY_S16_FROM_BITS(extent_or_major_delta)) {
		clip_or_minor_delta |= DRAW_LINE_CLIP_RIGHT;
	}
	if ((legacy_u8)clip_or_minor_delta & (legacy_u8)(clip_or_minor_delta >> LEGACY_BYTE_BITS)) {
		clip_or_minor_delta =
			(legacy_u8)clip_or_minor_delta & (legacy_u8)(clip_or_minor_delta >> LEGACY_BYTE_BITS);
		return draw_line_reject(line, clip_or_minor_delta);
	}
	*clip = (legacy_u16)((legacy_u8)clip_or_minor_delta |
						 (legacy_u8)(clip_or_minor_delta >> LEGACY_BYTE_BITS));
	return 0;
}

static legacy_u16 draw_line_set_slope_mode(legacy_u16 *line, legacy_u16 extent_or_major_delta,
										   legacy_u16 clip_or_minor_delta)
{
	legacy_u16 mode;
	legacy_u8 compute_step = 0;
	if (clip_or_minor_delta == 0) {
		line[DRAW_LINE_PIXEL_COUNT_INDEX] = (legacy_u16)(extent_or_major_delta + 1);
		mode = DRAW_LINE_MODE_VERTICAL;
	} else {
		if (clip_or_minor_delta == LEGACY_U16_SIGN_BIT) {
			return 1;
		}
		legacy_u8 left = LEGACY_S16_FROM_BITS(clip_or_minor_delta) < 0;
		if (left != 0) {
			clip_or_minor_delta = (legacy_u16)(0U - clip_or_minor_delta);
		}
		if (clip_or_minor_delta < extent_or_major_delta) {
			mode = left ? DRAW_LINE_MODE_Y_MAJOR_LEFT : DRAW_LINE_MODE_Y_MAJOR_RIGHT;
			compute_step = 1;
		} else if (clip_or_minor_delta == extent_or_major_delta) {
			mode = left ? DRAW_LINE_MODE_DIAGONAL_LEFT : DRAW_LINE_MODE_DIAGONAL_RIGHT;
			line[DRAW_LINE_PIXEL_COUNT_INDEX] = (legacy_u16)(extent_or_major_delta + 1);
		} else {
			mode = left ? DRAW_LINE_MODE_X_MAJOR_LEFT : DRAW_LINE_MODE_X_MAJOR_RIGHT;
			legacy_u16 boundary_or_swap = extent_or_major_delta;
			extent_or_major_delta = clip_or_minor_delta;
			clip_or_minor_delta = boundary_or_swap;
			compute_step = 1;
		}
	}
	line[DRAW_LINE_MODE_AND_CLIP_INDEX] =
		(legacy_u16)((line[DRAW_LINE_MODE_AND_CLIP_INDEX] & LEGACY_U16_HIGH_BYTE_MASK) | mode);
	if (compute_step != 0) {
		line[DRAW_LINE_STEP_INDEX] = draw_line_step(clip_or_minor_delta, extent_or_major_delta);
		if (extent_or_major_delta == LEGACY_S16_MAX) {
			return 1;
		}
		line[DRAW_LINE_PIXEL_COUNT_INDEX] = (legacy_u16)(extent_or_major_delta + 1);
	}
	return 0;
}

static legacy_u16 draw_line_setup_slope(legacy_u16 *line)
{
	legacy_s32 difference = (legacy_s32)LEGACY_S16_FROM_BITS(line[DRAW_LINE_END_Y_INDEX]) -
							(legacy_s32)LEGACY_S16_FROM_BITS(line[DRAW_LINE_START_Y_INDEX]);
	if (difference < -(legacy_s32)LEGACY_U16_SIGN_BIT || difference > (legacy_s32)LEGACY_S16_MAX) {
		return 1;
	}
	legacy_u16 vertical_delta = (legacy_u16)difference;
	difference = (legacy_s32)LEGACY_S16_FROM_BITS(line[DRAW_LINE_END_X_INDEX]) -
				 (legacy_s32)LEGACY_S16_FROM_BITS(line[DRAW_LINE_START_X_INDEX]);
	if (difference < -(legacy_s32)LEGACY_U16_SIGN_BIT || difference > (legacy_s32)LEGACY_S16_MAX) {
		return 1;
	}
	return draw_line_set_slope_mode(line, vertical_delta, (legacy_u16)difference);
}

static legacy_u16 draw_line_find_horizontal_clip(legacy_u16 *line, legacy_u16 *clip)
{
	legacy_u16 coordinate = line[DRAW_LINE_START_X_INDEX];
	legacy_u16 clip_or_minor_delta = 0;
	if (LEGACY_S16_FROM_BITS(coordinate) <
		LEGACY_S16_FROM_BITS(drawing_sprite.sprite_raster_left)) {
		clip_or_minor_delta |= DRAW_LINE_CLIP_LEFT << DRAW_LINE_CLIP_SHIFT;
	}
	legacy_u32 value32 =
		(legacy_u32)line[DRAW_LINE_START_X_FRACTION_INDEX] + DRAW_LINE_FIXED_ROUNDING;
	coordinate = (legacy_u16)(coordinate + (legacy_u16)(value32 >> LEGACY_WORD_BITS));
	if (LEGACY_S16_FROM_BITS(coordinate) >=
		LEGACY_S16_FROM_BITS(drawing_sprite.sprite_raster_right)) {
		clip_or_minor_delta |= DRAW_LINE_CLIP_RIGHT << DRAW_LINE_CLIP_SHIFT;
	}
	coordinate = line[DRAW_LINE_END_X_INDEX];
	if (LEGACY_S16_FROM_BITS(coordinate) <
		LEGACY_S16_FROM_BITS(drawing_sprite.sprite_raster_left)) {
		clip_or_minor_delta |= DRAW_LINE_CLIP_LEFT;
	}
	if (LEGACY_S16_FROM_BITS(coordinate) >=
		LEGACY_S16_FROM_BITS(drawing_sprite.sprite_raster_right)) {
		clip_or_minor_delta |= DRAW_LINE_CLIP_RIGHT;
	}
	if ((legacy_u8)clip_or_minor_delta & (legacy_u8)(clip_or_minor_delta >> LEGACY_BYTE_BITS)) {
		clip_or_minor_delta =
			(legacy_u8)clip_or_minor_delta & (legacy_u8)(clip_or_minor_delta >> LEGACY_BYTE_BITS);
		return draw_line_reject(line, clip_or_minor_delta);
	}
	clip_or_minor_delta = (legacy_u16)((legacy_u8)clip_or_minor_delta |
									   (legacy_u8)(clip_or_minor_delta >> LEGACY_BYTE_BITS));
	*clip = clip_or_minor_delta;
	return 0;
}

static legacy_u16 draw_line_apply_horizontal_clipping(legacy_u16 *line, legacy_u16 clip)
{
	legacy_s16 clip_result;

	switch (clip & DRAW_LINE_CLIP_MASK) {
		case 0:
			return 0;
		case DRAW_LINE_CLIP_RIGHT:
			return draw_line_clip_right(line);
		case DRAW_LINE_CLIP_LEFT:
		case DRAW_LINE_CLIP_LEFT | DRAW_LINE_CLIP_RIGHT:
			clip_result = draw_line_clip_left(line);
			if (clip_result == DRAW_LINE_CLIP_LEFT) {
				return draw_line_reject(line, DRAW_LINE_CLIP_LEFT);
			}
			if (clip_result == 0) {
				return 0;
			}
			if (clip & DRAW_LINE_CLIP_RIGHT) {
				return draw_line_clip_right(line);
			}
			return 0;
	}
	return 0;
}

static legacy_u16 draw_line_apply_clipping(legacy_u16 *line, legacy_u16 clip)
{
	for (;;) {
		if ((clip & DRAW_LINE_CLIP_TOP) != 0) {
			if (draw_line_clip_top(line) == 0) {
				return 0;
			}
			if ((clip & DRAW_LINE_CLIP_BOTTOM) != 0 && draw_line_clip_bottom(line) == 0) {
				return 0;
			}
		} else if ((clip & DRAW_LINE_CLIP_BOTTOM) != 0) {
			if (draw_line_clip_bottom(line) == 0) {
				return 0;
			}
		} else {
			return draw_line_apply_horizontal_clipping(line, clip);
		}
		legacy_s16 clip_result = draw_line_find_horizontal_clip(line, &clip);
		if (clip_result != 0) {
			return clip_result;
		}
		if (clip == 0) {
			return 0;
		}
	}
}

legacy_u16 line_prepare(legacy_u16 start_x, legacy_u16 start_y, legacy_u16 end_x, legacy_u16 end_y,
						legacy_u16 *line, legacy_u16 skip_clipping)
{
	line[DRAW_LINE_MODE_AND_CLIP_INDEX] = DRAW_LINE_MODE_UNSET;
	line[DRAW_LINE_START_X_FRACTION_INDEX] = 0;
	line[DRAW_LINE_START_Y_FRACTION_INDEX] = 0;
	line[DRAW_LINE_START_LEFT_CLIP_COUNT_INDEX] = 0;
	line[DRAW_LINE_END_LEFT_CLIP_COUNT_INDEX] = 0;
	line[DRAW_LINE_START_RIGHT_CLIP_COUNT_INDEX] = 0;
	line[DRAW_LINE_END_RIGHT_CLIP_COUNT_INDEX] = 0;

	legacy_u16 coordinate = (legacy_u16)start_y;
	legacy_u16 boundary_or_swap = (legacy_u16)end_y;
	legacy_u16 extent_or_major_delta = (legacy_u16)start_x;
	legacy_u16 clip_or_minor_delta = (legacy_u16)end_x;
	if (LEGACY_S16_FROM_BITS(coordinate) <= LEGACY_S16_FROM_BITS(boundary_or_swap)) {
		line[DRAW_LINE_START_X_INDEX] = extent_or_major_delta;
		line[DRAW_LINE_START_Y_INDEX] = coordinate;
		line[DRAW_LINE_END_X_INDEX] = clip_or_minor_delta;
		line[DRAW_LINE_END_Y_INDEX] = boundary_or_swap;
	} else {
		line[DRAW_LINE_START_X_INDEX] = clip_or_minor_delta;
		line[DRAW_LINE_START_Y_INDEX] = boundary_or_swap;
		line[DRAW_LINE_END_X_INDEX] = extent_or_major_delta;
		line[DRAW_LINE_END_Y_INDEX] = coordinate;
	}
	if (coordinate == boundary_or_swap) {
		return draw_line_horizontal(coordinate, extent_or_major_delta, clip_or_minor_delta, line,
									skip_clipping);
	}
	legacy_u16 clip;
	for (;;) {
		clip = 0;
		if (skip_clipping == 0) {
			legacy_u16 clip_result = draw_line_find_initial_clip(line, &clip);
			if (clip_result != 0) {
				return clip_result;
			}
		}
		if (draw_line_setup_slope(line) != 0) {
			draw_line_subdivide(line);
			continue;
		}
		return draw_line_apply_clipping(line, clip);
	}
}
