#include "extended_gameinit.h"
#include "../externs.h"
#include "extended_track.h"
#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
#include "extended_replay_runtime.h"
#endif
#define EXTENDED_START_TILE_SIZE 1024L
#define EXTENDED_START_TILE_CENTER (EXTENDED_START_TILE_SIZE / 2L)
#define EXTENDED_START_POSITION_SCALE 64L
legacy_s16 extended_gameinit_car_start(struct CARSTATE *carstate, struct SIMD *simd,
									   legacy_s8 transmission, legacy_s16 column_offset,
									   legacy_s16 row_offset)
{
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		legacy_s32 x = (legacy_s32)start_finish_column * EXTENDED_START_TILE_SIZE +
					   EXTENDED_START_TILE_CENTER + column_offset;
		legacy_s32 z = ((legacy_s32)extended_track_height() - 1L - start_finish_row) *
						   EXTENDED_START_TILE_SIZE +
					   EXTENDED_START_TILE_CENTER + row_offset;
		legacy_s32 y = extended_track_elevation(start_finish_column, start_finish_row,
												EXTENDED_START_TILE_CENTER + column_offset,
												EXTENDED_START_TILE_CENTER - row_offset);
		init_carstate_from_simd(carstate, simd, transmission, x * EXTENDED_START_POSITION_SCALE,
								y * EXTENDED_START_POSITION_SCALE,
								z * EXTENDED_START_POSITION_SCALE,
								LEGACY_S16_WRAP_NEGATE(track_angle));
		return 1;
	}
#else
	(void)carstate;
	(void)simd;
	(void)transmission;
	(void)column_offset;
	(void)row_offset;
#endif
	return 0;
}
void extended_gameinit_follow_camera(legacy_s16 initial_height)
{
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		state.game_follow_camera_position[PLAYER_CAR_INDEX].y =
			extended_track_elevation(start_finish_column, start_finish_row,
									 EXTENDED_START_TILE_CENTER, EXTENDED_START_TILE_CENTER) +
			initial_height;
	}
#else
	(void)initial_height;
#endif
}
void extended_gameinit_reset(legacy_s16 initialization_mode)
{
#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
	if (extended_replay_is_active() && initialization_mode != GAMESTATE_INIT_TIMING_ONLY) {
		extended_replay_reset_simulation(initialization_mode == GAMESTATE_INIT_RESET_CHECKPOINTS);
	}
#else
	(void)initialization_mode;
#endif
}
legacy_s16 extended_gameinit_restore(legacy_u16 frame)
{
#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
	if (extended_replay_is_active()) {
		(void)extended_replay_seek(frame);
		return 1;
	}
#else
	(void)frame;
#endif
	return 0;
}
