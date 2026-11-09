#include "checkpoint_shape.h"

#include <stdlib.h>
#include <string.h>

#define CHECKPOINT_SHAPE_TERMINATOR_BYTES 2U
#define CHECKPOINT_SHAPE_QUAD_VERTICES 4U
#define CHECKPOINT_SHAPE_INTERSECTIONS 2U
#define CHECKPOINT_SHAPE_SPLIT_PARTS 2U
#define CHECKPOINT_SHAPE_TOP_PART 0U
#define CHECKPOINT_SHAPE_BOTTOM_PART 1U

static const legacy_u8 checkpoint_vertex_counts[SHAPE3D_PRIMITIVE_TYPE_COUNT] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 2, 6, 3, 0, 0};

struct CHECKPOINT_BANNER_SPLIT {
	legacy_u8 indices[CHECKPOINT_SHAPE_SPLIT_PARTS][CHECKPOINT_SHAPE_QUAD_VERTICES];
	legacy_u8 counts[CHECKPOINT_SHAPE_SPLIT_PARTS];
	struct VECTOR intersections[CHECKPOINT_SHAPE_INTERSECTIONS];
	legacy_u8 intersection_count;
};

static struct VECTOR checkpoint_shape_vertex(const struct SHAPE3D *mesh, legacy_u8 index)
{
	const legacy_u8 *bytes = mesh->shape3d_vertex_bytes + index * SHAPE3D_VERTEX_SIZE;
	struct VECTOR point = {LEGACY_READ_S16_LE(bytes),
						   LEGACY_READ_S16_LE(bytes + SHAPE3D_VERTEX_Y_OFFSET),
						   LEGACY_READ_S16_LE(bytes + SHAPE3D_VERTEX_Z_OFFSET)};
	return point;
}

static legacy_u8 checkpoint_primitive_white(const struct SHAPE3D *mesh, const legacy_u8 *primitive)
{
	for (legacy_u16 paint = 0; paint < mesh->shape3d_numpaints; paint++) {
		if (primitive[SHAPE3D_PRIMITIVE_HEADER_SIZE + paint] == CHECKPOINT_SHAPE_WHITE_MATERIAL) {
			return 1;
		}
	}
	return 0;
}

static legacy_s16 checkpoint_split_append(struct CHECKPOINT_BANNER_SPLIT *split, legacy_u8 part,
										  legacy_u8 index)
{
	if (split->counts[part] == CHECKPOINT_SHAPE_QUAD_VERTICES) {
		return 0;
	}
	split->indices[part][split->counts[part]++] = index;
	return 1;
}

/* Split the native upright banner quad after yaw, before road grading. Both
 * faces retain their original winding and share the exact same seam vertices. */
static legacy_s16 checkpoint_split_banner(const struct SHAPE3D *mesh, const legacy_u8 *primitive,
										  legacy_u16 first_vertex,
										  struct CHECKPOINT_BANNER_SPLIT *split)
{
	memset(split, 0, sizeof(*split));
	if (!checkpoint_primitive_white(mesh, primitive)) {
		return 0;
	}
	legacy_u8 count = checkpoint_vertex_counts[primitive[0]];
	const legacy_u8 *indices = primitive + SHAPE3D_PRIMITIVE_HEADER_SIZE + mesh->shape3d_numpaints;
	legacy_s16 minimum = (legacy_s16)LEGACY_S16_MAX, maximum = -(legacy_s16)LEGACY_S16_MAX;
	for (legacy_u8 index = 0; index < count; index++) {
		legacy_s16 height = checkpoint_shape_vertex(mesh, indices[index]).y;
		if (height < minimum) {
			minimum = height;
		}
		if (height > maximum) {
			maximum = height;
		}
	}
	if (minimum >= CHECKPOINT_GATE_SIGN_SPLIT || maximum <= CHECKPOINT_GATE_SIGN_SPLIT) {
		return 0;
	}
	if (primitive[0] != CHECKPOINT_SHAPE_QUAD_VERTICES ||
		first_vertex + CHECKPOINT_SHAPE_INTERSECTIONS > LEGACY_U8_MAX) {
		return -1;
	}
	for (legacy_u8 index = 0; index < count; index++) {
		struct VECTOR first = checkpoint_shape_vertex(mesh, indices[index]);
		struct VECTOR second = checkpoint_shape_vertex(mesh, indices[(index + 1U) % count]);
		if (first.y >= CHECKPOINT_GATE_SIGN_SPLIT &&
			!checkpoint_split_append(split, CHECKPOINT_SHAPE_TOP_PART, indices[index])) {
			return -1;
		}
		if (first.y <= CHECKPOINT_GATE_SIGN_SPLIT &&
			!checkpoint_split_append(split, CHECKPOINT_SHAPE_BOTTOM_PART, indices[index])) {
			return -1;
		}
		if ((first.y < CHECKPOINT_GATE_SIGN_SPLIT && second.y > CHECKPOINT_GATE_SIGN_SPLIT) ||
			(first.y > CHECKPOINT_GATE_SIGN_SPLIT && second.y < CHECKPOINT_GATE_SIGN_SPLIT)) {
			legacy_u8 new_index = (legacy_u8)(first_vertex + split->intersection_count);
			if (split->intersection_count == CHECKPOINT_SHAPE_INTERSECTIONS ||
				!checkpoint_split_append(split, CHECKPOINT_SHAPE_TOP_PART, new_index) ||
				!checkpoint_split_append(split, CHECKPOINT_SHAPE_BOTTOM_PART, new_index)) {
				return -1;
			}
			legacy_s32 rise = CHECKPOINT_GATE_SIGN_SPLIT - first.y;
			legacy_s32 height = (legacy_s32)second.y - first.y;
			struct VECTOR *point = &split->intersections[split->intersection_count++];
			point->x = first.x + (legacy_s64)((legacy_s32)second.x - first.x) * rise / height;
			point->y = CHECKPOINT_GATE_SIGN_SPLIT;
			point->z = first.z + (legacy_s64)((legacy_s32)second.z - first.z) * rise / height;
		}
	}
	return split->counts[CHECKPOINT_SHAPE_TOP_PART] == CHECKPOINT_SHAPE_QUAD_VERTICES &&
				   split->counts[CHECKPOINT_SHAPE_BOTTOM_PART] == CHECKPOINT_SHAPE_QUAD_VERTICES &&
				   split->intersection_count == CHECKPOINT_SHAPE_INTERSECTIONS
			   ? 1
			   : -1;
}

