#include <stddef.h>
#include <limits.h>
#if defined(RESTUNTS_SDL3)
#include <stdlib.h>
#include <string.h>
#endif
#include "../externs.h"
#include "../fileio.h"
#include "../legacy.h"
#include "../memmgr.h"
#include "shape3d.h"
#include "../shape2d/shape2d_internal.h"
#include "shape3d_internal.h"
#include "../shape2d/shape2d.h"
#include "../math/math_internal.h"
#include "../car/car_model.h"
#include "../math/projection.h"
#include "../residue.h"
#if defined(RESTUNTS_SDL3)
#undef memcpy
#include "../fatal.h"
#include "../hires/hires.h"
#include "../hires/shape3d_hires.h"
#endif

#include "shape3d_pipeline_internal.h"

polyinfo_index queued_ghost_primitives;
polyinfo_index polyinfo_primitive_capacity = POLYINFO_LEGACY_PRIMITIVE_CAPACITY;
polyinfo_offset polyinfo_last_valid_offset = POLYINFO_LEGACY_LAST_VALID_OFFSET;

#ifdef RESTUNTS_SDL3
/* Native classic scenes also expand for ghost ribbons. Keep their storage and
 * growth policy with the shared polygon queue, independent of the rasterizer. */
static legacy_u8 polyinfo_expanded_enabled;
static polyinfo_index native_primitive_capacity = POLYINFO_STORAGE_PRIMITIVE_CAPACITY;
static polyinfo_offset native_data_capacity;
static polyinfo_link *native_polygon_links;
static polyinfo_offset *native_polygon_offsets;
static legacy_u8 *native_polyinfo;

static void *polyinfo_allocate(size_t count, size_t size)
{
	if (count > (size_t)-1 / size) {
		fatal_error("Polygon scene exceeds addressable memory");
		return NULL;
	}
	void *result = malloc(count * size);
	if (result == NULL) {
		fatal_error("Cannot allocate Polygon scene queue");
	}
	return result;
}

void polyinfo_reserve(void)
{
	if (!polyinfo_expanded_enabled) {
		return;
	}
	if (polyinfonumpolys >= polyinfo_primitive_capacity) {
		polyinfo_index previous_capacity = polyinfo_primitive_capacity;
		/* Links use a signed index so that -1 remains the end marker. */
		if (previous_capacity > LEGACY_S32_MAX / 2U) {
			fatal_error("Polygon scene has too many primitives");
			return;
		}
		polyinfo_index capacity = previous_capacity * 2U;
		polyinfo_link *links = polyinfo_allocate((size_t)capacity + 1U, sizeof(*links));
		polyinfo_offset *offsets = polyinfo_allocate(capacity, sizeof(*offsets));
		memcpy(links, polygon_next_index, ((size_t)previous_capacity + 1U) * sizeof(*links));
		memcpy(offsets, polygon_record_offsets, (size_t)previous_capacity * sizeof(*offsets));
		/* The head sentinel lives after all primitive slots. Move it before
		 * its former slot becomes the next primitive, including mid-shape growth. */
		links[capacity] = links[previous_capacity];
		if (shape_polygon_predecessor == previous_capacity) {
			shape_polygon_predecessor = capacity;
		}
		if (polygon_insertion_cursor == previous_capacity) {
			polygon_insertion_cursor = capacity;
		}
		if (polygon_list_tail == previous_capacity) {
			polygon_list_tail = capacity;
		}
		free(native_polygon_links);
		free(native_polygon_offsets);
		polygon_next_index = native_polygon_links = links;
		polygon_record_offsets = native_polygon_offsets = offsets;
		polyinfo_primitive_capacity = native_primitive_capacity = capacity;
	}
	if (polyinfoptrnext > LEGACY_U32_MAX - POLYINFO_MAX_RECORD_SIZE) {
		fatal_error("Polygon scene data exceeds addressable memory");
		return;
	}
	polyinfo_offset required = polyinfoptrnext + POLYINFO_MAX_RECORD_SIZE;
	if (required > native_data_capacity) {
		polyinfo_offset capacity = native_data_capacity;
		if (capacity == 0) {
			capacity = POLYINFO_STORAGE_DATA_SIZE;
		}
		while (capacity < required) {
			capacity = capacity <= LEGACY_U32_MAX / 2U ? capacity * 2U : required;
		}
		legacy_u8 *data = polyinfo_allocate(capacity, sizeof(*data));
		if (polyinfoptrnext != 0) {
			memcpy(data, polyinfoptr, polyinfoptrnext);
		}
		free(native_polyinfo);
		polyinfoptr = native_polyinfo = data;
		native_data_capacity = capacity;
	} else if (polyinfoptr != native_polyinfo) {
		/* init_polyinfo and isolated render targets can supply a fresh legacy
		 * buffer. Reuse native storage without taking ownership of that buffer. */
		if (polyinfoptrnext != 0) {
			memcpy(native_polyinfo, polyinfoptr, polyinfoptrnext);
		}
		polyinfoptr = native_polyinfo;
	}
	polyinfo_last_valid_offset = native_data_capacity - POLYINFO_MAX_RECORD_SIZE;
}

