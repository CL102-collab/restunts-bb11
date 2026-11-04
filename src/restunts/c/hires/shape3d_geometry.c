#include "shape3d_queue.h"

#if defined(RESTUNTS_SDL3)
#include "../fatal.h"
#include "../car/car_model.h"
#include "../shape3d/shape3d_pipeline_internal.h"
#include <stdlib.h>
#include <string.h>

RESTUNTS_RENDER_THREAD_LOCAL struct SHAPE3D_HIRES_STATE *shape3d_hires_thread_state;

#define HIRES_GEOMETRY_PARALLEL_MERGE_MIN_PRIMITIVES 128U

struct HIRES_GEOMETRY_STATE {
	struct SHAPE3D_HIRES_STATE scene;
	legacy_u32 primitive_offset, shadow_offset;
};

struct HIRES_GEOMETRY_JOBS {
	struct HIRES_GEOMETRY_STATE *states;
	const struct SHAPE3D_HIRES_STATE *source;
	void (*job)(void *, legacy_s32);
	void *context;
};

struct HIRES_GEOMETRY_MERGE {
	struct SHAPE3D_HIRES_STATE *destination;
	legacy_s32 first;
	legacy_s8 material;
};

static struct HIRES_GEOMETRY_STATE *hires_geometry_states;
static legacy_s32 hires_geometry_capacity;

void shape3d_hires_geometry_error(const legacy_char *message)
{
	if (shape3d_hires_thread_state != NULL) {
		shape3d_hires_thread_state->allocation_error = message;
	} else {
		fatal_error(message);
	}
}

struct RECTANGLE *shape3d_hires_geometry_rectangle(struct RECTANGLE *rectangle)
{
	if (shape3d_hires_thread_state == NULL || rectangle == NULL) {
		return rectangle;
	}
	struct SHAPE3D_HIRES_STATE *state = shape3d_hires_thread_state;
	for (legacy_u32 index = 0; index < state->rectangle_count; index++) {
		if (state->rectangles[index].destination == rectangle) {
			return &state->rectangles[index].value;
		}
	}
	if (state->rectangle_count == HIRES_GEOMETRY_RECTANGLE_COUNT) {
		shape3d_hires_geometry_error("HyperVision geometry job has too many bounding rectangles");
		return &state->rectangles[0].value;
	}
	struct HIRES_GEOMETRY_RECTANGLE *entry = &state->rectangles[state->rectangle_count++];
	entry->destination = rectangle;
	entry->value = *rectangle;
	return &entry->value;
}

void shape3d_hires_geometry_shutdown(void)
{
	for (legacy_s32 index = 0; index < hires_geometry_capacity; index++) {
		struct SHAPE3D_HIRES_STATE *state = &hires_geometry_states[index].scene;
		free(state->primitives);
		free(state->shapes);
		free(state->scene);
		free(state->shadow_surfaces);
		free(state->shadow_grille_hits);
	}
	free(hires_geometry_states);
	hires_geometry_states = NULL;
	hires_geometry_capacity = 0;
}

static void hires_geometry_build(void *argument, legacy_s32 index)
{
	const struct HIRES_GEOMETRY_JOBS *jobs = argument;
	struct SHAPE3D_HIRES_STATE *state = &jobs->states[index].scene;
	/* Retain each job's owned arrays while refreshing the immutable camera,
	 * light and car-shadow snapshot for this frame. */
	struct SHAPE3D_HIRES_STATE previous = *state;
	*state = *jobs->source;
	state->primitives = previous.primitives;
	state->shapes = previous.shapes;
	state->primitive_capacity = previous.primitive_capacity;
	state->primitive_count = 0;
	state->scene = previous.scene;
	state->scene_capacity = previous.scene_capacity;
	state->scene_count = 0;
	state->shadow_surfaces = previous.shadow_surfaces;
	state->shadow_grille_hits = previous.shadow_grille_hits;
	state->shadow_surface_capacity = previous.shadow_surface_capacity;
	state->shadow_surface_count = 0;
	state->allocation_error = NULL;
	state->rectangle_count = 0;
	state->current_shape = 0;
	state->current_family = 1;
	struct SHAPE3D_HIRES_STATE *prior_context = shape3d_hires_thread_state;
	shape3d_hires_thread_state = state;
	jobs->job(jobs->context, index);
	shape3d_hires_thread_state = prior_context;
}

