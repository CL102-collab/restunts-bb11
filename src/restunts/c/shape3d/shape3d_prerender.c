#include "../externs.h"
#include "../legacy.h"
#include "../shape2d/shape2d.h"
#include "../shape2d/shape2d_internal.h"
#include "shape3d.h"
#include "shape3d_internal.h"
#include "shape3d_prerender_internal.h"

#define PRERENDER_POINT_WORD_COUNT 2U
#define PRERENDER_POINT_Y_OFFSET 1U
#define SPHERE_RASTER_TABLE_LIMIT 40U
#define SPHERE_PERIMETER_POINT_COUNT 16U
#define SPHERE_VERTEX_COUNT 32U
#define SPHERE_VERTEX_WORD_COUNT 64U
#define SPHERE_MIRROR_WORD_OFFSET 32U
#define SPHERE_DIAGONAL_NORMALIZATION 11585U
#define SPHERE_HALF_STEP_NORMALIZATION 14654U
#define SPHERE_QUARTER_STEP_NORMALIZATION 15895U
#define SPHERE_THREE_QUARTER_STEP_NORMALIZATION 13107U
#define WHEEL_SOURCE_WORD_COUNT 8U
#define WHEEL_PERIMETER_POINT_COUNT 16U
#define WHEEL_PERIMETER_INDEX_MASK 15U
#define WHEEL_INNER_WORD_OFFSET 32U
#define WHEEL_DEPTH_WORD_OFFSET 64U
#define WHEEL_POINT_WORD_COUNT 96U
#define WHEEL_SIDE_HALF_POINT_COUNT 9U
#define WHEEL_SIDE_LAST_POINT_INDEX 17U
#define WHEEL_SIDE_VERTEX_COUNT 18U
#define WHEEL_SIDE_WORD_COUNT 36U
#define WHEEL_QUAD_POINT_COUNT 4U
#define WHEEL_QUAD_WORD_COUNT 8U

void polygon_rasterize(legacy_u16 color, legacy_u16 vertex_count, const struct POINT2D *vertices,
					   legacy_u16 choose_edge_per_row);
void generate_poly_edges(legacy_s16 *edges, const legacy_u16 *line_setup, legacy_s16 clipping_mode);
void polygon_merge_second_edge(const legacy_u16 *line_setup, legacy_u16 choose_edge_per_row,
							   legacy_u16 needs_clipping, legacy_s16 *edge_tables);

static legacy_s16 prerender_line(legacy_u16 start_x, legacy_u16 start_y, legacy_u16 end_x,
								 legacy_u16 end_y, legacy_u16 color, legacy_u16 *line)
{
	line[DRAW_LINE_COLOR_INDEX] = color;
	if (line_prepare_clipped(start_x, start_y, end_x, end_y, line) == 0 &&
		LEGACY_S16_FROM_BITS(line[DRAW_LINE_PIXEL_COUNT_INDEX]) > 0) {
		sprite_draw_line_from_setup(line);
		return 1;
	}
	return 0;
}

static void prerender_grille_pixel(legacy_u16 x, legacy_u16 y)
{
	legacy_u16 row_shift = (y & 1U) == 0U ? LEGACY_BYTE_BITS : 0U;
	legacy_u16 bit = row_shift + (LEGACY_BYTE_BITS - 1U) - (x & (LEGACY_BYTE_BITS - 1U));
	if ((PRERENDER_BLACK_GRILLE_PATTERN & (1U << bit)) != 0U) {
		sprite_putpixel_clipped(LEGACY_S16_FROM_BITS(x), LEGACY_S16_FROM_BITS(y), 0);
	}
}

/* Walk the normal clipped line setup so thin trim and collapsed polygons use
 * the same screen-aligned grille as the filled body and wheel surfaces. */
