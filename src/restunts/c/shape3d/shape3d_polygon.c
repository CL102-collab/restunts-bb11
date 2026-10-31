#include "../externs.h"
#include "../legacy.h"
#include "../shape2d/shape2d.h"
#include "../shape2d/shape2d_internal.h"
#include "shape3d.h"
#include "shape3d_internal.h"

#include "shape3d_prerender_internal.h"

struct POLYGON_RASTER {
	const struct POINT2D *first_vertex;
	const struct POINT2D *last_vertex;
	const struct POINT2D *top_vertex;
	const struct POINT2D *bottom_vertex;
	legacy_s16 min_x, max_x;
	legacy_s16 top_y, bottom_y;
	legacy_s16 clip_left, clip_right, clip_top, clip_bottom;
	legacy_u16 needs_clipping;
};

static void polygon_find_bounds(const struct POINT2D *vertices, legacy_u16 vertex_count,
								struct POLYGON_RASTER *raster)
{
	raster->clip_left = drawing_sprite.sprite_raster_left;
	raster->clip_right = drawing_sprite.sprite_raster_right - 1;
	raster->clip_top = drawing_sprite.sprite_top;
	raster->clip_bottom = drawing_sprite.sprite_bottom;
	raster->first_vertex = vertices;
	raster->last_vertex = vertices + vertex_count - 1U;
	raster->top_vertex = vertices;
	raster->bottom_vertex = vertices;
	raster->bottom_y = raster->top_y = vertices->py;
	raster->max_x = raster->min_x = vertices->px;
	for (legacy_s16 vertex_index = 1; vertex_index < vertex_count; vertex_index++) {
		if (vertices[vertex_index].py <= raster->top_y) {
			raster->top_y = vertices[vertex_index].py;
			raster->top_vertex = &vertices[vertex_index];
		}
		if (vertices[vertex_index].py > raster->bottom_y) {
			raster->bottom_y = vertices[vertex_index].py;
			raster->bottom_vertex = &vertices[vertex_index];
		}
		if (vertices[vertex_index].px < raster->min_x) {
			raster->min_x = vertices[vertex_index].px;
		}
		if (vertices[vertex_index].px > raster->max_x) {
			raster->max_x = vertices[vertex_index].px;
		}
	}
}

static legacy_s16 polygon_is_outside(const struct POLYGON_RASTER *raster)
{
	return raster->max_x < raster->clip_left || raster->min_x >= raster->clip_right ||
		   raster->bottom_y < raster->clip_top || raster->top_y >= raster->clip_bottom;
}

static void polygon_prepare_segment(const struct POINT2D *start, const struct POINT2D *end,
									legacy_u16 *line_setup, legacy_u16 needs_clipping)
{
	if (needs_clipping != 0) {
		line_prepare_clipped(start->px, start->py, end->px, end->py, line_setup);
	} else {
		line_prepare_unclipped(start->px, start->py, end->px, end->py, line_setup);
	}
}

static void polygon_generate_first_side(const struct POLYGON_RASTER *raster, legacy_s16 *edges,
										legacy_u16 *line_setup)
{
	const struct POINT2D *current_vertex = raster->top_vertex;
	const struct POINT2D *start_vertex;
	do {
		start_vertex = current_vertex;
		current_vertex++;
		if (current_vertex > raster->last_vertex) {
			current_vertex = raster->first_vertex;
		}
		if (current_vertex->py > start_vertex->py) {
			polygon_prepare_segment(start_vertex, current_vertex, line_setup,
									raster->needs_clipping);
			generate_poly_edges(edges, line_setup,
								raster->needs_clipping != 0 ? PRERENDER_EDGE_CLIPPED_MODE
															: PRERENDER_EDGE_UNCLIPPED_MODE);
		}
	} while (current_vertex != raster->bottom_vertex);
}

static void polygon_merge_second_side(const struct POLYGON_RASTER *raster, legacy_s16 *edges,
									  legacy_u16 *line_setup, legacy_u16 choose_edge_per_row)
{
	const struct POINT2D *current_vertex = raster->top_vertex;
	const struct POINT2D *start_vertex;
	do {
		start_vertex = current_vertex;
		if (current_vertex == raster->first_vertex) {
			current_vertex = raster->last_vertex;
		} else {
			current_vertex--;
		}
		if (current_vertex->py > start_vertex->py) {
			polygon_prepare_segment(start_vertex, current_vertex, line_setup,
									raster->needs_clipping);
			polygon_merge_second_edge(line_setup, choose_edge_per_row, raster->needs_clipping,
									  edges);
		}
	} while (current_vertex != raster->bottom_vertex);
}

