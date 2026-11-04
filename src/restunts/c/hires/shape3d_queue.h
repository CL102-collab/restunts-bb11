#ifndef RESTUNTS_HIRES_SHAPE3D_QUEUE_H
#define RESTUNTS_HIRES_SHAPE3D_QUEUE_H
#include "../legacy.h"
#ifdef RESTUNTS_SDL3
struct HIRES_BACKEND_SCENE_PRIMITIVE {
	legacy_u16 material;
	legacy_u8 type;
};
#include "shape3d_hires_internal.h"
#define hires_backend_scene (shape3d_hires_state.scene)
#define hires_backend_scene_count (shape3d_hires_state.scene_count)
legacy_s32 hires_backend_scene_reserve(void);
/* Each job owns a private scene. Join in source order before appending any
 * dependent serial geometry with joined(). join_required may defer callbacks for
 * immutable groups; the final group always joins. NULL joins every group.
 * Callbacks must not nest worker jobs. */
void shape3d_hires_geometry_run(legacy_s32 count, void (*job)(void *, legacy_s32),
								void (*joined)(void *, legacy_s32),
								legacy_s32 (*join_required)(void *, legacy_s32), void *context);
void shape3d_hires_geometry_shutdown(void);
struct RECTANGLE *shape3d_hires_geometry_rectangle(struct RECTANGLE *rectangle);
void shape3d_hires_adjust_sort(legacy_u16 *sort_by_depth);
void shape3d_hires_scene_reset(void);
#endif

#endif
