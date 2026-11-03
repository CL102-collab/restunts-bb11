#ifndef RESTUNTS_WHEAT_FIELD_H
#define RESTUNTS_WHEAT_FIELD_H

#include "cstrk.h"
#include "../shape3d/shape3d.h"

#define WHEAT_FIELD_ROW_COUNT 6U
#define WHEAT_FIELD_HEIGHT 10
#define WHEAT_FIELD_BASE_HEIGHT 2
#define WHEAT_FIELD_ROW_HALF_WIDTH 70
#define WHEAT_FIELD_BASE_MATERIAL 25U
#define WHEAT_FIELD_CANOPY_MATERIAL 82U
#define WHEAT_FIELD_BOUNDING_VERTICES 8U
#define WHEAT_FIELD_QUAD_VERTICES 4U
#define WHEAT_FIELD_VERTEX_COUNT                                                                   \
	(WHEAT_FIELD_BOUNDING_VERTICES + WHEAT_FIELD_QUAD_VERTICES * (WHEAT_FIELD_ROW_COUNT + 1U))
#define WHEAT_FIELD_PRIMITIVE_COUNT (WHEAT_FIELD_ROW_COUNT + 1U)

/* Asset-free low crop rows without individual stalk or grain decorations.
 * Free the caller-owned result with extended_geometry_free_shape. */
legacy_s16 wheat_field_create_mesh(struct SHAPE3D *mesh);

#endif
