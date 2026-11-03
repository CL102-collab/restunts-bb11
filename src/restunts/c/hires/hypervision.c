#include "hypervision.h"
#include "../fatal.h"
#include "render_workers.h"
#include <float.h>
#include <stdlib.h>
#include <string.h>

#define HIRES_VISIBILITY_BAND_HEIGHT 8
#define HIRES_VISIBILITY_BAND_COUNT                                                                \
	((HIRES_HEIGHT + HIRES_VISIBILITY_BAND_HEIGHT - 1) / HIRES_VISIBILITY_BAND_HEIGHT)
#define HIRES_VISIBILITY_INITIAL_COMMANDS 1024U
#define HIRES_VISIBILITY_INITIAL_EDGES (HIRES_VISIBILITY_INITIAL_COMMANDS * 4U)
#define HIRES_VISIBILITY_INITIAL_BIN 256U
#define HIRES_VISIBILITY_INITIAL_BUILD_BUFFERS 8U
#define HIRES_VISIBILITY_INITIAL_BUILD_COMMANDS 128U
#define HIRES_VISIBILITY_INITIAL_BUILD_EDGES (HIRES_VISIBILITY_INITIAL_BUILD_COMMANDS * 4U)
#define HIRES_VISIBILITY_NO_COMMAND LEGACY_U32_MAX
#define HIRES_VISIBILITY_PARALLEL_MIN_AREA 262144U
/* A small relative offset replaces the old order-dependent decal families. */
#define HIRES_VISIBILITY_DECAL_DEPTH_SCALE 1.0001
#define HIRES_VISIBILITY_DEPTH_RELATIVE_EPSILON (4 * FLT_EPSILON)
#define HIRES_VISIBILITY_HALF_PIXEL HIRES_SAMPLE_CENTER_OFFSET
#define HIRES_VISIBILITY_SPAN_CAPACITY (HIRES_WIDTH + 2)
#define HIRES_VISIBILITY_EVALUATE_AS_DECLARED 0
#define HIRES_VISIBILITY_EVALUATE_AS_DOUBLE 1

struct HIRES_VISIBILITY_EDGE {
	legacy_f64 x, y, z, x_step, z_step;
	legacy_s32 top, bottom;
};
struct HIRES_VISIBILITY_COMMAND {
	struct HIRES_BACKEND_MATERIAL material;
	legacy_u32 first_edge, hires_edge_count;
	legacy_s32 top, bottom;
	struct HIRES_DEPTH_PLANE plane;
	legacy_s32 planar;
};
struct HIRES_VISIBILITY_BIN {
	legacy_u32 *hires_commands;
	size_t count, capacity;
	struct HIRES_RASTER_CONTEXT context;
};
struct HIRES_VISIBILITY_SPAN {
	legacy_s32 left, right;
	legacy_f64 origin, step;
	legacy_u32 command;
};
struct HIRES_VISIBILITY_CROSSING {
	legacy_f64 x, z;
};

struct HIRES_BACKEND_BUFFER {
	struct HIRES_VISIBILITY_COMMAND *commands;
	struct HIRES_VISIBILITY_EDGE *edges;
	size_t command_count, command_capacity, edge_count, edge_capacity;
	size_t command_offset, edge_offset;
	legacy_u32 command_area;
	legacy_s32 allocation_failed;
};

static struct HIRES_BACKEND_BUFFER hires_frame;
static struct HIRES_BACKEND_BUFFER *hires_build_buffers;
static size_t hires_build_capacity;
static struct HIRES_VISIBILITY_BIN hires_bins[HIRES_VISIBILITY_BAND_COUNT];
static struct HIRES_DEPTH_PLANE hires_ground;
static legacy_s32 hires_ground_enabled, hires_incremental;
static void *hires_visibility_reserve(void *buffer, size_t *capacity, size_t required,
									  size_t initial, size_t element_size, legacy_s32 *failed)
{
	if (required <= *capacity) {
		return buffer;
	}
	size_t next = *capacity != 0 ? *capacity : initial;
	while (next < required) {
		if (next > (size_t)-1 / 2U) {
			*failed = 1;
			return buffer;
		}
		next *= 2U;
	}
	if (next > (size_t)-1 / element_size) {
		*failed = 1;
		return buffer;
	}
	void *result = realloc(buffer, next * element_size);
	if (result == NULL) {
		*failed = 1;
		return buffer;
	}
	*capacity = next;
	return result;
}

