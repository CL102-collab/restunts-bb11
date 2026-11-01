#ifndef RESTUNTS_WHEAT_FIELD_H
#define RESTUNTS_WHEAT_FIELD_H

#include "cstrk.h"
#include "../shape3d/shape3d.h"

#define WHEAT_FIELD_ROW_COUNT 6U
#define WHEAT_FIELD_PLANTS_PER_ROW 5U
#define WHEAT_FIELD_HEIGHT 96
#define WHEAT_FIELD_CANOPY_HEIGHT 40
#define WHEAT_FIELD_BASE_HEIGHT 2
#define WHEAT_FIELD_ROW_HALF_WIDTH 70
#define WHEAT_FIELD_BASE_MATERIAL 25U
#define WHEAT_FIELD_CANOPY_MATERIAL 82U
#define WHEAT_FIELD_STEM_MATERIAL 6U
#define WHEAT_FIELD_GRAIN_MATERIAL 60U
#define WHEAT_FIELD_GRAIN_LIGHT_MATERIAL 62U
#define WHEAT_FIELD_BOUNDING_VERTICES 8U
#define WHEAT_FIELD_QUAD_VERTICES 4U
#define WHEAT_FIELD_PLANT_VERTICES 7U
#define WHEAT_FIELD_PLANT_PRIMITIVES 3U
#define WHEAT_FIELD_LOW_VERTEX_COUNT                                                               \
	(WHEAT_FIELD_BOUNDING_VERTICES + WHEAT_FIELD_QUAD_VERTICES * (WHEAT_FIELD_ROW_COUNT + 1U))
#define WHEAT_FIELD_LOW_PRIMITIVE_COUNT (WHEAT_FIELD_ROW_COUNT + 1U)
#define WHEAT_FIELD_VERTEX_COUNT                                                                   \
	(WHEAT_FIELD_LOW_VERTEX_COUNT +                                                                \
	 WHEAT_FIELD_ROW_COUNT * WHEAT_FIELD_PLANTS_PER_ROW * WHEAT_FIELD_PLANT_VERTICES)
#define WHEAT_FIELD_PRIMITIVE_COUNT                                                                \
	(WHEAT_FIELD_LOW_PRIMITIVE_COUNT +                                                             \
	 WHEAT_FIELD_ROW_COUNT * WHEAT_FIELD_PLANTS_PER_ROW * WHEAT_FIELD_PLANT_PRIMITIVES)

/* Asset-free rows of mature wheat. The full mesh adds individual stalks and
 * crossed grain heads; both detail levels retain the same ground and rows.
 * Free the caller-owned result with extended_geometry_free_shape. */
legacy_s16 wheat_field_create_mesh(struct SHAPE3D *mesh, legacy_u8 low_detail);

#endif
