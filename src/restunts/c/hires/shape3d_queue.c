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
struct HIRES_BACKEND_SCENE_PRIMITIVE *hires_backend_scene;
legacy_u32 hires_backend_scene_count;
static legacy_u32 hires_backend_scene_capacity;
#endif

#ifdef RESTUNTS_SDL3

void hires_backend_scene_reserve(void)
{
	if (hires_backend_scene_count < hires_backend_scene_capacity) {
		return;
	}
	if (hires_backend_scene_capacity > LEGACY_U32_MAX / 2U) {
		fatal_error("HyperVision scene has too many primitives");
		return;
	}
	size_t capacity = hires_backend_scene_capacity != 0 ? (size_t)hires_backend_scene_capacity * 2U
														: HIRES_INITIAL_PRIMITIVE_CAPACITY;
	if (capacity > LEGACY_U32_MAX || capacity > (size_t)-1 / sizeof(*hires_backend_scene)) {
		fatal_error("HyperVision scene exceeds addressable memory");
		return;
	}
	struct HIRES_BACKEND_SCENE_PRIMITIVE *scene =
		realloc(hires_backend_scene, capacity * sizeof(*hires_backend_scene));
	if (scene == NULL) {
		fatal_error("Cannot allocate HyperVision scene metadata");
		return;
	}
	hires_backend_scene = scene;
	hires_backend_scene_capacity = (legacy_u32)capacity;
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
	shape3d_hires_reset();
}
#endif
