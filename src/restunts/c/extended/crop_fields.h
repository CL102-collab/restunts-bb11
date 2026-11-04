#ifndef RESTUNTS_CROP_FIELDS_H
#define RESTUNTS_CROP_FIELDS_H

#include "cstrk.h"
#include "../shape3d/shape3d.h"

#define CROP_FIELDS_HALF_TILE (CSTRK_TILE_WORLD_SIZE / 2)
#define CROP_FIELDS_BOUNDING_VERTICES 8U
#define CROP_FIELDS_QUAD_VERTICES 4U
#define CROP_FIELDS_SOIL_HEIGHT 2
#define CROP_FIELDS_SOIL_MATERIAL 25U
#define TULIP_FIELD_ROW_COUNT 10U
#define TULIP_FIELD_ROW_HEIGHT 4
#define TULIP_FIELD_ROW_HALF_WIDTH 48
#define TULIP_FIELD_FLOWER_BAND_HEIGHT 12
#define TULIP_FIELD_FLOWER_BAND_HALF_WIDTH 40
#define TULIP_FIELD_HEIGHT TULIP_FIELD_FLOWER_BAND_HEIGHT
#define TULIP_FIELD_ROW_MATERIAL 69U
#define TULIP_FIELD_WHITE_MATERIAL 15U
#define TULIP_FIELD_GREEN_MATERIAL 10U
#define TULIP_FIELD_BLUE_MATERIAL 9U
#define TULIP_FIELD_RED_MATERIAL 54U
#define TULIP_FIELD_YELLOW_MATERIAL 61U
#define TULIP_FIELD_PURPLE_MATERIAL 75U
#define TULIP_FIELD_TEAL_MATERIAL 3U
#define TULIP_FIELD_PINK_MATERIAL 37U
#define TULIP_FIELD_VERTEX_COUNT                                                                   \
	(CROP_FIELDS_BOUNDING_VERTICES + CROP_FIELDS_QUAD_VERTICES * (2U * TULIP_FIELD_ROW_COUNT + 1U))
#define TULIP_FIELD_PRIMITIVE_COUNT (1U + 2U * TULIP_FIELD_ROW_COUNT)
#define VINE_FIELD_ROW_COUNT 24U
#define VINE_FIELD_ROW_WIDTH 22
#define VINE_FIELD_ROW_LEFT_OFFSET (-(VINE_FIELD_ROW_WIDTH / 2))
#define VINE_FIELD_ROW_RIGHT_OFFSET (VINE_FIELD_ROW_LEFT_OFFSET + VINE_FIELD_ROW_WIDTH)
#define VINE_FIELD_EAVE_HEIGHT 36
#define VINE_FIELD_RIDGE_OFFSET 0
#define VINE_FIELD_HEIGHT 48
#define VINE_FIELD_SECTION_VERTICES 5U
#define VINE_FIELD_ROW_VERTICES (2U * VINE_FIELD_SECTION_VERTICES)
#define VINE_FIELD_SIDE_COUNT (VINE_FIELD_SECTION_VERTICES - 1U)
#define VINE_FIELD_CAP_COUNT 2U
#define VINE_FIELD_SIDE_MATERIAL 68U
#define VINE_FIELD_TOP_MATERIAL 69U
#define VINE_FIELD_VERTEX_COUNT                                                                    \
	(CROP_FIELDS_BOUNDING_VERTICES + CROP_FIELDS_QUAD_VERTICES +                                   \
	 VINE_FIELD_ROW_COUNT * VINE_FIELD_ROW_VERTICES)
#define VINE_FIELD_PRIMITIVE_COUNT                                                                 \
	(1U + VINE_FIELD_ROW_COUNT * (VINE_FIELD_SIDE_COUNT + VINE_FIELD_CAP_COUNT))

/* Asset-free, one-tile crop rows. Each field uses one mesh within the native
 * byte-indexed vertex limit. The caller frees each generated mesh with
 * extended_geometry_free_shape. Invalid IDs or parts leave an empty result. */
legacy_u8 crop_fields_mesh_count(legacy_u16 id);
legacy_s16 crop_fields_create_mesh(struct SHAPE3D *mesh, legacy_u16 id, legacy_u8 part);

#endif
