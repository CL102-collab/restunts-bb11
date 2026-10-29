#ifndef RESTUNTS_EXTENDED_GEOMETRY_H
#define RESTUNTS_EXTENDED_GEOMETRY_H

#include "math.h"
#include "cstrk.h"
#include "shape3d.h"

/* Extended worlds retain the original word-sized local geometry. Only the
 * origin used to recover an absolute world coordinate has long precision. */
#define EXTENDED_GEOMETRY_PLANE_INDEX 536
#define EXTENDED_GEOMETRY_LEVEL_HEIGHT 450

struct EXTENDED_GEOMETRY_BASIS {
	double slope_x, slope_z, normal_x, normal_y, normal_z, normal_length;
	double tangent_xx, tangent_xy, tangent_xz, tangent_zx, tangent_zy, tangent_zz;
};

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
void extended_geometry_object_basis(const struct CSTRK_OBJECT *object,
									struct EXTENDED_GEOMETRY_BASIS *basis);
void extended_geometry_object_origin(const struct CSTRK_TRACK *track,
									 const struct CSTRK_OBJECT *object, struct VECTOR *origin);
void extended_geometry_forward(const struct EXTENDED_GEOMETRY_BASIS *basis,
							   const struct VECTOR *local, struct VECTOR *world);
void extended_geometry_inverse(const struct EXTENDED_GEOMETRY_BASIS *basis,
							   const struct VECTOR *world, struct VECTOR *local);
void extended_geometry_transform_plane(const struct EXTENDED_GEOMETRY_BASIS *basis,
									   const struct PLANE *source, struct PLANE *destination);
void extended_geometry_make_plane(struct PLANE *plane, double slope_x, double slope_z,
								  legacy_s16 height);
/* Temporary complete transformed mesh. Submit synchronously, then free. */
int extended_geometry_transform_shape(const struct SHAPE3D *source, struct SHAPE3D *destination,
									  const struct EXTENDED_GEOMETRY_BASIS *basis,
									  legacy_s16 orientation, unsigned int surface,
									  legacy_s16 physical_model);
void extended_geometry_free_shape(struct SHAPE3D *shape);
/* Original start/finish placement is flat at every plateau level. */
void extended_geometry_initial_pose(const struct CSTRK_OBJECT *object, legacy_s16 track_angle,
									struct VECTOR *rotation, struct VECTOR *normal_clearance);

#endif
