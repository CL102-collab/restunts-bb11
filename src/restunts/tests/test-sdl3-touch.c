#include "../platform/sdl3/touch.h"
#include "../platform/sdl3/controller_game.h"
#include "../c/game_input.h"
#include "../c/keyboard.h"
#include "../c/gamestate.h"
#include "../c/replay.h"
#include <assert.h>
#include <stdio.h>

#define TEST_WIDTH 1920
#define TEST_HEIGHT 1080
#define TEST_DEVICE 1
#define TEST_FIRST_FINGER 11
#define TEST_SECOND_FINGER 12
#define TEST_THIRD_FINGER 13
#define TEST_FOURTH_FINGER 14
#define TEST_NO_REPEAT_HOLD_MS 500U
#define TEST_KEY_CAPACITY 64U
#define TEST_MOUSE_CAPACITY 64U
#define TEST_VIEW_X 600.0f
#define TEST_VIEW_Y 400.0f
#define TEST_SWIPE_X 400.0f
#define TEST_TOOLBAR_X 1000.0f
#define TEST_TOOLBAR_Y 950.0f
#define TEST_OUTSIDE_OFFSET 3.0f
#define TEST_COORDINATE_EPSILON 0.01f
#define TEST_PRESENTATION_HEIGHT 240
#define TEST_SAFE_INSET_X 20
#define TEST_SAFE_INSET_Y 10

/* Exercise the same game-state bridge compiled into the Android library. */
struct GAMEINFO gameconfig;
legacy_u8 game_replay_mode;
legacy_s8 is_in_replay;
legacy_s8 mouse_driving_enabled;
static enum SDL3_DRIVING_INPUT driving_mode;

enum SDL3_DRIVING_INPUT sdl3_input_driving_mode(void)
{
	return driving_mode;
}

static const SDL_Rect area = {0, 0, TEST_WIDTH, TEST_HEIGHT};
static legacy_u16 queued_keys[TEST_KEY_CAPACITY];
static legacy_u32 key_count;
static legacy_u8 mouse_down[TEST_MOUSE_CAPACITY];
static legacy_u32 mouse_count;

SDL_Window *sdl3_video_window(void)
{
	return NULL;
}

void sdl3_video_window_to_game(legacy_f32 window_x, legacy_f32 window_y, legacy_f32 *x,
							   legacy_f32 *y)
{
	*x = window_x * SDL3_SCREEN_WIDTH / TEST_WIDTH;
	*y = window_y * SDL3_SCREEN_HEIGHT / TEST_HEIGHT;
}

void sdl3_input_queue_key(legacy_u16 value)
{
	assert(key_count < TEST_KEY_CAPACITY);
	queued_keys[key_count++] = value;
}

void sdl3_input_touch_mouse(legacy_f32 x, legacy_f32 y, legacy_u8 down)
{
	assert(x >= 0 && y >= 0);
	assert(mouse_count < TEST_MOUSE_CAPACITY);
	mouse_down[mouse_count++] = down;
}

static void event(SDL_EventType type, SDL_FingerID id, legacy_f32 x, legacy_f32 y)
{
	SDL_TouchFingerEvent finger;
	SDL_zero(finger);
	finger.type = type;
	finger.touchID = TEST_DEVICE;
	finger.fingerID = id;
	finger.x = x / TEST_WIDTH;
	finger.y = y / TEST_HEIGHT;
	sdl3_touch_event(&finger, &area);
}

static void circle_event(SDL_EventType type, SDL_FingerID id, enum TOUCH_CONTROL control)
{
	struct TOUCH_CIRCLE circle = sdl3_touch_circle(control, &area);
	event(type, id, circle.x, circle.y);
}

static void clear(void)
{
	sdl3_touch_reset();
	key_count = 0;
	mouse_count = 0;
}

static void assert_top_right_geometry(const SDL_Rect *rectangle)
{
	struct TOUCH_CIRCLE camera = sdl3_touch_circle(TOUCH_CAMERA, rectangle);
	struct TOUCH_CIRCLE follow = sdl3_touch_circle(TOUCH_FOLLOW, rectangle);
	struct TOUCH_CIRCLE accelerate = sdl3_touch_circle(TOUCH_ACCELERATE, rectangle);
	assert(SDL_fabsf(camera.x + camera.radius - (rectangle->x + rectangle->w)) <
		   TEST_COORDINATE_EPSILON);
	assert(SDL_fabsf(camera.y - camera.radius - rectangle->y) < TEST_COORDINATE_EPSILON);
	assert(camera.x == follow.x && camera.radius == follow.radius);
	assert(follow.y - follow.radius > camera.y + camera.radius);
	assert(follow.y + follow.radius < accelerate.y - accelerate.radius);
}

