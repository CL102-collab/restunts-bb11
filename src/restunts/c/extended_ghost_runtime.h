#ifndef RESTUNTS_EXTENDED_GHOST_RUNTIME_H
#define RESTUNTS_EXTENDED_GHOST_RUNTIME_H
#include "legacy.h"
struct GHOST_CAMERA_STATE;
#ifdef RESTUNTS_SDL3
#include "extended_track.h"
#define EXTENDED_GHOST_LINE_GRID_SIZE EXTENDED_TRACK_MAX_DIMENSION
legacy_s16 extended_ghost_runtime_line_width(void);
legacy_s16 extended_ghost_runtime_line_height(void);
void extended_ghost_runtime_clear(void);
legacy_u32 extended_ghost_runtime_recorded_frames(legacy_u32 fallback);
legacy_u32 extended_ghost_runtime_pose_offset(legacy_u32 fallback);
legacy_s16 extended_ghost_runtime_check_track(void);
legacy_s16 extended_ghost_runtime_prepare_inputs(legacy_u32 pose_size, legacy_s16 *failure);
legacy_s16 extended_ghost_runtime_begin(void);
void extended_ghost_runtime_end(void);
void extended_ghost_runtime_camera(struct GHOST_CAMERA_STATE *camera);
#endif
#endif
