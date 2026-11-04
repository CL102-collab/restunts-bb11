#include "crop_fields.h"

#include <stdlib.h>
#include <string.h>

#define CROP_FIELDS_PAINT_COUNT 1U
#define CROP_FIELDS_ALWAYS_VISIBLE 1U
#define CROP_FIELDS_PRIMITIVE_PREFIX (SHAPE3D_PRIMITIVE_HEADER_SIZE + CROP_FIELDS_PAINT_COUNT)
#define CROP_FIELDS_QUAD_BYTES (CROP_FIELDS_PRIMITIVE_PREFIX + CROP_FIELDS_QUAD_VERTICES)
#define CROP_FIELDS_TERMINATOR_BYTES 2U
#define CROP_FIELDS_SOIL_VERTEX_FIRST CROP_FIELDS_BOUNDING_VERTICES
#define CROP_FIELDS_ROWS_VERTEX_FIRST (CROP_FIELDS_SOIL_VERTEX_FIRST + CROP_FIELDS_QUAD_VERTICES)
#define TULIP_FIELD_BAND_VERTEX_FIRST                                                              \
	(CROP_FIELDS_ROWS_VERTEX_FIRST + TULIP_FIELD_ROW_COUNT * CROP_FIELDS_QUAD_VERTICES)
#define TULIP_FIELD_PRIMITIVE_BYTES                                                                \
	((2U * TULIP_FIELD_ROW_COUNT + 1U) * CROP_FIELDS_QUAD_BYTES + CROP_FIELDS_TERMINATOR_BYTES)
#define VINE_FIELD_CAP_BYTES (CROP_FIELDS_PRIMITIVE_PREFIX + VINE_FIELD_SECTION_VERTICES)
#define VINE_FIELD_GRAPE_VERTEX_FIRST CROP_FIELDS_BOUNDING_VERTICES
#define VINE_FIELD_PRIMITIVE_BYTES                                                                 \
	(CROP_FIELDS_QUAD_BYTES +                                                                      \
	 VINE_FIELD_ROW_COUNT * (VINE_FIELD_SIDE_COUNT * CROP_FIELDS_QUAD_BYTES +                      \
							 VINE_FIELD_CAP_COUNT * VINE_FIELD_CAP_BYTES) +                        \
	 CROP_FIELDS_TERMINATOR_BYTES)
#define VINE_FIELD_GRAPE_BATCH_PRIMITIVE_BYTES                                                     \
	(VINE_FIELD_GRAPE_BATCH_PRIMITIVE_COUNT * CROP_FIELDS_QUAD_BYTES + CROP_FIELDS_TERMINATOR_BYTES)

static void crop_fields_write_vertex(struct SHAPE3D *mesh, legacy_u16 index, legacy_s16 x,
									 legacy_s16 y, legacy_s16 z)
{
	legacy_u8 *bytes = mesh->shape3d_vertex_bytes + (size_t)index * SHAPE3D_VERTEX_SIZE;
	LEGACY_WRITE_U16_LE(bytes, (legacy_u16)x);
	LEGACY_WRITE_U16_LE(bytes + SHAPE3D_VERTEX_Y_OFFSET, (legacy_u16)y);
	LEGACY_WRITE_U16_LE(bytes + SHAPE3D_VERTEX_Z_OFFSET, (legacy_u16)z);
}

static legacy_u8 *crop_fields_write_primitive(legacy_u8 *bytes, legacy_u8 count, legacy_u8 material,
											  const legacy_u8 *indices)
{
	bytes[0] = count;
	bytes[1] = CROP_FIELDS_ALWAYS_VISIBLE;
	bytes[SHAPE3D_PRIMITIVE_HEADER_SIZE] = material;
	memcpy(bytes + CROP_FIELDS_PRIMITIVE_PREFIX, indices, count);
	return bytes + CROP_FIELDS_PRIMITIVE_PREFIX + count;
}

static legacy_u8 *crop_fields_write_quad(struct SHAPE3D *mesh, legacy_u8 *primitive,
										 legacy_u16 first, legacy_s16 left, legacy_s16 right,
										 legacy_s16 height, legacy_u8 material)
{
	for (legacy_u16 corner = 0; corner < CROP_FIELDS_QUAD_VERTICES; corner++) {
		crop_fields_write_vertex(mesh, first + corner, corner & 1U ? right : left, height,
								 corner & 2U ? CROP_FIELDS_HALF_TILE : -CROP_FIELDS_HALF_TILE);
	}
	legacy_u8 indices[CROP_FIELDS_QUAD_VERTICES];
	indices[0] = (legacy_u8)first;
	indices[1] = (legacy_u8)(first + 1U);
	indices[2] = (legacy_u8)(first + 3U);
	indices[3] = (legacy_u8)(first + 2U);
	return crop_fields_write_primitive(primitive, CROP_FIELDS_QUAD_VERTICES, material, indices);
}

