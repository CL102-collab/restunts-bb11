#ifndef RESTUNTS_SHAPE3D_HIRES_H
#define RESTUNTS_SHAPE3D_HIRES_H

#include "math.h"

#if defined(RESTUNTS_SDL3)

/* Keep fractional coordinates from object transformation through rasterization.
 * The original queue remains in logical 320x200 coordinates. */
struct SHAPE3D_HIRES_VECTOR {
	legacy_f64 x;
	legacy_f64 y;
	legacy_f64 z;
};

struct SHAPE3D_HIRES_POINT {
	legacy_f64 x;
	legacy_f64 y;
	legacy_f64 inverse_z;
};

struct TRANSFORMEDSHAPE3D;
/* Queue HyperVision geometry without quantizing generated local vertices into
 * a legacy shape resource. Vertices are copied during this call. */
legacy_u16 shape3d_hires_transform_and_queue(struct TRANSFORMEDSHAPE3D *instance,
											 const struct SHAPE3D_HIRES_VECTOR *vertices);
/* Add a precise camera-relative world offset to subsequent instance positions.
 * Pretransformed positions use view coordinates instead. NULL resets to zero. */
void shape3d_hires_set_origin(const struct SHAPE3D_HIRES_VECTOR *origin);

void shape3d_hires_project(const struct SHAPE3D_HIRES_VECTOR *vector,
						   struct SHAPE3D_HIRES_POINT *point);
legacy_u8 shape3d_hires_clip_flags(const struct SHAPE3D_HIRES_VECTOR *vector);
legacy_s32 shape3d_hires_polygon_visible(legacy_u32 index, legacy_s32 cull_backface);
legacy_u32 shape3d_hires_wheel_face(legacy_u32 index);
void shape3d_hires_reset(void);
/* Scale the stroke with model size; captured per queued primitive. */
void shape3d_hires_set_model_scale(legacy_f64 scale);
enum SHAPE3D_HIRES_DEPTH_MODE {
	SHAPE3D_HIRES_DEPTH_ORDERED,
	SHAPE3D_HIRES_DEPTH_SORTED,
	SHAPE3D_HIRES_DEPTH_BACKGROUND
};
void shape3d_hires_begin_shape(legacy_u32 index, legacy_s32 depth_mode);
void shape3d_hires_set_shadow_receiver(legacy_s32 enabled);
/* Clip added coverage against flat track ground, independently of car shadows. */
void shape3d_hires_ground_begin(const struct VECTOR *camera_position);
/* At most two car silhouettes; visible receivers reuse the scene depth buffer.
 * Positions are camera-relative world coordinates, with Y up and heading around Y. */
void shape3d_hires_shadows_begin(const struct VECTOR *camera_position);
/* Override the scene light after shadows_begin and before queuing cars.
 * Heading points along the cast shadow (+Z at zero, +X at a quarter turn);
 * cotangent is horizontal travel per unit height, zero for overhead light. */
void shape3d_hires_set_shadow_light(legacy_s16 heading, legacy_f64 cotangent);
void shape3d_hires_shadow_car(const struct VECTOR *relative_position, legacy_s16 heading,
							  legacy_s16 half_width, legacy_s16 half_length);
struct SHAPE3D;
/* Attach the loaded model to the last car; masks are cached across frames. */
void shape3d_hires_shadow_model(const struct SHAPE3D *shape);
/* Cast the last car's actual geometry onto its registered flat contact plane.
 * ground_height is the model-space Y resting on that plane. The cached mask
 * follows the car heading and scene light; this mode has no airborne shrink. */
void shape3d_hires_shadow_projected_model(const struct SHAPE3D *shape, legacy_s16 ground_height);
/* Invalidate cached geometry before car resources are replaced or freed. */
void shape3d_hires_shadow_models_reset(void);
/* Apply to visible receivers after the joined scene pass, before hires_end. */
void shape3d_hires_draw_shadows(void);
/* Collect only nearby receiving surfaces, before camera clipping/backface culling.
 * The input coordinates use the current view transform, without projection.
 * A collected polygon returns a nonzero token to link its visible queue entry. */
legacy_s32 shape3d_hires_shadows_active(void);
void shape3d_hires_set_shadow_surface(legacy_u32 index, legacy_u32 surface);
legacy_s32 shape3d_hires_shadow_overlap(const struct SHAPE3D_HIRES_VECTOR *vertices,
										legacy_u32 count);
legacy_u32 shape3d_hires_shadow_polygon(const struct SHAPE3D_HIRES_VECTOR *vertices,
										legacy_u32 count, legacy_s32 grille);
void shape3d_hires_queue(legacy_u32 index, legacy_u8 type, legacy_u16 vertex_count,
						 const legacy_u8 *indices, const struct SHAPE3D_HIRES_VECTOR *vertices,
						 legacy_u16 flags);
void shape3d_hires_update_bounds(legacy_u32 index, legacy_u8 type, struct RECTANGLE *rectangle);
/* Record a complete immutable scene, then draw disjoint row bands and join.
 * End returns the background worker count, or zero for the serial fallback. */
void shape3d_hires_batch_begin(void);
legacy_s32 shape3d_hires_batch_end(void);
void shape3d_hires_render(legacy_u32 index, legacy_u8 type, legacy_u16 color,
						  legacy_u16 second_color, legacy_u16 third_color, legacy_u16 pattern_type,
						  legacy_u16 pattern);

#endif

#endif
