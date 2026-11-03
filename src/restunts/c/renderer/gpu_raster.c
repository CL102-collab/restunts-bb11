#include "gpu_raster.h"
#include "../fatal.h"
#include <float.h>
#include <stdlib.h>
#include <string.h>

#define GPU_RASTER_INITIAL_WORDS 4096U
#define GPU_RASTER_BOUND_WORDS 4U

static legacy_u32 *gpu_input;
static size_t gpu_input_capacity;
static legacy_u32 *gpu_pixels;
static size_t gpu_pixel_capacity;
static legacy_s32 gpu_cleanup_registered;

static legacy_s32 gpu_word_range(size_t start, size_t count, size_t stride, size_t *end)
{
	size_t limit = (size_t)-1 / sizeof(legacy_u32);
	if (limit > LEGACY_U32_MAX) {
		limit = LEGACY_U32_MAX;
	}
	if (start > limit || count > (limit - start) / stride) {
		return 0;
	}
	*end = start + count * stride;
	return 1;
}

static void *gpu_reserve(void *data, size_t *capacity, size_t count, size_t element)
{
	if (count <= *capacity) {
		return data;
	}
	size_t next = *capacity != 0 ? *capacity : GPU_RASTER_INITIAL_WORDS;
	while (next < count) {
		if (next > (size_t)-1 / 2U) {
			fatal_error("GPU raster buffers exceed addressable memory");
			return NULL;
		}
		next *= 2U;
	}
	if (next > (size_t)-1 / element) {
		fatal_error("GPU raster buffers exceed addressable memory");
		return NULL;
	}
	void *result = realloc(data, next * element);
	if (result == NULL) {
		fatal_error("Cannot allocate GPU raster buffers");
		return NULL;
	}
	*capacity = next;
	return result;
}

static legacy_u32 gpu_float(legacy_f64 value)
{
	legacy_f32 converted = (legacy_f32)value;
	legacy_u32 word;
	memcpy(&word, &converted, sizeof(word));
	return word;
}

static legacy_f32 gpu_depth(legacy_u32 word)
{
	legacy_f32 value;
	memcpy(&value, &word, sizeof(value));
	return value;
}

static legacy_s32 gpu_bound(legacy_f64 value, legacy_s32 limit, legacy_s32 upper)
{
	if (value <= 0) {
		return 0;
	}
	if (value >= limit) {
		return limit;
	}
	legacy_s32 result = (legacy_s32)value;
	return upper && result < value ? result + 1 : result;
}

static void gpu_command_bounds(const struct GPU_RASTER_COMMAND *command,
							   const struct GPU_RASTER_EDGE *edges, legacy_s32 width,
							   legacy_s32 height, legacy_u32 *bounds)
{
	if ((command->material.flags & GPU_RASTER_LEGACY) != 0) {
		memcpy(bounds, command->bounds, sizeof(command->bounds));
		return;
	}
	legacy_f64 minimum = FLT_MAX, maximum = -FLT_MAX;
	for (legacy_u32 index = 0; index < command->hires_edge_count; index++) {
		const struct GPU_RASTER_EDGE *edge = &edges[command->first_edge + index];
		legacy_f64 end = (command->material.flags & GPU_RASTER_LINE) != 0
							 ? edge->x
							 : edge->x + edge->x_step * (edge->bottom - edge->y);
		if (edge->x < minimum) {
			minimum = edge->x;
		}
		if (end < minimum) {
			minimum = end;
		}
		if (edge->x > maximum) {
			maximum = edge->x;
		}
		if (end > maximum) {
			maximum = end;
		}
	}
	if ((command->material.flags & GPU_RASTER_LINE) != 0) {
		maximum += 1;
	}
	bounds[0] = (legacy_u32)gpu_bound(minimum, width, 0);
	bounds[1] = (legacy_u32)gpu_bound(maximum, width, 1);
	bounds[2] = (legacy_u32)(command->top < 0 ? 0 : command->top > height ? height : command->top);
	bounds[3] = (legacy_u32)(command->bottom < 0		? 0
							 : command->bottom > height ? height
														: command->bottom);
}

