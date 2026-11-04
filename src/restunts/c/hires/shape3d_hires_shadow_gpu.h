#ifndef RESTUNTS_SHAPE3D_HIRES_SHADOW_GPU_H
#define RESTUNTS_SHAPE3D_HIRES_SHADOW_GPU_H

#include "../legacy.h"

/* Return true when the native scene path owns shadow rendering, including
 * frames with no visible footprint. Software shadows remain a separate path. */
legacy_s32 shape3d_hires_draw_gpu_shadows(void);

#endif
