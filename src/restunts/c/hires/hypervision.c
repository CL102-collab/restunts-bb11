#include "hypervision.h"
#include "../fatal.h"
#include "render_workers.h"
#include <float.h>
#include <stdlib.h>
#include <string.h>

#define HV_BAND_HEIGHT 32
#define HV_BAND_COUNT ((HIRES_HEIGHT + HV_BAND_HEIGHT - 1) / HV_BAND_HEIGHT)
#define HV_INITIAL_COMMANDS 1024U
#define HV_INITIAL_EDGES (HV_INITIAL_COMMANDS * 4U)
#define HV_INITIAL_BIN 256U
#define HV_NO_COMMAND LEGACY_U32_MAX
#define HV_PARALLEL_MIN_AREA 262144U
/* A small relative offset replaces the old order-dependent decal families. */
#define HV_DECAL_DEPTH_SCALE 1.0001
#define HV_DEPTH_RELATIVE_EPSILON (4 * FLT_EPSILON)
#define HV_HALF_PIXEL HIRES_SAMPLE_CENTER_OFFSET
#define HV_SPAN_CAPACITY (HIRES_WIDTH + 2)
#define HV_EVALUATE_AS_DECLARED 0
#define HV_EVALUATE_AS_DOUBLE 1

struct HV_EDGE {
	legacy_f64 x, y, z, x_step, z_step;
	legacy_s32 top, bottom;
};
struct HV_COMMAND {
	struct HYPERVISION_MATERIAL material;
	legacy_u32 first_edge, edge_count;
	legacy_s32 top, bottom;
	struct HIRES_DEPTH_PLANE plane;
	legacy_s32 planar;
};
struct HV_BIN {
	legacy_u32 *commands;
	size_t count, capacity;
	struct HIRES_RASTER_CONTEXT context;
};
struct HV_SPAN {
	legacy_s32 left, right;
	legacy_f64 origin, step;
	legacy_u32 command;
};
struct HV_CROSSING {
	legacy_f64 x, z;
};

static struct HV_COMMAND *commands;
static struct HV_EDGE *edges;
static size_t command_count, command_capacity, edge_count, edge_capacity;
static struct HV_BIN bins[HV_BAND_COUNT];
static struct HIRES_DEPTH_PLANE ground;
static legacy_s32 ground_enabled, incremental;
static legacy_u32 command_area;
static void *hv_reserve(void *buffer, size_t *capacity, size_t required, size_t initial,
						size_t element_size)
{
	if (required <= *capacity) {
		return buffer;
	}
	size_t next = *capacity != 0 ? *capacity : initial;
	while (next < required) {
		if (next > (size_t)-1 / 2U) {
			fatal_error("HyperVision command buffer exceeds addressable memory");
			return buffer;
		}
		next *= 2U;
	}
	if (next > (size_t)-1 / element_size) {
		fatal_error("HyperVision command buffer exceeds addressable memory");
		return buffer;
	}
	void *result = realloc(buffer, next * element_size);
	if (result == NULL) {
		fatal_error("Cannot allocate HyperVision command buffer");
		return buffer;
	}
	*capacity = next;
	return result;
}

/* Round each scanline offset before adding the edge origin. Extended evaluation
 * must not move a crossing across an exact pixel center; ordinary assignments
 * can retain excess precision when the compiler uses its fast evaluation mode. */
static legacy_f64 hv_binary64(legacy_f64 value)
{
#if FLT_EVAL_METHOD != HV_EVALUATE_AS_DECLARED && FLT_EVAL_METHOD != HV_EVALUATE_AS_DOUBLE
	volatile legacy_f64 rounded = value;
	return rounded;
#else
	return value;
#endif
}

static legacy_s32 hv_ceil(legacy_f64 value)
{
	legacy_s32 result = (legacy_s32)value;
	return result < value ? result + 1 : result;
}

void hypervision_begin(const struct HIRES_DEPTH_PLANE *plane)
{
	command_count = 0;
	edge_count = 0;
	command_area = 0;
	ground_enabled = plane != NULL;
	if (plane != NULL) {
		ground = *plane;
	}
}

void hypervision_shutdown(void)
{
	free(commands);
	free(edges);
	commands = NULL;
	edges = NULL;
	command_capacity = 0;
	edge_capacity = 0;
	for (legacy_s32 band = 0; band < HV_BAND_COUNT; band++) {
		free(bins[band].commands);
		memset(&bins[band], 0, sizeof(bins[band]));
	}
	hypervision_begin(NULL);
}

