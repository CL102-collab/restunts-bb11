#include "checkpoint_shape.h"

void checkpoint_shape_modify(struct SHAPE3D *mesh, legacy_u16 checkpoint_id)
{
	static const legacy_u8 vertex_counts[SHAPE3D_PRIMITIVE_TYPE_COUNT] = {0, 1, 2,	3, 4, 5, 6, 7,
																		  8, 9, 10, 2, 6, 3, 0, 0};
	legacy_u8 material;
	if (checkpoint_id == CSTRK_BLUE_CHECKPOINT_ID) {
		material = CHECKPOINT_SHAPE_BLUE_MATERIAL;
	} else if (checkpoint_id == CSTRK_RED_CHECKPOINT_ID) {
		material = CHECKPOINT_SHAPE_RED_MATERIAL;
	} else {
		return;
	}
	legacy_u8 *primitive = mesh->shape3d_primitives;
	for (legacy_u16 index = 0; index < mesh->shape3d_numprimitives; index++) {
		if (primitive[0] >= SHAPE3D_PRIMITIVE_TYPE_COUNT || !vertex_counts[primitive[0]]) {
			return;
		}
		for (legacy_u16 paint = 0; paint < mesh->shape3d_numpaints; paint++) {
			legacy_u8 *selected = primitive + SHAPE3D_PRIMITIVE_HEADER_SIZE + paint;
			if (*selected == CHECKPOINT_SHAPE_WHITE_MATERIAL) {
				*selected = material;
			}
		}
		primitive +=
			SHAPE3D_PRIMITIVE_HEADER_SIZE + mesh->shape3d_numpaints + vertex_counts[primitive[0]];
	}
}