static void polygon_draw_spans(const struct POLYGON_RASTER *raster, legacy_s16 *edges,
							   legacy_u16 color)
{
	legacy_s16 bottom = raster->bottom_y;
	if (bottom >= raster->clip_bottom) {
		bottom = raster->clip_bottom - 1;
	}
	legacy_s16 top = raster->top_y;
	if (top < raster->clip_top) {
		top = raster->clip_top;
	}
	legacy_s16 line_count = bottom - top;
	if (line_count <= 0) {
		return;
	}
	line_count++;
	spritefunc(&edges[top], &edges[PRERENDER_EDGE_ROW_CAPACITY + top], top, line_count, color);
}

void polygon_rasterize(legacy_u16 color, legacy_u16 vertex_count, const struct POINT2D *vertices,
					   legacy_u16 choose_edge_per_row)
{
	if (vertex_count == 0U) {
		return;
	}
	struct POLYGON_RASTER raster;
	polygon_find_bounds(vertices, vertex_count, &raster);
	if (vertex_count == 1U) {
		imagefunc(vertices->px, vertices->py, vertices->px, vertices->py, color);
		return;
	}
	if (polygon_is_outside(&raster)) {
		return;
	}
	raster.needs_clipping = 0;
	if (raster.max_x > raster.clip_right || raster.min_x < raster.clip_left ||
		raster.bottom_y >= raster.clip_bottom || raster.top_y < raster.clip_top) {
		raster.needs_clipping = 1;
	}
	if (raster.bottom_y == raster.top_y || raster.max_x == raster.min_x) {
		imagefunc(raster.min_x, raster.top_y, raster.max_x, raster.bottom_y, color);
		return;
	}
	legacy_u16 line_setup[DRAW_LINE_WORD_COUNT * PRERENDER_LINE_BUFFER_COUNT];
	legacy_s16 edge_tables[PRERENDER_EDGE_ROW_CAPACITY * PRERENDER_EDGE_TABLE_COUNT];
	polygon_generate_first_side(&raster, edge_tables, line_setup);
	polygon_merge_second_side(&raster, edge_tables, line_setup, choose_edge_per_row);
	polygon_draw_spans(&raster, edge_tables, color);
}

static void generate_poly_edge_fill(legacy_s16 *edges, legacy_s16 offset, legacy_s16 count,
									legacy_s16 boundary)
{
	for (legacy_s16 index = 0; index < count; index++) {
		edges[offset + index] = boundary;
		edges[PRERENDER_EDGE_ROW_CAPACITY + offset + index] = boundary - 1;
	}
}

static void generate_poly_edge_padding(legacy_s16 *edges, const legacy_u16 *setup,
									   legacy_u16 count_index, legacy_s16 boundary,
									   legacy_s16 leading)
{
	legacy_s16 count = LEGACY_S16_FROM_BITS(setup[count_index]);
	if (count <= 0) {
		return;
	}
	legacy_s16 offset;
	if (leading != 0) {
		offset = LEGACY_S16_WRAP_SUB(LEGACY_S16_FROM_BITS(setup[DRAW_LINE_START_Y_INDEX]), count);
		if (LEGACY_S16_FROM_BITS(setup[DRAW_LINE_START_Y_FRACTION_INDEX]) < 0) {
			offset = LEGACY_S16_WRAP_ADD(offset, 1);
		}
	} else {
		offset = LEGACY_S16_WRAP_ADD(LEGACY_S16_FROM_BITS(setup[DRAW_LINE_END_Y_INDEX]), 1);
	}
	generate_poly_edge_fill(edges, offset, count, boundary);
}

static void generate_poly_vertical(legacy_s16 *edges, const legacy_u16 *line_setup,
								   legacy_s16 step_count, legacy_s16 row_index)
{
	for (legacy_s16 step_index = 0; step_index < step_count; step_index++) {
		edges[row_index + step_index] = LEGACY_S16_FROM_BITS(line_setup[DRAW_LINE_START_X_INDEX]);
		edges[PRERENDER_EDGE_ROW_CAPACITY + row_index + step_index] =
			LEGACY_S16_FROM_BITS(line_setup[DRAW_LINE_START_X_INDEX]);
	}
}

