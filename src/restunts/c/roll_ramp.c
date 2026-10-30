#include "roll_ramp.h"

#include <stdlib.h>
#include <string.h>

#define ROLL_RAMP_SECTION_COUNT (ROLL_RAMP_SEGMENT_COUNT + 1U)
#define ROLL_RAMP_SEGMENT_LENGTH (ROLL_RAMP_LENGTH / ROLL_RAMP_SEGMENT_COUNT)
#define ROLL_RAMP_CONCRETE_SEGMENTS (ROLL_RAMP_SEGMENT_COUNT / 2U)
#define ROLL_RAMP_BOUNDING_VERTICES 8U
#define ROLL_RAMP_ROAD_VERTEX_FIRST ROLL_RAMP_BOUNDING_VERTICES
#define ROLL_RAMP_BOTTOM_VERTEX_FIRST (ROLL_RAMP_ROAD_VERTEX_FIRST + 2U * ROLL_RAMP_SECTION_COUNT)
#define ROLL_RAMP_BOX_VERTEX_FIRST                                                                 \
	(ROLL_RAMP_BOTTOM_VERTEX_FIRST + 2U * (ROLL_RAMP_CONCRETE_SEGMENTS + 1U))
#define ROLL_RAMP_MARKING_VERTEX_FIRST                                                             \
	(ROLL_RAMP_BOX_VERTEX_FIRST + ROLL_RAMP_SUPPORT_BOX_COUNT * ROLL_RAMP_BOUNDING_VERTICES)
#define ROLL_RAMP_MARKING_VERTEX_COUNT 4U
#define ROLL_RAMP_VERTEX_COUNT                                                                     \
	(ROLL_RAMP_MARKING_VERTEX_FIRST + ROLL_RAMP_MARKING_COUNT * ROLL_RAMP_MARKING_VERTEX_COUNT)
#define ROLL_RAMP_BOX_FACE_COUNT 6U
#define ROLL_RAMP_STRUCTURAL_FACE_COUNT                                                            \
	(2U * ROLL_RAMP_CONCRETE_SEGMENTS + 2U + ROLL_RAMP_SUPPORT_BOX_COUNT * ROLL_RAMP_BOX_FACE_COUNT)
#define ROLL_RAMP_FACE_COUNT                                                                       \
	(ROLL_RAMP_ROAD_TRIANGLE_COUNT + ROLL_RAMP_STRUCTURAL_FACE_COUNT + ROLL_RAMP_MARKING_COUNT)
#define ROLL_RAMP_TRIANGLE_BYTES 6U
#define ROLL_RAMP_QUAD_BYTES 7U
#define ROLL_RAMP_PRIMITIVE_BYTES                                                                  \
	(ROLL_RAMP_ROAD_TRIANGLE_COUNT * ROLL_RAMP_TRIANGLE_BYTES +                                    \
	 (ROLL_RAMP_STRUCTURAL_FACE_COUNT + ROLL_RAMP_MARKING_COUNT) * ROLL_RAMP_QUAD_BYTES + 2U)
#define ROLL_RAMP_ALWAYS_VISIBLE 1U
#define ROLL_RAMP_NORMAL_SCALE (TRIG_FIXED_ONE / 2)
#define ROLL_RAMP_FULL_TURN_RADIANS 6.28318530717958647693
#define ROLL_RAMP_SUPPORT_HALF_WIDTH 20
#define ROLL_RAMP_SUPPORT_HALF_DEPTH 20
#define ROLL_RAMP_SUPPORT_X 190
#define ROLL_RAMP_SUPPORT_FIRST_DISTANCE (CSTRK_TILE_WORLD_SIZE + CSTRK_TILE_WORLD_SIZE / 4)
#define ROLL_RAMP_SUPPORT_DISTANCE_STEP (CSTRK_TILE_WORLD_SIZE / 2)
#define ROLL_RAMP_BEAM_HEIGHT 24
#define ROLL_RAMP_PAVED_MATERIAL 19U
#define ROLL_RAMP_DIRT_MATERIAL 25U
#define ROLL_RAMP_ICE_MATERIAL 28U

