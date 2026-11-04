#include "gpu_raster.h"
#include "gpu_scene.h"
#include "../fatal.h"
#include "../platform.h"
#include "../shape2d/shape2d_internal.h"
#include "../shape3d/shape3d_internal.h"
#include <stdlib.h>
#include <string.h>

#define CLASSIC_GPU_INITIAL_COMMANDS 256U
#define CLASSIC_GPU_INITIAL_EDGES (CLASSIC_GPU_INITIAL_COMMANDS * 4U)
#define CLASSIC_GPU_LINE_EDGE_COUNT 1U
#define CLASSIC_GPU_SAMPLE_DEPTH 1.0

static struct GPU_RASTER_COMMAND *classic_commands;
static struct GPU_RASTER_EDGE *classic_edges;
static legacy_u32 *classic_pixels;
static size_t classic_command_count, classic_command_capacity;
static size_t classic_edge_count, classic_edge_capacity, classic_pixel_capacity;
static struct SPRITE classic_target;
static legacy_s32 classic_active, classic_width, classic_height;

static void *classic_reserve(void *buffer, size_t *capacity, size_t required, size_t initial,
							 size_t element)
{
	if (required <= *capacity) {
		return buffer;
	}
	size_t next = *capacity != 0 ? *capacity : initial;
	while (next < required) {
		if (next > (size_t)-1 / 2U) {
			fatal_error("Classic GPU scene exceeds addressable memory");
			return NULL;
		}
		next *= 2U;
	}
	if (next > (size_t)-1 / element) {
		fatal_error("Classic GPU scene exceeds addressable memory");
		return NULL;
	}
	void *result = realloc(buffer, next * element);
	if (result == NULL) {
		fatal_error("Cannot allocate classic GPU scene");
		return NULL;
	}
	*capacity = next;
	return result;
}

static struct GPU_RASTER_COMMAND *classic_command(legacy_u32 edge_count,
												  const struct HIRES_BACKEND_MATERIAL *material)
{
	if (classic_command_count >= LEGACY_U32_MAX ||
		classic_edge_count > LEGACY_U32_MAX - edge_count) {
		fatal_error("Classic GPU scene has too many commands");
		return NULL;
	}
	classic_commands =
		classic_reserve(classic_commands, &classic_command_capacity, classic_command_count + 1U,
						CLASSIC_GPU_INITIAL_COMMANDS, sizeof(*classic_commands));
	classic_edges =
		classic_reserve(classic_edges, &classic_edge_capacity, classic_edge_count + edge_count,
						CLASSIC_GPU_INITIAL_EDGES, sizeof(*classic_edges));
	struct GPU_RASTER_COMMAND *command = &classic_commands[classic_command_count++];
	memset(command, 0, sizeof(*command));
	command->first_edge = (legacy_u32)classic_edge_count;
	command->material = *material;
	command->planar = 1;
	command->plane.origin = CLASSIC_GPU_SAMPLE_DEPTH;
	return command;
}

static struct HIRES_BACKEND_MATERIAL classic_material(legacy_u16 color)
{
	struct HIRES_BACKEND_MATERIAL material = {0};
	material.color = color;
	if (color == PRERENDER_GHOST_COLOR) {
		material.color = 0;
		material.mode = HIRES_PAINT_PATTERN;
		material.pattern = PRERENDER_BLACK_GRILLE_PATTERN;
	}
	return material;
}

/* Preserve the legacy setup fractions and 16.16 step. The GPU determines
 * each covered sample; this stage only clips and prepares line geometry. */
static void classic_edge(const legacy_u16 *line)
{
	classic_edges[classic_edge_count++] =
		(struct GPU_RASTER_EDGE){LEGACY_S16_FROM_BITS(line[DRAW_LINE_START_X_INDEX]),
								 LEGACY_S16_FROM_BITS(line[DRAW_LINE_START_Y_INDEX]),
								 line[DRAW_LINE_START_X_FRACTION_INDEX],
								 line[DRAW_LINE_START_Y_FRACTION_INDEX],
								 line[DRAW_LINE_STEP_INDEX],
								 line[DRAW_LINE_MODE_AND_CLIP_INDEX] & DRAW_LINE_MODE_MASK,
								 line[DRAW_LINE_PIXEL_COUNT_INDEX]};
}