/* Round each scanline offset before adding the edge origin. Extended evaluation
 * must not move a crossing across an exact pixel center; ordinary assignments
 * can retain excess precision when the compiler uses its fast evaluation mode. */
static legacy_f64 hires_visibility_binary64(legacy_f64 value)
{
#if FLT_EVAL_METHOD != HIRES_VISIBILITY_EVALUATE_AS_DECLARED &&                                    \
	FLT_EVAL_METHOD != HIRES_VISIBILITY_EVALUATE_AS_DOUBLE
	volatile legacy_f64 rounded = value;
	return rounded;
#else
	return value;
#endif
}

static legacy_s32 hires_visibility_ceil(legacy_f64 value)
{
	legacy_s32 result = (legacy_s32)value;
	return result < value ? result + 1 : result;
}

static void hires_backend_buffer_reset(struct HIRES_BACKEND_BUFFER *buffer)
{
	buffer->command_count = 0;
	buffer->edge_count = 0;
	buffer->command_area = 0;
	buffer->allocation_failed = 0;
}

static void hires_backend_buffer_free(struct HIRES_BACKEND_BUFFER *buffer)
{
	free(buffer->commands);
	free(buffer->edges);
	memset(buffer, 0, sizeof(*buffer));
}

void hires_backend_begin(const struct HIRES_DEPTH_PLANE *plane)
{
	hires_backend_buffer_reset(&hires_frame);
	hires_ground_enabled = plane != NULL;
	if (plane != NULL) {
		hires_ground = *plane;
	}
}

void hires_backend_shutdown(void)
{
	hires_backend_buffer_free(&hires_frame);
	for (size_t index = 0; index < hires_build_capacity; index++) {
		hires_backend_buffer_free(&hires_build_buffers[index]);
	}
	free(hires_build_buffers);
	hires_build_buffers = NULL;
	hires_build_capacity = 0;
	for (legacy_s32 band = 0; band < HIRES_VISIBILITY_BAND_COUNT; band++) {
		free(hires_bins[band].hires_commands);
		memset(&hires_bins[band], 0, sizeof(hires_bins[band]));
	}
	hires_backend_begin(NULL);
}

/* A planar polygon has one affine inverse-depth function. Reuse it across
 * scanlines rather than dividing between the same two interpolated edges. */
static legacy_s32 hires_visibility_polygon_plane(const struct HIRES_BACKEND_VERTEX *vertices,
												 legacy_u32 count, struct HIRES_DEPTH_PLANE *plane)
{
	const struct HIRES_BACKEND_VERTEX *origin = &vertices[0];
	legacy_f64 largest = 0;
	legacy_u32 chosen = 0;
	for (legacy_u32 index = 1; index + 1U < count; index++) {
		const struct HIRES_BACKEND_VERTEX *a = &vertices[index];
		const struct HIRES_BACKEND_VERTEX *b = &vertices[index + 1U];
		legacy_f64 area =
			(a->x - origin->x) * (b->y - origin->y) - (b->x - origin->x) * (a->y - origin->y);
		legacy_f64 absolute = area < 0 ? -area : area;
		if (absolute > largest) {
			largest = absolute;
			chosen = index;
		}
	}
	if (chosen == 0) {
		return 0;
	}
	const struct HIRES_BACKEND_VERTEX *a = &vertices[chosen];
	const struct HIRES_BACKEND_VERTEX *b = &vertices[chosen + 1U];
	legacy_f64 ax = a->x - origin->x, ay = a->y - origin->y;
	legacy_f64 bx = b->x - origin->x, by = b->y - origin->y;
	legacy_f64 az = a->inverse_z - origin->inverse_z, bz = b->inverse_z - origin->inverse_z;
	legacy_f64 reciprocal = 1.0 / (ax * by - bx * ay);
	plane->x_step = (az * by - bz * ay) * reciprocal;
	plane->y_step = (ax * bz - bx * az) * reciprocal;
	plane->origin = origin->inverse_z - origin->x * plane->x_step - origin->y * plane->y_step;
	for (legacy_u32 index = 0; index < count; index++) {
		const struct HIRES_BACKEND_VERTEX *point = &vertices[index];
		legacy_f64 error =
			plane->origin + point->x * plane->x_step + point->y * plane->y_step - point->inverse_z;
		legacy_f64 tolerance = point->inverse_z * HIRES_VISIBILITY_DEPTH_RELATIVE_EPSILON;
		if (!(error >= -tolerance && error <= tolerance)) {
			return 0;
		}
	}
	plane->origin += HIRES_VISIBILITY_HALF_PIXEL * (plane->x_step + plane->y_step);
	return 1;
}

