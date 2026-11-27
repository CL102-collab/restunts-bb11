#include "frame_render_private.h"

const struct CARSTATE *frame_second_car_state(void)
{
	if (gameconfig.game_opponenttype != 0) {
		return &frame_state->opponentstate;
	}
	return renderer_frame_snapshot_ghost(ghost_car_state());
}

const struct GHOST_CAMERA_STATE *frame_viewed_recorded_camera(void)
{
#ifdef RESTUNTS_MULTI_REPLAY
	const struct MULTI_REPLAY_CAR *replay_car = frame_viewed_multi_replay_car();
	if (replay_car != NULL) {
		return &replay_car->camera;
	}
#endif
	if (followOpponentFlag == 0 || gameconfig.game_opponenttype != 0) {
		return 0;
	}
	return renderer_frame_snapshot_ghost_camera(ghost_camera_state());
}