static void prerender_grille_line(legacy_u16 start_x, legacy_u16 start_y, legacy_u16 end_x,
								  legacy_u16 end_y, legacy_u16 color)
{
	legacy_u16 line[DRAW_LINE_WORD_COUNT] = {0};
	(void)color;
	if (line_prepare_clipped(start_x, start_y, end_x, end_y, line) != 0) {
		return;
	}
	legacy_u16 mode = line[DRAW_LINE_MODE_AND_CLIP_INDEX] & DRAW_LINE_MODE_MASK;
	legacy_u16 x = line[DRAW_LINE_START_X_INDEX];
	legacy_u16 y = line[DRAW_LINE_START_Y_INDEX];
	legacy_u16 fraction_x = line[DRAW_LINE_START_X_FRACTION_INDEX];
	legacy_u16 fraction_y = line[DRAW_LINE_START_Y_FRACTION_INDEX];
	if (fraction_x >= DRAW_LINE_FIXED_ROUNDING) {
		x++;
	}
	fraction_x = LEGACY_U16_WRAP_ADD(fraction_x, DRAW_LINE_FIXED_ROUNDING);
	if ((mode < DRAW_LINE_MODE_VERTICAL || mode > DRAW_LINE_MODE_Y_MAJOR_RIGHT) &&
		fraction_y >= DRAW_LINE_FIXED_ROUNDING) {
		y++;
	}
	fraction_y = LEGACY_U16_WRAP_ADD(fraction_y, DRAW_LINE_FIXED_ROUNDING);
	legacy_u16 step = line[DRAW_LINE_STEP_INDEX];
	for (legacy_s16 remaining = LEGACY_S16_FROM_BITS(line[DRAW_LINE_PIXEL_COUNT_INDEX]);
		 remaining > 0; remaining--) {
		prerender_grille_pixel(x, y);
		if (mode == DRAW_LINE_MODE_Y_MAJOR_LEFT) {
			legacy_u16 previous = fraction_x;
			fraction_x = LEGACY_U16_WRAP_SUB(fraction_x, step);
			if (previous < step) {
				x--;
			}
		} else if (mode == DRAW_LINE_MODE_Y_MAJOR_RIGHT) {
			legacy_u16 previous = fraction_x;
			fraction_x = LEGACY_U16_WRAP_ADD(fraction_x, step);
			if (fraction_x < previous) {
				x++;
			}
		} else if (mode == DRAW_LINE_MODE_DIAGONAL_LEFT || mode == DRAW_LINE_MODE_X_MAJOR_LEFT) {
			x--;
		} else if (mode != DRAW_LINE_MODE_VERTICAL) {
			x++;
		}
		if (mode >= DRAW_LINE_MODE_VERTICAL && mode <= DRAW_LINE_MODE_Y_MAJOR_RIGHT) {
			y++;
		} else if (mode == DRAW_LINE_MODE_X_MAJOR_LEFT || mode == DRAW_LINE_MODE_X_MAJOR_RIGHT) {
			legacy_u16 previous = fraction_y;
			fraction_y = LEGACY_U16_WRAP_ADD(fraction_y, step);
			if (fraction_y < previous) {
				y++;
			}
		}
	}
}

void preRender_line(legacy_u16 start_x, legacy_u16 start_y, legacy_u16 end_x, legacy_u16 end_y,
					legacy_u16 color)
{
	if (color == PRERENDER_GHOST_COLOR) {
		prerender_grille_line(start_x, start_y, end_x, end_y, 0);
		return;
	}
	legacy_u16 line[DRAW_LINE_WORD_COUNT];

	prerender_line(start_x, start_y, end_x, end_y, color, line);
}

void preRender_crack_line(legacy_u16 start_x, legacy_u16 start_y, legacy_u16 end_x,
						  legacy_u16 end_y, legacy_u16 color)
{
	legacy_u16 line[DRAW_LINE_WORD_COUNT];
	legacy_s16 drawn;

	drawn = prerender_line(start_x, start_y, end_x, end_y, color, line);
	shape3d_retain_legacy_crack_line(line, end_y, slow_video_mgmt_copy, drawn);
}

void draw_beveled_border(legacy_s16 x, legacy_s16 y, legacy_s16 width, legacy_s16 height,
						 legacy_s16 top_outer_color, legacy_s16 top_inner_color,
						 legacy_s16 bottom_outer_color, legacy_s16 bottom_inner_color)
{
	legacy_s16 right = x + width;
	legacy_s16 bottom = y + height;

	preRender_line(x, y, right, y, top_outer_color);
	preRender_line(x + 1, y + 1, right - 1, y + 1, top_outer_color);
	preRender_line(x + 2, y + 2, right - 2, y + 2, top_inner_color);

	preRender_line(x, y, x, bottom, top_outer_color);
	preRender_line(x + 1, y + 1, x + 1, bottom - 1, top_outer_color);
	preRender_line(x + 2, y + 2, x + 2, bottom - 2, top_inner_color);

	preRender_line(x, bottom, right, bottom, bottom_outer_color);
	preRender_line(x + 1, bottom - 1, right - 1, bottom - 1, bottom_outer_color);
	preRender_line(x + 2, bottom - 2, right - 2, bottom - 2, bottom_inner_color);

	preRender_line(right, y, right, bottom, bottom_outer_color);
	preRender_line(right - 1, y + 1, right - 1, bottom - 1, bottom_outer_color);
	preRender_line(right - 2, y + 2, right - 2, bottom - 2, bottom_inner_color);
}