static void test_visibility_and_geometry(void)
{
	sdl3_touch_enable(1);
	sdl3_touch_configure(1, 1, 1, 0);
	for (enum TOUCH_CONTROL control = TOUCH_ESCAPE; control < TOUCH_CONTROL_COUNT; control++) {
		assert(sdl3_touch_visible(control));
	}
	struct TOUCH_CIRCLE left = sdl3_touch_circle(TOUCH_LEFT, &area);
	struct TOUCH_CIRCLE right = sdl3_touch_circle(TOUCH_RIGHT, &area);
	struct TOUCH_CIRCLE up = sdl3_touch_circle(TOUCH_SHIFT_UP, &area);
	struct TOUCH_CIRCLE down = sdl3_touch_circle(TOUCH_SHIFT_DOWN, &area);
	assert(left.x == left.radius);
	assert(left.y == right.y);
	assert(up.x > (left.x + right.x) / 2.0f - TEST_COORDINATE_EPSILON);
	assert(up.x < (left.x + right.x) / 2.0f + TEST_COORDINATE_EPSILON);
	assert(up.x == down.x && up.y < left.y && down.y > left.y);
	SDL_Rect inset = {TEST_SAFE_INSET_X, TEST_SAFE_INSET_Y, TEST_WIDTH - TEST_SAFE_INSET_X * 2,
					  TEST_HEIGHT - TEST_SAFE_INSET_Y * 2};
	left = sdl3_touch_circle(TOUCH_LEFT, &inset);
	assert(left.x - left.radius == inset.x);
	assert_top_right_geometry(&area);
	assert_top_right_geometry(&inset);
	sdl3_touch_configure(1, 1, 0, 0);
	assert(!sdl3_touch_visible(TOUCH_SHIFT_UP) && !sdl3_touch_visible(TOUCH_SHIFT_DOWN));
	assert(sdl3_touch_visible(TOUCH_REWIND));
	assert(sdl3_touch_visible(TOUCH_CAMERA) && sdl3_touch_visible(TOUCH_FOLLOW));
	sdl3_touch_configure(1, 0, 1, 1);
	assert(!sdl3_touch_visible(TOUCH_SHIFT_UP) && !sdl3_touch_visible(TOUCH_SHIFT_DOWN));
	assert(!sdl3_touch_visible(TOUCH_REWIND) && sdl3_touch_visible(TOUCH_LEFT));
	assert(sdl3_touch_visible(TOUCH_CAMERA) && sdl3_touch_visible(TOUCH_FOLLOW));
	sdl3_touch_configure(1, 0, 0, 0);
	assert(sdl3_touch_visible(TOUCH_CAMERA) && sdl3_touch_visible(TOUCH_FOLLOW));
	sdl3_touch_configure(0, 1, 1, 0);
	for (enum TOUCH_CONTROL control = TOUCH_ESCAPE; control < TOUCH_CONTROL_COUNT; control++) {
		assert(!sdl3_touch_visible(control));
	}
	sdl3_touch_configure(1, 1, 1, 0);
	sdl3_touch_enable(0);
	for (enum TOUCH_CONTROL control = TOUCH_ESCAPE; control < TOUCH_CONTROL_COUNT; control++) {
		assert(!sdl3_touch_visible(control));
	}
	sdl3_touch_enable(1);
}

