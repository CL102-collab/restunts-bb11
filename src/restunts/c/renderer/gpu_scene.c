#include "gpu_scene.h"
#include "vulkan_scene.h"
#include "gpu_overlay.h"
#include "../fatal.h"
#include "../shape2d/shape2d.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GPU_SCENE_INITIAL_VERTICES 4096U
#define GPU_SCENE_TRIANGLE_VERTICES 3U
#define GPU_SCENE_MAX_TRIANGLES (HIRES_BACKEND_MAX_VERTICES - 2U)
#define GPU_SCENE_BUCKET_COUNT 3U
#define GPU_SCENE_DETAIL_BUCKET 1U
#define GPU_SCENE_PATTERN_BUCKET 2U
#define GPU_SCENE_SHADOW_MODE 3U
#define GPU_SCENE_POINT_VERTICES 4U
#define GPU_SCENE_HALF_PIXEL 0.5
#define GPU_SCENE_CLASSIC_DEPTH 1.0
#define GPU_SCENE_CLASSIC_WIDTH (HIRES_WIDTH / HIRES_SCALE)
#define GPU_SCENE_CLASSIC_HEIGHT (HIRES_HEIGHT / HIRES_SCALE)
#define GPU_SCENE_VIEWPORT_WIDTH_DIVISOR 2
#define GPU_SCENE_VIEWPORT_HEIGHT_DIVISOR 3
#define GPU_SCENE_CLIP_PLANES 4U
#define GPU_SCENE_CLIPPED_VERTICES (GPU_SCENE_TRIANGLE_VERTICES + GPU_SCENE_CLIP_PLANES)
#define GPU_SCENE_CLIP_LEFT 1U
#define GPU_SCENE_CLIP_RIGHT 2U
#define GPU_SCENE_CLIP_TOP 4U
#define GPU_SCENE_CLIP_BOTTOM 8U

struct GPU_SCENE_BUCKET {
	struct VULKAN_SCENE_VERTEX *vertices;
	size_t count, capacity;
};

static struct GPU_SCENE_BUCKET scene_buckets[GPU_SCENE_BUCKET_COUNT];
static struct GPU_SCENE_BUCKET scene_upload;
static legacy_s32 scene_is_active, scene_classic, scene_cleanup_registered;
static legacy_s32 scene_software_preview;
static legacy_f64 scene_clip[GPU_SCENE_CLIP_PLANES];

static void scene_clear(void)
{
	for (legacy_u32 bucket = 0; bucket < GPU_SCENE_BUCKET_COUNT; bucket++) {
		scene_buckets[bucket].count = 0;
	}
}

static legacy_s32 scene_reserve(struct GPU_SCENE_BUCKET *bucket, size_t extra)
{
	if (extra > LEGACY_U32_MAX - bucket->count) {
		fatal_error((const legacy_s8 *)"GPU scene has too many vertices");
		return 0;
	}
	size_t required = bucket->count + extra;
	if (required <= bucket->capacity) {
		return 1;
	}
	size_t next = bucket->capacity != 0 ? bucket->capacity : GPU_SCENE_INITIAL_VERTICES;
	while (next < required) {
		if (next > (size_t)-1 / 2U) {
			fatal_error((const legacy_s8 *)"GPU scene exceeds addressable memory");
			return 0;
		}
		next *= 2U;
	}
	if (next > (size_t)-1 / sizeof(*bucket->vertices)) {
		fatal_error((const legacy_s8 *)"GPU scene exceeds addressable memory");
		return 0;
	}
	void *vertices = realloc(bucket->vertices, next * sizeof(*bucket->vertices));
	if (vertices == NULL) {
		fatal_error((const legacy_s8 *)"Cannot allocate GPU scene vertices");
		return 0;
	}
	bucket->vertices = vertices;
	bucket->capacity = next;
	return 1;
}

legacy_s32 gpu_scene_active(void)
{
	return scene_is_active;
}

