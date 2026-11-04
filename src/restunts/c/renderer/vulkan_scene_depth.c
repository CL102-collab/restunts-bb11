#include "vulkan_scene_internal.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SCENE_PLANE_CAPACITY 4096U
#define SCENE_PLANE_HASH_CAPACITY 1024U
#define SCENE_PLANE_ALIAS_CAPACITY 16384U
#define SCENE_PLANE_TRIANGLE_VERTICES 3U
#define SCENE_PLANE_RELATIVE_TOLERANCE (4.0 * FLT_EPSILON)
#define SCENE_PLANE_ZERO_TOLERANCE (32.0f * FLT_EPSILON)
#define SCENE_PLANE_KEY_SHIFT 16U
#define SCENE_PLANE_CONSTANT_KEY_SHIFT 4U
#define SCENE_PLANE_HASH_MULTIPLIER 2654435761U
#define SCENE_PLANE_HASH_SHIFT 22U
#define SCENE_PLANE_NO_INDEX ((size_t) - 1)

struct SCENE_PLANE_SORT {
	size_t first;
	legacy_f64 area;
};

struct SCENE_PLANE_ALIAS {
	legacy_u64 key;
	size_t canonical;
	legacy_s32 next;
};

static legacy_f32 scene_planes[SCENE_PLANE_CAPACITY][3];
static size_t scene_plane_count, scene_plane_alias_count;
static legacy_s32 scene_plane_heads[SCENE_PLANE_HASH_CAPACITY];
static struct SCENE_PLANE_ALIAS scene_plane_aliases[SCENE_PLANE_ALIAS_CAPACITY];
static struct SCENE_PLANE_SORT *scene_plane_sort;
static size_t scene_plane_sort_capacity;

void vulkan_scene_depth_reset(void)
{
	scene_plane_count = scene_plane_alias_count = 0;
	memset(scene_plane_heads, 0xff, sizeof(scene_plane_heads));
}

void vulkan_scene_depth_shutdown(void)
{
	free(scene_plane_sort);
	scene_plane_sort = NULL;
	scene_plane_sort_capacity = 0;
	vulkan_scene_depth_reset();
}

static legacy_int scene_plane_compare(const void *a, const void *b)
{
	const struct SCENE_PLANE_SORT *first = a, *second = b;
	if (first->area != second->area) {
		return first->area < second->area ? 1 : -1;
	}
	return first->first > second->first ? 1 : first->first < second->first ? -1 : 0;
}

static legacy_u32 scene_float_bits(legacy_f32 value)
{
	legacy_u32 result;
	memcpy(&result, &value, sizeof(result));
	return result;
}

/* Keys only accelerate lookup. Vertex residuals decide whether planes match,
 * and a full search handles quantization boundaries and skinny triangles. */
static legacy_u64 scene_plane_key(const legacy_f32 *plane)
{
	legacy_f32 a = plane[0], b = plane[1];
	legacy_f32 magnitude = fabsf(a) > fabsf(b) ? fabsf(a) : fabsf(b);
	legacy_f32 zero = SCENE_PLANE_ZERO_TOLERANCE * magnitude;
	if (fabsf(a) <= zero) {
		a = 0;
	}
	if (fabsf(b) <= zero) {
		b = 0;
	}
	if (a == 0 && b == 0) {
		return ((legacy_u64)1U << 63U) |
			   (scene_float_bits(plane[2]) >> SCENE_PLANE_CONSTANT_KEY_SHIFT);
	}
	return ((legacy_u64)(scene_float_bits(a) >> SCENE_PLANE_KEY_SHIFT) << SCENE_PLANE_KEY_SHIFT) |
		   (scene_float_bits(b) >> SCENE_PLANE_KEY_SHIFT);
}

static legacy_s32 scene_plane_matches(const struct VULKAN_SCENE_VERTEX *triangle,
									  const legacy_f32 *canonical)
{
	for (size_t index = 0; index < SCENE_PLANE_TRIANGLE_VERTICES; index++) {
		const struct VULKAN_SCENE_VERTEX *vertex = triangle + index;
		legacy_f64 ax = (legacy_f64)canonical[0] * vertex->x;
		legacy_f64 by = (legacy_f64)canonical[1] * vertex->y;
		legacy_f64 predicted = ax + by + canonical[2];
		/* Near the horizon, large affine terms cancel. Include those terms in
		 * the float input uncertainty instead of scaling only by tiny depth. */
		legacy_f64 tolerance =
			SCENE_PLANE_RELATIVE_TOLERANCE *
			(fabs(ax) + fabs(by) + fabs(canonical[2]) + fabs(vertex->inverse_depth));
		if (fabs(predicted - vertex->inverse_depth) > tolerance) {
			return 0;
		}
	}
	return 1;
}