void hires_backend_polygon_to(struct HIRES_BACKEND_BUFFER *buffer,
							  const struct HIRES_BACKEND_VERTEX *vertices, legacy_u32 count,
							  const struct HIRES_BACKEND_MATERIAL *material)
{
	if (buffer == NULL) {
		buffer = &hires_frame;
	}
	if (buffer->allocation_failed) {
		return;
	}
	if (count < 3U || count > HIRES_BACKEND_MAX_VERTICES ||
		buffer->command_count >= HIRES_VISIBILITY_NO_COMMAND ||
		buffer->edge_count > LEGACY_U32_MAX - count) {
		return;
	}
	/* Validate before any float-to-integer conversion. Screen clipping below
	 * bounds even extreme custom models without narrowing their coordinates. */
	for (legacy_u32 vertex = 0; vertex < count; vertex++) {
		const struct HIRES_BACKEND_VERTEX *point = &vertices[vertex];
		if (!(point->x >= -FLT_MAX && point->x <= FLT_MAX && point->y >= -FLT_MAX &&
			  point->y <= FLT_MAX && point->inverse_z > 0 && point->inverse_z <= FLT_MAX)) {
			return;
		}
	}
	buffer->edges =
		hires_visibility_reserve(buffer->edges, &buffer->edge_capacity, buffer->edge_count + count,
								 buffer == &hires_frame ? HIRES_VISIBILITY_INITIAL_EDGES
														: HIRES_VISIBILITY_INITIAL_BUILD_EDGES,
								 sizeof(*buffer->edges), &buffer->allocation_failed);
	if (buffer->allocation_failed) {
		return;
	}
	struct HIRES_VISIBILITY_COMMAND command = {*material, (legacy_u32)buffer->edge_count, 0,
											   HIRES_HEIGHT, 0};
	command.planar = hires_visibility_polygon_plane(vertices, count, &command.plane);
	const struct HIRES_BACKEND_VERTEX *previous = &vertices[count - 1U];
	for (legacy_u32 vertex = 0; vertex < count; vertex++) {
		const struct HIRES_BACKEND_VERTEX *current = &vertices[vertex];
		const struct HIRES_BACKEND_VERTEX *lower = previous->y < current->y ? previous : current;
		const struct HIRES_BACKEND_VERTEX *upper = previous->y < current->y ? current : previous;
		previous = current;
		if (!(lower->y < upper->y) || upper->y <= HIRES_VISIBILITY_HALF_PIXEL ||
			lower->y > HIRES_HEIGHT - HIRES_VISIBILITY_HALF_PIXEL) {
			continue;
		}
		legacy_s32 top = lower->y <= HIRES_VISIBILITY_HALF_PIXEL
							 ? 0
							 : hires_visibility_ceil(lower->y - HIRES_VISIBILITY_HALF_PIXEL);
		legacy_s32 bottom = upper->y >= HIRES_HEIGHT
								? HIRES_HEIGHT
								: hires_visibility_ceil(upper->y - HIRES_VISIBILITY_HALF_PIXEL);
		if (top >= bottom) {
			continue;
		}
		legacy_f64 reciprocal = 1.0 / (upper->y - lower->y);
		buffer->edges[buffer->edge_count++] =
			(struct HIRES_VISIBILITY_EDGE){lower->x,
										   lower->y,
										   lower->inverse_z,
										   (upper->x - lower->x) * reciprocal,
										   (upper->inverse_z - lower->inverse_z) * reciprocal,
										   top,
										   bottom};
		command.hires_edge_count++;
		if (top < command.top) {
			command.top = top;
		}
		if (bottom > command.bottom) {
			command.bottom = bottom;
		}
	}
	if (command.hires_edge_count < 2U) {
		buffer->edge_count = command.first_edge;
		return;
	}
	buffer->commands = hires_visibility_reserve(
		buffer->commands, &buffer->command_capacity, buffer->command_count + 1U,
		buffer == &hires_frame ? HIRES_VISIBILITY_INITIAL_COMMANDS
							   : HIRES_VISIBILITY_INITIAL_BUILD_COMMANDS,
		sizeof(*buffer->commands), &buffer->allocation_failed);
	if (buffer->allocation_failed) {
		return;
	}
	buffer->commands[buffer->command_count++] = command;
	if (buffer->command_area < HIRES_VISIBILITY_PARALLEL_MIN_AREA) {
		buffer->command_area += (legacy_u32)(command.bottom - command.top) * HIRES_WIDTH;
	}
}