static legacy_s16 crop_fields_center(legacy_u16 index, legacy_u16 count)
{
	return (legacy_s16)(((legacy_s32)index * 2 + 1) * CSTRK_TILE_WORLD_SIZE / (2 * count) -
						CROP_FIELDS_HALF_TILE);
}

static legacy_u8 *crop_fields_write_tulips(struct SHAPE3D *mesh, legacy_u8 *primitive)
{
	for (legacy_u16 row = 0; row < TULIP_FIELD_ROW_COUNT; row++) {
		legacy_s16 center = crop_fields_center(row, TULIP_FIELD_ROW_COUNT);
		primitive = crop_fields_write_quad(
			mesh, primitive, CROP_FIELDS_ROWS_VERTEX_FIRST + row * CROP_FIELDS_QUAD_VERTICES,
			center - TULIP_FIELD_ROW_HALF_WIDTH, center + TULIP_FIELD_ROW_HALF_WIDTH,
			TULIP_FIELD_ROW_HEIGHT, TULIP_FIELD_ROW_MATERIAL);
	}
	static const legacy_u8 colors[TULIP_FIELD_ROW_COUNT] = {
		TULIP_FIELD_WHITE_MATERIAL,	 TULIP_FIELD_GREEN_MATERIAL, TULIP_FIELD_YELLOW_MATERIAL,
		TULIP_FIELD_ORANGE_MATERIAL, TULIP_FIELD_RED_MATERIAL,	 TULIP_FIELD_PURPLE_MATERIAL,
		TULIP_FIELD_PINK_MATERIAL,	 TULIP_FIELD_BLUE_MATERIAL};
	/* Continuous color bands represent the field's planted flower rows. */
	for (legacy_u16 row = 0; row < TULIP_FIELD_ROW_COUNT; row++) {
		legacy_s16 center = crop_fields_center(row, TULIP_FIELD_ROW_COUNT);
		primitive = crop_fields_write_quad(
			mesh, primitive, TULIP_FIELD_BAND_VERTEX_FIRST + row * CROP_FIELDS_QUAD_VERTICES,
			center - TULIP_FIELD_FLOWER_BAND_HALF_WIDTH,
			center + TULIP_FIELD_FLOWER_BAND_HALF_WIDTH, TULIP_FIELD_FLOWER_BAND_HEIGHT,
			colors[row]);
	}
	return primitive;
}

static legacy_u8 *crop_fields_write_grapes(struct SHAPE3D *mesh, legacy_u8 *primitive,
										   legacy_u16 row)
{
	legacy_u16 first = VINE_FIELD_GRAPE_VERTEX_FIRST;
	legacy_s16 center = crop_fields_center(row, VINE_FIELD_ROW_COUNT);
	for (legacy_u8 station = 0; station < VINE_FIELD_GRAPE_STATION_COUNT; station++) {
		legacy_s16 z = crop_fields_center(station, VINE_FIELD_GRAPE_STATION_COUNT);
		for (legacy_u8 side = 0; side < VINE_FIELD_GRAPE_SIDE_COUNT; side++) {
			legacy_s16 x = side ? VINE_FIELD_ROW_RIGHT_OFFSET + VINE_FIELD_GRAPE_OUTWARD_OFFSET
								: VINE_FIELD_ROW_LEFT_OFFSET - VINE_FIELD_GRAPE_OUTWARD_OFFSET;
			for (legacy_u8 part = 0; part < VINE_FIELD_GRAPE_PART_COUNT; part++) {
				legacy_s16 low =
					part ? VINE_FIELD_GRAPE_LOWER_HEIGHT : VINE_FIELD_GRAPE_JOIN_HEIGHT;
				legacy_s16 high =
					part ? VINE_FIELD_GRAPE_JOIN_HEIGHT : VINE_FIELD_GRAPE_UPPER_HEIGHT;
				legacy_s16 half =
					part ? VINE_FIELD_GRAPE_LOWER_HALF_WIDTH : VINE_FIELD_GRAPE_UPPER_HALF_WIDTH;
				for (legacy_u8 corner = 0; corner < CROP_FIELDS_QUAD_VERTICES; corner++) {
					legacy_s16 y = corner >= CROP_FIELDS_QUAD_VERTICES / 2U ? high : low;
					legacy_s16 depth =
						corner == 0 || corner == CROP_FIELDS_QUAD_VERTICES - 1U ? -half : half;
					crop_fields_write_vertex(mesh, first + corner, center + x, y, z + depth);
				}
				legacy_u8 indices[CROP_FIELDS_QUAD_VERTICES];
				for (legacy_u8 corner = 0; corner < CROP_FIELDS_QUAD_VERTICES; corner++) {
					legacy_u8 outward =
						side && corner ? CROP_FIELDS_QUAD_VERTICES - corner : corner;
					indices[corner] = (legacy_u8)(first + outward);
				}
				primitive = crop_fields_write_primitive(primitive, CROP_FIELDS_QUAD_VERTICES,
														VINE_FIELD_GRAPE_MATERIAL, indices);
				first += CROP_FIELDS_QUAD_VERTICES;
			}
		}
	}
	return primitive;
}

