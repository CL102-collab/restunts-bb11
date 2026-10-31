#ifdef RESTUNTS_SDL3
#include "../extended/extended_frame_internal.h"
#include <stdlib.h>
#include <string.h>

#if defined(RESTUNTS_SDL3)
legacy_u8 frame_extended_upright_scenery(legacy_s16 physical_model)
{
	return supersight_enabled &&
		   (physical_model == PHYSICAL_MODEL_PALM_TREE || physical_model == PHYSICAL_MODEL_CACTUS ||
			physical_model == PHYSICAL_MODEL_PINE_TREE);
}
#endif

#if defined(RESTUNTS_SDL3)
struct VECTOR frame_extended_scenery_anchor(const struct SHAPE3D *source,
											const struct TRACKOBJECT *definition,
											const struct CSTRK_OBJECT *object)
{
	/* Both detail levels use the uphill extent of the full mesh, including its
	 * enclosing box. This keeps the crown rigid, clear of the terrain, and at
	 * the same height when the renderer changes detail. */
	const struct SHAPE3D *full = definition->ss_shapePtr ? definition->ss_shapePtr : source;
	struct VECTOR anchor = {0};
	legacy_s32 maximum = 0;
	for (legacy_u16 vertex = 0; vertex < full->shape3d_numverts; vertex++) {
		struct VECTOR point;
		shape3d_vertex_read(full, vertex, &point);
		frame_extended_rotate_vertex(&point, definition->ss_rotY);
		legacy_s32 distance =
			(legacy_s32)object->slope_x * point.x - (legacy_s32)object->slope_y * point.z;
		if (vertex == 0 || distance > maximum) {
			maximum = distance;
			anchor = point;
		}
	}
	return anchor;
}
#endif

#if defined(RESTUNTS_SDL3)
legacy_u16 frame_extended_queue_precise_object(struct TRANSFORMEDSHAPE3D *instance,
											   const struct CSTRK_OBJECT *object,
											   const struct SHAPE3D *source,
											   const struct VECTOR *upright_anchor)
{
	const struct SHAPE3D *mesh = instance->shapeptr;
	if (mesh->shape3d_numverts > LEGACY_U8_MAX) {
		return shape3d_transform_and_queue(instance);
	}
	struct SHAPE3D_HIRES_VECTOR vertices[LEGACY_U8_MAX];
	for (legacy_u16 vertex = 0; vertex < mesh->shape3d_numverts; vertex++) {
		struct VECTOR point;
		shape3d_vertex_read(mesh, vertex, &point);
		const struct VECTOR *support = &point;
		if (upright_anchor) {
			struct VECTOR authored;
			shape3d_vertex_read(source, vertex, &authored);
			if (authored.y) {
				support = upright_anchor;
			}
		}
		legacy_f64 x = object->width * FRAME_EXTENDED_TERRAIN_HALF_TILE + support->x;
		legacy_f64 y = object->height * FRAME_EXTENDED_TERRAIN_HALF_TILE - support->z;
		if (object->slope_x < 0) {
			x -= object->width * CSTRK_TILE_WORLD_SIZE;
		}
		if (object->slope_y < 0) {
			y -= object->height * CSTRK_TILE_WORLD_SIZE;
		}
		legacy_f64 grade = (object->slope_x * x + object->slope_y * y) * CSTRK_LEVEL_WORLD_HEIGHT /
						   CSTRK_TILE_WORLD_SIZE;
		/* The packed fallback retains the original rounded grade. Restore the
		 * authored height before applying the exact affine grade for rendering. */
		vertices[vertex] = (struct SHAPE3D_HIRES_VECTOR){
			point.x, point.y - frame_extended_object_grade(object, support->x, support->z) + grade,
			point.z};
	}
	return shape3d_hires_transform_and_queue(instance, vertices);
}
#endif

legacy_s16 frame_extended_draw_object_mesh(const struct FRAME_EXTENDED_OBJECT *object,
										   const struct TRACKOBJECT *definition,
										   const struct SHAPE3D *source, legacy_u8 surface)
{
	struct SHAPE3D mesh;
	if (!extended_geometry_transform_shape(source, &mesh, definition->ss_rotY, surface,
										   object->definition->ss_physicalModel)) {
		return 0;
	}
	const struct VECTOR *upright_anchor = NULL;
#if defined(RESTUNTS_SDL3)
	struct VECTOR anchor;
	if (object->graded && frame_extended_upright_scenery(object->definition->ss_physicalModel)) {
		anchor = frame_extended_scenery_anchor(source, definition, object->object);
		upright_anchor = &anchor;
	}
#endif
	if (object->graded) {
		frame_extended_apply_object_grade(&mesh, object->object, upright_anchor);
	} else if (frame_extended_object_sloped(object->object) && !object->native_grass) {
		frame_extended_remove_hill_grass(&mesh);
	}
	if (!mesh.shape3d_numprimitives) {
		extended_geometry_free_shape(&mesh);
		return 0;
	}
	struct TRANSFORMEDSHAPE3D instance = frame_extended_object_instance(object, &mesh);
	legacy_s16 result;
#if defined(RESTUNTS_SDL3)
	if (supersight_enabled && object->graded) {
		result = (legacy_s16)frame_extended_queue_precise_object(&instance, object->object, source,
																 upright_anchor);
	} else
#endif
	{
		result = frame_extended_queue_hill_shape(&instance, object->edges);
	}
	extended_geometry_free_shape(&mesh);
	return result > 0 && supersight_enabled != 0;
}

legacy_s16 frame_extended_draw_object_part(const struct FRAME_EXTENDED_OBJECT *object,
										   const struct TRACKOBJECT *definition, legacy_u8 overlay)
{
	legacy_u8 surface = overlay && object->definition->ss_physicalModel == PHYSICAL_MODEL_OVERPASS
							? object->object->lower_surface
							: object->object->surface;
	const struct SHAPE3D *source = frame_track_shape(definition, object->detail);
	if (frame_extended_object_sloped(object->object) && !object->graded && object->native_grass &&
		(overlay || surface < source->shape3d_numpaints)) {
		struct TRANSFORMEDSHAPE3D instance =
			frame_extended_object_instance(object, (struct SHAPE3D *)source);
		instance.rotvec.z = definition->ss_rotY;
		instance.ts_flags |= definition->ss_ignoreZBias;
		instance.material = overlay ? 0 : surface;
		return (legacy_s16)frame_extended_queue_hill_shape(&instance, object->edges) > 0 &&
			   supersight_enabled != 0;
	}
	return frame_extended_draw_object_mesh(object, definition, source, surface);
}

#endif
