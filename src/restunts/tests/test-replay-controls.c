#include <assert.h>
#include "../c/replay_viewer.c"
#include "extended-replay-test-stubs.h"

static legacy_u16 pressed_key;
static legacy_s16 button_hit;
static legacy_s16 hidden_hit;

legacy_s16 input_checking(legacy_s16 delta)
{
	assert(delta == 3);
	return (legacy_s16)pressed_key;
}

legacy_s16 mouse_multi_hittest(legacy_s16 count, const struct BUTTON_AREA *buttons)
{
	if (buttons == &replay_hidden_bar_camera_button) {
		assert(count == 1);
		return hidden_hit;
	}
	assert(buttons == game_camera_buttons);
	return button_hit;
}

static void test_mouse_control_translation(void)
{
	cameramode = CAMERA_MODE_CUSTOM;
	button_hit = REPLAY_CONTROL_PLAY;
	hidden_hit = REPLAY_NO_SELECTION;
	replay_selected_control = REPLAY_CONTROL_PAUSE;
	pressed_key = 0;
	assert(replay_read_control_input(3) == 1);
	assert(replay_selected_control == REPLAY_CONTROL_PLAY);
	assert(replay_read_control_input(3) == 0);

	pressed_key = KEY_ENTER;
	button_hit = REPLAY_CONTROL_ZOOM;
	replay_zoom_button_top = 20;
	replay_zoom_button_bottom = 40;
	mouse_ypos = 30;
	assert(replay_read_control_input(3) == KEY_UP);
	mouse_ypos = 31;
	assert(replay_read_control_input(3) == KEY_DOWN);

	button_hit = REPLAY_NO_SELECTION;
	hidden_hit = 0;
	assert(replay_read_control_input(3) == 'c');
	pressed_key = KEY_SPACE;
	assert(replay_read_control_input(3) == 'c');
	pressed_key = KEY_ESCAPE;
	assert(replay_read_control_input(3) == KEY_ESCAPE);
}

static void test_pan_quadrants(void)
{
	static const legacy_s16 positions[][3] = {
		{100, 90, KEY_UP}, {110, 100, KEY_RIGHT}, {100, 110, KEY_DOWN}, {90, 100, KEY_LEFT}};

	button_hit = REPLAY_CONTROL_PAN;
	hidden_hit = REPLAY_NO_SELECTION;
	pressed_key = KEY_SPACE;
	replay_pan_button_top = replay_pan_button_left = 90;
	replay_pan_button_bottom = replay_pan_button_right = 110;
	for (legacy_u32 index = 0; index < sizeof(positions) / sizeof(positions[0]); index++) {
		mouse_xpos = positions[index][0];
		mouse_ypos = positions[index][1];
		assert(replay_read_control_input(3) == positions[index][2]);
	}
}

static void test_custom_camera_limits(void)
{
	custom_camera.azimuth_angle = 32760;
	legacy_u16 input = KEY_RIGHT;
	assert(replay_adjust_custom_camera(&input) == 1);
	assert(custom_camera.azimuth_angle == LEGACY_S16_WRAP_ADD(32760, CAMERA_ANGLE_STEP));
	input = KEY_LEFT;
	assert(replay_adjust_custom_camera(&input) == 1);
	assert(custom_camera.azimuth_angle == 32760);

	custom_camera.elevation_angle = REPLAY_CUSTOM_CAMERA_ELEVATION_LIMIT - CAMERA_ANGLE_STEP;
	input = KEY_UP;
	assert(replay_adjust_custom_camera(&input) == 0);
	assert(input == 0);
	custom_camera.elevation_angle--;
	input = KEY_UP;
	assert(replay_adjust_custom_camera(&input) == 1);
	assert(custom_camera.elevation_angle == REPLAY_CUSTOM_CAMERA_ELEVATION_LIMIT - 1);

	custom_camera.elevation_angle = -REPLAY_CUSTOM_CAMERA_ELEVATION_LIMIT + CAMERA_ANGLE_STEP;
	input = KEY_DOWN;
	assert(replay_adjust_custom_camera(&input) == 0);
	assert(input == 0);
	custom_camera.elevation_angle++;
	input = KEY_DOWN;
	assert(replay_adjust_custom_camera(&input) == 1);
	assert(custom_camera.elevation_angle == -REPLAY_CUSTOM_CAMERA_ELEVATION_LIMIT + 1);

	input = '+';
	assert(replay_adjust_custom_camera(&input) == 0);
	assert(input == '+');
	input = '-';
	assert(replay_adjust_custom_camera(&input) == 0);
	assert(input == '-');
	input = KEY_ESCAPE;
	assert(replay_adjust_custom_camera(&input) == 0);
	assert(input == 0);
}