static legacy_u8 *crop_fields_write_vines(struct SHAPE3D *mesh, legacy_u8 *primitive)
{
	static const legacy_s16 section_x[VINE_FIELD_SECTION_VERTICES] = {
		VINE_FIELD_ROW_LEFT_OFFSET, VINE_FIELD_ROW_LEFT_OFFSET, VINE_FIELD_RIDGE_OFFSET,
		VINE_FIELD_ROW_RIGHT_OFFSET, VINE_FIELD_ROW_RIGHT_OFFSET};
	static const legacy_s16 section_y[VINE_FIELD_SECTION_VERTICES] = {
		CROP_FIELDS_SOIL_HEIGHT, VINE_FIELD_EAVE_HEIGHT, VINE_FIELD_HEIGHT, VINE_FIELD_EAVE_HEIGHT,
		CROP_FIELDS_SOIL_HEIGHT};
	for (legacy_u16 row = 0; row < VINE_FIELD_ROW_COUNT; row++) {
		legacy_u16 first = CROP_FIELDS_ROWS_VERTEX_FIRST + row * VINE_FIELD_ROW_VERTICES;
		legacy_s16 center = crop_fields_center(row, VINE_FIELD_ROW_COUNT);
		for (legacy_u16 end = 0; end < VINE_FIELD_CAP_COUNT; end++) {
			for (legacy_u16 point = 0; point < VINE_FIELD_SECTION_VERTICES; point++) {
				crop_fields_write_vertex(mesh, first + end * VINE_FIELD_SECTION_VERTICES + point,
										 center + section_x[point], section_y[point],
										 end ? CROP_FIELDS_HALF_TILE : -CROP_FIELDS_HALF_TILE);
			}
		}
		for (legacy_u16 side = 0; side < VINE_FIELD_SIDE_COUNT; side++) {
			legacy_u8 indices[CROP_FIELDS_QUAD_VERTICES];
			indices[0] = (legacy_u8)(first + side);
			indices[1] = (legacy_u8)(first + side + 1U);
			indices[2] = (legacy_u8)(first + side + 1U + VINE_FIELD_SECTION_VERTICES);
			indices[3] = (legacy_u8)(first + side + VINE_FIELD_SECTION_VERTICES);
			legacy_u8 material = side == 0 || side == VINE_FIELD_SIDE_COUNT - 1U
									 ? VINE_FIELD_SIDE_MATERIAL
									 : VINE_FIELD_TOP_MATERIAL;
			primitive = crop_fields_write_primitive(primitive, CROP_FIELDS_QUAD_VERTICES, material,
													indices);
		}
		legacy_u8 indices[VINE_FIELD_SECTION_VERTICES];
		for (legacy_u16 point = 0; point < VINE_FIELD_SECTION_VERTICES; point++) {
			indices[point] = (legacy_u8)(first + point);
		}
		primitive = crop_fields_write_primitive(primitive, VINE_FIELD_SECTION_VERTICES,
												VINE_FIELD_SIDE_MATERIAL, indices);
		indices[0] = (legacy_u8)(first + VINE_FIELD_SECTION_VERTICES);
		for (legacy_u16 point = 1; point < VINE_FIELD_SECTION_VERTICES; point++) {
			indices[point] = (legacy_u8)(first + VINE_FIELD_ROW_VERTICES - point);
		}
		primitive = crop_fields_write_primitive(primitive, VINE_FIELD_SECTION_VERTICES,
												VINE_FIELD_SIDE_MATERIAL, indices);
	}
	return primitive;
}