static void generate_poly_diagonal_left(legacy_s16 *edges, const legacy_u16 *line_setup,
										legacy_s16 step_count, legacy_s16 row_index)
{
	for (legacy_s16 step_index = 0; step_index < step_count; step_index++) {
		edges[row_index + step_index] = LEGACY_S16_WRAP_SUB(
			LEGACY_S16_FROM_BITS(line_setup[DRAW_LINE_START_X_INDEX]), step_index);
		edges[PRERENDER_EDGE_ROW_CAPACITY + row_index + step_index] = LEGACY_S16_WRAP_SUB(
			LEGACY_S16_FROM_BITS(line_setup[DRAW_LINE_START_X_INDEX]), step_index);
	}
}

static void generate_poly_diagonal_right(legacy_s16 *edges, const legacy_u16 *line_setup,
										 legacy_s16 step_count, legacy_s16 row_index)
{
	for (legacy_s16 step_index = 0; step_index < step_count; step_index++) {
		edges[row_index + step_index] = LEGACY_S16_WRAP_ADD(
			LEGACY_S16_FROM_BITS(line_setup[DRAW_LINE_START_X_INDEX]), step_index);
		edges[PRERENDER_EDGE_ROW_CAPACITY + row_index + step_index] = LEGACY_S16_WRAP_ADD(
			LEGACY_S16_FROM_BITS(line_setup[DRAW_LINE_START_X_INDEX]), step_index);
	}
}

static void generate_poly_y_major(legacy_s16 *edges, const legacy_u16 *line_setup,
								  legacy_s16 step_count, legacy_s16 row_index)
{
	legacy_u32 x_position =
		LEGACY_U32_WRAP_ADD(LEGACY_U32_FROM_WORDS(line_setup[DRAW_LINE_START_X_FRACTION_INDEX],
												  line_setup[DRAW_LINE_START_X_INDEX]),
							DRAW_LINE_FIXED_ROUNDING);
	for (legacy_s16 step_index = 0; step_index < step_count; step_index++) {
		edges[row_index + step_index] =
			LEGACY_S16_FROM_BITS((legacy_u16)(x_position >> LEGACY_WORD_BITS));
		edges[PRERENDER_EDGE_ROW_CAPACITY + row_index + step_index] =
			LEGACY_S16_FROM_BITS((legacy_u16)(x_position >> LEGACY_WORD_BITS));
		if ((legacy_u8)line_setup[DRAW_LINE_MODE_AND_CLIP_INDEX] == DRAW_LINE_MODE_Y_MAJOR_LEFT) {
			x_position =
				LEGACY_U32_WRAP_SUB(x_position, (legacy_u16)line_setup[DRAW_LINE_STEP_INDEX]);
		} else {
			x_position =
				LEGACY_U32_WRAP_ADD(x_position, (legacy_u16)line_setup[DRAW_LINE_STEP_INDEX]);
		}
	}
}

static void generate_poly_x_major_left(legacy_s16 *edges, const legacy_u16 *line_setup,
									   legacy_s16 step_count, legacy_s16 row_index)
{
	legacy_u32 x_position = (legacy_u16)line_setup[DRAW_LINE_START_X_INDEX];
	legacy_u32 y_fraction = (legacy_u16)line_setup[DRAW_LINE_START_Y_FRACTION_INDEX];
	if (y_fraction + DRAW_LINE_FIXED_ROUNDING > PRERENDER_FIXED_CARRY_LIMIT) {
		row_index++;
	}
	y_fraction = (y_fraction + DRAW_LINE_FIXED_ROUNDING) & LEGACY_U16_MAX;
	edges[PRERENDER_EDGE_ROW_CAPACITY + row_index] = x_position;
	for (legacy_s16 step_index = 0; step_index < step_count; step_index++) {
		if (y_fraction + (legacy_u16)line_setup[DRAW_LINE_STEP_INDEX] <=
			PRERENDER_FIXED_CARRY_LIMIT) {
			x_position--;
			if (step_index == step_count - 1) {
				edges[row_index] = x_position + 1;
			}
		} else {
			edges[row_index] = x_position;
			x_position--;
			row_index++;
			if (step_index + 1 < step_count) {
				edges[PRERENDER_EDGE_ROW_CAPACITY + row_index] = x_position;
			}
		}
		y_fraction = (y_fraction + (legacy_u16)line_setup[DRAW_LINE_STEP_INDEX]) & LEGACY_U16_MAX;
	}
}

