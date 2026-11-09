#ifndef RESTUNTS_CHECKPOINT_SHAPE_H
#define RESTUNTS_CHECKPOINT_SHAPE_H

#include "cstrk.h"
#include "../shape3d/shape3d.h"

#define CHECKPOINT_SHAPE_WHITE_MATERIAL 15U
#define CHECKPOINT_SHAPE_BLUE_MATERIAL 9U
#define CHECKPOINT_SHAPE_RED_MATERIAL 12U
#define CHECKPOINT_START_FINISH_ID 1U
#define CHECKPOINT_GATE_INNER_HALF_WIDTH 120
#define CHECKPOINT_GATE_OUTER_HALF_WIDTH 132
#define CHECKPOINT_GATE_UPPER_INNER_HALF_WIDTH 126
#define CHECKPOINT_GATE_LOWER_POLE_HEIGHT 43
#define CHECKPOINT_GATE_SIGN_BOTTOM 86
#define CHECKPOINT_GATE_SIGN_TOP 121
#define CHECKPOINT_GATE_HALF_DEPTH 6
#define CHECKPOINT_GATE_PLANE 0

/* The native fini/zfin mesh contains only the gate: its road is a separate
 * overlay, and the direction arrow belongs to the editor bitmap. Repaint a
 * mutable copy without changing the original resource or its visibility masks. */
void checkpoint_shape_modify(struct SHAPE3D *mesh, legacy_u16 checkpoint_id);

#endif