static void test_holds_and_cancellation(void)
{
	clear();
	sdl3_touch_configure(1, 1, 1, 0);
	circle_event(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, TOUCH_LEFT);
	circle_event(SDL_EVENT_FINGER_DOWN, TEST_SECOND_FINGER, TOUCH_ACCELERATE);
	assert(sdl3_touch_key_state(SDL_SCANCODE_LEFT));
	assert(sdl3_touch_key_state(SDL_SCANCODE_UP));
	assert(queued_keys[0] == KEY_LEFT && queued_keys[1] == KEY_UP);
	circle_event(SDL_EVENT_FINGER_DOWN, TEST_THIRD_FINGER, TOUCH_LEFT);
	circle_event(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, TOUCH_LEFT);
	assert(sdl3_touch_key_state(SDL_SCANCODE_LEFT));
	circle_event(SDL_EVENT_FINGER_CANCELED, TEST_THIRD_FINGER, TOUCH_LEFT);
	assert(!sdl3_touch_key_state(SDL_SCANCODE_LEFT));
	struct TOUCH_CIRCLE throttle = sdl3_touch_circle(TOUCH_ACCELERATE, &area);
	event(SDL_EVENT_FINGER_MOTION, TEST_SECOND_FINGER,
		  throttle.x - throttle.radius * TEST_OUTSIDE_OFFSET, throttle.y);
	assert(!sdl3_touch_key_state(SDL_SCANCODE_UP));
	circle_event(SDL_EVENT_FINGER_MOTION, TEST_SECOND_FINGER, TOUCH_ACCELERATE);
	assert(sdl3_touch_key_state(SDL_SCANCODE_UP));
	sdl3_touch_configure(0, 1, 1, 0);
	assert(!sdl3_touch_key_state(SDL_SCANCODE_UP));
}

static void test_single_tap_shifts(void)
{
	clear();
	sdl3_touch_configure(1, 1, 1, 0);
	circle_event(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, TOUCH_SHIFT_UP);
	assert(sdl3_touch_take_shift_flags() == INPUT_SHIFT_UP_FLAG);
	assert(!sdl3_touch_key_state(SDL_SCANCODE_A));
	circle_event(SDL_EVENT_FINGER_MOTION, TEST_FIRST_FINGER, TOUCH_SHIFT_UP);
	circle_event(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, TOUCH_SHIFT_UP);
	sdl3_touch_update();
	assert(sdl3_touch_take_shift_flags() == 0);
	circle_event(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, TOUCH_SHIFT_UP);
	circle_event(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, TOUCH_SHIFT_UP);
	circle_event(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, TOUCH_SHIFT_UP);
	assert(sdl3_touch_take_shift_flags() == INPUT_SHIFT_UP_FLAG);
	circle_event(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, TOUCH_SHIFT_DOWN);
	assert(sdl3_touch_take_shift_flags() == INPUT_SHIFT_DOWN_FLAG);
	assert(key_count == 0);
	sdl3_touch_configure(1, 0, 1, 1);
	assert(sdl3_touch_take_shift_flags() == 0);
}

static void test_shift_tap_order(void)
{
	static const enum TOUCH_CONTROL controls[] = {TOUCH_SHIFT_UP, TOUCH_SHIFT_UP, TOUCH_SHIFT_DOWN,
												  TOUCH_SHIFT_UP, TOUCH_SHIFT_DOWN};
	clear();
	sdl3_touch_configure(1, 1, 1, 0);
	/* Multiple complete taps can arrive before the next 10 or 20 Hz input sample. */
	for (legacy_u32 index = 0; index < SDL_arraysize(controls); index++) {
		circle_event(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, controls[index]);
		circle_event(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, controls[index]);
	}
	circle_event(SDL_EVENT_FINGER_DOWN, TEST_SECOND_FINGER, TOUCH_SHIFT_UP);
	for (legacy_u32 index = 0; index < SDL_arraysize(controls); index++) {
		legacy_u16 expected =
			controls[index] == TOUCH_SHIFT_UP ? INPUT_SHIFT_UP_FLAG : INPUT_SHIFT_DOWN_FLAG;
		assert(sdl3_touch_take_shift_flags() == expected);
	}
	assert(sdl3_touch_take_shift_flags() == INPUT_SHIFT_UP_FLAG);
	circle_event(SDL_EVENT_FINGER_MOTION, TEST_SECOND_FINGER, TOUCH_SHIFT_UP);
	sdl3_touch_update();
	assert(sdl3_touch_take_shift_flags() == 0);
	assert(!sdl3_touch_key_state(SDL_SCANCODE_A));
	circle_event(SDL_EVENT_FINGER_UP, TEST_SECOND_FINGER, TOUCH_SHIFT_UP);
	assert(key_count == 0);
	for (legacy_u32 index = 0; index < SDL_arraysize(controls); index++) {
		circle_event(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, controls[index]);
		circle_event(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, controls[index]);
	}
	sdl3_touch_configure(1, 0, 1, 1);
	assert(sdl3_touch_take_shift_flags() == 0);
}