void hires_backend_polygon(const struct HIRES_BACKEND_VERTEX *vertices, legacy_u32 count,
						   const struct HIRES_BACKEND_MATERIAL *material)
{
	hires_backend_polygon_to(NULL, vertices, count, material);
	if (hires_frame.allocation_failed) {
		fatal_error("Cannot allocate HyperVision command buffer");
	}
}

struct HIRES_BACKEND_BUILD {
	void (*job)(void *, legacy_s32, struct HIRES_BACKEND_BUFFER *);
	void *context;
};

static void hires_backend_build_job(void *argument, legacy_s32 index)
{
	const struct HIRES_BACKEND_BUILD *build = argument;
	build->job(build->context, index, &hires_build_buffers[index]);
}

static void hires_backend_merge_job(void *argument, legacy_s32 index)
{
	(void)argument;
	const struct HIRES_BACKEND_BUFFER *buffer = &hires_build_buffers[index];
	for (size_t command = 0; command < buffer->command_count; command++) {
		struct HIRES_VISIBILITY_COMMAND copy = buffer->commands[command];
		copy.first_edge += (legacy_u32)buffer->edge_offset;
		hires_frame.commands[buffer->command_offset + command] = copy;
	}
	if (buffer->edge_count != 0) {
		memcpy(hires_frame.edges + buffer->edge_offset, buffer->edges,
			   buffer->edge_count * sizeof(*buffer->edges));
	}
}

void hires_backend_build(legacy_s32 count,
						 void (*job)(void *, legacy_s32, struct HIRES_BACKEND_BUFFER *),
						 void *context)
{
	if (count <= 1 || hires_render_workers_count() == 0) {
		for (legacy_s32 index = 0; index < count; index++) {
			job(context, index, NULL);
		}
		if (hires_frame.allocation_failed) {
			fatal_error("Cannot allocate HyperVision command buffer");
		}
		return;
	}
	legacy_s32 failed = 0;
	size_t previous_capacity = hires_build_capacity;
	hires_build_buffers = hires_visibility_reserve(
		hires_build_buffers, &hires_build_capacity, (size_t)count,
		HIRES_VISIBILITY_INITIAL_BUILD_BUFFERS, sizeof(*hires_build_buffers), &failed);
	if (failed) {
		fatal_error("Cannot allocate HyperVision build buffers");
		return;
	}
	memset(hires_build_buffers + previous_capacity, 0,
		   (hires_build_capacity - previous_capacity) * sizeof(*hires_build_buffers));
	for (legacy_s32 index = 0; index < count; index++) {
		hires_backend_buffer_reset(&hires_build_buffers[index]);
	}
	struct HIRES_BACKEND_BUILD build = {job, context};
	hires_render_workers_run(count, hires_backend_build_job, &build);
	size_t commands = hires_frame.command_count;
	size_t edges = hires_frame.edge_count;
	for (legacy_s32 index = 0; index < count; index++) {
		struct HIRES_BACKEND_BUFFER *buffer = &hires_build_buffers[index];
		if (buffer->allocation_failed ||
			buffer->command_count > HIRES_VISIBILITY_NO_COMMAND - commands ||
			buffer->edge_count > LEGACY_U32_MAX - edges) {
			fatal_error("Cannot allocate HyperVision command buffer");
			return;
		}
		buffer->command_offset = commands;
		buffer->edge_offset = edges;
		commands += buffer->command_count;
		edges += buffer->edge_count;
		if (hires_frame.command_area < HIRES_VISIBILITY_PARALLEL_MIN_AREA) {
			hires_frame.command_area += buffer->command_area;
		}
	}
	hires_frame.commands = hires_visibility_reserve(
		hires_frame.commands, &hires_frame.command_capacity, commands,
		HIRES_VISIBILITY_INITIAL_COMMANDS, sizeof(*hires_frame.commands), &failed);
	hires_frame.edges = hires_visibility_reserve(hires_frame.edges, &hires_frame.edge_capacity,
												 edges, HIRES_VISIBILITY_INITIAL_EDGES,
												 sizeof(*hires_frame.edges), &failed);
	if (failed) {
		fatal_error("Cannot allocate HyperVision command buffer");
		return;
	}
	/* Prefix offsets preserve authored primitive order while independent workers
	 * copy commands and edges into disjoint output ranges. */
	hires_render_workers_run(count, hires_backend_merge_job, NULL);
	hires_frame.command_count = commands;
	hires_frame.edge_count = edges;
}