static void generate_poly_x_major_right(legacy_s16 *edges, const legacy_u16 *line_setup,
										legacy_s16 step_count, legacy_s16 row_index)
{
	legacy_u32 x_position = (legacy_u16)line_setup[DRAW_LINE_START_X_INDEX];
	legacy_u32 y_fraction = (legacy_u16)line_setup[DRAW_LINE_START_Y_FRACTION_INDEX];
	if (y_fraction + DRAW_LINE_FIXED_ROUNDING > PRERENDER_FIXED_CARRY_LIMIT) {
		row_index++;
	}
	y_fraction = (y_fraction + DRAW_LINE_FIXED_ROUNDING) & LEGACY_U16_MAX;
	edges[row_index] = x_position;
	for (legacy_s16 step_index = 0; step_index < step_count; step_index++) {
		if (y_fraction + (legacy_u16)line_setup[DRAW_LINE_STEP_INDEX] <=
			PRERENDER_FIXED_CARRY_LIMIT) {
			x_position++;
			if (step_index == step_count - 1) {
				edges[PRERENDER_EDGE_ROW_CAPACITY + row_index] = x_position - 1;
			}
		} else {
			edges[PRERENDER_EDGE_ROW_CAPACITY + row_index] = x_position;
			x_position++;
			row_index++;
			if (step_index + 1 < step_count) {
				edges[row_index] = x_position;
			}
		}
		y_fraction = (y_fraction + (legacy_u16)line_setup[DRAW_LINE_STEP_INDEX]) & LEGACY_U16_MAX;
	}
}

void generate_poly_edges(legacy_s16 *edges, const legacy_u16 *line_setup, legacy_s16 clipping_mode)
{
	legacy_s16 clip_left = drawing_sprite.sprite_raster_left;
	legacy_s16 clip_right = drawing_sprite.sprite_raster_right;

	if (clipping_mode != PRERENDER_EDGE_UNCLIPPED_MODE) {
		generate_poly_edge_padding(edges, line_setup, DRAW_LINE_START_LEFT_CLIP_COUNT_INDEX,
								   clip_left, 1);
		generate_poly_edge_padding(edges, line_setup, DRAW_LINE_START_RIGHT_CLIP_COUNT_INDEX,
								   clip_right, 1);
		generate_poly_edge_padding(edges, line_setup, DRAW_LINE_END_LEFT_CLIP_COUNT_INDEX,
								   clip_left, 0);
		generate_poly_edge_padding(edges, line_setup, DRAW_LINE_END_RIGHT_CLIP_COUNT_INDEX,
								   clip_right, 0);
	}

	legacy_s16 step_count = LEGACY_S16_FROM_BITS(line_setup[DRAW_LINE_PIXEL_COUNT_INDEX]);
	if (step_count <= 0) {
		return;
	}

	legacy_s16 row_index = LEGACY_S16_FROM_BITS(line_setup[DRAW_LINE_START_Y_INDEX]);

	switch ((legacy_u8)line_setup[DRAW_LINE_MODE_AND_CLIP_INDEX]) {
		case DRAW_LINE_MODE_HORIZONTAL_REVERSED:
		case DRAW_LINE_MODE_HORIZONTAL:
			return;
		case DRAW_LINE_MODE_VERTICAL:
			generate_poly_vertical(edges, line_setup, step_count, row_index);
			return;

		case DRAW_LINE_MODE_DIAGONAL_LEFT:
			generate_poly_diagonal_left(edges, line_setup, step_count, row_index);
			return;

		case DRAW_LINE_MODE_DIAGONAL_RIGHT:
			generate_poly_diagonal_right(edges, line_setup, step_count, row_index);
			return;

		case DRAW_LINE_MODE_Y_MAJOR_LEFT:
		case DRAW_LINE_MODE_Y_MAJOR_RIGHT:
			generate_poly_y_major(edges, line_setup, step_count, row_index);
			return;

		case DRAW_LINE_MODE_X_MAJOR_LEFT:
			generate_poly_x_major_left(edges, line_setup, step_count, row_index);
			return;

		case DRAW_LINE_MODE_X_MAJOR_RIGHT:
			generate_poly_x_major_right(edges, line_setup, step_count, row_index);
			return;

		case DRAW_LINE_MODE_POINT:
		default:
			return;
	}
}

/* right_edges keeps the largest x seen on a row, left_edges the smallest. */
static void prerender_extend_edge(legacy_s16 *left_edges, legacy_s16 *right_edges, legacy_s16 index,
								  legacy_s16 value, legacy_s16 to_left)
{
	if (to_left) {
		if (left_edges[index] > value) {
			left_edges[index] = value;
		}
	} else if (right_edges[index] < value) {
		right_edges[index] = value;
	}
}

