#ifdef RESTUNTS_SDL3
#include "extended_frame_internal.h"
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

legacy_u8 hires_track_queue_object(struct TRANSFORMEDSHAPE3D *instance,
								   const struct FRAME_EXTENDED_OBJECT *object,
								   const struct SHAPE3D *source, const struct VECTOR *anchor,
								   legacy_s16 *result)
{
	if (!supersight_enabled || !object->graded ||
		instance->shapeptr->shape3d_numverts > LEGACY_U8_MAX) {
		return 0;
	}
	*result =
		(legacy_s16)frame_extended_queue_precise_object(instance, object->object, source, anchor);
	return 1;
}

const struct VECTOR *hires_track_object_anchor(const struct FRAME_EXTENDED_OBJECT *object,
											   const struct TRACKOBJECT *definition,
											   const struct SHAPE3D *source, struct VECTOR *anchor)
{
	if (!object->graded || !frame_extended_upright_scenery(object->definition->ss_physicalModel)) {
		return NULL;
	}
	*anchor = frame_extended_scenery_anchor(source, definition, object->object);
	return anchor;
}

#endif