static legacy_f64 hires_visibility_depth(const struct HIRES_VISIBILITY_SPAN *span, legacy_s32 x)
{
	if (span->command == HIRES_VISIBILITY_NO_COMMAND) {
		return -DBL_MAX;
	}
	const struct HIRES_BACKEND_MATERIAL *material = &hires_frame.commands[span->command].material;
	if ((material->flags & HIRES_BACKEND_BACKGROUND) != 0U) {
		return 0;
	}
	legacy_f64 depth = span->origin + span->step * x;
	return (material->flags & HIRES_BACKEND_DECAL) != 0U
			   ? depth * HIRES_VISIBILITY_DECAL_DEPTH_SCALE
			   : depth;
}

static void hires_visibility_append(struct HIRES_VISIBILITY_SPAN *output, legacy_s32 *count,
									const struct HIRES_VISIBILITY_SPAN *source, legacy_s32 left,
									legacy_s32 right)
{
	if (left >= right) {
		return;
	}
	if (*count != 0) {
		struct HIRES_VISIBILITY_SPAN *previous = &output[*count - 1];
		if (previous->right == left && previous->command == source->command &&
			previous->origin == source->origin && previous->step == source->step) {
			previous->right = right;
			return;
		}
	}
	output[*count] = *source;
	output[*count].left = left;
	output[(*count)++].right = right;
}

/* Visibility is an X-ordered partition, not a painter queue. Two linear depth
 * functions cross at most once, so an overlap needs only its endpoint tests. */
static void hires_visibility_insert(struct HIRES_VISIBILITY_SPAN *visible, legacy_s32 *count,
									struct HIRES_VISIBILITY_SPAN *scratch,
									const struct HIRES_VISIBILITY_SPAN *incoming, legacy_s32 y)
{
	const struct HIRES_BACKEND_MATERIAL *material =
		&hires_frame.commands[incoming->command].material;
	legacy_s32 first = 0;
	legacy_s32 end = *count;
	while (first < end) {
		legacy_s32 middle = first + (end - first) / 2;
		if (visible[middle].right <= incoming->left) {
			first = middle + 1;
		} else {
			end = middle;
		}
	}
	legacy_s32 last = first;
	legacy_s32 replacement = 0;
	hires_visibility_append(scratch, &replacement, &visible[first], visible[first].left,
							incoming->left);
	while (last < *count && visible[last].left < incoming->right) {
		const struct HIRES_VISIBILITY_SPAN *old = &visible[last];
		legacy_s32 left = old->left > incoming->left ? old->left : incoming->left;
		legacy_s32 right = old->right < incoming->right ? old->right : incoming->right;
		if ((material->flags & HIRES_BACKEND_DECAL) != 0U &&
			old->command != HIRES_VISIBILITY_NO_COMMAND &&
			material->family != HIRES_DEPTH_FAMILY_NONE &&
			material->family == hires_frame.commands[old->command].material.family) {
			/* An authored decal is a material layer on its support. Keep that
			 * support's depth for other shapes and shadows, independent of the
			 * decal's deliberately offset vertices. */
			struct HIRES_VISIBILITY_SPAN supported = *incoming;
			supported.origin = old->origin;
			supported.step = old->step;
			hires_visibility_append(scratch, &replacement, &supported, left, right);
			last++;
			continue;
		}
		/* Independently clipped coplanar edges can differ by a few depth ULPs.
		 * Treat those surfaces as tied across the overlap instead of amplifying
		 * a near-zero slope into alternating one-row visibility fragments. */
		legacy_f64 first_depth = hires_visibility_depth(incoming, left);
		legacy_f64 last_depth = hires_visibility_depth(incoming, right - 1);
		legacy_f64 old_first = hires_visibility_depth(old, left);
		legacy_f64 old_last = hires_visibility_depth(old, right - 1);
		if (hires_ground_enabled && (material->flags & HIRES_BACKEND_GROUND_CLIP) != 0U &&
			(old->command == HIRES_VISIBILITY_NO_COMMAND ||
			 (hires_frame.commands[old->command].material.flags & HIRES_BACKEND_BACKGROUND) !=
				 0U)) {
			old_first = hires_ground.origin + hires_ground.y_step * y + hires_ground.x_step * left;
			old_last =
				hires_ground.origin + hires_ground.y_step * y + hires_ground.x_step * (right - 1);
		}
		legacy_f64 start = first_depth * (1 + HIRES_VISIBILITY_DEPTH_RELATIVE_EPSILON) - old_first;
		legacy_f64 finish = last_depth * (1 + HIRES_VISIBILITY_DEPTH_RELATIVE_EPSILON) - old_last;
		if (start >= 0 && finish >= 0) {
			hires_visibility_append(scratch, &replacement, incoming, left, right);
		} else if (start < 0 && finish < 0) {
			hires_visibility_append(scratch, &replacement, old, left, right);
		} else {
			legacy_f64 crossing = left - start * (right - left - 1) / (finish - start);
			legacy_s32 split = hires_visibility_ceil(crossing);
			if (start >= 0 && split == crossing) {
				split++;
			}
			if (split < left) {
				split = left;
			} else if (split > right) {
				split = right;
			}
			hires_visibility_append(scratch, &replacement, start >= 0 ? incoming : old, left,
									split);
			hires_visibility_append(scratch, &replacement, start >= 0 ? old : incoming, split,
									right);
		}
		last++;
	}
	hires_visibility_append(scratch, &replacement, &visible[last - 1], incoming->right,
							visible[last - 1].right);
	memmove(visible + first + replacement, visible + last,
			(size_t)(*count - last) * sizeof(*visible));
	memcpy(visible + first, scratch, (size_t)replacement * sizeof(*visible));
	*count += replacement - (last - first);
}

