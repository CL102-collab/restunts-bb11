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
#include "../math_internal.h"
#include "../car/car_model.h"
#include "../projection.h"
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
struct HYPERVISION_SCENE_PRIMITIVE *hypervision_scene;
legacy_u32 hypervision_scene_count;
static legacy_u32 hypervision_scene_capacity;
#endif

#ifdef RESTUNTS_SDL3

void hypervision_scene_reserve(void)
{
	if (hypervision_scene_count < hypervision_scene_capacity) {
		return;
	}
	if (hypervision_scene_capacity > LEGACY_U32_MAX / 2U) {
		fatal_error("HyperVision scene has too many primitives");
		return;
	}
	size_t capacity = hypervision_scene_capacity != 0 ? (size_t)hypervision_scene_capacity * 2U
													  : POLYINFO_SUPERSIGHT_PRIMITIVE_CAPACITY;
	if (capacity > LEGACY_U32_MAX || capacity > (size_t)-1 / sizeof(*hypervision_scene)) {
		fatal_error("HyperVision scene exceeds addressable memory");
		return;
	}
	struct HYPERVISION_SCENE_PRIMITIVE *scene =
		realloc(hypervision_scene, capacity * sizeof(*hypervision_scene));
	if (scene == NULL) {
		fatal_error("Cannot allocate HyperVision scene metadata");
		return;
	}
	hypervision_scene = scene;
	hypervision_scene_capacity = (legacy_u32)capacity;
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
	hypervision_scene_count = 0;
	shape3d_hires_reset();
}
#endif