static void classic_bounds(struct GPU_RASTER_COMMAND *command, legacy_s16 left, legacy_s16 right,
						   legacy_s16 top, legacy_s16 bottom)
{
	command->material.flags |= GPU_RASTER_LEGACY;
	command->top = top;
	command->bottom = (legacy_s32)bottom + 1;
	command->bounds[0] = left < 0 ? 0U : (legacy_u32)left;
	command->bounds[1] = right < 0 ? 0U : (legacy_u32)right + 1U;
	command->bounds[2] = top < 0 ? 0U : (legacy_u32)top;
	command->bounds[3] = bottom < 0 ? 0U : (legacy_u32)bottom + 1U;
	if (command->bounds[1] > (legacy_u32)classic_width) {
		command->bounds[1] = (legacy_u32)classic_width;
	}
	if (command->bounds[3] > (legacy_u32)classic_height) {
		command->bounds[3] = (legacy_u32)classic_height;
	}
}

static void classic_line(legacy_s16 x0, legacy_s16 y0, legacy_s16 x1, legacy_s16 y1,
						 const struct HIRES_BACKEND_MATERIAL *material)
{
	if (gpu_scene_active()) {
		gpu_scene_line(x0, y0, x1, y1, material);
		return;
	}
	legacy_u16 line[DRAW_LINE_WORD_COUNT] = {0};
	if (line_prepare_clipped(x0, y0, x1, y1, line) != 0) {
		return;
	}
	struct GPU_RASTER_COMMAND *command = classic_command(CLASSIC_GPU_LINE_EDGE_COUNT, material);
	command->material.flags |= GPU_RASTER_LINE;
	classic_bounds(command, x0 < x1 ? x0 : x1, x0 > x1 ? x0 : x1, y0 < y1 ? y0 : y1,
				   y0 > y1 ? y0 : y1);
	command->hires_edge_count = CLASSIC_GPU_LINE_EDGE_COUNT;
	classic_edge(line);
}

void gpu_raster_classic_begin(void)
{
	classic_active = graphics_renderer() == GRAPHICS_RENDERER_NATIVE;
	classic_command_count = classic_edge_count = 0;
	if (!classic_active) {
		return;
	}
	classic_target = drawing_sprite;
	classic_width = classic_target.sprite_buffer_width;
	classic_height = classic_target.sprite_bottom;
	if (classic_width <= 0 || classic_width > HIRES_WIDTH || classic_height <= 0 ||
		classic_height > HIRES_HEIGHT ||
		classic_target.sprite_raster_left >= classic_target.sprite_raster_right ||
		classic_target.sprite_raster_right > classic_width ||
		classic_target.sprite_top >= classic_height || classic_target.sprite_lineofs == NULL ||
		classic_target.sprite_bitmapptr == NULL) {
		classic_active = 0;
		fatal_error("Invalid classic GPU drawing target");
		return;
	}
	gpu_scene_classic_begin(&classic_target, classic_width, classic_height);
	if (gpu_scene_software_preview()) {
		classic_active = 0;
	}
}

legacy_s32 gpu_raster_classic_active(void)
{
	return classic_active;
}

legacy_s32 gpu_raster_classic_line(legacy_s16 x0, legacy_s16 y0, legacy_s16 x1, legacy_s16 y1,
								   legacy_u16 color)
{
	if (!classic_active) {
		return 0;
	}
	struct HIRES_BACKEND_MATERIAL material = classic_material(color);
	classic_line(x0, y0, x1, y1, &material);
	return 1;
}

legacy_s32 gpu_raster_classic_span(legacy_s16 left, legacy_s16 right, legacy_s16 y,
								   legacy_u16 color)
{
	if (!classic_active) {
		return 0;
	}
	if (left <= right) {
		struct HIRES_BACKEND_MATERIAL material = classic_material(color);
		classic_line(left, y, right, y, &material);
	}
	return 1;
}