static void prerender_merge_linear_rows(const legacy_u16 *line, legacy_s16 *left_edges,
										legacy_s16 *right_edges)
{
	legacy_s16 x_position = LEGACY_S16_FROM_BITS(line[DRAW_LINE_START_X_INDEX]);
	legacy_u16 mode = (legacy_u8)line[DRAW_LINE_MODE_AND_CLIP_INDEX];
	legacy_s16 count = LEGACY_S16_FROM_BITS(line[DRAW_LINE_PIXEL_COUNT_INDEX]);
	legacy_s16 row_index = LEGACY_S16_FROM_BITS(line[DRAW_LINE_START_Y_INDEX]);
	while (count-- > 0) {
		if (right_edges[row_index] < x_position) {
			right_edges[row_index] = x_position;
		} else if (left_edges[row_index] > x_position) {
			left_edges[row_index] = x_position;
		}
		row_index++;
		if (mode == DRAW_LINE_MODE_DIAGONAL_LEFT) {
			x_position = LEGACY_S16_WRAP_SUB(x_position, 1);
		} else if (mode == DRAW_LINE_MODE_DIAGONAL_RIGHT) {
			x_position = LEGACY_S16_WRAP_ADD(x_position, 1);
		}
	}
}

static void prerender_merge_fractional_rows(const legacy_u16 *line, legacy_s16 *left_edges,
											legacy_s16 *right_edges)
{
	legacy_s16 x_position = LEGACY_S16_FROM_BITS(line[DRAW_LINE_START_X_INDEX]);
	legacy_u16 mode = (legacy_u8)line[DRAW_LINE_MODE_AND_CLIP_INDEX];
	legacy_s16 count = LEGACY_S16_FROM_BITS(line[DRAW_LINE_PIXEL_COUNT_INDEX]);
	legacy_s16 row_index = LEGACY_S16_FROM_BITS(line[DRAW_LINE_START_Y_INDEX]);
	legacy_u32 fixed_x =
		((legacy_u32)(legacy_u16)line[DRAW_LINE_START_X_INDEX] << LEGACY_WORD_BITS) |
		(legacy_u16)line[DRAW_LINE_START_X_FRACTION_INDEX];
	fixed_x += DRAW_LINE_FIXED_ROUNDING;
	legacy_u16 step = (legacy_u16)line[DRAW_LINE_STEP_INDEX];
	while (count-- > 0) {
		x_position = LEGACY_S16_FROM_BITS((legacy_u16)(fixed_x >> LEGACY_WORD_BITS));
		if (right_edges[row_index] < x_position) {
			right_edges[row_index] = x_position;
		} else if (left_edges[row_index] > x_position) {
			left_edges[row_index] = x_position;
		}
		row_index++;
		if (mode == DRAW_LINE_MODE_Y_MAJOR_LEFT) {
			fixed_x -= step;
		} else {
			fixed_x += step;
		}
	}
}

static void prerender_merge_x_major_rows(const legacy_u16 *line, legacy_s16 *left_edges,
										 legacy_s16 *right_edges)
{
	legacy_s16 x_position = LEGACY_S16_FROM_BITS(line[DRAW_LINE_START_X_INDEX]);
	legacy_u16 mode = (legacy_u8)line[DRAW_LINE_MODE_AND_CLIP_INDEX];
	legacy_s16 count = LEGACY_S16_FROM_BITS(line[DRAW_LINE_PIXEL_COUNT_INDEX]);
	legacy_s16 row_index = LEGACY_S16_FROM_BITS(line[DRAW_LINE_START_Y_INDEX]);
	/* Mode 7 walks the span right-to-left and mode 8
	 * left-to-right, which only swaps which edge array
	 * opens a row and which one closes it. */
	legacy_s16 to_left = mode == DRAW_LINE_MODE_X_MAJOR_LEFT ? 1 : 0;
	legacy_s16 x_step = to_left ? -1 : 1;
	legacy_u16 step = (legacy_u16)line[DRAW_LINE_STEP_INDEX];
	legacy_u32 sum =
		(legacy_u32)(legacy_u16)line[DRAW_LINE_START_Y_FRACTION_INDEX] + DRAW_LINE_FIXED_ROUNDING;
	legacy_u16 fraction = (legacy_u16)sum;
	if (sum > PRERENDER_FIXED_CARRY_LIMIT) {
		row_index++;
	}
	prerender_extend_edge(left_edges, right_edges, row_index, x_position, !to_left);
	while (count > 0) {
		sum = (legacy_u32)fraction + step;
		fraction = (legacy_u16)sum;
		legacy_s16 carry = sum > PRERENDER_FIXED_CARRY_LIMIT;
		if (carry) {
			prerender_extend_edge(left_edges, right_edges, row_index, x_position, to_left);
			row_index++;
			x_position = LEGACY_S16_WRAP_ADD(x_position, x_step);
			count--;
			if (count > 0) {
				prerender_extend_edge(left_edges, right_edges, row_index, x_position, !to_left);
			}
		} else {
			x_position = LEGACY_S16_WRAP_ADD(x_position, x_step);
			count--;
			if (count == 0) {
				x_position = LEGACY_S16_WRAP_SUB(x_position, x_step);
				prerender_extend_edge(left_edges, right_edges, row_index, x_position, to_left);
			}
		}
	}
}

