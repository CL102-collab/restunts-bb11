#include "pine_trees.h"
#include "extended_geometry.h"
#include "../track/trackdata_layout.h"

/* Five-tree groves use every other entry of the same irregular scatter.
 * Keep this layout in sync with the track editor's pine-group icon. */
static const struct PINE_TREE_INSTANCE pine_trees_layout[CSTRK_PINE_GROUP_LARGE_COUNT] = {
	{-340, 300, 128}, {-80, 340, 176},	 {240, 310, 144},  {350, 70, 112},	 {130, 80, 192},
	{-220, 80, 152},  {-350, -190, 168}, {-90, -300, 120}, {200, -260, 160}, {350, -110, 136}};

legacy_s16 pine_trees_instance(const struct CSTRK_OBJECT *object, legacy_u8 index,
							   struct PINE_TREE_INSTANCE *instance)
{
	if (!object || !instance || object->legacy_id != CSTRK_PINE_TREE_ID ||
		(object->pine_count != CSTRK_PINE_GROUP_SMALL_COUNT &&
		 object->pine_count != CSTRK_PINE_GROUP_LARGE_COUNT) ||
		index >= object->pine_count) {
		return 0;
	}
	legacy_u8 slot = object->pine_count == CSTRK_PINE_GROUP_SMALL_COUNT
						 ? index * (CSTRK_PINE_GROUP_LARGE_COUNT / CSTRK_PINE_GROUP_SMALL_COUNT)
						 : index;
	*instance = pine_trees_layout[slot];
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