void draw_three_color_beveled_border(legacy_s16 x, legacy_s16 y, legacy_s16 width,
									 legacy_s16 height, legacy_s16 outer_color,
									 legacy_s16 inner_color, legacy_s16 opposite_color)
{
	draw_beveled_border(x, y, width, height, outer_color, inner_color, opposite_color, inner_color);
}

void preRender_default_alt(legacy_u16 color, legacy_u16 vertex_count,
						   const struct POINT2D *vertices)
{
	//return ported_preRender_default_alt_(color, vertex_count, vertices);

	if (color == PRERENDER_GHOST_COLOR) {
		spritefunc = &draw_patterned_lines;
		imagefunc = &prerender_grille_line;
		raster_fill_pattern = PRERENDER_BLACK_GRILLE_PATTERN;
		color = 0;
	} else {
		spritefunc = &draw_filled_lines;
		imagefunc = &preRender_line;
	}
	polygon_rasterize(color, vertex_count, vertices, 0);
}

void preRender_default(legacy_u16 color, legacy_u16 vertex_count, const struct POINT2D *vertices)
{
	//return ported_preRender_default_(color, vertex_count, vertices);

	if (color == PRERENDER_GHOST_COLOR) {
		spritefunc = &draw_patterned_lines;
		imagefunc = &prerender_grille_line;
		raster_fill_pattern = PRERENDER_BLACK_GRILLE_PATTERN;
		color = 0;
	} else {
		spritefunc = &draw_filled_lines;
		imagefunc = &preRender_line;
	}
	polygon_rasterize(color, vertex_count, vertices, 1);
}

static void preRender_default_alt_words(legacy_u16 color, legacy_u16 vertex_count,
										const legacy_u16 *words)
{
	struct POINT2D vertices[SPHERE_VERTEX_COUNT];
	for (legacy_u16 index = 0; index < vertex_count; index++) {
		vertices[index].px = LEGACY_S16_FROM_BITS(words[index * PRERENDER_POINT_WORD_COUNT]);
		vertices[index].py = LEGACY_S16_FROM_BITS(
			words[index * PRERENDER_POINT_WORD_COUNT + PRERENDER_POINT_Y_OFFSET]);
	}
	preRender_default_alt(color, vertex_count, vertices);
}

static legacy_u16 sphere_scale_sum(legacy_u16 left, legacy_u16 right, legacy_u16 scale)
{
	return (legacy_u16)multiply_and_scale(LEGACY_S16_FROM_BITS(LEGACY_U16_WRAP_ADD(left, right)),
										  LEGACY_S16_FROM_BITS(scale));
}

static legacy_u16 sphere_scale_difference(legacy_u16 left, legacy_u16 right, legacy_u16 scale)
{
	return (legacy_u16)multiply_and_scale(LEGACY_S16_FROM_BITS(LEGACY_U16_WRAP_SUB(left, right)),
										  LEGACY_S16_FROM_BITS(scale));
}

static void sphere_build_coordinate(legacy_u16 *destination)
{
	legacy_u16 half_first = sar1_word(destination[0]);
	legacy_u16 quarter_first = sar1_word(half_first);
	legacy_u16 three_quarters_first = LEGACY_U16_WRAP_ADD(half_first, quarter_first);
	legacy_u16 half_second = sar1_word(destination[16]);
	legacy_u16 quarter_second = sar1_word(half_second);
	legacy_u16 three_quarters_second = LEGACY_U16_WRAP_ADD(half_second, quarter_second);
	destination[8] =
		sphere_scale_sum(destination[16], destination[0], SPHERE_DIAGONAL_NORMALIZATION);
	destination[4] = sphere_scale_sum(destination[0], half_second, SPHERE_HALF_STEP_NORMALIZATION);
	destination[12] = sphere_scale_sum(destination[16], half_first, SPHERE_HALF_STEP_NORMALIZATION);
	destination[2] =
		sphere_scale_sum(destination[0], quarter_second, SPHERE_QUARTER_STEP_NORMALIZATION);
	destination[14] =
		sphere_scale_sum(destination[16], quarter_first, SPHERE_QUARTER_STEP_NORMALIZATION);
	destination[6] = sphere_scale_sum(destination[0], three_quarters_second,
									  SPHERE_THREE_QUARTER_STEP_NORMALIZATION);
	destination[10] = sphere_scale_sum(destination[16], three_quarters_first,
									   SPHERE_THREE_QUARTER_STEP_NORMALIZATION);
	legacy_u16 negative_first = LEGACY_U16_WRAP_SUB(0, destination[0]);
	destination[24] =
		sphere_scale_sum(destination[16], negative_first, SPHERE_DIAGONAL_NORMALIZATION);
	destination[28] = sphere_scale_sum(half_second, negative_first, SPHERE_HALF_STEP_NORMALIZATION);
	destination[20] =
		sphere_scale_difference(destination[16], half_first, SPHERE_HALF_STEP_NORMALIZATION);
	destination[30] =
		sphere_scale_sum(quarter_second, negative_first, SPHERE_QUARTER_STEP_NORMALIZATION);
	destination[18] =
		sphere_scale_difference(destination[16], quarter_first, SPHERE_QUARTER_STEP_NORMALIZATION);
	destination[26] = sphere_scale_sum(three_quarters_second, negative_first,
									   SPHERE_THREE_QUARTER_STEP_NORMALIZATION);
	destination[22] = sphere_scale_difference(destination[16], three_quarters_first,
											  SPHERE_THREE_QUARTER_STEP_NORMALIZATION);
}