static void prerender_merge_fixed_y_edge(const legacy_u16 *line, legacy_s16 *left_edges,
										 legacy_s16 *right_edges)
{
	legacy_s16 x_position = LEGACY_S16_FROM_BITS(line[DRAW_LINE_START_X_INDEX]);
	legacy_u16 mode = (legacy_u8)line[DRAW_LINE_MODE_AND_CLIP_INDEX];
	legacy_s16 count = LEGACY_S16_FROM_BITS(line[DRAW_LINE_PIXEL_COUNT_INDEX]);
	legacy_s16 row_index = LEGACY_S16_FROM_BITS(line[DRAW_LINE_START_Y_INDEX]);
	legacy_u32 fixed_x =
		((legacy_u32)(legacy_u16)line[DRAW_LINE_START_X_INDEX] << LEGACY_WORD_BITS) |
		(legacy_u16)line[DRAW_LINE_START_X_FRACTION_INDEX];
	fixed_x += DRAW_LINE_FIXED_ROUNDING;
	legacy_u16 step = (legacy_u16)line[DRAW_LINE_STEP_INDEX];
	legacy_s16 target_edge = 0;

	while (count > 0) {
		x_position = mode >= DRAW_LINE_MODE_Y_MAJOR_LEFT
						 ? LEGACY_S16_FROM_BITS((legacy_u16)(fixed_x >> LEGACY_WORD_BITS))
						 : LEGACY_S16_FROM_BITS(line[DRAW_LINE_START_X_INDEX]);
		if (mode == DRAW_LINE_MODE_DIAGONAL_LEFT) {
			x_position = LEGACY_S16_WRAP_SUB(
				x_position, LEGACY_S16_WRAP_SUB(
								LEGACY_S16_FROM_BITS(line[DRAW_LINE_PIXEL_COUNT_INDEX]), count));
		} else if (mode == DRAW_LINE_MODE_DIAGONAL_RIGHT) {
			x_position = LEGACY_S16_WRAP_ADD(
				x_position, LEGACY_S16_WRAP_SUB(
								LEGACY_S16_FROM_BITS(line[DRAW_LINE_PIXEL_COUNT_INDEX]), count));
		}

		if (target_edge == 0) {
			if (left_edges[row_index] > x_position) {
				target_edge = 1;
			} else if (right_edges[row_index] < x_position) {
				target_edge = 2;
			}
		}
		if (target_edge == 1) {
			left_edges[row_index] = x_position;
		} else if (target_edge == 2) {
			right_edges[row_index] = x_position;
		}
		row_index++;
		count--;
		if (mode == DRAW_LINE_MODE_Y_MAJOR_LEFT) {
			fixed_x -= step;
		} else if (mode == DRAW_LINE_MODE_Y_MAJOR_RIGHT) {
			fixed_x += step;
		}
	}
}

static void prerender_finish_fixed_left(legacy_s16 *right_edges, legacy_s16 x_position,
										legacy_s16 count, legacy_s16 row_index, legacy_u16 fraction,
										legacy_u16 step)
{
	while (count > 0) {
		x_position = LEGACY_S16_WRAP_SUB(x_position, 1);
		legacy_u32 sum = (legacy_u32)fraction + step;
		fraction = (legacy_u16)sum;
		count--;
		if (count > 0 && sum > PRERENDER_FIXED_CARRY_LIMIT) {
			right_edges[row_index++] = x_position;
		}
	}
}