legacy_s32 gpu_scene_software_preview(void)
{
	return scene_software_preview;
}

void gpu_scene_begin(legacy_s32 classic)
{
	scene_clear();
	scene_classic = classic;
	scene_is_active = 0;
	scene_software_preview = 0;
	scene_clip[0] = scene_clip[2] = 0;
	scene_clip[1] = GPU_SCENE_CLASSIC_WIDTH;
	scene_clip[3] = GPU_SCENE_CLASSIC_HEIGHT;
#ifdef RESTUNTS_VULKAN_SCENE
	if (!vulkan_scene_active()) {
		return;
	}
	if (!classic) {
		struct HIRES_RASTER_TARGET target;
		if (!hires_raster_prepare(&target)) {
			return;
		}
		if (target.right - target.left < target.width / GPU_SCENE_VIEWPORT_WIDTH_DIVISOR ||
			target.bottom - target.top < target.height / GPU_SCENE_VIEWPORT_HEIGHT_DIVISOR) {
			scene_software_preview = 1;
			return;
		}
		scene_clip[0] = target.left;
		scene_clip[1] = target.right;
		scene_clip[2] = target.top;
		scene_clip[3] = target.bottom;
	}
	if (!scene_cleanup_registered) {
		if (atexit(gpu_scene_shutdown) != 0) {
			fatal_error((const legacy_s8 *)"Cannot register GPU scene cleanup");
			return;
		}
		scene_cleanup_registered = 1;
	}
	scene_is_active = 1;
#else
	(void)scene_cleanup_registered;
#endif
}

/* Cockpit windshields are substantial inset viewports. Keep those on the
 * hardware path while rendering small independent preview sprites on the CPU. */
legacy_s32 gpu_scene_target_eligible(const struct SPRITE *sprite, legacy_s32 width,
									 legacy_s32 height)
{
	return sprite != NULL && width == GPU_SCENE_CLASSIC_WIDTH &&
		   height == GPU_SCENE_CLASSIC_HEIGHT && sprite->sprite_buffer_width == width &&
		   sprite->sprite_raster_left < sprite->sprite_raster_right &&
		   sprite->sprite_top < sprite->sprite_bottom && sprite->sprite_raster_right <= width &&
		   sprite->sprite_bottom <= height &&
		   sprite->sprite_raster_right - sprite->sprite_raster_left >=
			   width / GPU_SCENE_VIEWPORT_WIDTH_DIVISOR &&
		   sprite->sprite_bottom - sprite->sprite_top >= height / GPU_SCENE_VIEWPORT_HEIGHT_DIVISOR;
}

void gpu_scene_classic_begin(const struct SPRITE *sprite, legacy_s32 width, legacy_s32 height)
{
	gpu_scene_begin(1);
	if (height <= 0 || height > GPU_SCENE_CLASSIC_HEIGHT ||
		!gpu_scene_target_eligible(sprite, width, GPU_SCENE_CLASSIC_HEIGHT)) {
		scene_software_preview = scene_is_active;
		scene_is_active = 0;
		return;
	}
	scene_clip[0] = sprite->sprite_raster_left;
	scene_clip[1] = sprite->sprite_raster_right;
	scene_clip[2] = sprite->sprite_top;
	scene_clip[3] = sprite->sprite_bottom;
}