void sphere_build_perimeter(legacy_u16 *source, legacy_u16 *destination)
{
	destination[0] = LEGACY_U16_WRAP_SUB(source[2], source[0]);
	destination[1] = LEGACY_U16_WRAP_SUB(source[3], source[1]);
	destination[16] = LEGACY_U16_WRAP_SUB(source[4], source[0]);
	destination[17] = LEGACY_U16_WRAP_SUB(source[5], source[1]);
	sphere_build_coordinate(destination);
	sphere_build_coordinate(destination + PRERENDER_POINT_Y_OFFSET);

	legacy_u16 center_x = (legacy_u16)source[0];
	legacy_u16 center_y = (legacy_u16)source[1];
	for (legacy_u16 index = 0; index < SPHERE_PERIMETER_POINT_COUNT; index++) {
		destination[SPHERE_MIRROR_WORD_OFFSET + index * PRERENDER_POINT_WORD_COUNT] =
			LEGACY_U16_WRAP_SUB(center_x, destination[index * PRERENDER_POINT_WORD_COUNT]);
		destination[SPHERE_MIRROR_WORD_OFFSET + index * PRERENDER_POINT_WORD_COUNT +
					PRERENDER_POINT_Y_OFFSET] =
			LEGACY_U16_WRAP_SUB(
				center_y,
				destination[index * PRERENDER_POINT_WORD_COUNT + PRERENDER_POINT_Y_OFFSET]);
		destination[index * PRERENDER_POINT_WORD_COUNT] =
			LEGACY_U16_WRAP_ADD(destination[index * PRERENDER_POINT_WORD_COUNT], center_x);
		destination[index * PRERENDER_POINT_WORD_COUNT + PRERENDER_POINT_Y_OFFSET] =
			LEGACY_U16_WRAP_ADD(
				destination[index * PRERENDER_POINT_WORD_COUNT + PRERENDER_POINT_Y_OFFSET],
				center_y);
	}
}

void sphere_draw_polygon(legacy_u16 *source, legacy_u16 color)
{
	legacy_u16 vertices[SPHERE_VERTEX_WORD_COUNT];

	sphere_build_perimeter(source, vertices);
	preRender_default_alt_words(color, SPHERE_VERTEX_COUNT, vertices);
}

