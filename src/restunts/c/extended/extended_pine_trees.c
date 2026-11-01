#include "pine_trees.h"
#include "extended_geometry.h"
#include "../track/trackdata_layout.h"

/* Base positions receive coordinate-derived scatter before rotation. Keep the
 * layouts and scatter recipe in sync with the track editor. */
static const struct PINE_TREE_INSTANCE pine_trees_ten_layout[CSTRK_PINE_GROUP_LARGE_COUNT] = {
	{-330, 310, 128}, {15, 360, 176},	 {312, 263, 144},  {-197, 99, 112},	 {101, 128, 192},
	{348, -48, 152},  {-365, -151, 168}, {-61, -190, 120}, {221, -311, 160}, {-261, -367, 136}};
static const struct PINE_TREE_INSTANCE pine_trees_six_layout[CSTRK_PINE_GROUP_SMALL_COUNT] = {
	{-318, 274, 152}, {-47, 355, 192},	 {305, 171, 136},
	{171, -143, 208}, {-119, -326, 160}, {-363, -89, 176}};

#define PINE_SCATTER_COLUMN_SEED 0x9e3779b1U
#define PINE_SCATTER_ROW_SEED 0x85ebca77U
#define PINE_SCATTER_TREE_SEED 0xc2b2ae3dU
#define PINE_SCATTER_MIX_FIRST 0x7feb352dU
#define PINE_SCATTER_MIX_SECOND 0x846ca68bU
#define PINE_SCATTER_MIX_OUTER_SHIFT 16U
#define PINE_SCATTER_MIX_INNER_SHIFT 15U
#define PINE_SCATTER_Z_SEED 0x68bc21ebU
#define PINE_SCATTER_SCALE_SEED 0x02e5be93U
#define PINE_SCATTER_POSITION_RANGE 193U
#define PINE_SCATTER_POSITION_RADIUS 96
#define PINE_SCATTER_TEN_POSITION_RANGE 129U
#define PINE_SCATTER_TEN_POSITION_RADIUS 64
#define PINE_SCATTER_SCALE_RANGE 33U
#define PINE_SCATTER_SCALE_RADIUS 16

static legacy_u32 pine_trees_mix(legacy_u32 value)
{
	value ^= value >> PINE_SCATTER_MIX_OUTER_SHIFT;
	value *= PINE_SCATTER_MIX_FIRST;
	value ^= value >> PINE_SCATTER_MIX_INNER_SHIFT;
	value *= PINE_SCATTER_MIX_SECOND;
	return value ^ (value >> PINE_SCATTER_MIX_OUTER_SHIFT);
}

static void pine_trees_scatter(const struct CSTRK_OBJECT *object, legacy_u8 index,
							   struct PINE_TREE_INSTANCE *instance)
{
	legacy_u16 position_range = object->pine_count == CSTRK_PINE_GROUP_LARGE_COUNT
									? PINE_SCATTER_TEN_POSITION_RANGE
									: PINE_SCATTER_POSITION_RANGE;
	legacy_s16 position_radius = object->pine_count == CSTRK_PINE_GROUP_LARGE_COUNT
									 ? PINE_SCATTER_TEN_POSITION_RADIUS
									 : PINE_SCATTER_POSITION_RADIUS;
	legacy_u32 hash = pine_trees_mix((legacy_u32)object->x * PINE_SCATTER_COLUMN_SEED ^
									 (legacy_u32)object->y * PINE_SCATTER_ROW_SEED ^
									 (legacy_u32)(index + 1U) * PINE_SCATTER_TREE_SEED);
	instance->x += (legacy_s16)(hash % position_range) - position_radius;
	hash = pine_trees_mix(hash ^ PINE_SCATTER_Z_SEED);
	instance->z += (legacy_s16)(hash % position_range) - position_radius;
	hash = pine_trees_mix(hash ^ PINE_SCATTER_SCALE_SEED);
	instance->scale = (legacy_u16)(instance->scale + (legacy_s16)(hash % PINE_SCATTER_SCALE_RANGE) -
								   PINE_SCATTER_SCALE_RADIUS);
}

legacy_s16 pine_trees_instance(const struct CSTRK_OBJECT *object, legacy_u8 index,
							   struct PINE_TREE_INSTANCE *instance)
{
	if (!object || !instance || object->legacy_id != CSTRK_PINE_TREE_ID ||
		(object->pine_count != CSTRK_PINE_GROUP_SMALL_COUNT &&
		 object->pine_count != CSTRK_PINE_GROUP_LARGE_COUNT) ||
		index >= object->pine_count) {
		return 0;
	}
	if (object->pine_count == CSTRK_PINE_GROUP_SMALL_COUNT) {
		*instance = pine_trees_six_layout[index];
	} else {
		*instance = pine_trees_ten_layout[index];
	}
	pine_trees_scatter(object, index, instance);
	legacy_s16 x = instance->x, z = instance->z;
	switch (object->rotation) {
		case 1:
			instance->x = z;
			instance->z = -x;
			break;
		case 2:
			instance->x = -x;
			instance->z = -z;
			break;
		case 3:
			instance->x = -z;
			instance->z = x;
			break;
	}
	return 1;
}

legacy_s16 pine_trees_create_mesh(const struct SHAPE3D *source, struct SHAPE3D *destination,
								  const struct CSTRK_OBJECT *object, legacy_u8 index)
{
	struct PINE_TREE_INSTANCE instance;
	if (!pine_trees_instance(object, index, &instance) ||
		!extended_geometry_transform_shape(source, destination,
										   object->rotation * ANGLE_QUARTER_TURN, CSTRK_ROAD_PAVED,
										   PHYSICAL_MODEL_PINE_TREE)) {
		return 0;
	}
	for (legacy_u16 vertex = 0; vertex < destination->shape3d_numverts; vertex++) {
		legacy_u8 *bytes = destination->shape3d_vertex_bytes + (size_t)vertex * SHAPE3D_VERTEX_SIZE;
		legacy_s16 x = (legacy_s16)((legacy_s32)LEGACY_READ_S16_LE(bytes) * instance.scale /
										(legacy_s32)PINE_TREE_SCALE_ONE +
									instance.x);
		legacy_s16 y =
			(legacy_s16)((legacy_s32)LEGACY_READ_S16_LE(bytes + SHAPE3D_VERTEX_Y_OFFSET) *
						 instance.scale / (legacy_s32)PINE_TREE_SCALE_ONE);
		legacy_s16 z =
			(legacy_s16)((legacy_s32)LEGACY_READ_S16_LE(bytes + SHAPE3D_VERTEX_Z_OFFSET) *
							 instance.scale / (legacy_s32)PINE_TREE_SCALE_ONE +
						 instance.z);
		LEGACY_WRITE_U16_LE(bytes, (legacy_u16)x);
		LEGACY_WRITE_U16_LE(bytes + SHAPE3D_VERTEX_Y_OFFSET, (legacy_u16)y);
		LEGACY_WRITE_U16_LE(bytes + SHAPE3D_VERTEX_Z_OFFSET, (legacy_u16)z);
	}
	return 1;
}
