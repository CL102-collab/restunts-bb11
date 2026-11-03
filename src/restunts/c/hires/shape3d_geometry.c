#include "shape3d_queue.h"

#if defined(RESTUNTS_SDL3)
#include "../fatal.h"
#include "../car/car_model.h"
#include "../shape3d/shape3d_pipeline_internal.h"
#include <stdlib.h>
#include <string.h>

RESTUNTS_RENDER_THREAD_LOCAL struct SHAPE3D_HIRES_STATE *shape3d_hires_thread_state;

struct HIRES_GEOMETRY_JOBS {
	struct SHAPE3D_HIRES_STATE *states;
	const struct SHAPE3D_HIRES_STATE *source;
	void (*job)(void *, legacy_s32);
	void *context;
};

static struct SHAPE3D_HIRES_STATE *hires_geometry_states;
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
		struct SHAPE3D_HIRES_STATE *state = &hires_geometry_states[index];
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
	struct SHAPE3D_HIRES_STATE *state = &jobs->states[index];
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

static legacy_s32 hires_geometry_merge(const struct SHAPE3D_HIRES_STATE *source)
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
	legacy_u32 first = hires_backend_scene_count;
	legacy_u32 shadows = (legacy_u32)shape3d_hires_state.shadow_surface_count;
	if (!hires_geometry_merge_shadows(source)) {
		return 0;
	}
	if (source->scene_count == 0) {
		return 1;
	}
	if (source->scene_count > LEGACY_U32_MAX - first) {
		shape3d_hires_geometry_error("HyperVision scene has too many primitives");
		return 0;
	}
	legacy_u32 end = first + source->scene_count;
	if (!shape3d_hires_reserve_primitives(end - 1U)) {
		return 0;
	}
	hires_backend_scene_count = end - 1U;
	legacy_s32 reserved = hires_backend_scene_reserve();
	hires_backend_scene_count = first;
	if (!reserved) {
		return 0;
	}
	memcpy(hires_backend_scene + first, source->scene,
		   (size_t)source->scene_count * sizeof(*source->scene));
	for (legacy_u32 index = first; index < end; index++) {
		if (hires_backend_scene[index].material == BACKLIGHT_PAINT_DEFAULT) {
			hires_backend_scene[index].material = backlights_paint_override;
		}
	}
	memcpy(shape3d_hires_state.shapes + first, source->shapes,
		   (size_t)source->scene_count * sizeof(*source->shapes));
	for (legacy_u32 index = 0; index < source->scene_count; index++) {
		struct HIRES_PRIMITIVE *primitive = &shape3d_hires_state.primitives[first + index];
		const struct HIRES_PRIMITIVE *incoming = &source->primitives[index];
		/* Clipped polygons usually use only three or four of the twenty slots.
		 * Copy live vertices and metadata without streaming unused point storage. */
		memcpy(primitive->points, incoming->points, incoming->count * sizeof(*incoming->points));
		memcpy(&primitive->count, &incoming->count,
			   sizeof(*primitive) - offsetof(struct HIRES_PRIMITIVE, count));
		primitive->shape += first;
		primitive->family += first;
		if (primitive->shadow_surface != 0) {
			primitive->shadow_surface += shadows;
		}
	}
	hires_backend_scene_count = end;
	shape3d_hires_state.primitive_count = end;
	shape3d_hires_state.current_shape = first + source->current_shape;
	shape3d_hires_state.current_family = first + source->current_family;
	return 1;
}

void shape3d_hires_geometry_run(legacy_s32 count, void (*job)(void *, legacy_s32),
								void (*joined)(void *, legacy_s32), void *context)
{
	if (count <= 1 || hires_render_workers_count() == 0) {
		for (legacy_s32 index = 0; index < count; index++) {
			job(context, index);
			if (joined != NULL) {
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
		struct SHAPE3D_HIRES_STATE *states =
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
		if (hires_geometry_states[index].allocation_error != NULL) {
			fatal_error(hires_geometry_states[index].allocation_error);
			return;
		}
	}
	for (legacy_s32 index = 0; index < count; index++) {
		if (!hires_geometry_merge(&hires_geometry_states[index])) {
			return;
		}
		if (joined != NULL) {
			joined(context, index);
		}
	}
}
#endif
