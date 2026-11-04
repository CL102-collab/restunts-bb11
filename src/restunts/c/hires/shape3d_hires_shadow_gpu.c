#include "shape3d_hires_shadow_gpu.h"

#ifdef RESTUNTS_SDL3
#include "shape3d_hires_internal.h"
#include "../math/projection.h"
#include "../renderer/gpu_scene.h"
#include <SDL3/SDL_stdinc.h>

#define GPU_SHADOW_POINTS 12U
#define GPU_SHADOW_CLIPPED_POINTS (GPU_SHADOW_POINTS + 1U)
#define GPU_SHADOW_LAYERS 3U
#define GPU_SHADOW_FEATHER_STEP 0.1
#define GPU_SHADOW_PAINT 3U

/* A short ellipse retains the car's orientation without sampling a CPU mask
 * or downloading the scene depth buffer. Layered alpha feathers its border. */
static const legacy_f64 gpu_shadow_circle[GPU_SHADOW_POINTS][2] = {
	{1, 0},	 {0.8660254037844386, 0.5},	  {0.5, 0.8660254037844386},
	{0, 1},	 {-0.5, 0.8660254037844386},  {-0.8660254037844386, 0.5},
	{-1, 0}, {-0.8660254037844386, -0.5}, {-0.5, -0.8660254037844386},
	{0, -1}, {0.5, -0.8660254037844386},  {0.8660254037844386, -0.5}};

static struct SHAPE3D_HIRES_VECTOR gpu_shadow_view(legacy_f64 x, legacy_f64 y, legacy_f64 z)
{
	const struct MATRIX *view = &shape3d_hires_state.shadow_view;
	return (struct SHAPE3D_HIRES_VECTOR){
		(x * view->m._11 + y * view->m._12 + z * view->m._13) / TRIG_FIXED_ONE,
		(x * view->m._21 + y * view->m._22 + z * view->m._23) / TRIG_FIXED_ONE,
		(x * view->m._31 + y * view->m._32 + z * view->m._33) / TRIG_FIXED_ONE};
}

static legacy_u32 gpu_shadow_clip(const struct SHAPE3D_HIRES_VECTOR *source,
								  struct HIRES_BACKEND_VERTEX *vertices)
{
	legacy_u32 count = 0;
	const struct SHAPE3D_HIRES_VECTOR *previous = &source[GPU_SHADOW_POINTS - 1U];
	for (legacy_u32 index = 0; index < GPU_SHADOW_POINTS; index++) {
		const struct SHAPE3D_HIRES_VECTOR *current = &source[index];
		if ((previous->z < HIRES_NEAR_CLIP_Z) != (current->z < HIRES_NEAR_CLIP_Z)) {
			struct SHAPE3D_HIRES_POINT point;
			shape3d_hires_project_intersection(previous, current, &point);
			vertices[count++] = (struct HIRES_BACKEND_VERTEX){point.x, point.y, point.inverse_z};
		}
		if (current->z >= HIRES_NEAR_CLIP_Z) {
			struct SHAPE3D_HIRES_POINT point;
			shape3d_hires_project(current, &point);
			vertices[count++] = (struct HIRES_BACKEND_VERTEX){point.x, point.y, point.inverse_z};
		}
		previous = current;
	}
	return count;
}

static void gpu_shadow_layer(const struct HIRES_CAR_SHADOW *shadow, legacy_f64 y, legacy_f64 gap,
							 legacy_f64 size, legacy_u16 opacity)
{
	legacy_f64 shrink = 1 + gap / HIRES_CAR_SHADOW_SHRINK_HEIGHT;
	legacy_f64 center_x = (shadow->min_x + shadow->max_x) * 0.5;
	legacy_f64 center_z = (shadow->min_z + shadow->max_z) * 0.5;
	legacy_f64 width = (shadow->max_x - shadow->min_x) * 0.5 * size / shrink;
	legacy_f64 length = (shadow->max_z - shadow->min_z) * 0.5 * size / shrink;
	legacy_f64 offset = shape3d_hires_shadow_light_offset(shadow, gap);
	struct SHAPE3D_HIRES_VECTOR view[GPU_SHADOW_POINTS];
	for (legacy_u32 index = 0; index < GPU_SHADOW_POINTS; index++) {
		legacy_f64 x = center_x + width * gpu_shadow_circle[index][0];
		legacy_f64 z = center_z + length * gpu_shadow_circle[index][1];
		legacy_f64 world_x = shadow->position.x + x * shadow->cosine + z * shadow->sine +
							 offset * shape3d_hires_state.shadow_light_x;
		legacy_f64 world_z = shadow->position.z - x * shadow->sine + z * shadow->cosine +
							 offset * shape3d_hires_state.shadow_light_z;
		view[index] = gpu_shadow_view(world_x, y, world_z);
	}
	struct HIRES_BACKEND_VERTEX vertices[GPU_SHADOW_CLIPPED_POINTS];
	legacy_u32 count = gpu_shadow_clip(view, vertices);
	if (count >= 3U) {
		const struct HIRES_BACKEND_MATERIAL material = {
			0, 0, opacity, 0, GPU_SHADOW_PAINT, HIRES_BACKEND_DECAL};
		gpu_scene_polygon(vertices, count, &material);
	}
}

legacy_s32 shape3d_hires_draw_gpu_shadows(void)
{
	if (!gpu_scene_active()) {
		return 0;
	}
	if (!hires_enabled() || hires_render_scale() == HIRES_MINIMUM_SCALE ||
		projection_focal_length_x == 0 || projection_focal_length_y == 0 ||
		shape3d_hires_state.car_shadow_count == 0) {
		return 1;
	}
	for (legacy_s32 car = 0; car < shape3d_hires_state.car_shadow_count; car++) {
		const struct HIRES_CAR_SHADOW *shadow = &shape3d_hires_state.car_shadows[car];
		legacy_f64 y = shadow->projected ? shadow->position.y : shape3d_hires_state.shadow_ground_y;
		legacy_f64 gap = shadow->projected ? 0 : SDL_max(0, shadow->position.y - y);
		if (gap >= HIRES_CAR_SHADOW_REACH || shadow->max_x <= shadow->min_x ||
			shadow->max_z <= shadow->min_z) {
			continue;
		}
		legacy_f64 fade = SDL_min(1, (HIRES_CAR_SHADOW_REACH - gap) / HIRES_CAR_SHADOW_FADE_HEIGHT);
		legacy_u16 opacity = (legacy_u16)(HIRES_CAR_SHADOW_MAX_OPACITY * fade / GPU_SHADOW_LAYERS);
		if (opacity == 0) {
			continue;
		}
		for (legacy_u32 layer = 0; layer < GPU_SHADOW_LAYERS; layer++) {
			gpu_shadow_layer(shadow, y, gap, 1 - layer * GPU_SHADOW_FEATHER_STEP, opacity);
		}
	}
	struct HIRES_RASTER_TARGET target;
	if (hires_raster_prepare(&target)) {
		gpu_scene_flush(&target, 1);
	}
	return 1;
}
#endif