void wheel_build_perimeter(legacy_u16 *source, legacy_u16 *destination)
{
	destination[0] = LEGACY_U16_WRAP_SUB(source[2], source[0]);
	destination[1] = LEGACY_U16_WRAP_SUB(source[3], source[1]);
	destination[8] = LEGACY_U16_WRAP_SUB(source[4], source[0]);
	destination[9] = LEGACY_U16_WRAP_SUB(source[5], source[1]);
	destination[4] =
		sphere_scale_sum(destination[8], destination[0], SPHERE_DIAGONAL_NORMALIZATION);
	destination[5] =
		sphere_scale_sum(destination[1], destination[9], SPHERE_DIAGONAL_NORMALIZATION);
	legacy_u16 half_x2 = sar1_word((legacy_u16)destination[8]);
	legacy_u16 half_y2 = sar1_word((legacy_u16)destination[9]);
	destination[2] = sphere_scale_sum(destination[0], half_x2, SPHERE_HALF_STEP_NORMALIZATION);
	destination[3] = sphere_scale_sum(destination[1], half_y2, SPHERE_HALF_STEP_NORMALIZATION);
	legacy_u16 half_x1 = sar1_word((legacy_u16)destination[0]);
	legacy_u16 half_y1 = sar1_word((legacy_u16)destination[1]);
	destination[6] = sphere_scale_sum(destination[8], half_x1, SPHERE_HALF_STEP_NORMALIZATION);
	destination[7] = sphere_scale_sum(destination[9], half_y1, SPHERE_HALF_STEP_NORMALIZATION);
	destination[12] =
		sphere_scale_difference(destination[8], destination[0], SPHERE_DIAGONAL_NORMALIZATION);
	destination[13] =
		sphere_scale_difference(destination[9], destination[1], SPHERE_DIAGONAL_NORMALIZATION);
	destination[14] =
		sphere_scale_difference(half_x2, destination[0], SPHERE_HALF_STEP_NORMALIZATION);
	destination[15] =
		sphere_scale_difference(half_y2, destination[1], SPHERE_HALF_STEP_NORMALIZATION);
	destination[10] =
		sphere_scale_difference(destination[8], half_x1, SPHERE_HALF_STEP_NORMALIZATION);
	destination[11] =
		sphere_scale_difference(destination[9], half_y1, SPHERE_HALF_STEP_NORMALIZATION);

	legacy_u16 center_x = (legacy_u16)source[0];
	legacy_u16 center_y = (legacy_u16)source[1];
	for (legacy_u16 index = 0; index < 8U; index++) {
		destination[16U + index * 2U] = LEGACY_U16_WRAP_SUB(center_x, destination[index * 2U]);
		destination[17U + index * 2U] = LEGACY_U16_WRAP_SUB(center_y, destination[index * 2U + 1U]);
		destination[index * 2U] = LEGACY_U16_WRAP_ADD(destination[index * 2U], center_x);
		destination[index * 2U + 1U] = LEGACY_U16_WRAP_ADD(destination[index * 2U + 1U], center_y);
	}
}

static legacy_u16 wheel_interpolate(legacy_u16 center, legacy_u16 target, legacy_u16 scale)
{
	return LEGACY_U16_WRAP_ADD(
		center,
		(legacy_u16)multiply_and_scale(LEGACY_S16_FROM_BITS(LEGACY_U16_WRAP_SUB(target, center)),
									   LEGACY_S16_FROM_BITS(scale)));
}

void wheel_build_rim_perimeters(legacy_u16 *source, legacy_u16 *destination, legacy_u16 scale)
{
	legacy_u16 center_x = (legacy_u16)source[0];
	legacy_u16 center_y = (legacy_u16)source[1];
	legacy_u16 scale_bits = (legacy_u16)scale;
	legacy_u16 inner_source[6];
	inner_source[0] = center_x;
	inner_source[1] = center_y;
	inner_source[2] = wheel_interpolate(center_x, (legacy_u16)source[2], scale_bits);
	inner_source[3] = wheel_interpolate(center_y, (legacy_u16)source[3], scale_bits);
	inner_source[4] = wheel_interpolate(center_x, (legacy_u16)source[4], scale_bits);
	inner_source[5] = wheel_interpolate(center_y, (legacy_u16)source[5], scale_bits);
	wheel_build_perimeter(source, destination);
	wheel_build_perimeter(inner_source, destination + WHEEL_INNER_WORD_OFFSET);
}

void wheel_build_vertices(legacy_u16 *source, legacy_u16 *destination, legacy_u16 scale)
{
	wheel_build_rim_perimeters(source, destination, scale);
	legacy_u16 offset_x = LEGACY_U16_WRAP_SUB(source[6], source[0]);
	legacy_u16 offset_y = LEGACY_U16_WRAP_SUB(source[7], source[1]);
	for (legacy_u16 index = 0; index < WHEEL_PERIMETER_POINT_COUNT; index++) {
		destination[WHEEL_DEPTH_WORD_OFFSET + index * PRERENDER_POINT_WORD_COUNT] =
			LEGACY_U16_WRAP_ADD(destination[index * PRERENDER_POINT_WORD_COUNT], offset_x);
		destination[WHEEL_DEPTH_WORD_OFFSET + index * PRERENDER_POINT_WORD_COUNT +
					PRERENDER_POINT_Y_OFFSET] =
			LEGACY_U16_WRAP_ADD(
				destination[index * PRERENDER_POINT_WORD_COUNT + PRERENDER_POINT_Y_OFFSET],
				offset_y);
	}
}