static void scene_canonical_plane(const struct VULKAN_SCENE_VERTEX *triangle, legacy_f32 *plane)
{
	legacy_u64 key = scene_plane_key(plane);
	legacy_u32 mixed = (legacy_u32)(key ^ (key >> 32U));
	legacy_u32 bucket = (mixed * SCENE_PLANE_HASH_MULTIPLIER) >> SCENE_PLANE_HASH_SHIFT;
	size_t found = SCENE_PLANE_NO_INDEX;
	for (legacy_s32 node = scene_plane_heads[bucket]; node >= 0;
		 node = scene_plane_aliases[node].next) {
		const struct SCENE_PLANE_ALIAS *alias = scene_plane_aliases + node;
		if (alias->key == key && scene_plane_matches(triangle, scene_planes[alias->canonical])) {
			found = alias->canonical;
			break;
		}
	}
	if (found == SCENE_PLANE_NO_INDEX) {
		for (size_t candidate = 0; candidate < scene_plane_count; candidate++) {
			if (scene_plane_matches(triangle, scene_planes[candidate])) {
				found = candidate;
				break;
			}
		}
		if (found == SCENE_PLANE_NO_INDEX && scene_plane_count < SCENE_PLANE_CAPACITY) {
			found = scene_plane_count++;
			memcpy(scene_planes[found], plane, 3U * sizeof(*plane));
		}
		if (found != SCENE_PLANE_NO_INDEX && scene_plane_alias_count < SCENE_PLANE_ALIAS_CAPACITY) {
			size_t node = scene_plane_alias_count++;
			scene_plane_aliases[node] =
				(struct SCENE_PLANE_ALIAS){key, found, scene_plane_heads[bucket]};
			scene_plane_heads[bucket] = (legacy_s32)node;
		}
	}
	if (found != SCENE_PLANE_NO_INDEX) {
		memcpy(plane, scene_planes[found], 3U * sizeof(*plane));
	}
}

static legacy_s32 scene_plane_finite(const struct VULKAN_SCENE_VERTEX *triangle,
									 const legacy_f32 *plane)
{
	if (!isfinite(plane[0]) || !isfinite(plane[1]) || !isfinite(plane[2])) {
		return 0;
	}
	legacy_f64 width = VULKAN_SCENE_TEXTURE_WIDTH, height = VULKAN_SCENE_TEXTURE_HEIGHT;
	for (size_t index = 0; index < SCENE_PLANE_TRIANGLE_VERTICES; index++) {
		if (fabs(triangle[index].x) > width) {
			width = fabs(triangle[index].x);
		}
		if (fabs(triangle[index].y) > height) {
			height = fabs(triangle[index].y);
		}
	}
	/* Finite coefficients alone do not bound float affine products. Leave
	 * headroom for decal scaling, margin arithmetic and shader rounding. */
	legacy_f64 magnitude = fabs(plane[0]) * width + fabs(plane[1]) * height + fabs(plane[2]);
	return magnitude <= (legacy_f64)FLT_MAX / 2;
}

static legacy_s32 scene_depth_plane(const struct VULKAN_SCENE_VERTEX *triangle, legacy_f32 *plane)
{
	legacy_f64 ax = triangle[0].x, ay = triangle[0].y, az = triangle[0].inverse_depth;
	legacy_f64 bx = triangle[1].x - ax, by = triangle[1].y - ay;
	legacy_f64 cx = triangle[2].x - ax, cy = triangle[2].y - ay;
	legacy_f64 bz = triangle[1].inverse_depth - az, cz = triangle[2].inverse_depth - az;
	legacy_f64 determinant = bx * cy - cx * by;
	legacy_f64 a = determinant != 0 ? (bz * cy - cz * by) / determinant : 0;
	legacy_f64 b = determinant != 0 ? (bx * cz - cx * bz) / determinant : 0;
	legacy_f64 origin = az - a * ax - b * ay;
	if (!isfinite(a) || !isfinite(b) || !isfinite(origin) || fabs(a) > FLT_MAX ||
		fabs(b) > FLT_MAX || fabs(origin) > FLT_MAX) {
		return 0;
	}
	plane[0] = (legacy_f32)a;
	plane[1] = (legacy_f32)b;
	plane[2] = (legacy_f32)origin;
	if (!scene_plane_finite(triangle, plane)) {
		return 0;
	}
	scene_canonical_plane(triangle, plane);
	if (!scene_plane_finite(triangle, plane)) {
		return 0;
	}
	plane[3] = -FLT_MAX;
	for (size_t index = 0; index < SCENE_PLANE_TRIANGLE_VERTICES; index++) {
		legacy_f32 depth = (legacy_f32)((legacy_f64)plane[0] * triangle[index].x +
										(legacy_f64)plane[1] * triangle[index].y + plane[2]);
		if (depth > plane[3]) {
			plane[3] = depth;
		}
	}
	return 1;
}