#endif

legacy_u16 polyinfo_read_word(const legacy_u8 far *record, legacy_u16 word_index);
void polyinfo_write_word(legacy_u8 far *record, legacy_u16 word_index, legacy_u16 value);
void polyinfo_read_point(const legacy_u8 far *record, legacy_u16 point_index,
						 struct POINT2D *point);
void polyinfo_read_points(const legacy_u8 far *record, struct POINT2D *points,
						  legacy_u16 point_count);
void polyinfo_write_point(legacy_u8 far *record, legacy_u16 point_index,
						  const struct POINT2D *point);
extern legacy_u16 polygon_insert_newest(legacy_u16 depth, legacy_u16 sort_by_depth);
void polyinfo_reset(void);
void init_direction_sector_thresholds(void);
void init_polyinfo(void);

legacy_u16 polyinfo_read_word(const legacy_u8 far *record, legacy_u16 word_index)
{
	return LEGACY_READ_U16_LE(record + LEGACY_U16_WRAP_MUL(word_index, LEGACY_WORD_BYTES));
}

void polyinfo_write_word(legacy_u8 far *record, legacy_u16 word_index, legacy_u16 value)
{
	LEGACY_WRITE_U16_LE(record + LEGACY_U16_WRAP_MUL(word_index, LEGACY_WORD_BYTES), value);
}

void polyinfo_read_point(const legacy_u8 far *record, legacy_u16 point_index, struct POINT2D *point)
{
	legacy_u16 word_index = LEGACY_U16_WRAP_ADD(
		POLYINFO_HEADER_WORD_COUNT, LEGACY_U16_WRAP_MUL(point_index, POLYINFO_POINT_WORD_COUNT));
	point->px = LEGACY_S16_FROM_BITS(polyinfo_read_word(record, word_index));
	point->py =
		LEGACY_S16_FROM_BITS(polyinfo_read_word(record, LEGACY_U16_WRAP_ADD(word_index, 1U)));
}

void polyinfo_read_points(const legacy_u8 far *record, struct POINT2D *points,
						  legacy_u16 point_count)
{
	for (legacy_u16 index = 0; index < point_count; index++) {
		polyinfo_read_point(record, index, &points[index]);
	}
}

void polyinfo_write_point(legacy_u8 far *record, legacy_u16 point_index,
						  const struct POINT2D *point)
{
	legacy_u16 word_index = LEGACY_U16_WRAP_ADD(
		POLYINFO_HEADER_WORD_COUNT, LEGACY_U16_WRAP_MUL(point_index, POLYINFO_POINT_WORD_COUNT));
	polyinfo_write_word(record, word_index, (legacy_u16)point->px);
	polyinfo_write_word(record, LEGACY_U16_WRAP_ADD(word_index, 1U), (legacy_u16)point->py);
}

