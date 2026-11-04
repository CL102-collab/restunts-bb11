#include "vulkan_scene_internal.h"
#include <stdlib.h>
#include <string.h>

#define SCENE_INITIAL_CACHED_BATCHES 16U

static void *scene_cache_reserve(void *buffer, size_t *capacity, size_t required, size_t element,
								 size_t initial)
{
	if (required <= *capacity) {
		return buffer;
	}
	size_t next = *capacity != 0 ? *capacity : initial;
	while (next < required) {
		if (next > (size_t)-1 / 2U) {
			return NULL;
		}
		next *= 2U;
	}
	if (next > (size_t)-1 / element) {
		return NULL;
	}
	void *result = realloc(buffer, next * element);
	if (result != NULL) {
		*capacity = next;
	}
	return result;
}

legacy_s32 vulkan_scene_cache(const struct VULKAN_SCENE_VERTEX *vertices, size_t count,
							  legacy_s32 width, legacy_s32 height, legacy_s32 left,
							  legacy_s32 right, legacy_s32 top, legacy_s32 bottom,
							  legacy_s32 preserve, legacy_s32 classic, legacy_char *error,
							  size_t capacity)
{
	if (vulkan_scene.replaying) {
		return 1;
	}
	if (!preserve) {
		vulkan_scene.cached_vertex_count = 0;
		vulkan_scene.cached_batch_count = 0;
		vulkan_scene.image_palette_generation = vulkan_scene.palette_generation;
	}
	size_t first = vulkan_scene.cached_vertex_count;
	if (count > (size_t)-1 - first || vulkan_scene.cached_batch_count == (size_t)-1) {
		return vulkan_error(error, capacity, "Vulkan retained scene exceeds addressable memory",
							VK_ERROR_OUT_OF_HOST_MEMORY);
	}
	if (count != 0) {
		void *storage =
			scene_cache_reserve(vulkan_scene.cached_vertices, &vulkan_scene.cached_vertex_capacity,
								first + count, sizeof(*vertices), VULKAN_SCENE_FIRST_VERTICES);
		if (storage == NULL) {
			return vulkan_error(error, capacity, "Retain Vulkan scene geometry",
								VK_ERROR_OUT_OF_HOST_MEMORY);
		}
		vulkan_scene.cached_vertices = storage;
		memcpy(vulkan_scene.cached_vertices + first, vertices, count * sizeof(*vertices));
	}
	void *batches =
		scene_cache_reserve(vulkan_scene.cached_batches, &vulkan_scene.cached_batch_capacity,
							vulkan_scene.cached_batch_count + 1U,
							sizeof(*vulkan_scene.cached_batches), SCENE_INITIAL_CACHED_BATCHES);
	if (batches == NULL) {
		return vulkan_error(error, capacity, "Retain Vulkan scene batch metadata",
							VK_ERROR_OUT_OF_HOST_MEMORY);
	}
	vulkan_scene.cached_batches = batches;
	struct VULKAN_SCENE_BATCH *batch =
		&vulkan_scene.cached_batches[vulkan_scene.cached_batch_count++];
	*batch = (struct VULKAN_SCENE_BATCH){first, count, width,  height,		  left,
										 right, top,   bottom, preserve != 0, classic != 0};
	vulkan_scene.cached_vertex_count += count;
	return 1;
}

legacy_s32 vulkan_scene_refresh_palette(legacy_char *error, size_t error_capacity)
{
	if (!vulkan_scene.initialized || vulkan_scene.cached_batch_count == 0 ||
		vulkan_scene.palette_generation == vulkan_scene.image_palette_generation) {
		return 1;
	}
	vulkan_scene.replaying = 1;
	legacy_s32 result = 1;
	for (size_t index = 0; index < vulkan_scene.cached_batch_count; index++) {
		const struct VULKAN_SCENE_BATCH *batch = &vulkan_scene.cached_batches[index];
		const struct VULKAN_SCENE_VERTEX *vertices =
			batch->count != 0 ? vulkan_scene.cached_vertices + batch->first : NULL;
		if (!vulkan_scene_submit(vertices, batch->count, batch->width, batch->height, batch->left,
								 batch->right, batch->top, batch->bottom, batch->preserve,
								 batch->classic, error, error_capacity)) {
			result = 0;
			break;
		}
	}
	vulkan_scene.replaying = 0;
	if (result) {
		vulkan_scene.image_palette_generation = vulkan_scene.palette_generation;
	}
	return result;
}