static void checkpoint_repaint_primitive(legacy_u8 *primitive, legacy_u16 paints,
										 legacy_u8 material)
{
	for (legacy_u16 paint = 0; paint < paints; paint++) {
		legacy_u8 *selected = primitive + SHAPE3D_PRIMITIVE_HEADER_SIZE + paint;
		if (*selected == CHECKPOINT_SHAPE_WHITE_MATERIAL) {
			*selected = material;
		}
	}
}

/* Validate the complete layout before allocating or changing the owned mesh. */
static legacy_s16 checkpoint_combined_layout(const struct SHAPE3D *mesh, size_t *record_bytes,
											 legacy_u16 *vertices, legacy_u16 *primitives)
{
	const legacy_u8 *primitive = mesh->shape3d_primitives;
	*record_bytes = CHECKPOINT_SHAPE_TERMINATOR_BYTES;
	*vertices = mesh->shape3d_numverts;
	*primitives = mesh->shape3d_numprimitives;
	for (legacy_u16 index = 0; index < mesh->shape3d_numprimitives; index++) {
		legacy_u8 type = primitive[0];
		if (type >= SHAPE3D_PRIMITIVE_TYPE_COUNT || !checkpoint_vertex_counts[type]) {
			return 0;
		}
		legacy_u8 count = checkpoint_vertex_counts[type];
		size_t prefix = SHAPE3D_PRIMITIVE_HEADER_SIZE + mesh->shape3d_numpaints;
		for (legacy_u8 vertex = 0; vertex < count; vertex++) {
			if (primitive[prefix + vertex] >= mesh->shape3d_numverts) {
				return 0;
			}
		}
		*record_bytes += prefix + count;
		struct CHECKPOINT_BANNER_SPLIT split;
		legacy_s16 divided = checkpoint_split_banner(mesh, primitive, *vertices, &split);
		if (divided < 0 || (divided && *primitives == LEGACY_U16_MAX)) {
			return 0;
		}
		if (divided) {
			*vertices += CHECKPOINT_SHAPE_INTERSECTIONS;
			(*primitives)++;
			*record_bytes += prefix + CHECKPOINT_SHAPE_QUAD_VERTICES;
		}
		primitive += prefix + count;
	}
	return 1;
}