static void hires_visibility_sample_span(struct HIRES_RASTER_CONTEXT *context,
										 const struct HIRES_VISIBILITY_SPAN *span, legacy_s32 y)
{
	const struct HIRES_BACKEND_MATERIAL *material = &hires_frame.commands[span->command].material;
	legacy_f64 scale =
		(material->flags & HIRES_BACKEND_DECAL) != 0U ? HIRES_VISIBILITY_DECAL_DEPTH_SCALE : 1;
	if (hires_ground_enabled && (material->flags & HIRES_BACKEND_GROUND_CLIP) != 0U) {
		legacy_f64 depth = (span->origin + span->step * span->left) * scale;
		for (legacy_s32 x = span->left; x < span->right; x++, depth += span->step * scale) {
			legacy_u8 color = (legacy_u8)material->color;
			if (material->mode != HIRES_PAINT_SOLID) {
				legacy_u32 bit = ((y & HIRES_PATTERN_ROW_MASK) == 0 ? HIRES_PATTERN_WIDTH : 0U) +
								 HIRES_PATTERN_WIDTH - 1U - (x & HIRES_PATTERN_COLUMN_MASK);
				if ((material->pattern & (1U << bit)) != 0U) {
					if (material->mode == HIRES_PAINT_ALTERNATE) {
						color = (legacy_u8)material->alternate;
					}
				} else if (material->mode == HIRES_PAINT_PATTERN) {
					continue;
				}
			}
			hires_coverage_pixel(context, x, y, depth, material->family,
								 (material->flags & HIRES_BACKEND_DECAL) != 0U
									 ? HIRES_DEPTH_ATTACHED
									 : HIRES_DEPTH_SURFACE,
								 color, &hires_ground);
		}
		return;
	}

	hires_raster_span(
		context, span->left, span->right, y, (span->origin + span->step * span->left) * scale,
		span->step * scale, material->family,
		(material->flags & HIRES_BACKEND_DECAL) != 0U ? HIRES_DEPTH_ATTACHED : HIRES_DEPTH_SURFACE,
		material->color, material->alternate, material->pattern, material->mode,
		(material->flags & HIRES_BACKEND_BACKGROUND) == 0U);
}