extern legacy_u16 polygon_insert_newest(legacy_u16 depth, legacy_u16 sort_by_depth)
{
	//return ported_insert_newest_poly_in_poly_linked_list_40ED6_(depth, sort_by_depth);

	polyinfo_link next_polygon;
#if defined(RESTUNTS_SDL3)
	shape3d_hires_adjust_sort(&sort_by_depth);
#endif
	if (sort_by_depth == 0) {
		next_polygon = polygon_next_index[polygon_insertion_cursor];
	} else {
		polygon_insertion_cursor = shape_polygon_predecessor;
		next_polygon = polygon_next_index[shape_polygon_predecessor];
		polyinfo_index remaining_polygons = shape_polygon_count;

		while (next_polygon >= 0) {
			polyinfo_index previous_remaining_count = remaining_polygons;
			remaining_polygons--;
			if (previous_remaining_count == 0) {
				break;
			}
			if (LEGACY_READ_S16_LE(polyinfoptr + polygon_record_offsets[next_polygon]) <
				(legacy_s16)depth) {
				break;
			}
			polygon_insertion_cursor = next_polygon;
			next_polygon = polygon_next_index[next_polygon];
		}
	}

	polygon_next_index[polyinfonumpolys] = next_polygon;
	polygon_next_index[polygon_insertion_cursor] = polyinfonumpolys;
	shape_polygon_count++;
	if (next_polygon < 0) {
		polygon_list_tail = polyinfonumpolys;
	}
	polygon_insertion_cursor = polygon_next_index[polygon_insertion_cursor];
	polyinfonumpolys++;
	polyinfoptrnext += transshapenumvertscopy * sizeof(struct POINT2D) + 6U;
#if defined(RESTUNTS_SDL3)
	if (polyinfo_expanded_enabled) {
		return 0;
	}
#endif
	if (polyinfonumpolys == polyinfo_primitive_capacity) {
		return 1;
	}
	if (polyinfoptrnext <= polyinfo_last_valid_offset) {
		return 0;
	}
	return 1;
}

void polyinfo_reset(void)
{
#if defined(RESTUNTS_SDL3)
	shape3d_hires_scene_reset();
#endif
	queued_ghost_primitives = 0;
	polyinfonumpolys = 0;
	polyinfoptrnext = 0;
	polygon_buffer_full = 0;
	polygon_next_index[polyinfo_primitive_capacity] = POLYINFO_LIST_SENTINEL;
	polygon_list_tail = polyinfo_primitive_capacity;
}

void init_direction_sector_thresholds(void)
{
	direction_sector_sine = sin_fast(PROJECTION_SINCOS_ANGLE);
	direction_sector_cosine = cos_fast(PROJECTION_SINCOS_ANGLE);
	direction_sector_sine_copy = sin_fast(PROJECTION_SINCOS_ANGLE);
	direction_sector_cosine_copy = cos_fast(PROJECTION_SINCOS_ANGLE);
}

void init_polyinfo(void)
{
	polyinfoptr = mmgr_alloc_resbytes("polyinfo", POLYINFO_STORAGE_DATA_SIZE);

	mat_rot_y(&mat_y0, 0);
	mat_rot_y(&mat_y100, ANGLE_QUARTER_TURN);
	mat_rot_y(&mat_y200, ANGLE_HALF_TURN);
	mat_rot_y(&mat_y300, ANGLE_THREE_QUARTER_TURN);
	init_direction_sector_thresholds();
}

void polyinfo_set_expanded(legacy_u8 enabled)
{
	polyinfo_index capacity = POLYINFO_LEGACY_PRIMITIVE_CAPACITY;
	polyinfo_offset last_offset = POLYINFO_LEGACY_LAST_VALID_OFFSET;
#if defined(RESTUNTS_SDL3)
	polyinfo_expanded_enabled = enabled != 0U;
#endif
	if (enabled != 0U) {
		capacity = POLYINFO_STORAGE_PRIMITIVE_CAPACITY;
		last_offset = POLYINFO_STORAGE_DATA_SIZE - POLYINFO_MAX_RECORD_SIZE;
#if defined(RESTUNTS_SDL3)
		capacity = native_primitive_capacity;
#endif
	}
	if (polyinfo_primitive_capacity != capacity) {
		polyinfo_primitive_capacity = capacity;
		polyinfo_last_valid_offset = last_offset;
		polyinfo_reset();
	}
}
