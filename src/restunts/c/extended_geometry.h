#ifndef RESTUNTS_EXTENDED_GEOMETRY_H
#define RESTUNTS_EXTENDED_GEOMETRY_H

#include "math.h"
#include "cstrk.h"
#include "shape3d.h"

/* Extended worlds retain the original word-sized local geometry. Only the
 * origin used to recover an absolute world coordinate has long precision. */
#define EXTENDED_GEOMETRY_PLANE_INDEX 536

struct EXTENDED_TERRAIN_SAMPLE {
	legacy_s16 column, north_row;
	legacy_s16 surface;
	legacy_u8 terrain_id, base_height;
	struct VECTOR origin;
	struct PLANE plane;
};

legacy_s32 extended_geometry_unwrap(legacy_s16 coordinate, legacy_s32 reference);
void extended_geometry_set_context(legacy_s32 fixed_x, legacy_s32 fixed_z);
void extended_geometry_get_context(legacy_s32 *world_x, legacy_s32 *world_z);
void extended_geometry_world_position(const struct VECTOR *wrapped, legacy_s32 *x, legacy_s32 *z);
int extended_geometry_terrain(const struct CSTRK_TRACK *track, legacy_s32 world_x,
							  legacy_s32 world_z, struct EXTENDED_TERRAIN_SAMPLE *sample);
void extended_geometry_object_origin(const struct CSTRK_TRACK *track,
									 const struct CSTRK_OBJECT *object, struct VECTOR *origin);
/* Original compatible one-cell roads and ramps keep their native hill models. */
legacy_s16 extended_geometry_native_hill_id(const struct CSTRK_OBJECT *object,
											legacy_s16 resolved_id);
/* Other sloped elements use the common affine grade transform. */
legacy_s16 extended_geometry_uses_grade(const struct CSTRK_OBJECT *object);
/* Height above the object's lowest support corner, in world-oriented local axes. */
legacy_s16 extended_geometry_object_grade(const struct CSTRK_OBJECT *object, legacy_s16 local_x,
										  legacy_s16 local_z);
/* Raw support height without selecting native or generic geometry. */
legacy_s16 extended_geometry_support_grade(const struct CSTRK_OBJECT *object, legacy_s16 local_x,
										   legacy_s16 local_z);
/* Apply the grade's linear part to a direction or inverse transpose to a normal.
 * The normal helper preserves scale; callers may normalize after transforming. */
void extended_geometry_grade_vector(const struct CSTRK_OBJECT *object, struct VECTOR *direction);
void extended_geometry_grade_normal(const struct CSTRK_OBJECT *object, struct VECTOR *normal);
/* Transform an authored plane relative to the object's lowest support origin. */
void extended_geometry_grade_plane(const struct CSTRK_OBJECT *object, struct PLANE *plane);
/* Temporary quarter-turned mesh with its selected paint. Submit synchronously, then free. */
int extended_geometry_transform_shape(const struct SHAPE3D *source, struct SHAPE3D *destination,
									  legacy_s16 orientation, legacy_u16 surface,
									  legacy_s16 physical_model);
void extended_geometry_free_shape(struct SHAPE3D *shape);

#endif
