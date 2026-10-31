#ifndef RESTUNTS_HIRES_FRAME_SNAPSHOT_H
#define RESTUNTS_HIRES_FRAME_SNAPSHOT_H

#include "../math.h"

struct GAMESTATE;
struct CARSTATE;
struct GHOST_CAMERA_STATE;

extern const struct CARSTATE *frame_ghost;
extern const struct GHOST_CAMERA_STATE *frame_ghost_camera;
extern legacy_u8 frame_uses_snapshot;

/* Render a visual snapshot without changing the simulation or its retained scratch. */
void update_frame_snapshot(legacy_s8 buffer_index, struct RECTANGLE *cliprect,
						   const struct GAMESTATE *render_state,
						   const struct CARSTATE *render_ghost,
						   const struct GHOST_CAMERA_STATE *render_ghost_camera);

#endif