legacy_u8 crop_fields_mesh_count(legacy_u16 id)
{
	if (id == CSTRK_TULIP_FIELD_ID) {
		return 1;
	}
	return id == CSTRK_VINE_FIELD_ID ? VINE_FIELD_MESH_COUNT : 0;
}

legacy_s16 crop_fields_create_mesh(struct SHAPE3D *mesh, legacy_u16 id, legacy_u8 part)
{
	if (!mesh) {
		return 0;
	}
	memset(mesh, 0, sizeof(*mesh));
	if (part >= crop_fields_mesh_count(id)) {
		return 0;
	}
	legacy_u8 tulips = id == CSTRK_TULIP_FIELD_ID;
	legacy_u8 grapes = !tulips && part != 0;
	legacy_u16 vertices = tulips   ? TULIP_FIELD_VERTEX_COUNT
						  : grapes ? VINE_FIELD_GRAPE_BATCH_VERTEX_COUNT
								   : VINE_FIELD_VERTEX_COUNT;
	legacy_u16 primitives = tulips	 ? TULIP_FIELD_PRIMITIVE_COUNT
							: grapes ? VINE_FIELD_GRAPE_BATCH_PRIMITIVE_COUNT
									 : VINE_FIELD_PRIMITIVE_COUNT;
	legacy_s16 height = tulips ? TULIP_FIELD_HEIGHT : VINE_FIELD_HEIGHT;
	size_t vertex_bytes = (size_t)vertices * SHAPE3D_VERTEX_SIZE;
	size_t primitive_bytes = tulips	  ? TULIP_FIELD_PRIMITIVE_BYTES
							 : grapes ? VINE_FIELD_GRAPE_BATCH_PRIMITIVE_BYTES
									  : VINE_FIELD_PRIMITIVE_BYTES;
	size_t mask_bytes = (size_t)primitives * SHAPE3D_VISIBILITY_MASK_SIZE;
	legacy_u8 *memory = malloc(vertex_bytes + primitive_bytes + mask_bytes * 2U);
	if (!memory) {
		return 0;
	}
	mesh->shape3d_numverts = vertices;
	mesh->shape3d_vertex_bytes = memory;
	mesh->shape3d_numprimitives = primitives;
	mesh->shape3d_numpaints = CROP_FIELDS_PAINT_COUNT;
	mesh->shape3d_primitives = memory + vertex_bytes;
	mesh->shape3d_visibility_masks = mesh->shape3d_primitives + primitive_bytes;
	mesh->shape3d_front_facing_masks = mesh->shape3d_visibility_masks + mask_bytes;
	memset(mesh->shape3d_visibility_masks, LEGACY_U8_MAX, mask_bytes);
	memset(mesh->shape3d_front_facing_masks, 0, mask_bytes);
	/* Keep the native culler's first/opposite corners at indices zero/four. */
	static const legacy_u8 bounds_order[CROP_FIELDS_BOUNDING_VERTICES] = {0, 1, 3, 2, 7, 6, 4, 5};
	for (legacy_u16 index = 0; index < CROP_FIELDS_BOUNDING_VERTICES; index++) {
		legacy_u8 corner = bounds_order[index];
		crop_fields_write_vertex(
			mesh, index, corner & 1U ? CROP_FIELDS_HALF_TILE : -CROP_FIELDS_HALF_TILE,
			corner & 2U ? height : 0, corner & 4U ? CROP_FIELDS_HALF_TILE : -CROP_FIELDS_HALF_TILE);
	}
	legacy_u8 *primitive = mesh->shape3d_primitives;
	if (grapes) {
		primitive = crop_fields_write_grapes(mesh, primitive, part - 1U);
	} else {
		primitive = crop_fields_write_quad(mesh, primitive, CROP_FIELDS_SOIL_VERTEX_FIRST,
										   -CROP_FIELDS_HALF_TILE, CROP_FIELDS_HALF_TILE,
										   CROP_FIELDS_SOIL_HEIGHT, CROP_FIELDS_SOIL_MATERIAL);
		primitive = tulips ? crop_fields_write_tulips(mesh, primitive)
						   : crop_fields_write_vines(mesh, primitive);
	}
	primitive[0] = primitive[1] = 0;
	return 1;
}