static void test_single_tap_shortcuts(void)
{
	static const enum TOUCH_CONTROL controls[] = {TOUCH_CAMERA, TOUCH_FOLLOW};
	static const SDL_Scancode scancodes[] = {SDL_SCANCODE_C, SDL_SCANCODE_T};
	static const legacy_u16 keys[] = {'c', 't'};
	static const enum TOUCH_CONTROL burst[] = {TOUCH_FOLLOW, TOUCH_CAMERA, TOUCH_CAMERA,
											   TOUCH_FOLLOW};
	clear();
	sdl3_touch_configure(1, 0, 0, 0);
	for (legacy_u32 index = 0; index < SDL_arraysize(controls); index++) {
		circle_event(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER + index, controls[index]);
		assert(sdl3_touch_key_state(scancodes[index]));
		assert(queued_keys[index] == keys[index]);
	}
	/* Menu arrows repeat after this delay; these shortcuts must remain one event per tap. */
	SDL_Delay(TEST_NO_REPEAT_HOLD_MS);
	sdl3_touch_update();
	assert(key_count == SDL_arraysize(controls));
	for (legacy_u32 index = 0; index < SDL_arraysize(controls); index++) {
		circle_event(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER + index, controls[index]);
		circle_event(SDL_EVENT_FINGER_MOTION, TEST_FIRST_FINGER + index, controls[index]);
		struct TOUCH_CIRCLE circle = sdl3_touch_circle(controls[index], &area);
		event(SDL_EVENT_FINGER_MOTION, TEST_FIRST_FINGER + index,
			  circle.x - circle.radius * TEST_OUTSIDE_OFFSET, circle.y);
		assert(!sdl3_touch_key_state(scancodes[index]));
		circle_event(SDL_EVENT_FINGER_MOTION, TEST_FIRST_FINGER + index, controls[index]);
		assert(sdl3_touch_key_state(scancodes[index]));
		circle_event(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER + index, controls[index]);
		assert(!sdl3_touch_key_state(scancodes[index]));
		assert(key_count == SDL_arraysize(controls));
	}
	for (legacy_u32 index = 0; index < SDL_arraysize(burst); index++) {
		circle_event(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, burst[index]);
		circle_event(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, burst[index]);
		assert(queued_keys[SDL_arraysize(controls) + index] ==
			   (burst[index] == TOUCH_CAMERA ? 'c' : 't'));
	}
	assert(key_count == SDL_arraysize(controls) + SDL_arraysize(burst));
	assert(mouse_count == 0 && sdl3_touch_take_shift_flags() == 0);
	clear();
	sdl3_touch_configure(1, 1, 0, 0);
	circle_event(SDL_EVENT_FINGER_DOWN, TEST_THIRD_FINGER, TOUCH_LEFT);
	circle_event(SDL_EVENT_FINGER_DOWN, TEST_FOURTH_FINGER, TOUCH_ACCELERATE);
	for (legacy_u32 index = 0; index < SDL_arraysize(controls); index++) {
		circle_event(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER + index, controls[index]);
		circle_event(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER + index, controls[index]);
	}
	assert(sdl3_touch_key_state(SDL_SCANCODE_LEFT) && sdl3_touch_key_state(SDL_SCANCODE_UP));
	circle_event(SDL_EVENT_FINGER_UP, TEST_THIRD_FINGER, TOUCH_LEFT);
	circle_event(SDL_EVENT_FINGER_UP, TEST_FOURTH_FINGER, TOUCH_ACCELERATE);
	assert(!sdl3_touch_key_state(SDL_SCANCODE_LEFT) && !sdl3_touch_key_state(SDL_SCANCODE_UP));
	assert(key_count == SDL_arraysize(controls) + 2U);
	assert(queued_keys[0] == KEY_LEFT && queued_keys[1] == KEY_UP && queued_keys[2] == 'c' &&
		   queued_keys[3] == 't');
}

