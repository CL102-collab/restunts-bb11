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
#include "../shape3d/shape3d.h"
#include "../shape2d/shape2d_internal.h"
#include "../shape3d/shape3d_internal.h"
#include "../shape2d/shape2d.h"
#include "../math_internal.h"
#include "../car/car_model.h"
#include "../projection.h"
#include "../residue.h"
#if defined(RESTUNTS_SDL3)
#undef memcpy
#include "../fatal.h"
#include "../hires/hires.h"
#include "../hires/shape3d_hires.h"
#endif

#include "../shape3d/shape3d_pipeline_internal.h"
#include "shape3d_queue.h"

#ifdef RESTUNTS_SDL3
static legacy_u8 polyinfo_supersight_enabled;
static polyinfo_index native_primitive_capacity = POLYINFO_SUPERSIGHT_PRIMITIVE_CAPACITY;
static polyinfo_offset native_data_capacity;
static polyinfo_link *native_polygon_links;
static polyinfo_offset *native_polygon_offsets;
static legacy_u8 *native_polyinfo;
struct HYPERVISION_SCENE_PRIMITIVE *hypervision_scene;
legacy_u32 hypervision_scene_count;
static legacy_u32 hypervision_scene_capacity;
#endif

#ifdef RESTUNTS_SDL3
static void *polyinfo_allocate(size_t count, size_t size)
{
	if (count > (size_t)-1 / size) {
		fatal_error("SuperSight scene exceeds addressable memory");
		return NULL;
	}
	void *result = malloc(count * size);
	if (result == NULL) {
		fatal_error("Cannot allocate SuperSight scene queue");
	}
	return result;
}

void polyinfo_reserve(void)
{
	if (!polyinfo_supersight_enabled) {
		return;
	}
	if (polyinfonumpolys >= polyinfo_primitive_capacity) {
		polyinfo_index previous_capacity = polyinfo_primitive_capacity;
		/* Links use a signed index so that -1 remains the end marker. */
		if (previous_capacity > LEGACY_S32_MAX / 2U) {
			fatal_error("SuperSight scene has too many primitives");
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
		fatal_error("SuperSight scene data exceeds addressable memory");
		return;
	}
	polyinfo_offset required = polyinfoptrnext + POLYINFO_MAX_RECORD_SIZE;
	if (required > native_data_capacity) {
		polyinfo_offset capacity = native_data_capacity;
		if (capacity == 0) {
			capacity = POLYINFO_SUPERSIGHT_DATA_SIZE;
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

void hypervision_scene_reserve(void)
{
	if (hypervision_scene_count < hypervision_scene_capacity) {
		return;
	}
	if (hypervision_scene_capacity > LEGACY_U32_MAX / 2U) {
		fatal_error("HyperVision scene has too many primitives");
		return;
	}
	size_t capacity = hypervision_scene_capacity != 0 ? (size_t)hypervision_scene_capacity * 2U
													  : POLYINFO_SUPERSIGHT_PRIMITIVE_CAPACITY;
	if (capacity > LEGACY_U32_MAX || capacity > (size_t)-1 / sizeof(*hypervision_scene)) {
		fatal_error("HyperVision scene exceeds addressable memory");
		return;
	}
	struct HYPERVISION_SCENE_PRIMITIVE *scene =
		realloc(hypervision_scene, capacity * sizeof(*hypervision_scene));
	if (scene == NULL) {
		fatal_error("Cannot allocate HyperVision scene metadata");
		return;
	}
	hypervision_scene = scene;
	hypervision_scene_capacity = (legacy_u32)capacity;
}

legacy_u16 shape3d_hires_sort_policy(legacy_u16 sort_by_depth)
{
	/* HyperVision resolves visibility in its depth buffer. */
	return hires_enabled() ? 0 : sort_by_depth;
}
legacy_u8 shape3d_hires_queue_is_unlimited(void)
{
	return polyinfo_supersight_enabled;
}
void shape3d_hires_scene_reset(void)
{
	hypervision_scene_count = 0;
	shape3d_hires_reset();
}
#endif

legacy_u16 shape3d_hires_polyinfo_size(void)
{
	return POLYINFO_SUPERSIGHT_DATA_SIZE;
}
void shape3d_hires_set_queue_mode(legacy_u8 enabled)
{
	polyinfo_index capacity =
		enabled != 0U ? POLYINFO_SUPERSIGHT_PRIMITIVE_CAPACITY : POLYINFO_LEGACY_PRIMITIVE_CAPACITY;
#if defined(RESTUNTS_SDL3)
	polyinfo_supersight_enabled = enabled != 0U;
	if (polyinfo_supersight_enabled) {
		capacity = native_primitive_capacity;
	}
#endif
	if (polyinfo_primitive_capacity != capacity) {
		polyinfo_primitive_capacity = capacity;
		polyinfo_last_valid_offset = enabled != 0U ? POLYINFO_SUPERSIGHT_LAST_VALID_OFFSET
												   : POLYINFO_LEGACY_LAST_VALID_OFFSET;
		polyinfo_reset();
	}
}