static void preRender_wheel_side(const legacy_u16 *wheel_points, legacy_u16 minimum_index,
								 legacy_u16 reverse, legacy_u16 color)
{
	legacy_u16 side[WHEEL_SIDE_WORD_COUNT];
	for (legacy_u16 step = 0; step < WHEEL_SIDE_HALF_POINT_COUNT; step++) {
		legacy_u16 point_index;
		if (reverse == 0U) {
			point_index = (legacy_u16)((minimum_index + step) & WHEEL_PERIMETER_INDEX_MASK);
		} else {
			point_index = (legacy_u16)((minimum_index + WHEEL_PERIMETER_POINT_COUNT - step) &
									   WHEEL_PERIMETER_INDEX_MASK);
		}
		legacy_u16 destination_index = (legacy_u16)(step * PRERENDER_POINT_WORD_COUNT);
		side[destination_index] = wheel_points[point_index * PRERENDER_POINT_WORD_COUNT];
		side[destination_index + PRERENDER_POINT_Y_OFFSET] =
			wheel_points[point_index * PRERENDER_POINT_WORD_COUNT + PRERENDER_POINT_Y_OFFSET];
		destination_index =
			(legacy_u16)((WHEEL_SIDE_LAST_POINT_INDEX - step) * PRERENDER_POINT_WORD_COUNT);
		side[destination_index] =
			wheel_points[WHEEL_INNER_WORD_OFFSET + point_index * PRERENDER_POINT_WORD_COUNT];
		side[destination_index + PRERENDER_POINT_Y_OFFSET] =
			wheel_points[WHEEL_INNER_WORD_OFFSET + point_index * PRERENDER_POINT_WORD_COUNT +
						 PRERENDER_POINT_Y_OFFSET];
	}
	preRender_default_alt_words(color, WHEEL_SIDE_VERTEX_COUNT, side);
}

void preRender_wheel(const struct POINT2D *source, legacy_u16 scale, legacy_u16 outer_color,
					 legacy_u16 side_color, legacy_u16 inner_color)
{
	legacy_u16 source_words[WHEEL_SOURCE_WORD_COUNT];
	for (legacy_u16 index = 0; index < WHEEL_SOURCE_POINT_COUNT; index++) {
		source_words[index * PRERENDER_POINT_WORD_COUNT] = (legacy_u16)source[index].px;
		source_words[index * PRERENDER_POINT_WORD_COUNT + PRERENDER_POINT_Y_OFFSET] =
			(legacy_u16)source[index].py;
	}
	legacy_u16 wheel_points[WHEEL_POINT_WORD_COUNT];
	wheel_build_vertices(source_words, wheel_points, scale);
	legacy_u16 quad[WHEEL_QUAD_WORD_COUNT];
	for (legacy_u16 index = 0; index < WHEEL_PERIMETER_POINT_COUNT; index++) {
		legacy_u16 next_index = (legacy_u16)((index + 1U) & WHEEL_PERIMETER_INDEX_MASK);
		quad[0] = wheel_points[index * PRERENDER_POINT_WORD_COUNT];
		quad[1] = wheel_points[index * PRERENDER_POINT_WORD_COUNT + PRERENDER_POINT_Y_OFFSET];
		quad[2] = wheel_points[next_index * PRERENDER_POINT_WORD_COUNT];
		quad[3] = wheel_points[next_index * PRERENDER_POINT_WORD_COUNT + PRERENDER_POINT_Y_OFFSET];
		quad[4] = wheel_points[WHEEL_DEPTH_WORD_OFFSET + next_index * PRERENDER_POINT_WORD_COUNT];
		quad[5] = wheel_points[WHEEL_DEPTH_WORD_OFFSET + next_index * PRERENDER_POINT_WORD_COUNT +
							   PRERENDER_POINT_Y_OFFSET];
		quad[6] = wheel_points[WHEEL_DEPTH_WORD_OFFSET + index * PRERENDER_POINT_WORD_COUNT];
		quad[7] = wheel_points[WHEEL_DEPTH_WORD_OFFSET + index * PRERENDER_POINT_WORD_COUNT +
							   PRERENDER_POINT_Y_OFFSET];
		preRender_default_alt_words(outer_color, WHEEL_QUAD_POINT_COUNT, quad);
	}

	legacy_u16 minimum_y = (legacy_u16)wheel_points[1];
	legacy_u16 minimum_index = 0;
	for (legacy_u16 index = 1; index < WHEEL_PERIMETER_POINT_COUNT; index++) {
		if (LEGACY_S16_FROM_BITS(
				wheel_points[index * PRERENDER_POINT_WORD_COUNT + PRERENDER_POINT_Y_OFFSET]) <
			LEGACY_S16_FROM_BITS(minimum_y)) {
			minimum_y = (legacy_u16)
				wheel_points[index * PRERENDER_POINT_WORD_COUNT + PRERENDER_POINT_Y_OFFSET];
			minimum_index = index;
		}
	}

	preRender_wheel_side(wheel_points, minimum_index, 0U, side_color);
	preRender_wheel_side(wheel_points, minimum_index, 1U, side_color);
	preRender_default_alt_words(inner_color, WHEEL_PERIMETER_POINT_COUNT,
								&wheel_points[WHEEL_INNER_WORD_OFFSET]);
}

