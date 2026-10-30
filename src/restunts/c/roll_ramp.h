#ifndef RESTUNTS_ROLL_RAMP_H
#define RESTUNTS_ROLL_RAMP_H

#include "cstrk.h"
#include "shape3d.h"

#define ROLL_RAMP_LENGTH (2 * CSTRK_TILE_WORLD_SIZE)
#define ROLL_RAMP_HALF_LENGTH CSTRK_TILE_WORLD_SIZE
#define ROLL_RAMP_HALF_WIDTH 120
#define ROLL_RAMP_ENTRY_EASE 128
#define ROLL_RAMP_RISE_PER_TILE 390
#define ROLL_RAMP_DEGREES_PER_TURN 360L
#define ROLL_RAMP_LIP_BANK_DEGREES 71L
#define ROLL_RAMP_LIP_BANK                                                                         \
	((ANGLE_FULL_TURN * ROLL_RAMP_LIP_BANK_DEGREES) / ROLL_RAMP_DEGREES_PER_TURN)
#define ROLL_RAMP_SEGMENT_COUNT 32U
#define ROLL_RAMP_ROAD_TRIANGLE_COUNT (2U * ROLL_RAMP_SEGMENT_COUNT)
#define ROLL_RAMP_CONCRETE_MATERIAL 31U
#define ROLL_RAMP_CONCRETE_LIGHT_MATERIAL 32U
#define ROLL_RAMP_GRILLE_MATERIAL SHAPE3D_STOCK_GRILLE_MATERIAL
#define ROLL_RAMP_MARKING_MATERIAL 21U
#define ROLL_RAMP_GRILLE_MARKING_MATERIAL 24U
#define ROLL_RAMP_MARKING_HALF_WIDTH 2
#define ROLL_RAMP_MARKING_LENGTH 85
#define ROLL_RAMP_MARKINGS_PER_TILE 6U
#define ROLL_RAMP_MARKING_COUNT (2U * ROLL_RAMP_MARKINGS_PER_TILE)
#define ROLL_RAMP_MARKING_CLEARANCE 6
#define ROLL_RAMP_SUPPORT_BOX_COUNT 6U
#define ROLL_RAMP_LAUNCH_CONNECTION 6
#define ROLL_RAMP_JUMP_MAX_EMPTY_TILES 6U
#define ROLL_RAMP_ROUTE_POINT_COUNT 17U

struct ROLL_RAMP_BOX {
	struct VECTOR minimum;
	struct VECTOR maximum;
};

/* Canonical entry is z=+1024, departure is z=-1024, and lateral is local x.
 * The roll-ramp modifier is directional even though its native backing ID is not. */
legacy_s16 roll_ramp_orientation(const struct CSTRK_OBJECT *object);
legacy_s16 roll_ramp_center_height(legacy_s16 distance);
legacy_s16 roll_ramp_bank_angle(legacy_s16 distance);
void roll_ramp_sample(legacy_s16 distance, legacy_s16 lateral, struct VECTOR *point,
					  struct VECTOR *normal);
legacy_u16 roll_ramp_road_triangle_count(void);
legacy_s16 roll_ramp_road_triangle(legacy_u16 index, struct VECTOR vertices[3]);
legacy_u16 roll_ramp_support_box_count(void);
legacy_s16 roll_ramp_support_box(legacy_u16 index, struct ROLL_RAMP_BOX *box);
/* One owned allocation contains vertices, primitive bytes and visibility masks. */
legacy_s16 roll_ramp_create_mesh(struct SHAPE3D *mesh, legacy_u8 surface);
void roll_ramp_free_mesh(struct SHAPE3D *mesh);

#endif