#ifdef RESTUNTS_SDL3
#define TEST_REPLAY_LENGTH 1000U
#define TEST_REPLAY_MIDDLE 500U
static legacy_s16 touch_direction;

legacy_s16 sdl3_touch_take_seek(void)
{
	legacy_s16 direction = touch_direction;
	touch_direction = 0;
	return direction;
}

static void test_touch_seek_targets(void)
{
	static const legacy_u8 rates[] = {GAME_FRAME_RATE_LOW, GAME_FRAME_RATE_NORMAL};
	gameconfig.game_recordedframes = TEST_REPLAY_LENGTH;
	kbormouse = mouse_butstate = 0;
	for (legacy_u32 index = 0; index < sizeof(rates) / sizeof(rates[0]); index++) {
		gameconfig.game_framespersec = rates[index];
		elapsed_time2 = TEST_REPLAY_MIDDLE;
		touch_direction = -1;
		assert(replay_touch_seek_target() ==
			   TEST_REPLAY_MIDDLE - REPLAY_TOUCH_SKIP_SECONDS * rates[index]);
		assert(replay_touch_seek_target() == REPLAY_TOUCH_NO_SEEK);
		touch_direction = 1;
		assert(replay_touch_seek_target() ==
			   TEST_REPLAY_MIDDLE + REPLAY_TOUCH_SKIP_SECONDS * rates[index]);
		elapsed_time2 = REPLAY_FIRST_FRAME;
		touch_direction = -1;
		assert(replay_touch_seek_target() == REPLAY_FIRST_FRAME);
		elapsed_time2 = TEST_REPLAY_LENGTH;
		touch_direction = 1;
		assert(replay_touch_seek_target() == TEST_REPLAY_LENGTH);
	}
	kbormouse = 1;
	mouse_butstate = REPLAY_TOUCH_LEFT_BUTTON;
	mouse_ypos = REPLAY_TIMELINE_Y;
	mouse_xpos = REPLAY_TIMELINE_X;
	assert(replay_touch_seek_target() == REPLAY_FIRST_FRAME);
	mouse_xpos += REPLAY_TIMELINE_POSITION_RANGE / 2;
	assert(replay_touch_seek_target() == TEST_REPLAY_MIDDLE);
	mouse_xpos = REPLAY_TIMELINE_X + REPLAY_TIMELINE_POSITION_RANGE;
	assert(replay_touch_seek_target() == TEST_REPLAY_LENGTH);
	mouse_xpos = REPLAY_TIMELINE_X + REPLAY_TIMELINE_WIDTH - 1;
	assert(replay_touch_seek_target() == TEST_REPLAY_LENGTH);
	mouse_xpos++;
	assert(replay_touch_seek_target() == REPLAY_TOUCH_NO_SEEK);
	mouse_xpos = REPLAY_TIMELINE_X;
	mouse_butstate = 0;
	assert(replay_touch_seek_target() == REPLAY_TOUCH_NO_SEEK);
}
#endif

legacy_int main(void)
{
	test_mouse_control_translation();
	test_pan_quadrants();
	test_custom_camera_limits();
#ifdef RESTUNTS_SDL3
	test_touch_seek_targets();
#endif
	return 0;
}