static void test_gestures_and_pointer(void)
{
	clear();
	sdl3_touch_configure(1, 0, 1, 1);
	event(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, TEST_VIEW_X, TEST_VIEW_Y);
	event(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, TEST_VIEW_X + TEST_SWIPE_X, TEST_VIEW_Y);
	assert(sdl3_touch_take_seek() == 1 && sdl3_touch_take_seek() == 0);
	assert(mouse_count == 0);
	event(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, TEST_VIEW_X, TEST_VIEW_Y);
	event(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, TEST_VIEW_X - TEST_SWIPE_X, TEST_VIEW_Y);
	assert(sdl3_touch_take_seek() == -1);
	event(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, TEST_VIEW_X, TEST_VIEW_Y);
	event(SDL_EVENT_FINGER_CANCELED, TEST_FIRST_FINGER, TEST_VIEW_X + TEST_SWIPE_X, TEST_VIEW_Y);
	assert(sdl3_touch_take_seek() == 0 && mouse_count == 0);
	event(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, TEST_TOOLBAR_X, TEST_TOOLBAR_Y);
	event(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, TEST_TOOLBAR_X + TEST_SWIPE_X, TEST_TOOLBAR_Y);
	assert(sdl3_touch_take_seek() == 0 && mouse_count == 2);
	assert(mouse_down[0] && !mouse_down[1]);
	clear();
	sdl3_touch_configure(0, 0, 0, 0);
	event(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, TEST_VIEW_X, TEST_VIEW_Y);
	event(SDL_EVENT_FINGER_DOWN, TEST_SECOND_FINGER, TEST_TOOLBAR_X, TEST_TOOLBAR_Y);
	event(SDL_EVENT_FINGER_UP, TEST_SECOND_FINGER, TEST_TOOLBAR_X, TEST_TOOLBAR_Y);
	assert(mouse_count == 1 && mouse_down[0]);
	event(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, TEST_VIEW_X, TEST_VIEW_Y);
	assert(mouse_count == 2 && mouse_down[0] && !mouse_down[1]);
}

static void test_game_state_and_rewind(void)
{
	clear();
	gameconfig.game_playertransmission = TRANSMISSION_MANUAL;
	game_replay_mode = REPLAY_MODE_LIVE;
	is_in_replay = 0;
	sdl3_touch_set_game_active(1);
	assert(sdl3_touch_visible(TOUCH_REWIND) && sdl3_touch_visible(TOUCH_SHIFT_UP));
	circle_event(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, TOUCH_SHIFT_UP);
	circle_event(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, TOUCH_SHIFT_UP);
	circle_event(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, TOUCH_REWIND);
	circle_event(SDL_EVENT_FINGER_DOWN, TEST_SECOND_FINGER, TOUCH_LEFT);
	circle_event(SDL_EVENT_FINGER_DOWN, TEST_THIRD_FINGER, TOUCH_ACCELERATE);
	sdl3_touch_set_rewind_active(1);
	assert(!sdl3_touch_visible(TOUCH_SHIFT_UP) && !sdl3_touch_visible(TOUCH_SHIFT_DOWN));
	assert(sdl3_touch_take_shift_flags() == 0);
	game_replay_mode = REPLAY_MODE_PLAYBACK;
	is_in_replay = 1;
	sdl3_touch_sync_game();
	assert(sdl3_touch_visible(TOUCH_REWIND) && sdl3_touch_key_state(SDL_SCANCODE_Q));
	assert(sdl3_touch_key_state(SDL_SCANCODE_LEFT) && sdl3_touch_key_state(SDL_SCANCODE_UP));
	circle_event(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, TOUCH_REWIND);
	assert(!sdl3_touch_key_state(SDL_SCANCODE_Q));
	game_replay_mode = REPLAY_MODE_LIVE;
	is_in_replay = 0;
	sdl3_touch_set_rewind_active(0);
	assert(sdl3_touch_visible(TOUCH_REWIND) && sdl3_touch_visible(TOUCH_SHIFT_UP));
	assert(sdl3_touch_key_state(SDL_SCANCODE_LEFT) && sdl3_touch_key_state(SDL_SCANCODE_UP));
	assert(sdl3_touch_take_shift_flags() == 0);
	sdl3_touch_set_replay_active(0);
	assert(!sdl3_touch_visible(TOUCH_REWIND) && !sdl3_touch_visible(TOUCH_SHIFT_UP));
	sdl3_touch_set_replay_active(1);
	game_replay_mode = REPLAY_MODE_PLAYBACK;
	is_in_replay = 1;
	sdl3_touch_sync_game();
	assert(!sdl3_touch_visible(TOUCH_REWIND) && !sdl3_touch_visible(TOUCH_SHIFT_UP));
	mouse_driving_enabled = 1;
	driving_mode = SDL3_DRIVING_MOUSE;
	sdl3_touch_sync_game();
	assert(!sdl3_touch_visible(TOUCH_LEFT));
	mouse_driving_enabled = 0;
	driving_mode = SDL3_DRIVING_JOYSTICK;
	sdl3_touch_sync_game();
	assert(!sdl3_touch_visible(TOUCH_ESCAPE));
	driving_mode = SDL3_DRIVING_CONTROLLER;
	sdl3_touch_sync_game();
	assert(!sdl3_touch_visible(TOUCH_LEFT) && !sdl3_touch_visible(TOUCH_ESCAPE));
	/* Last-used keyboard steering restores touch even if a legacy flag is stale. */
	mouse_driving_enabled = 1;
	driving_mode = SDL3_DRIVING_KEYBOARD;
	sdl3_touch_sync_game();
	assert(sdl3_touch_visible(TOUCH_LEFT) && sdl3_touch_visible(TOUCH_ESCAPE));
	mouse_driving_enabled = 0;
	sdl3_touch_set_game_active(0);
}