static legacy_s32 hires_geometry_merge_shadows(const struct SHAPE3D_HIRES_STATE *source)
{
	if (source->shadow_surface_count == 0) {
		return 1;
	}
	struct SHAPE3D_HIRES_STATE *destination = &shape3d_hires_state;
	if (source->shadow_surface_count > LEGACY_U32_MAX - destination->shadow_surface_count) {
		shape3d_hires_geometry_error("HyperVision shadow surfaces exceed addressable memory");
		return 0;
	}
	size_t count = destination->shadow_surface_count + source->shadow_surface_count;
	if (count > destination->shadow_surface_capacity) {
		size_t capacity = destination->shadow_surface_capacity != 0
							  ? destination->shadow_surface_capacity
							  : HIRES_SHADOW_SURFACE_INITIAL_CAPACITY;
		while (capacity < count) {
			if (capacity > LEGACY_U32_MAX / 2U) {
				shape3d_hires_geometry_error(
					"HyperVision shadow surfaces exceed addressable memory");
				return 0;
			}
			capacity *= 2U;
		}
		if (capacity > (size_t)-1 / sizeof(*destination->shadow_surfaces) ||
			capacity > (size_t)-1 / (HIRES_SHADOW_SURFACE_MAX_HITS *
									 sizeof(*destination->shadow_grille_hits))) {
			shape3d_hires_geometry_error("HyperVision shadow surfaces exceed addressable memory");
			return 0;
		}
		struct SHADOW_SURFACE *surfaces = malloc(capacity * sizeof(*surfaces));
		legacy_f64 *hits = malloc(capacity * HIRES_SHADOW_SURFACE_MAX_HITS * sizeof(*hits));
		if (surfaces == NULL || hits == NULL) {
			free(surfaces);
			free(hits);
			shape3d_hires_geometry_error("Cannot allocate HyperVision shadow surfaces");
			return 0;
		}
		if (destination->shadow_surface_count != 0) {
			memcpy(surfaces, destination->shadow_surfaces,
				   destination->shadow_surface_count * sizeof(*surfaces));
		}
		free(destination->shadow_surfaces);
		free(destination->shadow_grille_hits);
		destination->shadow_surfaces = surfaces;
		destination->shadow_grille_hits = hits;
		destination->shadow_surface_capacity = capacity;
	}
	memcpy(destination->shadow_surfaces + destination->shadow_surface_count,
		   source->shadow_surfaces,
		   source->shadow_surface_count * sizeof(*source->shadow_surfaces));
	destination->shadow_surface_count = count;
	for (legacy_s32 car = 0; car < destination->car_shadow_count; car++) {
		if (source->car_shadows[car].surface_top > destination->car_shadows[car].surface_top) {
			destination->car_shadows[car].surface_top = source->car_shadows[car].surface_top;
		}
		if (source->car_shadows[car].grille_top > destination->car_shadows[car].grille_top) {
			destination->car_shadows[car].grille_top = source->car_shadows[car].grille_top;
		}
	}
	return 1;
}

static void hires_geometry_merge_rectangles(const struct SHAPE3D_HIRES_STATE *source)
{
	for (legacy_u32 index = 0; index < source->rectangle_count; index++) {
		const struct HIRES_GEOMETRY_RECTANGLE *entry = &source->rectangles[index];
		struct RECTANGLE *destination = entry->destination;
		if (entry->value.left < destination->left) {
			destination->left = entry->value.left;
		}
		if (entry->value.right > destination->right) {
			destination->right = entry->value.right;
		}
		if (entry->value.top < destination->top) {
			destination->top = entry->value.top;
		}
		if (entry->value.bottom > destination->bottom) {
			destination->bottom = entry->value.bottom;
		}
	}
}

static void hires_geometry_copy(void *argument, legacy_s32 index)
{
	const struct HIRES_GEOMETRY_MERGE *merge = argument;
	const struct HIRES_GEOMETRY_STATE *job = &hires_geometry_states[merge->first + index];
	const struct SHAPE3D_HIRES_STATE *source = &job->scene;
	struct SHAPE3D_HIRES_STATE *destination = merge->destination;
	legacy_u32 first = job->primitive_offset;
	if (source->scene_count == 0) {
		return;
	}
	memcpy(destination->scene + first, source->scene,
		   (size_t)source->scene_count * sizeof(*source->scene));
	for (legacy_u32 index = first; index < first + source->scene_count; index++) {
		if (destination->scene[index].material == BACKLIGHT_PAINT_DEFAULT) {
			destination->scene[index].material = merge->material;
		}
	}
	memcpy(destination->shapes + first, source->shapes,
		   (size_t)source->scene_count * sizeof(*source->shapes));
	for (legacy_u32 index = 0; index < source->scene_count; index++) {
		struct HIRES_PRIMITIVE *primitive = &destination->primitives[first + index];
		const struct HIRES_PRIMITIVE *incoming = &source->primitives[index];
		/* Clipped polygons usually use only three or four of the twenty slots.
		 * Copy live vertices and metadata without streaming unused point storage. */
		memcpy(primitive->points, incoming->points, incoming->count * sizeof(*incoming->points));
		memcpy(&primitive->count, &incoming->count,
			   sizeof(*primitive) - offsetof(struct HIRES_PRIMITIVE, count));
		primitive->shape += first;
		primitive->family += first;
		if (primitive->shadow_surface != 0) {
			primitive->shadow_surface += job->shadow_offset;
		}
	}
}

