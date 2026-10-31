#include "../frame_render_private.h"

static legacy_u8 frame_uses_snapshot;

static const struct GHOST_CAMERA_STATE *frame_ghost_camera;

static const struct CARSTATE *frame_ghost;

/* A visual snapshot may lag behind physics. Damage, sinking and disappearing
 * cars still follow the authoritative events, including their original timing. */
void frame_preserve_authoritative_events(struct GAMESTATE *presentation, struct CARSTATE *ghost,
										 struct GHOST_CAMERA_STATE *ghost_camera)
{
	presentation->playerstate.car_crashBmpFlag = state.playerstate.car_crashBmpFlag;
	presentation->opponentstate.car_crashBmpFlag = state.opponentstate.car_crashBmpFlag;
	presentation->game_pEndFrame = state.game_pEndFrame;
	presentation->game_oEndFrame = state.game_oEndFrame;
	if (ghost != 0) {
		const struct CARSTATE *confirmed_ghost = ghost_car_state();
		ghost->car_crashBmpFlag =
			confirmed_ghost != 0 ? confirmed_ghost->car_crashBmpFlag : CRASH_EVENT_NONE;
	}
	if (ghost_camera != 0) {
		const struct GHOST_CAMERA_STATE *confirmed_camera = ghost_camera_state();
		if (confirmed_camera != 0) {
			ghost_camera->frame = confirmed_camera->frame;
			ghost_camera->crash_frame = confirmed_camera->crash_frame;
		}
	}
}

void update_frame_snapshot(legacy_s8 buffer_index, struct RECTANGLE *cliprect,
						   const struct GAMESTATE *render_state,
						   const struct CARSTATE *render_ghost,
						   const struct GHOST_CAMERA_STATE *render_ghost_camera)
{
	/* External-camera clearance borrows the collision and wheel-travel scratch.
	 * Extra presentations must not leave their visual geometry for physics. */
	struct TRACK_COLLISION_SNAPSHOT saved_collision;
	track_collision_capture(&saved_collision);
	legacy_s16 saved_plane = planindex_copy;
	legacy_s16 saved_heading = wheel_heading_offset;
	legacy_s16 saved_pitch = car_initial_pitch;
	legacy_s16 saved_roll = car_initial_roll;
	legacy_s16 saved_yaw = car_initial_yaw;
	struct VECTOR saved_forward_travel = wheel_forward_travel;
	struct VECTOR saved_world_travel = wheel_world_travel;
	struct LEGACY_EXECUTION_RESIDUE saved_residue = legacy_execution_residue;
	legacy_s16 saved_render_headings = legacy_render_player_headings_active;

	struct GAMESTATE presentation = *render_state;
	struct CARSTATE ghost;
	struct GHOST_CAMERA_STATE ghost_camera;
	if (render_ghost != 0) {
		ghost = *render_ghost;
	}
	if (render_ghost_camera != 0) {
		ghost_camera = *render_ghost_camera;
	}
	frame_preserve_authoritative_events(&presentation, render_ghost != 0 ? &ghost : 0,
										render_ghost_camera != 0 ? &ghost_camera : 0);
	frame_state = &presentation;
	frame_ghost = render_ghost != 0 ? &ghost : 0;
	frame_ghost_camera = render_ghost_camera != 0 ? &ghost_camera : 0;
	frame_uses_snapshot = 1;
	update_frame(buffer_index, cliprect);
	frame_uses_snapshot = 0;
	frame_state = &state;
	frame_ghost = 0;
	frame_ghost_camera = 0;

	track_collision_restore(&saved_collision);
	planindex_copy = saved_plane;
	wheel_heading_offset = saved_heading;
	car_initial_pitch = saved_pitch;
	car_initial_roll = saved_roll;
	car_initial_yaw = saved_yaw;
	wheel_forward_travel = saved_forward_travel;
	wheel_world_travel = saved_world_travel;
	legacy_execution_residue = saved_residue;
	legacy_render_player_headings_active = saved_render_headings;
}

const struct CARSTATE *frame_snapshot_ghost(const struct CARSTATE *current)
{
	return frame_uses_snapshot != 0 ? frame_ghost : current;
}

const struct GHOST_CAMERA_STATE *
frame_snapshot_ghost_camera(const struct GHOST_CAMERA_STATE *current)
{
	return frame_uses_snapshot != 0 ? frame_ghost_camera : current;
}

legacy_u8 frame_snapshot_active(void)
{
	return frame_uses_snapshot;
}
