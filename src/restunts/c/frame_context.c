#include "frame_render_private.h"

const struct CARSTATE *frame_second_car_state(void)
{
	if (gameconfig.game_opponenttype != 0) {
		return &frame_state->opponentstate;
	}
	return frame_snapshot_ghost(ghost_car_state());
}

const struct GHOST_CAMERA_STATE *frame_viewed_ghost_camera(void)
{
	if (followOpponentFlag == 0 || gameconfig.game_opponenttype != 0) {
		return 0;
	}
	return frame_snapshot_ghost_camera(ghost_camera_state());
}