legacy_s32 gpu_raster_classic_polygon(legacy_u16 color, legacy_u16 alternate, legacy_u16 pattern,
									  legacy_u8 mode, legacy_u16 count,
									  const struct POINT2D *points, legacy_s32 patterned_line)
{
	if (!classic_active) {
		return 0;
	}
	if (count == 0) {
		return 1;
	}
	struct HIRES_BACKEND_MATERIAL material = {0, color, alternate, pattern, mode, 0};
	legacy_s16 left = points[0].px, right = left, top = points[0].py, bottom = top;
	for (legacy_u32 index = 1; index < count; index++) {
		if (points[index].px < left) {
			left = points[index].px;
		}
		if (points[index].px > right) {
			right = points[index].px;
		}
		if (points[index].py < top) {
			top = points[index].py;
		}
		if (points[index].py > bottom) {
			bottom = points[index].py;
		}
	}
	if (count < 3U || left == right || top == bottom) {
		if ((count == 1U || left == right || top == bottom) && !patterned_line) {
			material.mode = HIRES_PAINT_SOLID;
		}
		if (count < 3U) {
			const struct POINT2D *last = &points[count - 1U];
			classic_line(points[0].px, points[0].py, last->px, last->py, &material);
		} else {
			classic_line(left, top, right, bottom, &material);
		}
		return 1;
	}
	if (gpu_scene_active()) {
		if (count <= HIRES_BACKEND_MAX_VERTICES) {
			struct HIRES_BACKEND_VERTEX vertices[HIRES_BACKEND_MAX_VERTICES];
			for (legacy_u32 vertex = 0; vertex < count; vertex++) {
				vertices[vertex] = (struct HIRES_BACKEND_VERTEX){
					points[vertex].px, points[vertex].py, CLASSIC_GPU_SAMPLE_DEPTH};
			}
			gpu_scene_polygon(vertices, count, &material);
		}
		return 1;
	}
	struct GPU_RASTER_COMMAND *command = classic_command(count, &material);
	classic_bounds(command, left, right, top, bottom);
	for (legacy_u32 index = 0; index < count; index++) {
		const struct POINT2D *first = &points[index];
		const struct POINT2D *last = &points[index + 1U == count ? 0 : index + 1U];
		if (first->py == last->py) {
			continue;
		}
		legacy_u16 line[DRAW_LINE_WORD_COUNT] = {0};
		line_prepare_unclipped(first->px, first->py, last->px, last->py, line);
		classic_edge(line);
		command->hires_edge_count++;
	}
	return 1;
}

void gpu_raster_classic_end(void)
{
	if (!classic_active) {
		return;
	}
	classic_active = 0;
	if (gpu_scene_active()) {
		gpu_scene_classic_flush(&classic_target, classic_width, classic_height);
		return;
	}
	if (classic_command_count == 0) {
		return;
	}
	size_t words = (size_t)classic_width * classic_height * GPU_RASTER_OUTPUT_WORDS;
	classic_pixels = classic_reserve(classic_pixels, &classic_pixel_capacity, words,
									 CLASSIC_GPU_INITIAL_COMMANDS * GPU_RASTER_OUTPUT_WORDS,
									 sizeof(*classic_pixels));
	memset(classic_pixels, 0, words * sizeof(*classic_pixels));
	if (!gpu_raster_submit(classic_width, classic_height, classic_target.sprite_raster_left,
						   classic_target.sprite_raster_right, classic_target.sprite_top,
						   classic_target.sprite_bottom, GPU_RASTER_CLASSIC, classic_commands,
						   classic_command_count, classic_edges, classic_edge_count, NULL, 0,
						   classic_pixels)) {
		fatal_error("Cannot finish classic GPU scene");
		return;
	}
	legacy_u8 *bitmap =
		dos_memory_make_pointer(dos_memory_pointer_segment(classic_target.sprite_bitmapptr), 0);
	for (legacy_s32 y = classic_target.sprite_top; y < classic_target.sprite_bottom; y++) {
		legacy_u16 row = shape2d_get_word(
			shape2d_line_pointer(&classic_target, (legacy_u16)(y * LEGACY_WORD_BYTES)));
		for (legacy_s32 x = classic_target.sprite_raster_left;
			 x < classic_target.sprite_raster_right; x++) {
			legacy_u32 sample =
				classic_pixels[((size_t)y * classic_width + x) * GPU_RASTER_OUTPUT_WORDS];
			if ((sample & GPU_RASTER_TOUCHED) != 0) {
				legacy_u16 offset = LEGACY_U16_WRAP_ADD(row, (legacy_u16)x);
				hires_write(bitmap, offset, (legacy_u8)sample);
				bitmap[offset] = (legacy_u8)sample;
			}
		}
	}
}

void gpu_raster_classic_shutdown(void)
{
	free(classic_commands);
	free(classic_edges);
	free(classic_pixels);
	classic_commands = NULL;
	classic_edges = NULL;
	classic_pixels = NULL;
	classic_command_count = classic_command_capacity = 0;
	classic_edge_count = classic_edge_capacity = classic_pixel_capacity = 0;
	classic_active = classic_width = classic_height = 0;
}