legacy_s16 roll_ramp_orientation(const struct CSTRK_OBJECT *object)
{
	legacy_u16 native = object->legacy_id == CSTRK_ROLL_RAMP_LAST_ID ? ANGLE_QUARTER_TURN : 0;
	return (legacy_s16)((native + object->rotation * ANGLE_QUARTER_TURN) & ANGLE_MASK);
}

static legacy_s16 roll_ramp_clamp_distance(legacy_s16 distance)
{
	return distance < 0 ? 0 : distance > ROLL_RAMP_LENGTH ? ROLL_RAMP_LENGTH : distance;
}

legacy_s16 roll_ramp_center_height(legacy_s16 distance)
{
	legacy_s32 along = roll_ramp_clamp_distance(distance);
	legacy_s32 numerator, denominator;
	if (along < ROLL_RAMP_ENTRY_EASE) {
		numerator = ROLL_RAMP_RISE_PER_TILE * along * along;
		denominator = 2L * ROLL_RAMP_ENTRY_EASE * CSTRK_TILE_WORLD_SIZE;
	} else {
		numerator = ROLL_RAMP_RISE_PER_TILE * (along - ROLL_RAMP_ENTRY_EASE / 2);
		denominator = CSTRK_TILE_WORLD_SIZE;
	}
	return (legacy_s16)((numerator + denominator / 2) / denominator);
}

legacy_s16 roll_ramp_bank_angle(legacy_s16 distance)
{
	legacy_s16 along = roll_ramp_clamp_distance(distance);
	if (along <= CSTRK_TILE_WORLD_SIZE) {
		return 0;
	}
	legacy_f64 q = (legacy_f64)(along - CSTRK_TILE_WORLD_SIZE) / CSTRK_TILE_WORLD_SIZE;
	/* The departure derivative remains nonzero so wheel contact can impart
	 * angular momentum. A conventional smoothstep would stop rolling at the lip. */
	return (legacy_s16)(ROLL_RAMP_LIP_BANK * (2 * q * q - q * q * q) + .5);
}

static legacy_s16 roll_ramp_scaled_lateral(legacy_s16 lateral, legacy_s16 factor)
{
	legacy_s32 product = (legacy_s32)lateral * factor;
	product += product < 0 ? -TRIG_FIXED_ONE / 2 : TRIG_FIXED_ONE / 2;
	return (legacy_s16)(product / TRIG_FIXED_ONE);
}

void roll_ramp_sample(legacy_s16 distance, legacy_s16 lateral, struct VECTOR *point,
					  struct VECTOR *normal)
{
	legacy_s16 along = roll_ramp_clamp_distance(distance);
	legacy_u16 angle = (legacy_u16)roll_ramp_bank_angle(along);
	legacy_s16 sine = sin_fast(angle), cosine = cos_fast(angle);
	point->x = roll_ramp_scaled_lateral(lateral, cosine);
	point->y = roll_ramp_center_height(along) + roll_ramp_scaled_lateral(lateral, sine);
	point->z = ROLL_RAMP_HALF_LENGTH - along;
	if (normal) {
		legacy_f64 slope = (legacy_f64)ROLL_RAMP_RISE_PER_TILE / CSTRK_TILE_WORLD_SIZE;
		if (along < ROLL_RAMP_ENTRY_EASE) {
			slope *= (legacy_f64)along / ROLL_RAMP_ENTRY_EASE;
		}
		legacy_f64 bank_slope = 0;
		if (along > CSTRK_TILE_WORLD_SIZE) {
			legacy_f64 q = (legacy_f64)(along - CSTRK_TILE_WORLD_SIZE) / CSTRK_TILE_WORLD_SIZE;
			bank_slope = ROLL_RAMP_LIP_BANK * ROLL_RAMP_FULL_TURN_RADIANS / ANGLE_FULL_TURN *
						 (4 * q - 3 * q * q) / CSTRK_TILE_WORLD_SIZE;
		}
		normal->x = -sine / 2;
		normal->y = cosine / 2;
		normal->z = (legacy_s16)((cosine * slope + lateral * bank_slope * TRIG_FIXED_ONE) / 2);
		legacy_s16 length = polarRadius3D(normal);
		if (length) {
			normal->x = (legacy_s16)((legacy_s32)normal->x * ROLL_RAMP_NORMAL_SCALE / length);
			normal->y = (legacy_s16)((legacy_s32)normal->y * ROLL_RAMP_NORMAL_SCALE / length);
			normal->z = (legacy_s16)((legacy_s32)normal->z * ROLL_RAMP_NORMAL_SCALE / length);
		}
	}
}