static legacy_f64 scene_cross(const struct HIRES_BACKEND_VERTEX *a,
							  const struct HIRES_BACKEND_VERTEX *b,
							  const struct HIRES_BACKEND_VERTEX *c)
{
	return (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
}

static legacy_s32 scene_inside(const struct HIRES_BACKEND_VERTEX *point,
							   const struct HIRES_BACKEND_VERTEX *a,
							   const struct HIRES_BACKEND_VERTEX *b,
							   const struct HIRES_BACKEND_VERTEX *c, legacy_f64 winding)
{
	return scene_cross(a, b, point) * winding >= 0 && scene_cross(b, c, point) * winding >= 0 &&
		   scene_cross(c, a, point) * winding >= 0;
}

static legacy_u32 scene_triangulate(const struct HIRES_BACKEND_VERTEX *vertices, legacy_u32 count,
									legacy_u32 *triangles)
{
	if (count == GPU_SCENE_TRIANGLE_VERTICES) {
		if (scene_cross(&vertices[0], &vertices[1], &vertices[2]) == 0) {
			return 0;
		}
		triangles[0] = 0;
		triangles[1] = 1;
		triangles[2] = 2;
		return GPU_SCENE_TRIANGLE_VERTICES;
	}
	legacy_f64 area = 0;
	for (legacy_u32 index = 0; index < count; index++) {
		const struct HIRES_BACKEND_VERTEX *a = &vertices[index];
		const struct HIRES_BACKEND_VERTEX *b = &vertices[(index + 1U) % count];
		area += a->x * b->y - b->x * a->y;
	}
	if (area == 0) {
		return 0;
	}
	legacy_f64 winding = area > 0 ? 1 : -1;
	legacy_s32 convex = 1;
	for (legacy_u32 index = 0; index < count; index++) {
		if (scene_cross(&vertices[index], &vertices[(index + 1U) % count],
						&vertices[(index + 2U) % count]) *
				winding <
			0) {
			convex = 0;
			break;
		}
	}
	legacy_u32 result = 0;
	if (convex) {
		for (legacy_u32 index = 1; index + 1U < count; index++) {
			if (scene_cross(&vertices[0], &vertices[index], &vertices[index + 1U]) == 0) {
				continue;
			}
			triangles[result++] = 0;
			triangles[result++] = index;
			triangles[result++] = index + 1U;
		}
		return result;
	}
	legacy_u32 polygon[HIRES_BACKEND_MAX_VERTICES];
	for (legacy_u32 index = 0; index < count; index++) {
		polygon[index] = index;
	}
	legacy_u32 remaining = count;
	while (remaining > 2U) {
		legacy_s32 clipped = 0;
		for (legacy_u32 index = 0; index < remaining; index++) {
			legacy_u32 first = polygon[index == 0 ? remaining - 1U : index - 1U];
			legacy_u32 middle = polygon[index];
			legacy_u32 last = polygon[(index + 1U) % remaining];
			if (scene_cross(&vertices[first], &vertices[middle], &vertices[last]) * winding <= 0) {
				continue;
			}
			legacy_s32 occupied = 0;
			for (legacy_u32 candidate = 0; candidate < remaining; candidate++) {
				legacy_u32 point = polygon[candidate];
				if (point != first && point != middle && point != last &&
					scene_inside(&vertices[point], &vertices[first], &vertices[middle],
								 &vertices[last], winding)) {
					occupied = 1;
					break;
				}
			}
			if (occupied) {
				continue;
			}
			triangles[result++] = first;
			triangles[result++] = middle;
			triangles[result++] = last;
			memmove(polygon + index, polygon + index + 1U,
					(remaining - index - 1U) * sizeof(*polygon));
			remaining--;
			clipped = 1;
			break;
		}
		if (!clipped) {
			return 0;
		}
	}
	return result;
}

/* Geometry is projected in binary64. Clip before narrowing to GPU floats:
 * distant terrain can extend tens of thousands of pixels beyond the viewport,
 * amplifying float interpolation error between otherwise coplanar surfaces. */
static legacy_u32 scene_outcode(const struct HIRES_BACKEND_VERTEX *vertex)
{
	return (vertex->x < scene_clip[0] ? GPU_SCENE_CLIP_LEFT : 0U) |
		   (vertex->x > scene_clip[1] ? GPU_SCENE_CLIP_RIGHT : 0U) |
		   (vertex->y < scene_clip[2] ? GPU_SCENE_CLIP_TOP : 0U) |
		   (vertex->y > scene_clip[3] ? GPU_SCENE_CLIP_BOTTOM : 0U);
}

static void scene_write_vertex(struct VULKAN_SCENE_VERTEX *output,
							   const struct HIRES_BACKEND_VERTEX *source,
							   const struct HIRES_BACKEND_MATERIAL *material)
{
	output->x = (legacy_f32)source->x;
	output->y = (legacy_f32)source->y;
	output->inverse_depth = (legacy_f32)source->inverse_z;
	output->color = material->color;
	output->alternate = material->alternate;
	output->pattern = material->pattern;
	output->mode = material->mode;
	output->flags = material->flags;
}

static legacy_s32 scene_clip_append(struct HIRES_BACKEND_VERTEX *output, legacy_u32 *count,
									const struct HIRES_BACKEND_VERTEX *vertex)
{
	if (*count != 0 && output[*count - 1U].x == vertex->x && output[*count - 1U].y == vertex->y) {
		return 1;
	}
	if (*count == GPU_SCENE_CLIPPED_VERTICES) {
		return 0;
	}
	output[(*count)++] = *vertex;
	return 1;
}

static legacy_u32 scene_clip_plane(const struct HIRES_BACKEND_VERTEX *input, legacy_u32 count,
								   struct HIRES_BACKEND_VERTEX *output, legacy_u32 plane)
{
	legacy_u32 result = 0;
	const struct HIRES_BACKEND_VERTEX *previous = &input[count - 1U];
	legacy_f64 boundary = scene_clip[plane];
	legacy_f64 previous_value = plane < 2U ? previous->x : previous->y;
	legacy_s32 previous_inside =
		(plane & 1U) == 0 ? previous_value >= boundary : previous_value <= boundary;
	for (legacy_u32 index = 0; index < count; index++) {
		const struct HIRES_BACKEND_VERTEX *current = &input[index];
		legacy_f64 value = plane < 2U ? current->x : current->y;
		legacy_s32 inside = (plane & 1U) == 0 ? value >= boundary : value <= boundary;
		if (inside != previous_inside) {
			legacy_f64 fraction = (boundary - previous_value) / (value - previous_value);
			struct HIRES_BACKEND_VERTEX intersection = {
				previous->x + fraction * (current->x - previous->x),
				previous->y + fraction * (current->y - previous->y),
				previous->inverse_z + fraction * (current->inverse_z - previous->inverse_z)};
			if (plane < 2U) {
				intersection.x = boundary;
			} else {
				intersection.y = boundary;
			}
			if (!scene_clip_append(output, &result, &intersection)) {
				return 0;
			}
		}
		if (inside && !scene_clip_append(output, &result, current)) {
			return 0;
		}
		previous = current;
		previous_value = value;
		previous_inside = inside;
	}
	if (result > 1U && output[0].x == output[result - 1U].x &&
		output[0].y == output[result - 1U].y) {
		result--;
	}
	return result;
}

static void scene_clipped_triangle(struct GPU_SCENE_BUCKET *bucket,
								   const struct HIRES_BACKEND_VERTEX *a,
								   const struct HIRES_BACKEND_VERTEX *b,
								   const struct HIRES_BACKEND_VERTEX *c,
								   const struct HIRES_BACKEND_MATERIAL *material)
{
	struct HIRES_BACKEND_VERTEX first[GPU_SCENE_CLIPPED_VERTICES];
	struct HIRES_BACKEND_VERTEX second[GPU_SCENE_CLIPPED_VERTICES];
	first[0] = *a;
	first[1] = *b;
	first[2] = *c;
	struct HIRES_BACKEND_VERTEX *input = first, *output = second;
	legacy_u32 count = GPU_SCENE_TRIANGLE_VERTICES;
	for (legacy_u32 plane = 0; plane < GPU_SCENE_CLIP_PLANES; plane++) {
		count = scene_clip_plane(input, count, output, plane);
		if (count < GPU_SCENE_TRIANGLE_VERTICES) {
			return;
		}
		struct HIRES_BACKEND_VERTEX *swap = input;
		input = output;
		output = swap;
	}
	if (!scene_reserve(bucket, (count - 2U) * GPU_SCENE_TRIANGLE_VERTICES)) {
		return;
	}
	for (legacy_u32 index = 1; index + 1U < count; index++) {
		if (scene_cross(&input[0], &input[index], &input[index + 1U]) == 0) {
			continue;
		}
		scene_write_vertex(&bucket->vertices[bucket->count++], &input[0], material);
		scene_write_vertex(&bucket->vertices[bucket->count++], &input[index], material);
		scene_write_vertex(&bucket->vertices[bucket->count++], &input[index + 1U], material);
	}
}

void gpu_scene_polygon(const struct HIRES_BACKEND_VERTEX *vertices, legacy_u32 count,
					   const struct HIRES_BACKEND_MATERIAL *material)
{
	if (!scene_is_active || count < GPU_SCENE_TRIANGLE_VERTICES ||
		count > HIRES_BACKEND_MAX_VERTICES) {
		return;
	}
	struct HIRES_BACKEND_VERTEX selected[HIRES_BACKEND_MAX_VERTICES];
	legacy_u32 kept = 0;
	for (legacy_u32 index = 0; index < count; index++) {
		const struct HIRES_BACKEND_VERTEX *vertex = &vertices[index];
		if (!(vertex->x >= -FLT_MAX && vertex->x <= FLT_MAX && vertex->y >= -FLT_MAX &&
			  vertex->y <= FLT_MAX && vertex->inverse_z > 0 && vertex->inverse_z <= FLT_MAX)) {
			return;
		}
		if (kept == 0 || vertex->x != selected[kept - 1U].x || vertex->y != selected[kept - 1U].y) {
			selected[kept++] = *vertex;
		}
	}
	if (kept > 1U && selected[0].x == selected[kept - 1U].x &&
		selected[0].y == selected[kept - 1U].y) {
		kept--;
	}
	if (kept < GPU_SCENE_TRIANGLE_VERTICES) {
		return;
	}
	/* Clipping can add vertices exactly on an existing edge. Removing those
	 * points prevents a boundary-only point from blocking every candidate ear. */
	for (legacy_u32 index = 0; kept > GPU_SCENE_TRIANGLE_VERTICES && index < kept;) {
		legacy_u32 previous = index == 0 ? kept - 1U : index - 1U;
		legacy_u32 next = (index + 1U) % kept;
		if (scene_cross(&selected[previous], &selected[index], &selected[next]) == 0) {
			memmove(selected + index, selected + index + 1U,
					(kept - index - 1U) * sizeof(*selected));
			kept--;
			index = 0;
		} else {
			index++;
		}
	}
	legacy_u32 outcodes[HIRES_BACKEND_MAX_VERTICES];
	legacy_u32 outside = 0, common = GPU_SCENE_CLIP_LEFT | GPU_SCENE_CLIP_RIGHT |
									 GPU_SCENE_CLIP_TOP | GPU_SCENE_CLIP_BOTTOM;
	for (legacy_u32 vertex = 0; vertex < kept; vertex++) {
		outcodes[vertex] = scene_outcode(&selected[vertex]);
		outside |= outcodes[vertex];
		common &= outcodes[vertex];
	}
	if (common != 0) {
		return;
	}
	legacy_u32 triangles[GPU_SCENE_MAX_TRIANGLES * GPU_SCENE_TRIANGLE_VERTICES];
	legacy_u32 triangle_vertices = scene_triangulate(selected, kept, triangles);
	legacy_u32 index = 0;
	if (!scene_classic) {
		if (material->mode == HIRES_PAINT_PATTERN) {
			index = GPU_SCENE_PATTERN_BUCKET;
		} else if (material->mode == GPU_SCENE_SHADOW_MODE ||
				   (material->flags & (HIRES_BACKEND_DECAL | HIRES_BACKEND_GROUND_CLIP)) != 0) {
			index = GPU_SCENE_DETAIL_BUCKET;
		}
	}
	struct GPU_SCENE_BUCKET *bucket = &scene_buckets[index];
	if (!scene_reserve(bucket, triangle_vertices)) {
		return;
	}
	if (outside == 0) {
		for (legacy_u32 vertex = 0; vertex < triangle_vertices; vertex++) {
			scene_write_vertex(&bucket->vertices[bucket->count++], &selected[triangles[vertex]],
							   material);
		}
		return;
	}
	for (legacy_u32 vertex = 0; vertex < triangle_vertices; vertex += GPU_SCENE_TRIANGLE_VERTICES) {
		legacy_u32 a = triangles[vertex], b = triangles[vertex + 1U], c = triangles[vertex + 2U];
		if ((outcodes[a] & outcodes[b] & outcodes[c]) != 0) {
			continue;
		}
		if ((outcodes[a] | outcodes[b] | outcodes[c]) == 0) {
			if (!scene_reserve(bucket, GPU_SCENE_TRIANGLE_VERTICES)) {
				return;
			}
			scene_write_vertex(&bucket->vertices[bucket->count++], &selected[a], material);
			scene_write_vertex(&bucket->vertices[bucket->count++], &selected[b], material);
			scene_write_vertex(&bucket->vertices[bucket->count++], &selected[c], material);
		} else {
			scene_clipped_triangle(bucket, &selected[a], &selected[b], &selected[c], material);
		}
	}
}

void gpu_scene_line(legacy_f32 x0, legacy_f32 y0, legacy_f32 x1, legacy_f32 y1,
					const struct HIRES_BACKEND_MATERIAL *material)
{
	legacy_f64 dx = (legacy_f64)x1 - x0;
	legacy_f64 dy = (legacy_f64)y1 - y0;
	legacy_f64 length = sqrt(dx * dx + dy * dy);
	legacy_f64 along_x = length != 0 ? dx * GPU_SCENE_HALF_PIXEL / length : 0;
	legacy_f64 along_y = length != 0 ? dy * GPU_SCENE_HALF_PIXEL / length : 0;
	legacy_f64 across_x = length != 0 ? -along_y : GPU_SCENE_HALF_PIXEL;
	legacy_f64 across_y = length != 0 ? along_x : 0;
	struct HIRES_BACKEND_VERTEX vertices[GPU_SCENE_POINT_VERTICES];
	if (length == 0) {
		vertices[0] = (struct HIRES_BACKEND_VERTEX){x0, y0, GPU_SCENE_CLASSIC_DEPTH};
		vertices[1] = (struct HIRES_BACKEND_VERTEX){x0 + 1, y0, GPU_SCENE_CLASSIC_DEPTH};
		vertices[2] = (struct HIRES_BACKEND_VERTEX){x0 + 1, y0 + 1, GPU_SCENE_CLASSIC_DEPTH};
		vertices[3] = (struct HIRES_BACKEND_VERTEX){x0, y0 + 1, GPU_SCENE_CLASSIC_DEPTH};
	} else {
		legacy_f64 first_x = x0 + GPU_SCENE_HALF_PIXEL - along_x;
		legacy_f64 first_y = y0 + GPU_SCENE_HALF_PIXEL - along_y;
		legacy_f64 last_x = x1 + GPU_SCENE_HALF_PIXEL + along_x;
		legacy_f64 last_y = y1 + GPU_SCENE_HALF_PIXEL + along_y;
		vertices[0] = (struct HIRES_BACKEND_VERTEX){first_x + across_x, first_y + across_y,
													GPU_SCENE_CLASSIC_DEPTH};
		vertices[1] = (struct HIRES_BACKEND_VERTEX){last_x + across_x, last_y + across_y,
													GPU_SCENE_CLASSIC_DEPTH};
		vertices[2] = (struct HIRES_BACKEND_VERTEX){last_x - across_x, last_y - across_y,
													GPU_SCENE_CLASSIC_DEPTH};
		vertices[3] = (struct HIRES_BACKEND_VERTEX){first_x - across_x, first_y - across_y,
													GPU_SCENE_CLASSIC_DEPTH};
	}
	gpu_scene_polygon(vertices, GPU_SCENE_POINT_VERTICES, material);
}

static legacy_s32 scene_submit(legacy_s32 width, legacy_s32 height, legacy_s32 left,
							   legacy_s32 right, legacy_s32 top, legacy_s32 bottom,
							   legacy_s32 preserve)
{
#ifdef RESTUNTS_VULKAN_SCENE
	size_t count = 0;
	for (legacy_u32 bucket = 0; bucket < GPU_SCENE_BUCKET_COUNT; bucket++) {
		if (scene_buckets[bucket].count > LEGACY_U32_MAX - count) {
			fatal_error((const legacy_s8 *)"GPU scene has too many vertices");
			return 0;
		}
		count += scene_buckets[bucket].count;
	}
	if (count == 0) {
		return 0;
	}
	scene_upload.count = 0;
	if (!scene_reserve(&scene_upload, count)) {
		return 0;
	}
	for (legacy_u32 bucket = 0; bucket < GPU_SCENE_BUCKET_COUNT; bucket++) {
		const struct GPU_SCENE_BUCKET *source = &scene_buckets[bucket];
		if (source->count != 0) {
			memcpy(scene_upload.vertices + scene_upload.count, source->vertices,
				   source->count * sizeof(*source->vertices));
		}
		scene_upload.count += source->count;
	}
	legacy_char error[GRAPHICS_ERROR_CAPACITY];
	if (!vulkan_scene_submit(scene_upload.vertices, count, width, height, left, right, top, bottom,
							 preserve, scene_classic, error, sizeof(error))) {
		fatal_error((const legacy_s8 *)"GPU scene rasterization failed: %s", error);
		return 0;
	}
	scene_clear();
	return 1;
#else
	(void)width;
	(void)height;
	(void)left;
	(void)right;
	(void)top;
	(void)bottom;
	(void)preserve;
	return 0;
#endif
}

legacy_s32 gpu_scene_flush(const struct HIRES_RASTER_TARGET *target, legacy_s32 preserve_depth)
{
	if (!scene_is_active ||
		!scene_submit(target->width, target->height, target->left, target->right, target->top,
					  target->bottom, preserve_depth)) {
		return 0;
	}
	if (!preserve_depth) {
		gpu_overlay_begin(target);
	}
	return 1;
}

legacy_s32 gpu_scene_classic_flush(const struct SPRITE *sprite, legacy_s32 width, legacy_s32 height)
{
	if (!scene_is_active || width != GPU_SCENE_CLASSIC_WIDTH || height <= 0 ||
		height > GPU_SCENE_CLASSIC_HEIGHT ||
		!scene_submit(GPU_SCENE_CLASSIC_WIDTH, GPU_SCENE_CLASSIC_HEIGHT, sprite->sprite_raster_left,
					  sprite->sprite_raster_right, sprite->sprite_top, sprite->sprite_bottom, 0)) {
		return 0;
	}
	gpu_overlay_begin_sprite(sprite, GPU_SCENE_CLASSIC_WIDTH, GPU_SCENE_CLASSIC_HEIGHT);
	return 1;
}

void gpu_scene_shutdown(void)
{
	for (legacy_u32 bucket = 0; bucket < GPU_SCENE_BUCKET_COUNT; bucket++) {
		free(scene_buckets[bucket].vertices);
		memset(&scene_buckets[bucket], 0, sizeof(scene_buckets[bucket]));
	}
	free(scene_upload.vertices);
	memset(&scene_upload, 0, sizeof(scene_upload));
	scene_is_active = 0;
	scene_software_preview = 0;
}
