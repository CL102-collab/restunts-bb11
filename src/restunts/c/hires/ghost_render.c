#include "ghost_render.h"
#ifdef RESTUNTS_SDL3
#include "../ghost/ghost_internal.h"
#include "../fileio.h"
#include "../externs.h"
#include "frame_interpolation.h"
#endif
#ifdef RESTUNTS_SDL3
static struct {
	struct GHOST_POSE lower;
	struct GHOST_POSE upper;
	legacy_u32 lower_frame;
	legacy_u32 upper_frame;
	legacy_u8 valid;
} hires_ghost_render_cache;
#endif
#ifdef RESTUNTS_SDL3
void hires_ghost_render_reset(void)
{
	hires_ghost_render_cache.valid = 0;
}
#endif
#ifdef RESTUNTS_SDL3
static legacy_s16 hires_ghost_read_render_pose(legacy_u32 frame, struct GHOST_POSE *pose)
{
	if (ghost_copy_live_pose(frame, pose) != 0) {
		return 1;
	}
	if (hires_ghost_render_cache.valid != 0) {
		if (hires_ghost_render_cache.lower_frame == frame) {
			*pose = hires_ghost_render_cache.lower;
			return 1;
		}
		if (hires_ghost_render_cache.upper_frame == frame) {
			*pose = hires_ghost_render_cache.upper;
			return 1;
		}
	}
	return ghost_read_pose(frame, pose);
}

legacy_s16 hires_ghost_sample_render_pose(legacy_u32 live_frame, legacy_u16 live_frame_rate,
										  legacy_u32 lag, struct CARSTATE *car,
										  struct GHOST_CAMERA_STATE *camera)
{
	const struct CARSTATE *live_car = ghost_car_state();
	const struct GHOST_CAMERA_STATE *live_camera = ghost_camera_state();
	if (!ghost_is_active() || live_car == 0 || live_frame_rate == 0 || car == 0 || camera == 0) {
		return 0;
	}
	legacy_u64 live_time = (legacy_u64)live_frame * FRAME_INTERPOLATION_ONE;
	live_time = live_time > lag ? live_time - lag : 0;
	legacy_u64 source_time = live_time * ghost_config.game_framespersec / live_frame_rate;
	legacy_u64 end_time = (legacy_u64)ghost_recorded_frames() * FRAME_INTERPOLATION_ONE;
	if (source_time > end_time) {
		source_time = end_time;
	}
	legacy_u32 lower_frame = (legacy_u32)(source_time / FRAME_INTERPOLATION_ONE);
	legacy_u32 fraction = (legacy_u32)(source_time % FRAME_INTERPOLATION_ONE);
	legacy_u32 upper_frame = lower_frame + (fraction != 0);
	struct GHOST_POSE lower, upper;
	if (!hires_ghost_read_render_pose(lower_frame, &lower)) {
		return 0;
	}
	if (lower_frame == upper_frame) {
		upper = lower;
	} else if (!hires_ghost_read_render_pose(upper_frame, &upper)) {
		return 0;
	}
	/* Commit a complete bracket only. Failed reads leave the authoritative pose
	 * and the caller's output untouched, so presentation can fall back safely. */
	hires_ghost_render_cache.lower = lower;
	hires_ghost_render_cache.upper = upper;
	hires_ghost_render_cache.lower_frame = lower_frame;
	hires_ghost_render_cache.upper_frame = upper_frame;
	hires_ghost_render_cache.valid = 1;
	if (lower.car.car_crashBmpFlag != upper.car.car_crashBmpFlag ||
		upper.car.car_crashBmpFlag != live_car->car_crashBmpFlag) {
		/* A known future impact must not move the rendered car before the live
		 * ghost confirms it. A newly confirmed impact snaps body and camera together. */
		*car = *live_car;
		*camera = *live_camera;
	} else {
		struct CARSTATE blended;
		frame_hires_interpolate_car(&blended, &upper.car, &lower.car, fraction);
		*car = *live_car;
		car->car_position = blended.car_position;
		car->car_rotate = blended.car_rotate;
		car->car_steeringAngle = blended.car_steeringAngle;
		for (legacy_u16 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
			car->car_suspension_deflection[wheel] = blended.car_suspension_deflection[wheel];
		}
		*camera = *live_camera;
		/* Roadside-camera cuts keep their authoritative index, but do not
		 * interrupt the independent continuous helicopter-camera motion. */
		frame_hires_interpolate_vector(&camera->follow_position, &upper.camera.follow_position,
									   &lower.camera.follow_position, fraction);
		frame_hires_interpolate_vector(&camera->previous_position, &upper.camera.previous_position,
									   &lower.camera.previous_position, fraction);
	}
	car->car_sound_flags = CAR_SOUND_NONE;
	/* Cached future poses supply geometry only; visible events keep their
	 * authoritative timing, including the ghost-view elapsed/crash overlay. */
	car->car_crashBmpFlag = live_car->car_crashBmpFlag;
	camera->frame = live_camera->frame;
	camera->crash_frame = live_camera->crash_frame;
	return 1;
}
#endif