/* A planar polygon has one affine inverse-depth function. Reuse it across
 * scanlines rather than dividing between the same two interpolated edges. */
static legacy_s32 hv_polygon_plane(const struct HYPERVISION_VERTEX *vertices, legacy_u32 count,
								   struct HIRES_DEPTH_PLANE *plane)
{
	const struct HYPERVISION_VERTEX *origin = &vertices[0];
	legacy_f64 largest = 0;
	legacy_u32 chosen = 0;
	for (legacy_u32 index = 1; index + 1U < count; index++) {
		const struct HYPERVISION_VERTEX *a = &vertices[index];
		const struct HYPERVISION_VERTEX *b = &vertices[index + 1U];
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
	const struct HYPERVISION_VERTEX *a = &vertices[chosen];
	const struct HYPERVISION_VERTEX *b = &vertices[chosen + 1U];
	legacy_f64 ax = a->x - origin->x, ay = a->y - origin->y;
	legacy_f64 bx = b->x - origin->x, by = b->y - origin->y;
	legacy_f64 az = a->inverse_z - origin->inverse_z, bz = b->inverse_z - origin->inverse_z;
	legacy_f64 reciprocal = 1.0 / (ax * by - bx * ay);
	plane->x_step = (az * by - bz * ay) * reciprocal;
	plane->y_step = (ax * bz - bx * az) * reciprocal;
	plane->origin = origin->inverse_z - origin->x * plane->x_step - origin->y * plane->y_step;
	for (legacy_u32 index = 0; index < count; index++) {
		const struct HYPERVISION_VERTEX *point = &vertices[index];
		legacy_f64 error =
			plane->origin + point->x * plane->x_step + point->y * plane->y_step - point->inverse_z;
		legacy_f64 tolerance = point->inverse_z * HV_DEPTH_RELATIVE_EPSILON;
		if (!(error >= -tolerance && error <= tolerance)) {
			return 0;
		}
	}
	plane->origin += HV_HALF_PIXEL * (plane->x_step + plane->y_step);
	return 1;
}

void hypervision_polygon(const struct HYPERVISION_VERTEX *vertices, legacy_u32 count,
						 const struct HYPERVISION_MATERIAL *material)
{
	if (count < 3U || count > HYPERVISION_MAX_VERTICES || command_count >= HV_NO_COMMAND ||
		edge_count > LEGACY_U32_MAX - count) {
		return;
	}
	/* Validate before any float-to-integer conversion. Screen clipping below
	 * bounds even extreme custom models without narrowing their coordinates. */
	for (legacy_u32 vertex = 0; vertex < count; vertex++) {
		const struct HYPERVISION_VERTEX *point = &vertices[vertex];
		if (!(point->x >= -FLT_MAX && point->x <= FLT_MAX && point->y >= -FLT_MAX &&
			  point->y <= FLT_MAX && point->inverse_z > 0 && point->inverse_z <= FLT_MAX)) {
			return;
		}
	}
	edges = hv_reserve(edges, &edge_capacity, edge_count + count, HV_INITIAL_EDGES, sizeof(*edges));
	struct HV_COMMAND command = {*material, (legacy_u32)edge_count, 0, HIRES_HEIGHT, 0};
	command.planar = hv_polygon_plane(vertices, count, &command.plane);
	const struct HYPERVISION_VERTEX *previous = &vertices[count - 1U];
	for (legacy_u32 vertex = 0; vertex < count; vertex++) {
		const struct HYPERVISION_VERTEX *current = &vertices[vertex];
		const struct HYPERVISION_VERTEX *lower = previous->y < current->y ? previous : current;
		const struct HYPERVISION_VERTEX *upper = previous->y < current->y ? current : previous;
		previous = current;
		if (!(lower->y < upper->y) || upper->y <= HV_HALF_PIXEL ||
			lower->y > HIRES_HEIGHT - HV_HALF_PIXEL) {
			continue;
		}
		legacy_s32 top = lower->y <= HV_HALF_PIXEL ? 0 : hv_ceil(lower->y - HV_HALF_PIXEL);
		legacy_s32 bottom =
			upper->y >= HIRES_HEIGHT ? HIRES_HEIGHT : hv_ceil(upper->y - HV_HALF_PIXEL);
		if (top >= bottom) {
			continue;
		}
		legacy_f64 reciprocal = 1.0 / (upper->y - lower->y);
		edges[edge_count++] = (struct HV_EDGE){lower->x,
											   lower->y,
											   lower->inverse_z,
											   (upper->x - lower->x) * reciprocal,
											   (upper->inverse_z - lower->inverse_z) * reciprocal,
											   top,
											   bottom};
		command.edge_count++;
		if (top < command.top) {
			command.top = top;
		}
		if (bottom > command.bottom) {
			command.bottom = bottom;
		}
	}
	if (command.edge_count < 2U) {
		edge_count = command.first_edge;
		return;
	}
	commands = hv_reserve(commands, &command_capacity, command_count + 1U, HV_INITIAL_COMMANDS,
						  sizeof(*commands));
	commands[command_count++] = command;
	if (command_area < HV_PARALLEL_MIN_AREA) {
		command_area += (legacy_u32)(command.bottom - command.top) * HIRES_WIDTH;
	}
}

static legacy_f64 hv_depth(const struct HV_SPAN *span, legacy_s32 x)
{
	if (span->command == HV_NO_COMMAND) {
		return -DBL_MAX;
	}
	const struct HYPERVISION_MATERIAL *material = &commands[span->command].material;
	if ((material->flags & HYPERVISION_BACKGROUND) != 0U) {
		return 0;
	}
	legacy_f64 depth = span->origin + span->step * x;
	return (material->flags & HYPERVISION_DECAL) != 0U ? depth * HV_DECAL_DEPTH_SCALE : depth;
}

static void hv_append(struct HV_SPAN *output, legacy_s32 *count, const struct HV_SPAN *source,
					  legacy_s32 left, legacy_s32 right)
{
	if (left >= right) {
		return;
	}
	if (*count != 0) {
		struct HV_SPAN *previous = &output[*count - 1];
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
static void hv_insert(struct HV_SPAN *visible, legacy_s32 *count, struct HV_SPAN *scratch,
					  const struct HV_SPAN *incoming, legacy_s32 y)
{
	const struct HYPERVISION_MATERIAL *material = &commands[incoming->command].material;
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
	hv_append(scratch, &replacement, &visible[first], visible[first].left, incoming->left);
	while (last < *count && visible[last].left < incoming->right) {
		const struct HV_SPAN *old = &visible[last];
		legacy_s32 left = old->left > incoming->left ? old->left : incoming->left;
		legacy_s32 right = old->right < incoming->right ? old->right : incoming->right;
		if ((material->flags & HYPERVISION_DECAL) != 0U && old->command != HV_NO_COMMAND &&
			material->family != HIRES_DEPTH_FAMILY_NONE &&
			material->family == commands[old->command].material.family) {
			/* An authored decal is a material layer on its support. Keep that
			 * support's depth for other shapes and shadows, independent of the
			 * decal's deliberately offset vertices. */
			struct HV_SPAN supported = *incoming;
			supported.origin = old->origin;
			supported.step = old->step;
			hv_append(scratch, &replacement, &supported, left, right);
			last++;
			continue;
		}
		/* Independently clipped coplanar edges can differ by a few depth ULPs.
		 * Treat those surfaces as tied across the overlap instead of amplifying
		 * a near-zero slope into alternating one-row visibility fragments. */
		legacy_f64 first_depth = hv_depth(incoming, left);
		legacy_f64 last_depth = hv_depth(incoming, right - 1);
		legacy_f64 old_first = hv_depth(old, left);
		legacy_f64 old_last = hv_depth(old, right - 1);
		if (ground_enabled && (material->flags & HYPERVISION_GROUND_CLIP) != 0U &&
			(old->command == HV_NO_COMMAND ||
			 (commands[old->command].material.flags & HYPERVISION_BACKGROUND) != 0U)) {
			old_first = ground.origin + ground.y_step * y + ground.x_step * left;
			old_last = ground.origin + ground.y_step * y + ground.x_step * (right - 1);
		}
		legacy_f64 start = first_depth * (1 + HV_DEPTH_RELATIVE_EPSILON) - old_first;
		legacy_f64 finish = last_depth * (1 + HV_DEPTH_RELATIVE_EPSILON) - old_last;
		if (start >= 0 && finish >= 0) {
			hv_append(scratch, &replacement, incoming, left, right);
		} else if (start < 0 && finish < 0) {
			hv_append(scratch, &replacement, old, left, right);
		} else {
			legacy_f64 crossing = left - start * (right - left - 1) / (finish - start);
			legacy_s32 split = hv_ceil(crossing);
			if (start >= 0 && split == crossing) {
				split++;
			}
			if (split < left) {
				split = left;
			} else if (split > right) {
				split = right;
			}
			hv_append(scratch, &replacement, start >= 0 ? incoming : old, left, split);
			hv_append(scratch, &replacement, start >= 0 ? old : incoming, split, right);
		}
		last++;
	}
	hv_append(scratch, &replacement, &visible[last - 1], incoming->right, visible[last - 1].right);
	memmove(visible + first + replacement, visible + last,
			(size_t)(*count - last) * sizeof(*visible));
	memcpy(visible + first, scratch, (size_t)replacement * sizeof(*visible));
	*count += replacement - (last - first);
}

static void hv_sample_span(struct HIRES_RASTER_CONTEXT *context, const struct HV_SPAN *span,
						   legacy_s32 y)
{
	const struct HYPERVISION_MATERIAL *material = &commands[span->command].material;
	legacy_f64 scale = (material->flags & HYPERVISION_DECAL) != 0U ? HV_DECAL_DEPTH_SCALE : 1;
	if (ground_enabled && (material->flags & HYPERVISION_GROUND_CLIP) != 0U) {
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
								 (material->flags & HYPERVISION_DECAL) != 0U ? HIRES_DEPTH_ATTACHED
																			 : HIRES_DEPTH_SURFACE,
								 color, &ground);
		}
		return;
	}

	hires_raster_span(
		context, span->left, span->right, y, (span->origin + span->step * span->left) * scale,
		span->step * scale, material->family,
		(material->flags & HYPERVISION_DECAL) != 0U ? HIRES_DEPTH_ATTACHED : HIRES_DEPTH_SURFACE,
		material->color, material->alternate, material->pattern, material->mode,
		(material->flags & HYPERVISION_BACKGROUND) == 0U);
}

static void hv_scan_command(struct HV_BIN *bin, legacy_u32 command_index, legacy_s32 y,
							struct HV_SPAN *visible, legacy_s32 *visible_count,
							struct HV_SPAN *scratch, legacy_s32 sampled)
{
	const struct HV_COMMAND *command = &commands[command_index];
	struct HV_CROSSING crossings[HYPERVISION_MAX_VERTICES];
	legacy_u32 crossing_count = 0;
	for (legacy_u32 index = 0; index < command->edge_count; index++) {
		const struct HV_EDGE *edge = &edges[command->first_edge + index];
		if (y < edge->top || y >= edge->bottom) {
			continue;
		}
		legacy_f64 distance = y + HV_HALF_PIXEL - edge->y;
		struct HV_CROSSING crossing = {
			edge->x + hv_binary64(distance * edge->x_step),
			command->planar ? 0 : edge->z + hv_binary64(distance * edge->z_step)};
		legacy_u32 insertion = crossing_count++;
		while (insertion != 0 && crossings[insertion - 1U].x > crossing.x) {
			crossings[insertion] = crossings[insertion - 1U];
			insertion--;
		}
		crossings[insertion] = crossing;
	}
	const struct HIRES_RASTER_TARGET *target = bin->context.target;
	for (legacy_u32 index = 0; index + 1U < crossing_count; index += 2U) {
		const struct HV_CROSSING *first = &crossings[index];
		const struct HV_CROSSING *last = &crossings[index + 1U];
		if (!(first->x < last->x) || first->x > target->right - HV_HALF_PIXEL ||
			last->x <= target->left + HV_HALF_PIXEL) {
			continue;
		}
		legacy_s32 left = first->x <= target->left + HV_HALF_PIXEL
							  ? target->left
							  : hv_ceil(first->x - HV_HALF_PIXEL);
		legacy_s32 right =
			last->x >= target->right ? target->right : hv_ceil(last->x - HV_HALF_PIXEL);
		if (left >= right) {
			continue;
		}
		legacy_f64 step =
			command->planar ? command->plane.x_step : (last->z - first->z) / (last->x - first->x);
		legacy_f64 origin = command->planar ? command->plane.origin + y * command->plane.y_step
											: first->z + (HV_HALF_PIXEL - first->x) * step;
		struct HV_SPAN span = {left, right, origin, step, command_index};

		if (span.left >= span.right) {
			continue;
		}
		if (sampled) {
			hv_sample_span(&bin->context, &span, y);
		} else {
			hv_insert(visible, visible_count, scratch, &span, y);
		}
	}
}

static void hv_draw_band(void *argument, legacy_s32 band)
{
	(void)argument;
	struct HV_BIN *bin = &bins[band];
	struct HV_SPAN visible[HV_SPAN_CAPACITY];
	struct HV_SPAN scratch[HV_SPAN_CAPACITY];
	for (legacy_s32 y = bin->context.top; y < bin->context.bottom; y++) {
		legacy_s32 count = 1;
		visible[0] = (struct HV_SPAN){bin->context.target->left, bin->context.target->right, 0, 0,
									  HV_NO_COMMAND};
		for (size_t index = 0; index < bin->count; index++) {
			legacy_u32 entry = bin->commands[index];
			const struct HV_COMMAND *command = &commands[entry];
			if (y < command->top || y >= command->bottom ||
				(command->material.mode == HIRES_PAINT_PATTERN ||
				 (command->material.flags & (HYPERVISION_DECAL | HYPERVISION_GROUND_CLIP)) != 0U)) {
				continue;
			}
			hv_scan_command(bin, entry, y, visible, &count, scratch, incremental);
		}
		/* Solid detail layers share the same visibility partition. Most road
		 * markings and line quads therefore need no per-sample depth checks. */
		for (size_t index = 0; index < bin->count; index++) {
			legacy_u32 entry = bin->commands[index];
			const struct HV_COMMAND *command = &commands[entry];
			if (command->material.mode != HIRES_PAINT_PATTERN &&
				(command->material.flags & (HYPERVISION_DECAL | HYPERVISION_GROUND_CLIP)) != 0U &&
				y >= command->top && y < command->bottom) {
				hv_scan_command(bin, entry, y, visible, &count, scratch, incremental);
			}
		}
		if (!incremental) {
			for (legacy_s32 index = 0; index < count; index++) {
				const struct HV_SPAN *span = &visible[index];
				if (span->command == HV_NO_COMMAND) {
					continue;
				}
				const struct HYPERVISION_MATERIAL *material = &commands[span->command].material;
				hires_raster_resolved_span(
					&bin->context, span->left, span->right, y,
					span->origin + span->step * span->left, span->step,
					(material->flags & HYPERVISION_BACKGROUND) != 0U ? 0U : material->family,
					material->color, material->alternate, material->pattern, material->mode);
			}
		}
		for (size_t index = 0; index < bin->count; index++) {
			legacy_u32 entry = bin->commands[index];
			const struct HV_COMMAND *command = &commands[entry];
			if (command->material.mode == HIRES_PAINT_PATTERN && y >= command->top &&
				y < command->bottom) {
				hv_scan_command(bin, entry, y, visible, &count, scratch, 1);
			}
		}
	}
}

legacy_s32 hypervision_end(const struct HIRES_RASTER_TARGET *target, legacy_s32 preserve_depth)
{
	if (command_count == 0 || target->left >= target->right || target->top >= target->bottom) {
		return 0;
	}
	incremental = preserve_depth;
	legacy_s32 band_count = (target->height + HV_BAND_HEIGHT - 1) / HV_BAND_HEIGHT;
	for (legacy_s32 band = 0; band < band_count; band++) {
		struct HV_BIN *bin = &bins[band];
		bin->count = 0;
		bin->context = (struct HIRES_RASTER_CONTEXT){target, band * HV_BAND_HEIGHT,
													 (band + 1) * HV_BAND_HEIGHT, 0};
		if (bin->context.top < target->top) {
			bin->context.top = target->top;
		}
		if (bin->context.bottom > target->bottom) {
			bin->context.bottom = target->bottom;
		}
	}
	for (size_t index = 0; index < command_count; index++) {
		const struct HV_COMMAND *command = &commands[index];
		legacy_s32 first = command->top / HV_BAND_HEIGHT;
		legacy_s32 last = (command->bottom - 1) / HV_BAND_HEIGHT;
		if (last >= band_count) {
			last = band_count - 1;
		}
		for (legacy_s32 band = first; band <= last; band++) {
			struct HV_BIN *bin = &bins[band];
			bin->commands = hv_reserve(bin->commands, &bin->capacity, bin->count + 1U,
									   HV_INITIAL_BIN, sizeof(*bin->commands));
			bin->commands[bin->count++] = (legacy_u32)index;
		}
	}
	legacy_s32 workers = 0;
	if (command_area >= HV_PARALLEL_MIN_AREA && render_workers_count() != 0) {
		workers = render_workers_run(band_count, hv_draw_band, NULL);
	} else {
		for (legacy_s32 band = 0; band < band_count; band++) {
			hv_draw_band(NULL, band);
		}
	}
	legacy_u32 cleared = 0;
	for (legacy_s32 band = 0; band < band_count; band++) {
		cleared += bins[band].context.cleared_argb_cells;
	}
	hires_raster_finish(target, cleared);
	command_count = 0;
	edge_count = 0;
	return workers;
}
