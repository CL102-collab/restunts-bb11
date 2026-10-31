#ifndef RESTUNTS_HIRES_FRAME_SNAPSHOT_H
#define RESTUNTS_HIRES_FRAME_SNAPSHOT_H

#include "../math.h"

struct GAMESTATE;
struct CARSTATE;
struct GHOST_CAMERA_STATE;

legacy_u8 frame_snapshot_active(void);
const struct CARSTATE *frame_snapshot_ghost(const struct CARSTATE *current);
const struct GHOST_CAMERA_STATE *
frame_snapshot_ghost_camera(const struct GHOST_CAMERA_STATE *current);
void frame_preserve_authoritative_events(struct GAMESTATE *presentation, struct CARSTATE *ghost,
										 struct GHOST_CAMERA_STATE *ghost_camera);

/* Render a visual snapshot without changing the simulation or its retained scratch. */
void update_frame_snapshot(legacy_s8 buffer_index, struct RECTANGLE *cliprect,
						   const struct GAMESTATE *render_state,
						   const struct CARSTATE *render_ghost,
						   const struct GHOST_CAMERA_STATE *render_ghost_camera);

#endif