static legacy_s16 checkpoint_combined_shape(struct SHAPE3D *mesh)
{
	size_t record_bytes;
	legacy_u16 vertices, primitives;
	if (!checkpoint_combined_layout(mesh, &record_bytes, &vertices, &primitives)) {
		return 0;
	}
	size_t vertex_bytes = (size_t)vertices * SHAPE3D_VERTEX_SIZE;
	size_t mask_bytes = (size_t)primitives * SHAPE3D_VISIBILITY_MASK_SIZE;
	legacy_u8 *allocation = malloc(vertex_bytes + record_bytes + mask_bytes * 2U);
	if (!allocation) {
		return 0;
	}
	struct SHAPE3D result = *mesh;
	result.shape3d_numverts = vertices;
	result.shape3d_numprimitives = primitives;
	result.shape3d_vertex_bytes = allocation;
	result.shape3d_primitives = allocation + vertex_bytes;
	result.shape3d_visibility_masks = result.shape3d_primitives + record_bytes;
	result.shape3d_front_facing_masks = result.shape3d_visibility_masks + mask_bytes;
	memcpy(allocation, mesh->shape3d_vertex_bytes,
		   (size_t)mesh->shape3d_numverts * SHAPE3D_VERTEX_SIZE);
	const legacy_u8 *source = mesh->shape3d_primitives;
	legacy_u8 *output = result.shape3d_primitives;
	legacy_u16 next_vertex = mesh->shape3d_numverts, next_primitive = 0;
	size_t prefix = SHAPE3D_PRIMITIVE_HEADER_SIZE + mesh->shape3d_numpaints;
	for (legacy_u16 index = 0; index < mesh->shape3d_numprimitives; index++) {
		legacy_u8 count = checkpoint_vertex_counts[source[0]];
		struct CHECKPOINT_BANNER_SPLIT split;
		legacy_s16 divided = checkpoint_split_banner(mesh, source, next_vertex, &split);
		if (divided) {
			for (legacy_u8 vertex = 0; vertex < CHECKPOINT_SHAPE_INTERSECTIONS; vertex++) {
				const struct VECTOR *point = &split.intersections[vertex];
				legacy_u8 *bytes = allocation + (next_vertex + vertex) * SHAPE3D_VERTEX_SIZE;
				LEGACY_WRITE_U16_LE(bytes, (legacy_u16)point->x);
				LEGACY_WRITE_U16_LE(bytes + SHAPE3D_VERTEX_Y_OFFSET, (legacy_u16)point->y);
				LEGACY_WRITE_U16_LE(bytes + SHAPE3D_VERTEX_Z_OFFSET, (legacy_u16)point->z);
			}
			next_vertex += CHECKPOINT_SHAPE_INTERSECTIONS;
		}
		legacy_u8 parts = divided ? CHECKPOINT_SHAPE_SPLIT_PARTS : 1U;
		for (legacy_u8 part = 0; part < parts; part++) {
			memcpy(output, source, prefix);
			memcpy(output + prefix, divided ? split.indices[part] : source + prefix, count);
			legacy_u8 material = CHECKPOINT_SHAPE_BLUE_MATERIAL;
			if (divided ? part == CHECKPOINT_SHAPE_BOTTOM_PART
						: checkpoint_shape_vertex(mesh, source[prefix]).y <
							  CHECKPOINT_GATE_SIGN_SPLIT) {
				material = CHECKPOINT_SHAPE_RED_MATERIAL;
			}
			checkpoint_repaint_primitive(output, mesh->shape3d_numpaints, material);
			memcpy(result.shape3d_visibility_masks + next_primitive * SHAPE3D_VISIBILITY_MASK_SIZE,
				   mesh->shape3d_visibility_masks + index * SHAPE3D_VISIBILITY_MASK_SIZE,
				   SHAPE3D_VISIBILITY_MASK_SIZE);
			memcpy(result.shape3d_front_facing_masks +
					   next_primitive * SHAPE3D_VISIBILITY_MASK_SIZE,
				   mesh->shape3d_front_facing_masks + index * SHAPE3D_VISIBILITY_MASK_SIZE,
				   SHAPE3D_VISIBILITY_MASK_SIZE);
			output += prefix + count;
			next_primitive++;
		}
		source += prefix + count;
	}
	output[0] = 0;
	output[1] = 0;
	free(mesh->shape3d_vertex_bytes);
	*mesh = result;
	return 1;
}

legacy_s16 checkpoint_shape_modify(struct SHAPE3D *mesh, legacy_u16 checkpoint_id)
{
	if (checkpoint_id == CSTRK_COMBINED_CHECKPOINT_ID) {
		return checkpoint_combined_shape(mesh);
	}
	legacy_u8 material;
	if (checkpoint_id == CSTRK_BLUE_CHECKPOINT_ID) {
		material = CHECKPOINT_SHAPE_BLUE_MATERIAL;
	} else if (checkpoint_id == CSTRK_RED_CHECKPOINT_ID) {
		material = CHECKPOINT_SHAPE_RED_MATERIAL;
	} else {
		return 1;
	}
	legacy_u8 *primitive = mesh->shape3d_primitives;
	for (legacy_u16 index = 0; index < mesh->shape3d_numprimitives; index++) {
		if (primitive[0] >= SHAPE3D_PRIMITIVE_TYPE_COUNT ||
			!checkpoint_vertex_counts[primitive[0]]) {
			return 0;
		}
		checkpoint_repaint_primitive(primitive, mesh->shape3d_numpaints, material);
		primitive += SHAPE3D_PRIMITIVE_HEADER_SIZE + mesh->shape3d_numpaints +
					 checkpoint_vertex_counts[primitive[0]];
	}
	return 1;
}
