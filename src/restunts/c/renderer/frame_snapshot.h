#ifndef RESTUNTS_RENDERER_FRAME_SNAPSHOT_H
#define RESTUNTS_RENDERER_FRAME_SNAPSHOT_H

/* Snapshot substitution belongs to HyperVision; classic rendering uses live state. */
#if defined(RESTUNTS_SDL3)
#include "../hires/frame_snapshot.h"
#define renderer_frame_snapshot_ghost frame_hires_snapshot_ghost
#define renderer_frame_snapshot_ghost_camera frame_hires_snapshot_ghost_camera
#else
#define renderer_frame_snapshot_ghost(current) (current)
#define renderer_frame_snapshot_ghost_camera(current) (current)
#endif

#endif