legacy_u16 roll_ramp_road_triangle_count(void)
{
	return ROLL_RAMP_ROAD_TRIANGLE_COUNT;
}

legacy_s16 roll_ramp_road_triangle(legacy_u16 index, struct VECTOR vertices[3])
{
	if (index >= ROLL_RAMP_ROAD_TRIANGLE_COUNT) {
		return 0;
	}
	legacy_s16 start = (legacy_s16)((index / 2U) * ROLL_RAMP_SEGMENT_LENGTH);
	struct VECTOR left, right, next_left, next_right;
	roll_ramp_sample(start, -ROLL_RAMP_HALF_WIDTH, &left, NULL);
	roll_ramp_sample(start, ROLL_RAMP_HALF_WIDTH, &right, NULL);
	roll_ramp_sample(start + ROLL_RAMP_SEGMENT_LENGTH, -ROLL_RAMP_HALF_WIDTH, &next_left, NULL);
	roll_ramp_sample(start + ROLL_RAMP_SEGMENT_LENGTH, ROLL_RAMP_HALF_WIDTH, &next_right, NULL);
	vertices[0] = left;
	vertices[1] = index % 2U ? next_right : right;
	vertices[2] = index % 2U ? next_left : next_right;
	return 1;
}

legacy_u16 roll_ramp_support_box_count(void)
{
	return ROLL_RAMP_SUPPORT_BOX_COUNT;
}

static legacy_s16 roll_ramp_support_edge_height(legacy_s16 distance)
{
	legacy_s16 along = roll_ramp_clamp_distance(distance);
	legacy_s16 start = along / ROLL_RAMP_SEGMENT_LENGTH * ROLL_RAMP_SEGMENT_LENGTH;
	struct VECTOR first, second;
	roll_ramp_sample(start, -ROLL_RAMP_HALF_WIDTH, &first, NULL);
	roll_ramp_sample(start + ROLL_RAMP_SEGMENT_LENGTH, -ROLL_RAMP_HALF_WIDTH, &second, NULL);
	/* The rendered strip is linear between section vertices. Round down so
	 * the flat support touches its lower edge without entering the road. */
	return (legacy_s16)(((legacy_s32)first.y * ROLL_RAMP_SEGMENT_LENGTH +
						 (legacy_s32)(second.y - first.y) * (along - start)) /
						ROLL_RAMP_SEGMENT_LENGTH);
}

legacy_s16 roll_ramp_support_box(legacy_u16 index, struct ROLL_RAMP_BOX *box)
{
	if (index >= ROLL_RAMP_SUPPORT_BOX_COUNT) {
		return 0;
	}
	legacy_u16 support = index / 3U, part = index % 3U;
	legacy_s16 distance =
		ROLL_RAMP_SUPPORT_FIRST_DISTANCE + support * ROLL_RAMP_SUPPORT_DISTANCE_STEP;
	legacy_s16 start = distance - ROLL_RAMP_SUPPORT_HALF_DEPTH;
	legacy_s16 end = distance + ROLL_RAMP_SUPPORT_HALF_DEPTH;
	legacy_s16 top = roll_ramp_support_edge_height(start);
	/* Include section boundaries so the whole crossbeam remains below the
	 * finite triangle mesh even if the bank profile changes in the future. */
	for (legacy_s16 along = start / ROLL_RAMP_SEGMENT_LENGTH * ROLL_RAMP_SEGMENT_LENGTH;
		 along <= end; along += ROLL_RAMP_SEGMENT_LENGTH) {
		if (along >= start) {
			legacy_s16 height = roll_ramp_support_edge_height(along);
			if (height < top) {
				top = height;
			}
		}
	}
	legacy_s16 end_height = roll_ramp_support_edge_height(end);
	if (end_height < top) {
		top = end_height;
	}
	legacy_s16 z = ROLL_RAMP_HALF_LENGTH - distance;
	if (part < 2U) {
		legacy_s16 x = part ? ROLL_RAMP_SUPPORT_X : -ROLL_RAMP_SUPPORT_X;
		box->minimum =
			(struct VECTOR){x - ROLL_RAMP_SUPPORT_HALF_WIDTH, 0, z - ROLL_RAMP_SUPPORT_HALF_DEPTH};
		box->maximum = (struct VECTOR){x + ROLL_RAMP_SUPPORT_HALF_WIDTH, top,
									   z + ROLL_RAMP_SUPPORT_HALF_DEPTH};
	} else {
		box->minimum =
			(struct VECTOR){-ROLL_RAMP_SUPPORT_X - ROLL_RAMP_SUPPORT_HALF_WIDTH,
							top - ROLL_RAMP_BEAM_HEIGHT, z - ROLL_RAMP_SUPPORT_HALF_DEPTH};
		box->maximum = (struct VECTOR){ROLL_RAMP_SUPPORT_X + ROLL_RAMP_SUPPORT_HALF_WIDTH, top,
									   z + ROLL_RAMP_SUPPORT_HALF_DEPTH};
	}
	return 1;
}