static void test_intro_visibility_and_taps(void)
{
	clear();
	gameconfig.game_playertransmission = TRANSMISSION_MANUAL;
	game_replay_mode = REPLAY_MODE_LIVE;
	is_in_replay = 0;
	sdl3_touch_set_game_active(1);
	circle_event(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, TOUCH_SHIFT_UP);
	circle_event(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, TOUCH_SHIFT_UP);
	circle_event(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, TOUCH_LEFT);
	circle_event(SDL_EVENT_FINGER_DOWN, TEST_SECOND_FINGER, TOUCH_ACCELERATE);
	assert(sdl3_touch_key_state(SDL_SCANCODE_LEFT) && sdl3_touch_key_state(SDL_SCANCODE_UP));
	sdl3_touch_set_intro_active(1);
	sdl3_touch_sync_game();
	for (enum TOUCH_CONTROL control = TOUCH_ESCAPE; control < TOUCH_CONTROL_COUNT; control++) {
		assert(!sdl3_touch_visible(control));
	}
	assert(!sdl3_touch_key_state(SDL_SCANCODE_LEFT) && !sdl3_touch_key_state(SDL_SCANCODE_UP));
	assert(sdl3_touch_take_shift_flags() == 0);
	/* Discard already observed key output so intro taps cannot masquerade as controls. */
	key_count = 0;
	circle_event(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, TOUCH_LEFT);
	sdl3_touch_update();
	assert(key_count == 0 && mouse_count == 1 && mouse_down[0]);
	/* Repeated notifications must leave a pending tap pressed until its release. */
	sdl3_touch_set_intro_active(1);
	assert(mouse_count == 1);
	circle_event(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, TOUCH_LEFT);
	assert(mouse_count == 2 && !mouse_down[1]);
	circle_event(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, TOUCH_ESCAPE);
	assert(key_count == 0 && mouse_count == 3 && mouse_down[2]);
	sdl3_touch_set_intro_active(0);
	assert(mouse_count == 4 && !mouse_down[3]);
	circle_event(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, TOUCH_ESCAPE);
	assert(mouse_count == 4);
	for (enum TOUCH_CONTROL control = TOUCH_ESCAPE; control < TOUCH_CONTROL_COUNT; control++) {
		assert(sdl3_touch_visible(control));
	}
	assert(!sdl3_touch_key_state(SDL_SCANCODE_LEFT) && !sdl3_touch_key_state(SDL_SCANCODE_UP));
	assert(sdl3_touch_take_shift_flags() == 0);
	circle_event(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, TOUCH_LEFT);
	assert(sdl3_touch_key_state(SDL_SCANCODE_LEFT) && key_count == 1);
	assert(queued_keys[0] == KEY_LEFT);
	circle_event(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, TOUCH_LEFT);
	assert(!sdl3_touch_key_state(SDL_SCANCODE_LEFT));

	/* A later intro exit still respects the selected input device. */
	sdl3_touch_set_game_active(0);
	sdl3_touch_set_intro_active(1);
	mouse_driving_enabled = 1;
	driving_mode = SDL3_DRIVING_MOUSE;
	sdl3_touch_sync_game();
	sdl3_touch_set_intro_active(0);
	for (enum TOUCH_CONTROL control = TOUCH_ESCAPE; control < TOUCH_CONTROL_COUNT; control++) {
		assert(!sdl3_touch_visible(control));
	}
	mouse_driving_enabled = 0;
	driving_mode = SDL3_DRIVING_KEYBOARD;
	sdl3_touch_sync_game();
	assert(sdl3_touch_visible(TOUCH_LEFT) && sdl3_touch_visible(TOUCH_ESCAPE));
	assert(!sdl3_touch_visible(TOUCH_SHIFT_UP) && !sdl3_touch_visible(TOUCH_REWIND));
	clear();
}

