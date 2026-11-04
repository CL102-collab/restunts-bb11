#ifndef RESTUNTS_GPU_READBACK_H
#define RESTUNTS_GPU_READBACK_H

#include "../hires/hires.h"

/* Import completed GPU pixels without repeating the per-pixel raster API.
 * GPU samples contain color/touched, inverse-depth bits and depth family. */
void gpu_readback_hires(const struct HIRES_RASTER_TARGET *target, const legacy_u32 *samples);

#endif