static void roll_ramp_write_vertex(struct SHAPE3D *mesh, legacy_u16 index,
								   const struct VECTOR *point)
{
	legacy_u8 *bytes = mesh->shape3d_vertex_bytes + index * SHAPE3D_VERTEX_SIZE;
	LEGACY_WRITE_U16_LE(bytes, (legacy_u16)point->x);
	LEGACY_WRITE_U16_LE(bytes + SHAPE3D_VERTEX_Y_OFFSET, (legacy_u16)point->y);
	LEGACY_WRITE_U16_LE(bytes + SHAPE3D_VERTEX_Z_OFFSET, (legacy_u16)point->z);
}

static legacy_u8 *roll_ramp_write_face(legacy_u8 *bytes, legacy_u8 count, legacy_u8 material,
									   const legacy_u8 *indices)
{
	bytes[0] = count;
	bytes[1] = ROLL_RAMP_ALWAYS_VISIBLE;
	bytes[2] = material;
	memcpy(bytes + 3U, indices, count);
	return bytes + 3U + count;
}

static void roll_ramp_write_box(struct SHAPE3D *mesh, legacy_u16 first,
								const struct ROLL_RAMP_BOX *box)
{
	for (legacy_u16 corner = 0; corner < ROLL_RAMP_BOUNDING_VERTICES; corner++) {
		struct VECTOR point = {corner & 1U ? box->maximum.x : box->minimum.x,
							   corner & 2U ? box->maximum.y : box->minimum.y,
							   corner & 4U ? box->maximum.z : box->minimum.z};
		roll_ramp_write_vertex(mesh, first + corner, &point);
	}
}