legacy_s32 gpu_raster_submit(legacy_s32 width, legacy_s32 height, legacy_s32 left, legacy_s32 right,
							 legacy_s32 top, legacy_s32 bottom, legacy_u32 mode,
							 const struct GPU_RASTER_COMMAND *commands, size_t command_count,
							 const struct GPU_RASTER_EDGE *edges, size_t edge_count,
							 const struct HIRES_DEPTH_PLANE *ground, legacy_s32 preserve_depth,
							 legacy_u32 *output)
{
	if (width <= 0 || height <= 0 || width > HIRES_WIDTH || height > HIRES_HEIGHT || left < 0 ||
		top < 0 || right > width || bottom > height || left >= right || top >= bottom ||
		command_count > LEGACY_U32_MAX / GPU_RASTER_COMMAND_WORDS ||
		edge_count > LEGACY_U32_MAX / GPU_RASTER_EDGE_WORDS) {
		fatal_error("Invalid GPU raster frame dimensions or command count");
		return 0;
	}
	if (!gpu_cleanup_registered) {
		if (atexit(gpu_raster_shutdown) != 0) {
			fatal_error("Cannot register GPU raster cleanup");
			return 0;
		}
		gpu_cleanup_registered = 1;
	}
	legacy_u32 tiles_x = ((legacy_u32)width + GPU_RASTER_TILE_SIZE - 1U) / GPU_RASTER_TILE_SIZE;
	legacy_u32 tiles_y = ((legacy_u32)height + GPU_RASTER_TILE_SIZE - 1U) / GPU_RASTER_TILE_SIZE;
	if ((size_t)tiles_x > (size_t)-1 / tiles_y) {
		fatal_error("GPU raster tile grid exceeds addressable memory");
		return 0;
	}
	size_t tile_count = (size_t)tiles_x * tiles_y;
	size_t command_base = GPU_RASTER_HEADER_WORDS;
	size_t edge_base, tile_base, index_base;
	if (!gpu_word_range(command_base, command_count, GPU_RASTER_COMMAND_WORDS, &edge_base) ||
		!gpu_word_range(edge_base, edge_count, GPU_RASTER_EDGE_WORDS, &tile_base) ||
		!gpu_word_range(tile_base, tile_count, 2U, &index_base)) {
		fatal_error("GPU raster tile list exceeds addressable memory");
		return 0;
	}
	legacy_u32 *bounds =
		calloc(command_count != 0 ? command_count : 1U, GPU_RASTER_BOUND_WORDS * sizeof(*bounds));
	legacy_u32 *counts = calloc(tile_count, sizeof(*counts));
	if (bounds == NULL || counts == NULL) {
		free(bounds);
		free(counts);
		fatal_error("Cannot allocate GPU raster tile bins");
		return 0;
	}
	size_t words = index_base;
	for (size_t index = 0; index < command_count; index++) {
		const struct GPU_RASTER_COMMAND *command = &commands[index];
		if (command->first_edge > edge_count ||
			command->hires_edge_count > edge_count - command->first_edge) {
			free(bounds);
			free(counts);
			fatal_error("Invalid GPU raster edge range");
			return 0;
		}
		legacy_u32 *box = bounds + index * GPU_RASTER_BOUND_WORDS;
		gpu_command_bounds(command, edges, width, height, box);
		if (box[0] < (legacy_u32)left) {
			box[0] = (legacy_u32)left;
		}
		if (box[1] > (legacy_u32)right) {
			box[1] = (legacy_u32)right;
		}
		if (box[2] < (legacy_u32)top) {
			box[2] = (legacy_u32)top;
		}
		if (box[3] > (legacy_u32)bottom) {
			box[3] = (legacy_u32)bottom;
		}
		if (box[0] >= box[1] || box[2] >= box[3]) {
			continue;
		}
		size_t columns = (box[1] - 1U) / GPU_RASTER_TILE_SIZE - box[0] / GPU_RASTER_TILE_SIZE + 1U;
		size_t rows = (box[3] - 1U) / GPU_RASTER_TILE_SIZE - box[2] / GPU_RASTER_TILE_SIZE + 1U;
		if (!gpu_word_range(words, columns, rows, &words)) {
			free(bounds);
			free(counts);
			fatal_error("GPU raster tile list exceeds addressable memory");
			return 0;
		}
		for (legacy_u32 y = box[2] / GPU_RASTER_TILE_SIZE;
			 y <= (box[3] - 1U) / GPU_RASTER_TILE_SIZE; y++) {
			for (legacy_u32 x = box[0] / GPU_RASTER_TILE_SIZE;
				 x <= (box[1] - 1U) / GPU_RASTER_TILE_SIZE; x++) {
				counts[(size_t)y * tiles_x + x]++;
			}
		}
	}
	gpu_input = gpu_reserve(gpu_input, &gpu_input_capacity, words, sizeof(*gpu_input));
	memset(gpu_input, 0, words * sizeof(*gpu_input));
	legacy_u32 *header = gpu_input;
	header[0] = (legacy_u32)width;
	header[1] = (legacy_u32)height;
	header[2] = (legacy_u32)left;
	header[3] = (legacy_u32)right;
	header[4] = (legacy_u32)top;
	header[5] = (legacy_u32)bottom;
	header[6] = mode;
	header[7] = (legacy_u32)command_count;
	header[8] = (legacy_u32)command_base;
	header[9] = (legacy_u32)edge_base;
	header[10] = (legacy_u32)tile_base;
	header[11] = (legacy_u32)index_base;
	header[12] = tiles_x;
	header[13] = tiles_y;
	header[14] = ground != NULL;
	if (ground != NULL) {
		header[15] = gpu_float(ground->x_step);
		header[16] = gpu_float(ground->y_step);
		header[17] = gpu_float(ground->origin);
	}
	header[18] = preserve_depth != 0;
	header[19] = (legacy_u32)width * (legacy_u32)height * GPU_RASTER_OUTPUT_WORDS;
	for (size_t index = 0; index < command_count; index++) {
		const struct GPU_RASTER_COMMAND *source = &commands[index];
		legacy_u32 *wire = gpu_input + command_base + index * GPU_RASTER_COMMAND_WORDS;
		wire[0] = source->first_edge;
		wire[1] = source->hires_edge_count;
		wire[2] = (legacy_u32)source->top;
		wire[3] = (legacy_u32)source->bottom;
		wire[4] = source->material.color;
		wire[5] = source->material.alternate;
		wire[6] = source->material.pattern;
		wire[7] = source->material.mode;
		wire[8] = source->material.flags;
		wire[9] = source->material.family;
		wire[10] = source->planar != 0;
		wire[11] = gpu_float(source->plane.x_step);
		wire[12] = gpu_float(source->plane.y_step);
		wire[13] = gpu_float(source->plane.origin);
		memcpy(wire + 14, bounds + index * GPU_RASTER_BOUND_WORDS,
			   GPU_RASTER_BOUND_WORDS * sizeof(*bounds));
	}
	for (size_t index = 0; index < edge_count; index++) {
		const struct GPU_RASTER_EDGE *source = &edges[index];
		legacy_u32 *wire = gpu_input + edge_base + index * GPU_RASTER_EDGE_WORDS;
		wire[0] = gpu_float(source->x);
		wire[1] = gpu_float(source->y);
		wire[2] = gpu_float(source->z);
		wire[3] = gpu_float(source->x_step);
		wire[4] = gpu_float(source->z_step);
		wire[5] = (legacy_u32)source->top;
		wire[6] = (legacy_u32)source->bottom;
	}
	legacy_u32 offset = 0;
	for (size_t index = 0; index < tile_count; index++) {
		gpu_input[tile_base + index * 2U] = offset;
		gpu_input[tile_base + index * 2U + 1U] = counts[index];
		offset += counts[index];
		counts[index] = 0;
	}
	for (size_t index = 0; index < command_count; index++) {
		const legacy_u32 *box = bounds + index * GPU_RASTER_BOUND_WORDS;
		if (box[0] >= box[1] || box[2] >= box[3]) {
			continue;
		}
		for (legacy_u32 y = box[2] / GPU_RASTER_TILE_SIZE;
			 y <= (box[3] - 1U) / GPU_RASTER_TILE_SIZE; y++) {
			for (legacy_u32 x = box[0] / GPU_RASTER_TILE_SIZE;
				 x <= (box[1] - 1U) / GPU_RASTER_TILE_SIZE; x++) {
				size_t tile = (size_t)y * tiles_x + x;
				gpu_input[index_base + gpu_input[tile_base + tile * 2U] + counts[tile]++] =
					(legacy_u32)index;
			}
		}
	}
	free(bounds);
	free(counts);
	legacy_char error[GRAPHICS_ERROR_CAPACITY];
	if (!graphics_gpu_dispatch(gpu_input, words, output,
							   (size_t)width * height * GPU_RASTER_OUTPUT_WORDS,
							   (legacy_u32)width * height, error, sizeof(error))) {
		fatal_error("GPU rasterization failed: %s", error);
		return 0;
	}
	return 1;
}

