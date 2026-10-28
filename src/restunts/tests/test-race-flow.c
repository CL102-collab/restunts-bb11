#include <assert.h>
#include <string.h>

/* Exercise the frame scheduler and viewport policy without a DOS display. */
#include "../c/race.c"

legacy_u8 supersight_enabled;
static legacy_u32 projection_updates;

void set_projection(legacy_s16 x, legacy_s16 y, legacy_s16 width, legacy_s16 height)
{
	assert(x == RACE_PROJECTION_HORIZONTAL_SCALE);
	assert(y == height / RACE_PROJECTION_VERTICAL_DIVISOR);
	assert(width == RACE_SCREEN_WIDTH);
	assert(height == dashbmp_y_copy);
	projection_updates++;
}

static legacy_u32 updates;
static legacy_u32 analog_updates;
static legacy_u8 joystick_enabled;

legacy_u8 dos_joystick_is_enabled(void)
{
	return joystick_enabled;
}

void replay_apply_analog_steering_history(void)
{
	assert(updates == 0);
	analog_updates++;
}

void update_gamestate(void)
{
	updates++;
}

static void reset_frame(void)
{
	memset(&state, 0, sizeof(state));
	updates = 0;
	analog_updates = 0;
	joystick_enabled = 0;
	mouse_driving_enabled = 0;
	game_replay_mode = REPLAY_MODE_LIVE;
	race_exit_request = 0;
	state.game_inputmode = GAME_INPUT_MODE_ACTIVE;
	state.game_frame = 12;
	elapsed_time2 = 12;
}

static void test_frame_scheduling(void)
{
	reset_frame();
	legacy_s64 last_frame = -1;
	assert(race_frame_is_ready(&last_frame) == 1);
	assert(last_frame == 12);
	assert(race_frame_is_ready(&last_frame) == 0);
	assert(updates == 0);
	race_exit_request = 1;
	assert(race_frame_is_ready(&last_frame) == 1);
	race_exit_request = 0;
	state.game_inputmode = GAME_INPUT_MODE_WAITING;
	assert(race_frame_is_ready(&last_frame) == 1);
	game_replay_mode = REPLAY_MODE_PLAYBACK;
	assert(race_frame_is_ready(&last_frame) == 1);
}

static void test_frame_catchup(void)
{
	reset_frame();
	elapsed_time2 = 13;
	mouse_driving_enabled = 1;
	legacy_s64 last_frame = -1;
	assert(race_frame_is_ready(&last_frame) == 0);
	assert(updates == 1);
	assert(analog_updates == 1);
	assert(last_frame == -1);

	reset_frame();
	elapsed_time2 = 13;
	joystick_enabled = 1;
	assert(race_frame_is_ready(&last_frame) == 0);
	assert(updates == 1);
	assert(analog_updates == 1);

	reset_frame();
	elapsed_time2 = 13;
	game_replay_mode = REPLAY_MODE_PLAYBACK;
	mouse_driving_enabled = 1;
	assert(race_frame_is_ready(&last_frame) == 0);
	assert(updates == 1);
	assert(analog_updates == 0);
}

struct DASHBOARD_CASE {
	legacy_s8 mode;
	legacy_s8 idle;
	legacy_s8 dashboard;
	legacy_s8 following_opponent;
	legacy_s8 replay;
	legacy_s8 replay_bar;
	legacy_s8 rewind;
	legacy_s16 expected_bottom;
	legacy_s8 expected_dashboard;
	legacy_s8 expected_bar;
};