static void hires_visibility_scan_command(struct HIRES_VISIBILITY_BIN *bin,
										  legacy_u32 command_index, legacy_s32 y,
										  struct HIRES_VISIBILITY_SPAN *visible,
										  legacy_s32 *visible_count,
										  struct HIRES_VISIBILITY_SPAN *scratch, legacy_s32 sampled)
{
	const struct HIRES_VISIBILITY_COMMAND *command = &hires_frame.commands[command_index];
	struct HIRES_VISIBILITY_CROSSING crossings[HIRES_BACKEND_MAX_VERTICES];
	legacy_u32 crossing_count = 0;
	for (legacy_u32 index = 0; index < command->hires_edge_count; index++) {
		const struct HIRES_VISIBILITY_EDGE *edge = &hires_frame.edges[command->first_edge + index];
		if (y < edge->top || y >= edge->bottom) {
			continue;
		}
		legacy_f64 distance = y + HIRES_VISIBILITY_HALF_PIXEL - edge->y;
		struct HIRES_VISIBILITY_CROSSING crossing = {
			edge->x + hires_visibility_binary64(distance * edge->x_step),
			command->planar ? 0 : edge->z + hires_visibility_binary64(distance * edge->z_step)};
		legacy_u32 insertion = crossing_count++;
		while (insertion != 0 && crossings[insertion - 1U].x > crossing.x) {
			crossings[insertion] = crossings[insertion - 1U];
			insertion--;
		}
		crossings[insertion] = crossing;
	}
	const struct HIRES_RASTER_TARGET *target = bin->context.target;
	for (legacy_u32 index = 0; index + 1U < crossing_count; index += 2U) {
		const struct HIRES_VISIBILITY_CROSSING *first = &crossings[index];
		const struct HIRES_VISIBILITY_CROSSING *last = &crossings[index + 1U];
		if (!(first->x < last->x) || first->x > target->right - HIRES_VISIBILITY_HALF_PIXEL ||
			last->x <= target->left + HIRES_VISIBILITY_HALF_PIXEL) {
			continue;
		}
		legacy_s32 left = first->x <= target->left + HIRES_VISIBILITY_HALF_PIXEL
							  ? target->left
							  : hires_visibility_ceil(first->x - HIRES_VISIBILITY_HALF_PIXEL);
		legacy_s32 right = last->x >= target->right
							   ? target->right
							   : hires_visibility_ceil(last->x - HIRES_VISIBILITY_HALF_PIXEL);
		if (left >= right) {
			continue;
		}
		legacy_f64 step =
			command->planar ? command->plane.x_step : (last->z - first->z) / (last->x - first->x);
		legacy_f64 origin = command->planar
								? command->plane.origin + y * command->plane.y_step
								: first->z + (HIRES_VISIBILITY_HALF_PIXEL - first->x) * step;
		struct HIRES_VISIBILITY_SPAN span = {left, right, origin, step, command_index};

		if (span.left >= span.right) {
			continue;
		}
		if (sampled) {
			hires_visibility_sample_span(&bin->context, &span, y);
		} else {
			hires_visibility_insert(visible, visible_count, scratch, &span, y);
		}
	}
}

static void hires_visibility_draw_band(void *argument, legacy_s32 band)
{
	(void)argument;
	struct HIRES_VISIBILITY_BIN *bin = &hires_bins[band];
	struct HIRES_VISIBILITY_SPAN visible[HIRES_VISIBILITY_SPAN_CAPACITY];
	struct HIRES_VISIBILITY_SPAN scratch[HIRES_VISIBILITY_SPAN_CAPACITY];
	for (legacy_s32 y = bin->context.top; y < bin->context.bottom; y++) {
		legacy_s32 count = 1;
		visible[0] =
			(struct HIRES_VISIBILITY_SPAN){bin->context.target->left, bin->context.target->right, 0,
										   0, HIRES_VISIBILITY_NO_COMMAND};
		for (size_t index = 0; index < bin->count; index++) {
			legacy_u32 entry = bin->hires_commands[index];
			const struct HIRES_VISIBILITY_COMMAND *command = &hires_frame.commands[entry];
			if (y < command->top || y >= command->bottom ||
				(command->material.mode == HIRES_PAINT_PATTERN ||
				 (command->material.flags & (HIRES_BACKEND_DECAL | HIRES_BACKEND_GROUND_CLIP)) !=
					 0U)) {
				continue;
			}
			hires_visibility_scan_command(bin, entry, y, visible, &count, scratch,
										  hires_incremental);
		}
		/* Solid detail layers share the same visibility partition. Most road
		 * markings and line quads therefore need no per-sample depth checks. */
		for (size_t index = 0; index < bin->count; index++) {
			legacy_u32 entry = bin->hires_commands[index];
			const struct HIRES_VISIBILITY_COMMAND *command = &hires_frame.commands[entry];
			if (command->material.mode != HIRES_PAINT_PATTERN &&
				(command->material.flags & (HIRES_BACKEND_DECAL | HIRES_BACKEND_GROUND_CLIP)) !=
					0U &&
				y >= command->top && y < command->bottom) {
				hires_visibility_scan_command(bin, entry, y, visible, &count, scratch,
											  hires_incremental);
			}
		}
		if (!hires_incremental) {
			for (legacy_s32 index = 0; index < count; index++) {
				const struct HIRES_VISIBILITY_SPAN *span = &visible[index];
				if (span->command == HIRES_VISIBILITY_NO_COMMAND) {
					continue;
				}
				const struct HIRES_BACKEND_MATERIAL *material =
					&hires_frame.commands[span->command].material;
				hires_raster_resolved_span(
					&bin->context, span->left, span->right, y,
					span->origin + span->step * span->left, span->step,
					(material->flags & HIRES_BACKEND_BACKGROUND) != 0U ? 0U : material->family,
					material->color, material->alternate, material->pattern, material->mode);
			}
		}
		for (size_t index = 0; index < bin->count; index++) {
			legacy_u32 entry = bin->hires_commands[index];
			const struct HIRES_VISIBILITY_COMMAND *command = &hires_frame.commands[entry];
			if (command->material.mode == HIRES_PAINT_PATTERN && y >= command->top &&
				y < command->bottom) {
				hires_visibility_scan_command(bin, entry, y, visible, &count, scratch, 1);
			}
		}
	}
}