struct SPHERE_RASTER {
	legacy_u16 x_bits;
	legacy_u16 y_bits;
	legacy_u16 size_bits;
	legacy_u16 effective_height;
	legacy_u16 half_height;
	legacy_u16 half_width;
	legacy_u16 top;
	legacy_u16 line_count;
	legacy_u16 left_bound;
	legacy_u16 right_bound;
};

static legacy_s16 sphere_build_spans(struct SPHERE_RASTER *sphere, legacy_s16 *left_edges,
									 legacy_s16 *right_edges)
{
	legacy_u8 *radii = sphere_radius_rows[sphere->half_width];
	sphere->line_count = sphere->effective_height;
	legacy_s16 mirror_offset = (legacy_s16)((sphere->effective_height - 1U) << 1);
	legacy_u16 output_index = 0;
	for (;;) {
		legacy_u8 radius = *radii++;
		legacy_u16 left = LEGACY_U16_WRAP_SUB(sphere->x_bits, radius);
		legacy_u16 right = LEGACY_U16_WRAP_ADD(sphere->x_bits, radius);
		if (LEGACY_S16_FROM_BITS(left) > LEGACY_S16_FROM_BITS(sphere->right_bound) ||
			LEGACY_S16_FROM_BITS(right) < LEGACY_S16_FROM_BITS(sphere->left_bound)) {
			sphere->top = LEGACY_U16_WRAP_ADD(sphere->top, 1U);
			sphere->line_count = LEGACY_U16_WRAP_SUB(sphere->line_count, 2U);
			mirror_offset -= 4;
			if (mirror_offset < 0) {
				return 0;
			}
			continue;
		}
		if (LEGACY_S16_FROM_BITS(left) < LEGACY_S16_FROM_BITS(sphere->left_bound)) {
			left = sphere->left_bound;
		}
		if (LEGACY_S16_FROM_BITS(right) > LEGACY_S16_FROM_BITS(sphere->right_bound)) {
			right = sphere->right_bound;
		}
		left_edges[output_index] = left;
		right_edges[output_index] = right;
		left_edges[output_index + (legacy_u16)(mirror_offset >> 1)] = left;
		right_edges[output_index + (legacy_u16)(mirror_offset >> 1)] = right;
		output_index++;
		mirror_offset -= 4;
		if (mirror_offset < 0) {
			break;
		}
	}

	return 1;
}

static void sphere_draw_spans(struct SPHERE_RASTER *sphere, legacy_s16 *left_edges,
							  legacy_s16 *right_edges, legacy_u16 color)
{
	legacy_s16 clip_delta = LEGACY_S16_WRAP_SUB(drawing_sprite.sprite_top, sphere->top);
	legacy_u16 skip_lines = 0;
	if (clip_delta > 0) {
		sphere->line_count = LEGACY_U16_WRAP_SUB(sphere->line_count, clip_delta);
		skip_lines = (legacy_u16)clip_delta;
		sphere->top = drawing_sprite.sprite_top;
	}
	clip_delta = LEGACY_S16_WRAP_SUB(LEGACY_U16_WRAP_ADD(sphere->top, sphere->line_count),
									 drawing_sprite.sprite_bottom);
	if (clip_delta > 0) {
		sphere->line_count = LEGACY_U16_WRAP_SUB(sphere->line_count, clip_delta);
	}
	if (color == PRERENDER_GHOST_COLOR) {
		raster_fill_pattern = PRERENDER_BLACK_GRILLE_PATTERN;
		draw_patterned_lines(&left_edges[skip_lines], &right_edges[skip_lines], sphere->top,
							 sphere->line_count, 0);
	} else {
		draw_filled_lines(&left_edges[skip_lines], &right_edges[skip_lines], sphere->top,
						  sphere->line_count, color);
	}
}

