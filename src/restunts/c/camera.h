#ifndef RESTUNTS_CAMERA_H
#define RESTUNTS_CAMERA_H

#include "legacy.h"
#include "keyboard.h"
#include "math.h"

enum CAMERA_MODE {
	CAMERA_MODE_COCKPIT = 0,
	CAMERA_MODE_FOLLOW = 1,
	CAMERA_MODE_CUSTOM = 2,
	CAMERA_MODE_TRACKSIDE = 3
};

#define CAMERA_MODE_COUNT 4U
#define CAMERA_MODE_MASK (CAMERA_MODE_COUNT - 1U)

#define CUSTOM_CAMERA_INITIAL_DISTANCE 210
#define CUSTOM_CAMERA_INITIAL_ELEVATION_ANGLE 80
#define CUSTOM_CAMERA_INITIAL_AZIMUTH_ANGLE 464
#define CAMERA_ZOOM_STEP 30
#define CAMERA_ANGLE_STEP 16
#define CUSTOM_CAMERA_ELEVATION_LIMIT ANGLE_QUARTER_TURN

/* The free camera the player steers with the keypad in replay mode. */
struct CUSTOM_CAMERA {
	legacy_s16 distance;
	legacy_s16 elevation_angle;
	legacy_s16 azimuth_angle;
};
extern struct CUSTOM_CAMERA custom_camera;
extern legacy_s8 cameramode;
extern legacy_s8 followOpponentFlag;
extern legacy_s8 followOpponentFlag_copy;
extern legacy_s16 camera_track_height_offset;

/* Keypad and controller camera movement share the original angle limits. */
static inline legacy_s16 camera_adjust_custom_direction(legacy_u16 direction)
{
	switch (direction) {
		case KEY_RIGHT:
			custom_camera.azimuth_angle =
				LEGACY_S16_WRAP_ADD(custom_camera.azimuth_angle, CAMERA_ANGLE_STEP);
			return 1;
		case KEY_LEFT:
			custom_camera.azimuth_angle =
				LEGACY_S16_WRAP_SUB(custom_camera.azimuth_angle, CAMERA_ANGLE_STEP);
			return 1;
		case KEY_UP:
			if (LEGACY_S16_WRAP_ADD(custom_camera.elevation_angle, CAMERA_ANGLE_STEP) <
				CUSTOM_CAMERA_ELEVATION_LIMIT) {
				custom_camera.elevation_angle =
					LEGACY_S16_WRAP_ADD(custom_camera.elevation_angle, CAMERA_ANGLE_STEP);
				return 1;
			}
			break;
		case KEY_DOWN:
			if (LEGACY_S16_WRAP_SUB(custom_camera.elevation_angle, CAMERA_ANGLE_STEP) >
				-CUSTOM_CAMERA_ELEVATION_LIMIT) {
				custom_camera.elevation_angle =
					LEGACY_S16_WRAP_SUB(custom_camera.elevation_angle, CAMERA_ANGLE_STEP);
				return 1;
			}
			break;
	}
	return 0;
}

/* Selecting another camera ends the F3 preset cycle and restores its original view. */
void camera_select_mode(legacy_s8 mode);

#endif
