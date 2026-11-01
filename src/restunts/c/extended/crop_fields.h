#ifndef RESTUNTS_CROP_FIELDS_H
#define RESTUNTS_CROP_FIELDS_H

#include "cstrk.h"
#include "../shape3d/shape3d.h"

#define CROP_FIELDS_HALF_TILE (CSTRK_TILE_WORLD_SIZE / 2)
#define CROP_FIELDS_BOUNDING_VERTICES 8U
#define CROP_FIELDS_QUAD_VERTICES 4U
#define CROP_FIELDS_SOIL_HEIGHT 2
#define CROP_FIELDS_SOIL_MATERIAL 25U
#define TULIP_FIELD_ROW_COUNT 6U
#define TULIP_FIELD_PLANTS_PER_ROW 5U
#define TULIP_FIELD_ROW_HEIGHT 4
#define TULIP_FIELD_ROW_HALF_WIDTH 80
#define TULIP_FIELD_FLOWER_BAND_HEIGHT 12
#define TULIP_FIELD_FLOWER_BAND_HALF_WIDTH 70
#define TULIP_FIELD_CUP_BOTTOM 8
#define TULIP_FIELD_HEIGHT 16
#define TULIP_FIELD_PETAL_RADIUS 10
#define TULIP_FIELD_PLANT_VERTICES 6U
#define TULIP_FIELD_PETAL_COUNT 4U
#define TULIP_FIELD_PLANT_PRIMITIVES (1U + TULIP_FIELD_PETAL_COUNT)
#define TULIP_FIELD_ROW_MATERIAL 69U
#define TULIP_FIELD_STEM_MATERIAL 16U
#define TULIP_FIELD_RED_MATERIAL 54U
#define TULIP_FIELD_YELLOW_MATERIAL 61U
#define TULIP_FIELD_PURPLE_MATERIAL 75U
#define TULIP_FIELD_ORANGE_MATERIAL 124U
#define TULIP_FIELD_PINK_MATERIAL 37U
#define TULIP_FIELD_VERTEX_COUNT                                                                   \
	(CROP_FIELDS_BOUNDING_VERTICES +                                                               \
	 CROP_FIELDS_QUAD_VERTICES * (2U * TULIP_FIELD_ROW_COUNT + 1U) +                               \
	 TULIP_FIELD_ROW_COUNT * TULIP_FIELD_PLANTS_PER_ROW * TULIP_FIELD_PLANT_VERTICES)
#define TULIP_FIELD_PRIMITIVE_COUNT                                                                \
	(1U + 2U * TULIP_FIELD_ROW_COUNT +                                                             \
	 TULIP_FIELD_ROW_COUNT * TULIP_FIELD_PLANTS_PER_ROW * TULIP_FIELD_PLANT_PRIMITIVES)
#define VINE_FIELD_ROW_COUNT 4U
#define VINE_FIELD_ROW_HALF_WIDTH 64
#define VINE_FIELD_SHOULDER_OFFSET 48
#define VINE_FIELD_SHOULDER_HEIGHT 128
#define VINE_FIELD_HEIGHT 192
#define VINE_FIELD_SECTION_VERTICES 5U
#define VINE_FIELD_ROW_VERTICES (2U * VINE_FIELD_SECTION_VERTICES)
#define VINE_FIELD_SIDE_COUNT (VINE_FIELD_SECTION_VERTICES - 1U)
#define VINE_FIELD_CAP_COUNT 2U
#define VINE_FIELD_SIDE_MATERIAL 68U
#define VINE_FIELD_TOP_MATERIAL 69U
#define VINE_FIELD_GRAPE_MATERIAL 73U
#define VINE_FIELD_GRAPE_STATION_COUNT 8U
#define VINE_FIELD_GRAPE_SIDE_COUNT 2U
#define VINE_FIELD_GRAPE_PART_COUNT 2U
#define VINE_FIELD_GRAPE_LOWER_HEIGHT 64
#define VINE_FIELD_GRAPE_JOIN_HEIGHT 80
#define VINE_FIELD_GRAPE_UPPER_HEIGHT 104
#define VINE_FIELD_GRAPE_UPPER_HALF_WIDTH 16
#define VINE_FIELD_GRAPE_LOWER_HALF_WIDTH 8
#define VINE_FIELD_GRAPE_OUTWARD_OFFSET 2
#define VINE_FIELD_GRAPE_PRIMITIVE_COUNT                                                           \
	(VINE_FIELD_ROW_COUNT * VINE_FIELD_GRAPE_STATION_COUNT * VINE_FIELD_GRAPE_SIDE_COUNT *         \
	 VINE_FIELD_GRAPE_PART_COUNT)
#define VINE_FIELD_VERTEX_COUNT                                                                    \
	(CROP_FIELDS_BOUNDING_VERTICES + CROP_FIELDS_QUAD_VERTICES +                                   \
	 VINE_FIELD_ROW_COUNT * VINE_FIELD_ROW_VERTICES)
#define VINE_FIELD_PRIMITIVE_COUNT                                                                 \
	(1U + VINE_FIELD_ROW_COUNT * (VINE_FIELD_SIDE_COUNT + VINE_FIELD_CAP_COUNT))
#define VINE_FIELD_MESH_COUNT (1U + VINE_FIELD_ROW_COUNT)
#define VINE_FIELD_GRAPE_BATCH_PRIMITIVE_COUNT                                                     \
	(VINE_FIELD_GRAPE_STATION_COUNT * VINE_FIELD_GRAPE_SIDE_COUNT * VINE_FIELD_GRAPE_PART_COUNT)
#define VINE_FIELD_GRAPE_BATCH_VERTEX_COUNT                                                        \
	(CROP_FIELDS_BOUNDING_VERTICES +                                                               \
	 VINE_FIELD_GRAPE_BATCH_PRIMITIVE_COUNT * CROP_FIELDS_QUAD_VERTICES)

/* Asset-free, one-tile crop rows. Tulips use one mesh. Vines use a foliage mesh
 * followed by one grape mesh per row, keeping every batch within the native
 * byte-indexed vertex limit. The caller frees each generated mesh with
 * extended_geometry_free_shape. Invalid IDs or parts leave an empty result. */
legacy_u8 crop_fields_mesh_count(legacy_u16 id);
legacy_s16 crop_fields_create_mesh(struct SHAPE3D *mesh, legacy_u16 id, legacy_u8 part);

#endif