static void test_dashboard_layout(void)
{
	static const struct DASHBOARD_CASE cases[] = {
		{REPLAY_MODE_LIVE, 0, 1, 0, 0, 1, 0, 140, 1, 0},
		{REPLAY_MODE_LIVE, 0, 1, 1, 0, 1, 0, 200, 0, 0},
		{REPLAY_MODE_PLAYBACK, 0, 0, 0, 1, 1, 0, 151, 0, 1},
		{REPLAY_MODE_PLAYBACK, 0, 0, 0, 0, 0, 0, 200, 0, 0},
		{REPLAY_MODE_PLAYBACK, 0, 1, 0, 1, 1, 0, 140, 1, 1},
		{REPLAY_MODE_PLAYBACK, 0, 1, 0, 0, 0, 0, 140, 1, 0},
		{REPLAY_MODE_PLAYBACK, 1, 1, 0, 1, 1, 0, 200, 0, 0},
		{REPLAY_MODE_PLAYBACK, 0, 0, 0, 1, 1, 1, 200, 0, 0},
		{REPLAY_MODE_PLAYBACK, 0, 1, 0, 1, 1, 1, 140, 1, 0},
		{REPLAY_MODE_PLAYBACK, 0, 1, 1, 1, 1, 1, 200, 0, 0},
	};

	for (legacy_u32 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		game_replay_mode = cases[index].mode;
		idle_expired = cases[index].idle;
		dashb_toggle = cases[index].dashboard;
		followOpponentFlag = cases[index].following_opponent;
		is_in_replay = cases[index].replay;
		replaybar_toggle = cases[index].replay_bar;
		dashbmp_y = 140;
		roofbmpheight = 13;
		height_above_replaybar = 777;
		race_update_dashboard_layout(cases[index].rewind);
		assert(dashbmp_y_copy == cases[index].expected_bottom);
		assert(dashboard_visible == cases[index].expected_dashboard);
		assert(replaybar_enabled == cases[index].expected_bar);
		assert(roofbmpheight_copy == (dashboard_visible ? 13 : 0));
		assert(game_replay_mode_copy == game_replay_mode);
		assert(followOpponentFlag_copy == followOpponentFlag);
		if (dashboard_visible) {
			assert(height_above_replaybar == (replaybar_enabled ? 151 : 200));
		} else {
			assert(height_above_replaybar == 777);
		}
	}
}

static void test_replay_viewport_toggle(void)
{
	/* Cover dashboard boundaries on both sides of the toolbar, including ZMP4. */
	static const legacy_s16 dashboard_tops[] = {140, 151, 165, 180};
	for (legacy_u16 index = 0; index < sizeof(dashboard_tops) / sizeof(dashboard_tops[0]);
		 index++) {
		struct RACE_VIEWPORT_CACHE cache = {-1, -1, 0};
		game_replay_mode = REPLAY_MODE_PLAYBACK;
		game_replay_mode_copy = RACE_REPLAY_MODE_UNINITIALIZED;
		idle_expired = followOpponentFlag = 0;
		dashb_toggle = replaybar_toggle = 1;
		is_in_replay = 0;
		dashbmp_y = dashboard_tops[index];
		roofbmpheight = 13;
		viewport_bottom_cache = -1;
		video_page_count = 2;
		projection_updates = 0;
		legacy_s16 enhanced_bottom = dashbmp_y > 151 ? 151 : dashbmp_y;
		for (legacy_u16 pass = 0; pass < 3; pass++) {
			supersight_enabled = pass == 1;
			full_redraw_frames_remaining = 0;
			race_update_viewport(&cache, 0);
			legacy_s16 expected_bottom = supersight_enabled ? enhanced_bottom : dashbmp_y;
			assert(dashbmp_y_copy == expected_bottom);
			assert(rect_windshield.bottom == expected_bottom);
			assert(height_above_replaybar == 151);
			assert(dashboard_visible == 1 && replaybar_enabled == 1);
			assert(cache.supersight == supersight_enabled);
			assert(full_redraw_frames_remaining == (pass == 0 || dashbmp_y > 151 ? 2 : 0));
		}
		assert(projection_updates == (dashbmp_y > 151 ? 3 : 1));

		supersight_enabled = 1;
		replaybar_toggle = 0;
		race_update_viewport(&cache, 0);
		assert(replaybar_enabled == 0 && height_above_replaybar == 200);
		assert(rect_windshield.bottom == dashbmp_y);
		replaybar_toggle = 1;
		race_update_viewport(&cache, 0);
		assert(replaybar_enabled == 1 && rect_windshield.bottom == enhanced_bottom);
	}
	supersight_enabled = 0;
}

int main(void)
{
	test_frame_scheduling();
	test_frame_catchup();
	test_dashboard_layout();
	test_replay_viewport_toggle();
	return 0;
}