void preRender_sphere(legacy_s16 x, legacy_s16 y, legacy_u16 size, legacy_u16 color)
{
	struct SPHERE_RASTER sphere;
	sphere.x_bits = (legacy_u16)x;
	sphere.y_bits = (legacy_u16)y;
	sphere.size_bits = (legacy_u16)size;
	sphere.effective_height =
		LEGACY_U16_WRAP_SUB(sphere.size_bits, (legacy_u16)(sphere.size_bits >> 2));
	sphere.effective_height =
		LEGACY_U16_WRAP_ADD(sphere.effective_height, (legacy_u16)(sphere.size_bits >> 4));
	if (LEGACY_S16_FROM_BITS(sphere.effective_height) <= 0) {
		return;
	}

	sphere.half_height = (legacy_u16)(sphere.effective_height >> 1);
	if (sphere.half_height == 0) {
		if (color == PRERENDER_GHOST_COLOR) {
			prerender_grille_pixel(sphere.x_bits, sphere.y_bits);
		} else {
			sprite_putpixel_clipped(LEGACY_S16_FROM_BITS(sphere.x_bits),
									LEGACY_S16_FROM_BITS(sphere.y_bits), color);
		}
		return;
	}
	sphere.half_width = LEGACY_U16_WRAP_SUB(sphere.effective_height, sphere.half_height);
	sphere.left_bound = drawing_sprite.sprite_raster_left;
	sphere.right_bound = LEGACY_U16_WRAP_SUB(drawing_sprite.sprite_raster_right, 1U);
	sphere.top = LEGACY_U16_WRAP_SUB(sphere.y_bits, sphere.half_height);
	if (LEGACY_S16_FROM_BITS(sphere.top) >= LEGACY_S16_FROM_BITS(drawing_sprite.sprite_bottom)) {
		return;
	}
	if (LEGACY_S16_FROM_BITS(LEGACY_U16_WRAP_ADD(sphere.y_bits, sphere.half_width)) <=
		LEGACY_S16_FROM_BITS(drawing_sprite.sprite_top)) {
		return;
	}
	sphere.half_width =
		LEGACY_U16_WRAP_ADD(sphere.half_width, (legacy_u16)(sphere.half_width >> 2));
	if (LEGACY_S16_FROM_BITS(LEGACY_U16_WRAP_SUB(sphere.x_bits, sphere.half_width)) >
		LEGACY_S16_FROM_BITS(sphere.right_bound)) {
		return;
	}
	if (LEGACY_S16_FROM_BITS(LEGACY_U16_WRAP_ADD(sphere.x_bits, sphere.half_width)) <
		LEGACY_S16_FROM_BITS(sphere.left_bound)) {
		return;
	}

	sphere.half_width = LEGACY_U16_WRAP_SUB(sphere.effective_height, sphere.half_height);
	legacy_u16 helper_points[6];
	if (sphere.half_width >= SPHERE_RASTER_TABLE_LIMIT) {
		helper_points[0] = sphere.x_bits;
		helper_points[1] = sphere.y_bits;
		helper_points[2] = sphere.x_bits;
		helper_points[3] = LEGACY_U16_WRAP_ADD(sphere.y_bits, sphere.half_height);
		helper_points[4] = LEGACY_U16_WRAP_ADD(sphere.x_bits, (legacy_u16)(sphere.size_bits >> 1));
		helper_points[5] = sphere.y_bits;
		sphere_draw_polygon(helper_points, color);
		return;
	}

	legacy_s16 right_edges[SPHERE_RASTER_TABLE_LIMIT * 2U];
	legacy_s16 left_edges[SPHERE_RASTER_TABLE_LIMIT * 2U];
	if (sphere_build_spans(&sphere, left_edges, right_edges)) {
		sphere_draw_spans(&sphere, left_edges, right_edges, color);
	}
}

void skybox_fill_polygon(legacy_u16 color, legacy_u16 vertex_count, struct POINT2D vertices[])
{
	preRender_default(color, vertex_count, vertices);
}

void wheel_fill_polygon(legacy_u16 color, legacy_u16 vertex_count, struct POINT2D vertices[])
{
	preRender_default_alt(color, vertex_count, vertices);
}

void preRender_two_color(legacy_u16 pattern, legacy_u16 color, legacy_u16 alternate_color,
						 legacy_u16 vertex_count, const struct POINT2D *vertices)
{
	spritefunc = &draw_two_color_lines;
	imagefunc = &preRender_line;

	raster_fill_pattern = pattern;
	raster_alternate_color = alternate_color;
	polygon_rasterize(color, vertex_count, vertices, 1);
}

void preRender_patterned(legacy_u16 pattern, legacy_u16 color, legacy_u16 vertex_count,
						 const struct POINT2D *vertices)
{
	//return ported_preRender_patterned_(pattern, color, vertex_count, vertices);

	spritefunc = &draw_patterned_lines;
	imagefunc = &preRender_line;
	raster_fill_pattern = pattern;

	polygon_rasterize(color, vertex_count, vertices, 1);
}