static void prerender_merge_fixed_left_edge(const legacy_u16 *line, legacy_s16 *left_edges,
											legacy_s16 *right_edges)
{
	legacy_s16 x_position = LEGACY_S16_FROM_BITS(line[DRAW_LINE_START_X_INDEX]);
	legacy_s16 count = LEGACY_S16_FROM_BITS(line[DRAW_LINE_PIXEL_COUNT_INDEX]);
	legacy_s16 row_index = LEGACY_S16_FROM_BITS(line[DRAW_LINE_START_Y_INDEX]);
	legacy_u16 step = (legacy_u16)line[DRAW_LINE_STEP_INDEX];
	legacy_u32 sum =
		(legacy_u32)(legacy_u16)line[DRAW_LINE_START_Y_FRACTION_INDEX] + DRAW_LINE_FIXED_ROUNDING;
	legacy_u16 fraction = (legacy_u16)sum;
	if (sum > PRERENDER_FIXED_CARRY_LIMIT) {
		row_index++;
	}

	while (count > 0) {
		if (right_edges[row_index] < x_position) {
			right_edges[row_index++] = x_position;
			prerender_finish_fixed_left(right_edges, x_position, count, row_index, fraction, step);
			break;
		}

		sum = (legacy_u32)fraction + step;
		fraction = (legacy_u16)sum;
		legacy_s16 carry = sum > PRERENDER_FIXED_CARRY_LIMIT;
		if (carry && left_edges[row_index] > x_position) {
			left_edges[row_index++] = x_position;
			x_position = LEGACY_S16_WRAP_SUB(x_position, 1);
			count--;
			while (count > 0) {
				sum = (legacy_u32)fraction + step;
				fraction = (legacy_u16)sum;
				carry = sum > PRERENDER_FIXED_CARRY_LIMIT;
				if (carry) {
					left_edges[row_index++] = x_position;
				}
				x_position = LEGACY_S16_WRAP_SUB(x_position, 1);
				count--;
				if (count == 0 && !carry) {
					x_position = LEGACY_S16_WRAP_ADD(x_position, 1);
					left_edges[row_index] = x_position;
				}
			}
			break;
		}
		if (carry) {
			row_index++;
		}
		x_position = LEGACY_S16_WRAP_SUB(x_position, 1);
		count--;
	}
}

static void prerender_finish_fixed_right(legacy_s16 *left_edges, legacy_s16 x_position,
										 legacy_s16 count, legacy_s16 row_index,
										 legacy_u16 fraction, legacy_u16 step)
{
	while (count > 0) {
		legacy_u32 sum = (legacy_u32)fraction + step;
		fraction = (legacy_u16)sum;
		if (sum > PRERENDER_FIXED_CARRY_LIMIT) {
			left_edges[row_index++] = x_position;
		}
		x_position = LEGACY_S16_WRAP_ADD(x_position, 1);
		count--;
	}
}

static void prerender_merge_fixed_right_edge(const legacy_u16 *line, legacy_s16 *left_edges,
											 legacy_s16 *right_edges)
{
	legacy_s16 x_position = LEGACY_S16_FROM_BITS(line[DRAW_LINE_START_X_INDEX]);
	legacy_s16 count = LEGACY_S16_FROM_BITS(line[DRAW_LINE_PIXEL_COUNT_INDEX]);
	legacy_s16 row_index = LEGACY_S16_FROM_BITS(line[DRAW_LINE_START_Y_INDEX]);
	legacy_u16 step = (legacy_u16)line[DRAW_LINE_STEP_INDEX];
	legacy_u32 sum =
		(legacy_u32)(legacy_u16)line[DRAW_LINE_START_Y_FRACTION_INDEX] + DRAW_LINE_FIXED_ROUNDING;
	legacy_u16 fraction = (legacy_u16)sum;
	if (sum > PRERENDER_FIXED_CARRY_LIMIT) {
		row_index++;
	}

	while (count > 0) {
		if (left_edges[row_index] > x_position) {
			left_edges[row_index++] = x_position;
			x_position = LEGACY_S16_WRAP_ADD(x_position, 1);
			count--;
			prerender_finish_fixed_right(left_edges, x_position, count, row_index, fraction, step);
			break;
		}

		sum = (legacy_u32)fraction + step;
		fraction = (legacy_u16)sum;
		legacy_s16 carry = sum > PRERENDER_FIXED_CARRY_LIMIT;
		if (carry && right_edges[row_index] < x_position) {
			right_edges[row_index] = x_position;
			while (count > 0) {
				x_position = LEGACY_S16_WRAP_ADD(x_position, 1);
				sum = (legacy_u32)fraction + step;
				fraction = (legacy_u16)sum;
				carry = sum > PRERENDER_FIXED_CARRY_LIMIT;
				if (carry) {
					row_index++;
				}
				count--;
				if (count > 0 && carry) {
					right_edges[row_index] = x_position;
				} else if (count == 0) {
					if (!carry) {
						row_index++;
					}
					x_position = LEGACY_S16_WRAP_SUB(x_position, 1);
					right_edges[row_index] = x_position;
				}
			}
			break;
		}
		if (carry) {
			row_index++;
		}
		x_position = LEGACY_S16_WRAP_ADD(x_position, 1);
		count--;
	}
}

