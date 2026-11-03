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
#include "../math/math_internal.h"
#include "../car/car_model.h"
#include "../math/projection.h"
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

legacy_s32 hires_backend_scene_reserve(void)
{
	if (hires_backend_scene_count < shape3d_hires_state.scene_capacity) {
		return 1;
	}
	if (shape3d_hires_state.scene_capacity > LEGACY_U32_MAX / 2U) {
		shape3d_hires_geometry_error("HyperVision scene has too many primitives");
		return 0;
	}
	size_t capacity = shape3d_hires_state.scene_capacity != 0 ? shape3d_hires_state.scene_capacity
					  : shape3d_hires_thread_state != NULL	  ? HIRES_GEOMETRY_INITIAL_PRIMITIVES
															  : HIRES_INITIAL_PRIMITIVE_CAPACITY;
	while (capacity <= hires_backend_scene_count) {
		if (capacity > LEGACY_U32_MAX / 2U) {
			shape3d_hires_geometry_error("HyperVision scene has too many primitives");
			return 0;
		}
		capacity *= 2U;
	}
	if (capacity > LEGACY_U32_MAX || capacity > (size_t)-1 / sizeof(*hires_backend_scene)) {
		shape3d_hires_geometry_error("HyperVision scene exceeds addressable memory");
		return 0;
	}
	struct HIRES_BACKEND_SCENE_PRIMITIVE *scene =
		realloc(hires_backend_scene, capacity * sizeof(*hires_backend_scene));
	if (scene == NULL) {
		shape3d_hires_geometry_error("Cannot allocate HyperVision scene metadata");
		return 0;
	}
	hires_backend_scene = scene;
	shape3d_hires_state.scene_capacity = (legacy_u32)capacity;
	return 1;
}

void shape3d_hires_adjust_sort(legacy_u16 *sort_by_depth)
{
	/* HyperVision resolves visibility in its depth buffer. */
	if (hires_enabled()) {
		*sort_by_depth = 0;
	}
}
void shape3d_hires_scene_reset(void)
{
	hires_backend_scene_count = 0;
	shape3d_hires_state.allocation_error = NULL;
	shape3d_hires_reset();
}
#endif