legacy_s16 roll_ramp_create_mesh(struct SHAPE3D *mesh, legacy_u8 surface)
{
	memset(mesh, 0, sizeof(*mesh));
	if (surface > CSTRK_ROAD_ICE) {
		return 0;
	}
	size_t vertex_bytes = ROLL_RAMP_VERTEX_COUNT * SHAPE3D_VERTEX_SIZE;
	size_t mask_bytes = ROLL_RAMP_FACE_COUNT * SHAPE3D_VISIBILITY_MASK_SIZE;
	legacy_u8 *memory = malloc(vertex_bytes + ROLL_RAMP_PRIMITIVE_BYTES + mask_bytes * 2U);
	if (!memory) {
		return 0;
	}
	mesh->shape3d_numverts = ROLL_RAMP_VERTEX_COUNT;
	mesh->shape3d_vertex_bytes = memory;
	mesh->shape3d_numprimitives = ROLL_RAMP_FACE_COUNT;
	mesh->shape3d_numpaints = 1;
	mesh->shape3d_primitives = memory + vertex_bytes;
	mesh->shape3d_visibility_masks = mesh->shape3d_primitives + ROLL_RAMP_PRIMITIVE_BYTES;
	mesh->shape3d_front_facing_masks = mesh->shape3d_visibility_masks + mask_bytes;
	memset(mesh->shape3d_visibility_masks, 255, mask_bytes);
	memset(mesh->shape3d_front_facing_masks, 0, mask_bytes);
	struct ROLL_RAMP_BOX bounds = {
		{-ROLL_RAMP_SUPPORT_X - ROLL_RAMP_SUPPORT_HALF_WIDTH, 0, -ROLL_RAMP_HALF_LENGTH},
		{ROLL_RAMP_SUPPORT_X + ROLL_RAMP_SUPPORT_HALF_WIDTH,
		 roll_ramp_center_height(ROLL_RAMP_LENGTH) + ROLL_RAMP_HALF_WIDTH, ROLL_RAMP_HALF_LENGTH}};
	/* The native culler reads vertices0 and4 as opposite enclosing corners. */
	static const legacy_u8 bounds_order[ROLL_RAMP_BOUNDING_VERTICES] = {0, 1, 3, 2, 7, 6, 4, 5};
	for (legacy_u16 index = 0; index < ROLL_RAMP_BOUNDING_VERTICES; index++) {
		legacy_u8 corner = bounds_order[index];
		struct VECTOR point = {corner & 1U ? bounds.maximum.x : bounds.minimum.x,
							   corner & 2U ? bounds.maximum.y : bounds.minimum.y,
							   corner & 4U ? bounds.maximum.z : bounds.minimum.z};
		roll_ramp_write_vertex(mesh, index, &point);
	}
	for (legacy_u16 section = 0; section < ROLL_RAMP_SECTION_COUNT; section++) {
		struct VECTOR left, right;
		legacy_s16 distance = (legacy_s16)(section * ROLL_RAMP_SEGMENT_LENGTH);
		roll_ramp_sample(distance, -ROLL_RAMP_HALF_WIDTH, &left, NULL);
		roll_ramp_sample(distance, ROLL_RAMP_HALF_WIDTH, &right, NULL);
		roll_ramp_write_vertex(mesh, ROLL_RAMP_ROAD_VERTEX_FIRST + section * 2U, &left);
		roll_ramp_write_vertex(mesh, ROLL_RAMP_ROAD_VERTEX_FIRST + section * 2U + 1U, &right);
		if (section <= ROLL_RAMP_CONCRETE_SEGMENTS) {
			left.y = right.y = 0;
			roll_ramp_write_vertex(mesh, ROLL_RAMP_BOTTOM_VERTEX_FIRST + section * 2U, &left);
			roll_ramp_write_vertex(mesh, ROLL_RAMP_BOTTOM_VERTEX_FIRST + section * 2U + 1U, &right);
		}
	}
	legacy_u8 *primitive = mesh->shape3d_primitives;
	legacy_u8 road_material = surface == CSTRK_ROAD_DIRT  ? ROLL_RAMP_DIRT_MATERIAL
							  : surface == CSTRK_ROAD_ICE ? ROLL_RAMP_ICE_MATERIAL
														  : ROLL_RAMP_PAVED_MATERIAL;
	for (legacy_u16 segment = 0; segment < ROLL_RAMP_SEGMENT_COUNT; segment++) {
		legacy_u8 first = (legacy_u8)(ROLL_RAMP_ROAD_VERTEX_FIRST + segment * 2U);
		legacy_u8 first_triangle[] = {first, first + 1U, first + 3U};
		legacy_u8 second_triangle[] = {first, first + 3U, first + 2U};
		legacy_u8 material =
			segment < ROLL_RAMP_CONCRETE_SEGMENTS ? road_material : ROLL_RAMP_GRILLE_MATERIAL;
		primitive = roll_ramp_write_face(primitive, 3, material, first_triangle);
		primitive = roll_ramp_write_face(primitive, 3, material, second_triangle);
	}
	for (legacy_u16 segment = 0; segment < ROLL_RAMP_CONCRETE_SEGMENTS; segment++) {
		legacy_u8 top = (legacy_u8)(ROLL_RAMP_ROAD_VERTEX_FIRST + segment * 2U);
		legacy_u8 bottom = (legacy_u8)(ROLL_RAMP_BOTTOM_VERTEX_FIRST + segment * 2U);
		legacy_u8 left[] = {top, top + 2U, bottom + 2U, bottom};
		legacy_u8 right[] = {top + 1U, bottom + 1U, bottom + 3U, top + 3U};
		primitive = roll_ramp_write_face(primitive, 4, ROLL_RAMP_CONCRETE_MATERIAL, left);
		primitive = roll_ramp_write_face(primitive, 4, ROLL_RAMP_CONCRETE_LIGHT_MATERIAL, right);
	}
	legacy_u8 bottom = ROLL_RAMP_BOTTOM_VERTEX_FIRST;
	legacy_u8 bottom_end = ROLL_RAMP_BOTTOM_VERTEX_FIRST + ROLL_RAMP_CONCRETE_SEGMENTS * 2U;
	legacy_u8 top_end = ROLL_RAMP_ROAD_VERTEX_FIRST + ROLL_RAMP_CONCRETE_SEGMENTS * 2U;
	legacy_u8 base[] = {bottom, bottom + 1U, bottom_end + 1U, bottom_end};
	legacy_u8 end[] = {top_end, top_end + 1U, bottom_end + 1U, bottom_end};
	primitive = roll_ramp_write_face(primitive, 4, ROLL_RAMP_CONCRETE_MATERIAL, base);
	primitive = roll_ramp_write_face(primitive, 4, ROLL_RAMP_CONCRETE_MATERIAL, end);
	static const legacy_u8 box_faces[ROLL_RAMP_BOX_FACE_COUNT][4] = {
		{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
	for (legacy_u16 index = 0; index < ROLL_RAMP_SUPPORT_BOX_COUNT; index++) {
		struct ROLL_RAMP_BOX box;
		roll_ramp_support_box(index, &box);
		legacy_u8 first =
			(legacy_u8)(ROLL_RAMP_BOX_VERTEX_FIRST + index * ROLL_RAMP_BOUNDING_VERTICES);
		roll_ramp_write_box(mesh, first, &box);
		for (legacy_u16 face = 0; face < ROLL_RAMP_BOX_FACE_COUNT; face++) {
			legacy_u8 corners[4];
			for (legacy_u16 corner = 0; corner < 4U; corner++) {
				corners[corner] = first + box_faces[face][corner];
			}
			primitive = roll_ramp_write_face(primitive, 4,
											 face % 2U ? ROLL_RAMP_CONCRETE_LIGHT_MATERIAL
													   : ROLL_RAMP_CONCRETE_MATERIAL,
											 corners);
		}
	}
	/* Match the stock road's six narrow, 85-unit yellow dashes per tile. The
	 * grille uses its own masked yellow paint, sharing the road's stock mask.
	 * Lift the strips above the small center ridges in the triangulated twist;
	 * this artwork never changes the finite collision triangles. */
	for (legacy_u16 dash = 0; dash < ROLL_RAMP_MARKING_COUNT; dash++) {
		legacy_s16 start = (dash / ROLL_RAMP_MARKINGS_PER_TILE) * CSTRK_TILE_WORLD_SIZE +
						   (dash % ROLL_RAMP_MARKINGS_PER_TILE) * CSTRK_TILE_WORLD_SIZE /
							   ROLL_RAMP_MARKINGS_PER_TILE;
		legacy_u8 first =
			(legacy_u8)(ROLL_RAMP_MARKING_VERTEX_FIRST + dash * ROLL_RAMP_MARKING_VERTEX_COUNT);
		for (legacy_u16 corner = 0; corner < ROLL_RAMP_MARKING_VERTEX_COUNT; corner++) {
			legacy_s16 distance = start + (corner >= 2U ? ROLL_RAMP_MARKING_LENGTH : 0);
			legacy_s16 lateral =
				corner & 1U ? ROLL_RAMP_MARKING_HALF_WIDTH : -ROLL_RAMP_MARKING_HALF_WIDTH;
			struct VECTOR point;
			roll_ramp_sample(distance, lateral, &point, NULL);
			point.y += ROLL_RAMP_MARKING_CLEARANCE;
			roll_ramp_write_vertex(mesh, first + corner, &point);
		}
		legacy_u8 indices[ROLL_RAMP_MARKING_VERTEX_COUNT];
		indices[0] = first;
		indices[1] = first + 1U;
		indices[2] = first + 3U;
		indices[3] = first + 2U;
		legacy_u8 material = dash < ROLL_RAMP_MARKINGS_PER_TILE ? ROLL_RAMP_MARKING_MATERIAL
																: ROLL_RAMP_GRILLE_MARKING_MATERIAL;
		primitive = roll_ramp_write_face(primitive, 4, material, indices);
	}
	primitive[0] = primitive[1] = 0;
	return 1;
}

void roll_ramp_free_mesh(struct SHAPE3D *mesh)
{
	free(mesh->shape3d_vertex_bytes);
	memset(mesh, 0, sizeof(*mesh));
}