static void test_rendering(void)
{
	assert(SDL_InitSubSystem(SDL_INIT_VIDEO));
	SDL_Window *window = SDL_CreateWindow("Touch layout", TEST_WIDTH, TEST_HEIGHT, 0);
	assert(window != NULL);
	SDL_Renderer *renderer = SDL_CreateRenderer(window, "software");
	assert(renderer != NULL);
	assert(SDL_SetRenderLogicalPresentation(renderer, SDL3_SCREEN_WIDTH, TEST_PRESENTATION_HEIGHT,
											SDL_LOGICAL_PRESENTATION_LETTERBOX));
	assert(SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE));
	assert(SDL_RenderClear(renderer));
	sdl3_touch_configure(1, 1, 1, 0);
	sdl3_touch_set_intro_active(1);
	sdl3_touch_draw(renderer, window);
	legacy_int width, height;
	SDL_RendererLogicalPresentation mode;
	assert(SDL_GetRenderLogicalPresentation(renderer, &width, &height, &mode));
	assert(width == SDL3_SCREEN_WIDTH && height == TEST_PRESENTATION_HEIGHT &&
		   mode == SDL_LOGICAL_PRESENTATION_LETTERBOX);
	/* Readback clips to the current viewport; sample the full window for overlay circles. */
	assert(SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED));
	SDL_Surface *hidden = SDL_RenderReadPixels(renderer, NULL);
	assert(hidden != NULL);
	for (enum TOUCH_CONTROL control = TOUCH_ESCAPE; control < TOUCH_CONTROL_COUNT; control++) {
		struct TOUCH_CIRCLE circle = sdl3_touch_circle(control, &area);
		legacy_u8 red, green, blue, alpha;
		assert(SDL_ReadSurfacePixel(hidden, (legacy_int)circle.x, (legacy_int)circle.y, &red,
									&green, &blue, &alpha));
		assert(red == 0 && green == 0 && blue == 0 && alpha == SDL_ALPHA_OPAQUE);
	}
	SDL_DestroySurface(hidden);
	assert(SDL_SetRenderLogicalPresentation(renderer, width, height, mode));
	sdl3_touch_set_intro_active(0);
	sdl3_touch_draw(renderer, window);
	assert(SDL_GetRenderLogicalPresentation(renderer, &width, &height, &mode));
	assert(width == SDL3_SCREEN_WIDTH && height == TEST_PRESENTATION_HEIGHT &&
		   mode == SDL_LOGICAL_PRESENTATION_LETTERBOX);
	assert(SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED));
	SDL_Surface *image = SDL_RenderReadPixels(renderer, NULL);
	assert(image != NULL);
	assert(SDL_SavePNG(image, "touch-layout.png"));
	SDL_DestroySurface(image);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
}

legacy_int main(void)
{
	SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
	assert(SDL_Init(0));
	test_visibility_and_geometry();
	test_holds_and_cancellation();
	test_single_tap_shifts();
	test_shift_tap_order();
	test_single_tap_shortcuts();
	test_gestures_and_pointer();
	test_game_state_and_rewind();
	test_intro_visibility_and_taps();
	test_rendering();
	sdl3_touch_reset();
	SDL_Quit();
	puts("SDL3 touch layout, visibility, multitouch, tap shortcuts, shifting and replay gestures "
		 "passed.");
	return 0;
}