legacy_s32 gpu_raster_hires(const struct HIRES_RASTER_TARGET *target,
							const struct GPU_RASTER_COMMAND *commands, size_t command_count,
							const struct GPU_RASTER_EDGE *edges, size_t edge_count,
							const struct HIRES_DEPTH_PLANE *ground, legacy_s32 preserve_depth)
{
	size_t pixels = (size_t)target->width * target->height;
	gpu_pixels = gpu_reserve(gpu_pixels, &gpu_pixel_capacity, pixels * GPU_RASTER_OUTPUT_WORDS,
							 sizeof(*gpu_pixels));
	memset(gpu_pixels, 0, pixels * GPU_RASTER_OUTPUT_WORDS * sizeof(*gpu_pixels));
	for (legacy_s32 y = target->depth_top; y < target->depth_bottom; y++) {
		for (legacy_s32 x = target->depth_left; x < target->depth_right; x++) {
			size_t pixel = (size_t)y * target->width + x;
			memcpy(&gpu_pixels[pixel * GPU_RASTER_OUTPUT_WORDS + 1U], &target->inverse_depth[pixel],
				   sizeof(legacy_u32));
			gpu_pixels[pixel * GPU_RASTER_OUTPUT_WORDS + 2U] = target->depth_family[pixel];
		}
	}
	if (!gpu_raster_submit(target->width, target->height, target->left, target->right, target->top,
						   target->bottom, GPU_RASTER_HIRES, commands, command_count, edges,
						   edge_count, ground, preserve_depth, gpu_pixels)) {
		return 0;
	}
	struct HIRES_RASTER_CONTEXT context = {target, target->top, target->bottom, 0};
	for (legacy_s32 y = target->top; y < target->bottom; y++) {
		for (legacy_s32 x = target->left; x < target->right; x++) {
			size_t pixel = (size_t)y * target->width + x;
			const legacy_u32 *sample = gpu_pixels + pixel * GPU_RASTER_OUTPUT_WORDS;
			if ((sample[0] & GPU_RASTER_TOUCHED) != 0) {
				hires_raster_pixel(&context, x, y, (legacy_u8)sample[0]);
			}
			if (x >= target->depth_left && x < target->depth_right && y >= target->depth_top &&
				y < target->depth_bottom) {
				target->inverse_depth[pixel] = gpu_depth(sample[1]);
				target->depth_family[pixel] = sample[2];
			}
		}
	}
	hires_raster_finish(target, context.cleared_argb_cells);
	return 1;
}

void gpu_raster_shutdown(void)
{
	gpu_raster_classic_shutdown();
	free(gpu_input);
	gpu_input = NULL;
	gpu_input_capacity = 0;
	free(gpu_pixels);
	gpu_pixels = NULL;
	gpu_pixel_capacity = 0;
}
