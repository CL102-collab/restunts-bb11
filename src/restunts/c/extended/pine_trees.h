#ifndef RESTUNTS_PINE_TREES_H
#define RESTUNTS_PINE_TREES_H

#include "cstrk.h"
#include "../shape3d/shape3d.h"

#define PINE_TREE_SCALE_ONE 256U

struct PINE_TREE_INSTANCE {
	legacy_s16 x;
	legacy_s16 z;
	legacy_u16 scale;
};

/* Stable authored scatter, rotated into this object's map axes. */
legacy_s16 pine_trees_instance(const struct CSTRK_OBJECT *object, legacy_u8 index,
							   struct PINE_TREE_INSTANCE *instance);
/* One native pine, scaled and positioned inside its tile. The caller owns the
 * result and releases it with extended_geometry_free_shape. */
legacy_s16 pine_trees_create_mesh(const struct SHAPE3D *source, struct SHAPE3D *destination,
								  const struct CSTRK_OBJECT *object, legacy_u8 index);

#endif