static void prerender_merge_clip_padding(const legacy_u16 *line, legacy_s16 *left_edges,
										 legacy_s16 *right_edges)
{
	legacy_u32 sum =
		(legacy_u32)(legacy_u16)line[DRAW_LINE_START_Y_FRACTION_INDEX] + DRAW_LINE_FIXED_ROUNDING;
	legacy_s16 carry = sum > PRERENDER_FIXED_CARRY_LIMIT;

	legacy_s16 count = LEGACY_S16_FROM_BITS(line[DRAW_LINE_START_LEFT_CLIP_COUNT_INDEX]);
	legacy_s16 row_index;
	if (count > 0) {
		row_index = LEGACY_S16_WRAP_SUB(
			LEGACY_S16_WRAP_ADD(LEGACY_S16_FROM_BITS(line[DRAW_LINE_START_Y_INDEX]), carry), count);
		while (count-- > 0) {
			left_edges[row_index++] = LEGACY_S16_FROM_BITS(drawing_sprite.sprite_raster_left);
		}
	}

	count = LEGACY_S16_FROM_BITS(line[DRAW_LINE_START_RIGHT_CLIP_COUNT_INDEX]);
	if (count > 0) {
		row_index = LEGACY_S16_WRAP_SUB(
			LEGACY_S16_WRAP_ADD(LEGACY_S16_FROM_BITS(line[DRAW_LINE_START_Y_INDEX]), carry), count);
		while (count-- > 0) {
			right_edges[row_index++] =
				LEGACY_S16_WRAP_SUB(LEGACY_S16_FROM_BITS(drawing_sprite.sprite_raster_right), 1);
		}
	}

	count = LEGACY_S16_FROM_BITS(line[DRAW_LINE_END_LEFT_CLIP_COUNT_INDEX]);
	if (count > 0) {
		row_index = LEGACY_S16_WRAP_ADD(LEGACY_S16_FROM_BITS(line[DRAW_LINE_END_Y_INDEX]), 1);
		while (count-- > 0) {
			left_edges[row_index++] = LEGACY_S16_FROM_BITS(drawing_sprite.sprite_raster_left);
		}
	}

	count = LEGACY_S16_FROM_BITS(line[DRAW_LINE_END_RIGHT_CLIP_COUNT_INDEX]);
	if (count > 0) {
		row_index = LEGACY_S16_WRAP_ADD(LEGACY_S16_FROM_BITS(line[DRAW_LINE_END_Y_INDEX]), 1);
		while (count-- > 0) {
			right_edges[row_index++] =
				LEGACY_S16_WRAP_SUB(LEGACY_S16_FROM_BITS(drawing_sprite.sprite_raster_right), 1);
		}
	}
}

void polygon_merge_second_edge(const legacy_u16 *line_setup, legacy_u16 choose_edge_per_row,
							   legacy_u16 needs_clipping, legacy_s16 *edge_tables)
{
	legacy_s16 *left_edges = edge_tables;
	legacy_s16 *right_edges = left_edges + PRERENDER_EDGE_ROW_CAPACITY;
	legacy_s16 count = LEGACY_S16_FROM_BITS(line_setup[DRAW_LINE_PIXEL_COUNT_INDEX]);
	legacy_u16 mode = (legacy_u8)line_setup[DRAW_LINE_MODE_AND_CLIP_INDEX];

	if (count > 0 && mode >= DRAW_LINE_MODE_VERTICAL && mode <= DRAW_LINE_MODE_X_MAJOR_RIGHT) {
		if (choose_edge_per_row != 0U) {
			if (mode <= DRAW_LINE_MODE_DIAGONAL_RIGHT) {
				prerender_merge_linear_rows(line_setup, left_edges, right_edges);
			} else if (mode <= DRAW_LINE_MODE_Y_MAJOR_RIGHT) {
				prerender_merge_fractional_rows(line_setup, left_edges, right_edges);
			} else {
				prerender_merge_x_major_rows(line_setup, left_edges, right_edges);
			}
		} else if (mode <= DRAW_LINE_MODE_Y_MAJOR_RIGHT) {
			prerender_merge_fixed_y_edge(line_setup, left_edges, right_edges);
		} else if (mode == DRAW_LINE_MODE_X_MAJOR_LEFT) {
			prerender_merge_fixed_left_edge(line_setup, left_edges, right_edges);
		} else {
			prerender_merge_fixed_right_edge(line_setup, left_edges, right_edges);
		}
	}
	if (needs_clipping != 0U) {
		prerender_merge_clip_padding(line_setup, left_edges, right_edges);
	}
}