static legacy_s32 hires_geometry_merge(legacy_s32 first, legacy_s32 end)
{
	struct SHAPE3D_HIRES_STATE *destination = &shape3d_hires_state;
	legacy_u32 original_count = destination->scene_count;
	legacy_u32 count = original_count;
	legacy_s32 last_nonempty = first;
	/* Compute prefixes and reserve before workers write disjoint ranges. Bounds
	 * and shadow reductions stay ordered and are visible at each serial join. */
	for (legacy_s32 index = first; index < end; index++) {
		struct HIRES_GEOMETRY_STATE *job = &hires_geometry_states[index];
		const struct SHAPE3D_HIRES_STATE *source = &job->scene;
		hires_geometry_merge_rectangles(source);
		job->primitive_offset = count;
		job->shadow_offset = (legacy_u32)destination->shadow_surface_count;
		if (!hires_geometry_merge_shadows(source)) {
			return 0;
		}
		if (source->scene_count > LEGACY_U32_MAX - count) {
			shape3d_hires_geometry_error("HyperVision scene has too many primitives");
			return 0;
		}
		if (source->scene_count != 0) {
			count += source->scene_count;
			last_nonempty = index;
		}
	}
	if (count == original_count) {
		return 1;
	}
	if (!shape3d_hires_reserve_primitives(count - 1U)) {
		return 0;
	}
	destination->scene_count = count - 1U;
	legacy_s32 reserved = hires_backend_scene_reserve();
	destination->scene_count = original_count;
	if (!reserved) {
		return 0;
	}
	struct HIRES_GEOMETRY_MERGE merge = {destination, first, backlights_paint_override};
	if (count - original_count >= HIRES_GEOMETRY_PARALLEL_MERGE_MIN_PRIMITIVES) {
		hires_render_workers_run(end - first, hires_geometry_copy, &merge);
	} else {
		for (legacy_s32 index = first; index < end; index++) {
			hires_geometry_copy(&merge, index - first);
		}
	}
	const struct HIRES_GEOMETRY_STATE *last = &hires_geometry_states[last_nonempty];
	destination->scene_count = count;
	destination->primitive_count = count;
	destination->current_shape = last->primitive_offset + last->scene.current_shape;
	destination->current_family = last->primitive_offset + last->scene.current_family;
	return 1;
}

void shape3d_hires_geometry_run(legacy_s32 count, void (*job)(void *, legacy_s32),
								void (*joined)(void *, legacy_s32),
								legacy_s32 (*join_required)(void *, legacy_s32), void *context)
{
	if (count <= 1 || hires_render_workers_count() == 0) {
		for (legacy_s32 index = 0; index < count; index++) {
			job(context, index);
			if (joined != NULL &&
				(join_required == NULL || join_required(context, index) || index == count - 1)) {
				joined(context, index);
			}
		}
		return;
	}
	if (count > hires_geometry_capacity) {
		if ((size_t)count > (size_t)-1 / sizeof(*hires_geometry_states)) {
			fatal_error("HyperVision geometry jobs exceed addressable memory");
			return;
		}
		struct HIRES_GEOMETRY_STATE *states =
			realloc(hires_geometry_states, (size_t)count * sizeof(*states));
		if (states == NULL) {
			fatal_error("Cannot allocate HyperVision geometry jobs");
			return;
		}
		memset(states + hires_geometry_capacity, 0,
			   (size_t)(count - hires_geometry_capacity) * sizeof(*states));
		hires_geometry_states = states;
		hires_geometry_capacity = count;
	}
	struct HIRES_GEOMETRY_JOBS jobs = {hires_geometry_states, &shape3d_hires_state, job, context};
	hires_render_workers_run(count, hires_geometry_build, &jobs);
	/* Allocation failures are reported on the calling thread after every worker
	 * has joined, before exposing a partially assembled frame to serial callers. */
	for (legacy_s32 index = 0; index < count; index++) {
		if (hires_geometry_states[index].scene.allocation_error != NULL) {
			fatal_error(hires_geometry_states[index].scene.allocation_error);
			return;
		}
	}
	legacy_s32 first = 0;
	for (legacy_s32 index = 0; index < count; index++) {
		if (index + 1 < count && joined != NULL && join_required != NULL &&
			!join_required(context, index)) {
			continue;
		}
		if (index + 1 < count && joined == NULL) {
			continue;
		}
		if (!hires_geometry_merge(first, index + 1)) {
			return;
		}
		if (joined != NULL) {
			joined(context, index);
		}
		first = index + 1;
	}
}
#endif