legacy_s32 hires_backend_end(const struct HIRES_RASTER_TARGET *target, legacy_s32 preserve_depth)
{
	if (hires_frame.allocation_failed) {
		fatal_error("Cannot allocate HyperVision command buffer");
		return 0;
	}
	if (hires_frame.command_count == 0 || target->left >= target->right ||
		target->top >= target->bottom) {
		return 0;
	}
	hires_incremental = preserve_depth;
	legacy_s32 band_count =
		(target->height + HIRES_VISIBILITY_BAND_HEIGHT - 1) / HIRES_VISIBILITY_BAND_HEIGHT;
	for (legacy_s32 band = 0; band < band_count; band++) {
		struct HIRES_VISIBILITY_BIN *bin = &hires_bins[band];
		bin->count = 0;
		bin->context = (struct HIRES_RASTER_CONTEXT){target, band * HIRES_VISIBILITY_BAND_HEIGHT,
													 (band + 1) * HIRES_VISIBILITY_BAND_HEIGHT, 0};
		if (bin->context.top < target->top) {
			bin->context.top = target->top;
		}
		if (bin->context.bottom > target->bottom) {
			bin->context.bottom = target->bottom;
		}
	}
	for (size_t index = 0; index < hires_frame.command_count; index++) {
		const struct HIRES_VISIBILITY_COMMAND *command = &hires_frame.commands[index];
		legacy_s32 first = command->top / HIRES_VISIBILITY_BAND_HEIGHT;
		legacy_s32 last = (command->bottom - 1) / HIRES_VISIBILITY_BAND_HEIGHT;
		if (last >= band_count) {
			last = band_count - 1;
		}
		for (legacy_s32 band = first; band <= last; band++) {
			struct HIRES_VISIBILITY_BIN *bin = &hires_bins[band];
			bin->hires_commands = hires_visibility_reserve(
				bin->hires_commands, &bin->capacity, bin->count + 1U, HIRES_VISIBILITY_INITIAL_BIN,
				sizeof(*bin->hires_commands), &hires_frame.allocation_failed);
			if (hires_frame.allocation_failed) {
				fatal_error("Cannot allocate HyperVision visibility bins");
				return 0;
			}
			bin->hires_commands[bin->count++] = (legacy_u32)index;
		}
	}
	legacy_s32 workers = 0;
	if (hires_frame.command_area >= HIRES_VISIBILITY_PARALLEL_MIN_AREA &&
		hires_render_workers_count() != 0) {
		workers = hires_render_workers_run(band_count, hires_visibility_draw_band, NULL);
	} else {
		for (legacy_s32 band = 0; band < band_count; band++) {
			hires_visibility_draw_band(NULL, band);
		}
	}
	legacy_u32 cleared = 0;
	for (legacy_s32 band = 0; band < band_count; band++) {
		cleared += hires_bins[band].context.cleared_argb_cells;
	}
	hires_raster_finish(target, cleared);
	hires_frame.command_count = 0;
	hires_frame.edge_count = 0;
	return workers;
}