legacy_s32 vulkan_scene_write_vertices(struct VULKAN_SCENE_GPU_VERTEX *output,
									   const struct VULKAN_SCENE_VERTEX *vertices, size_t count,
									   legacy_s32 classic, legacy_char *error, size_t capacity)
{
	/* The mapped Vulkan allocation can be uncached on discrete GPUs. Read only
	 * normal RAM during plane construction, then write complete GPU records. */
	if (count > (size_t)-1 / sizeof(*output) || count % SCENE_PLANE_TRIANGLE_VERTICES != 0 ||
		(count != 0 && (output == NULL || vertices == NULL))) {
		return vulkan_error(error, capacity, "Invalid Vulkan depth vertex storage",
							VK_ERROR_INITIALIZATION_FAILED);
	}
	if (count == 0) {
		return 1;
	}
	for (size_t index = 0; index < count; index++) {
		if (!isfinite(vertices[index].x) || !isfinite(vertices[index].y) ||
			!isfinite(vertices[index].inverse_depth)) {
			return vulkan_error(error, capacity, "Nonfinite Vulkan depth vertex",
								VK_ERROR_INITIALIZATION_FAILED);
		}
	}
	if (classic) {
		for (size_t index = 0; index < count; index++) {
			output[index].base = vertices[index];
			output[index].plane[0] = output[index].plane[1] = 0;
			output[index].plane[2] = output[index].plane[3] = 1;
		}
		return 1;
	}
	size_t triangles = count / SCENE_PLANE_TRIANGLE_VERTICES;
	if (triangles > (size_t)-1 / sizeof(*scene_plane_sort)) {
		return vulkan_error(error, capacity,
							"Vulkan depth plane ordering exceeds addressable memory",
							VK_ERROR_OUT_OF_HOST_MEMORY);
	}
	if (triangles > scene_plane_sort_capacity) {
		void *next = realloc(scene_plane_sort, triangles * sizeof(*scene_plane_sort));
		if (next == NULL) {
			return vulkan_error(error, capacity, "Allocate Vulkan depth plane ordering",
								VK_ERROR_OUT_OF_HOST_MEMORY);
		}
		scene_plane_sort = next;
		scene_plane_sort_capacity = triangles;
	}
	for (size_t index = 0; index < triangles; index++) {
		size_t first = index * SCENE_PLANE_TRIANGLE_VERTICES;
		const struct VULKAN_SCENE_VERTEX *triangle = vertices + first;
		legacy_f64 bx = (legacy_f64)triangle[1].x - triangle[0].x;
		legacy_f64 by = (legacy_f64)triangle[1].y - triangle[0].y;
		legacy_f64 cx = (legacy_f64)triangle[2].x - triangle[0].x;
		legacy_f64 cy = (legacy_f64)triangle[2].y - triangle[0].y;
		scene_plane_sort[index] = (struct SCENE_PLANE_SORT){first, fabs(bx * cy - cx * by)};
	}
	/* Large triangles give stable representatives before subpixel triangles
	 * whose float coordinates cannot recover the original plane accurately. */
	qsort(scene_plane_sort, triangles, sizeof(*scene_plane_sort), scene_plane_compare);
	for (size_t sorted = 0; sorted < triangles; sorted++) {
		size_t first = scene_plane_sort[sorted].first;
		legacy_f32 plane[4];
		if (!scene_depth_plane(vertices + first, plane)) {
			return vulkan_error(error, capacity, "Nonfinite Vulkan depth plane",
								VK_ERROR_INITIALIZATION_FAILED);
		}
		for (size_t index = 0; index < SCENE_PLANE_TRIANGLE_VERTICES; index++) {
			output[first + index].base = vertices[first + index];
			memcpy(output[first + index].plane, plane, sizeof(plane));
		}
	}
	return 1;
}
