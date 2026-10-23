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
#define TEST_HD_WIDTH 1280
#define TEST_HD_HEIGHT 720
#define TEST_WIDE_WIDTH 2340
#define TEST_16X10_WIDTH 1280
#define TEST_16X10_HEIGHT 800
#define TEST_FORMER_SHRINK_THRESHOLD_WIDTH 1900
#define TEST_EDGE_MARGIN 10.0f
#define TEST_RADIUS_HEIGHT 0.075f
#define TEST_STEERING_HEIGHT 0.62f
#define TEST_RIGHT_RADIUS_OFFSET 3.5f
#define TEST_OUTER_RADIUS_OFFSET 0.75f
#define TEST_DOUBLE_SIZE 2
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
static SDL_FRect game_viewport = {TEST_WIDTH / 8, 0, TEST_HEIGHT * 4 / 3, TEST_HEIGHT};
static SDL_Renderer *test_renderer;

static void set_test_viewport(legacy_int width, legacy_int height)
{
	legacy_f32 viewport_width = SDL_min((legacy_f32)width, height * (4.0f / 3.0f));
	legacy_f32 viewport_height = viewport_width * (3.0f / 4.0f);
	game_viewport = (SDL_FRect){(width - viewport_width) / 2.0f, (height - viewport_height) / 2.0f,
								viewport_width, viewport_height};
}

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

void sdl3_video_game_to_window(legacy_f32 x, legacy_f32 y, legacy_f32 *window_x,
							   legacy_f32 *window_y)
{
	if (test_renderer != NULL) {
		assert(SDL_RenderCoordinatesToWindow(
			test_renderer, x, y * ((legacy_f32)TEST_PRESENTATION_HEIGHT / SDL3_SCREEN_HEIGHT),
			window_x, window_y));
		return;
	}
	*window_x = game_viewport.x + x * game_viewport.w / SDL3_SCREEN_WIDTH;
	*window_y = game_viewport.y + y * game_viewport.h / SDL3_SCREEN_HEIGHT;
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
	assert(SDL_fabsf(camera.x + camera.radius - (rectangle->x + rectangle->w - TEST_EDGE_MARGIN)) <
		   TEST_COORDINATE_EPSILON);
	assert(SDL_fabsf(camera.y - camera.radius - (rectangle->y + TEST_EDGE_MARGIN)) <
		   TEST_COORDINATE_EPSILON);
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
	static const SDL_Rect rectangles[] = {
		{0, 0, SDL3_SCREEN_WIDTH, SDL3_SCREEN_HEIGHT},
		{0, 0, TEST_HD_WIDTH, TEST_HD_HEIGHT},
		{0, 0, TEST_WIDTH, TEST_HEIGHT},
		{0, 0, TEST_WIDTH * TEST_DOUBLE_SIZE, TEST_HEIGHT * TEST_DOUBLE_SIZE},
		{TEST_SAFE_INSET_X, TEST_SAFE_INSET_Y, TEST_WIDTH - TEST_SAFE_INSET_X * 2,
		 TEST_HEIGHT - TEST_SAFE_INSET_Y * 2}};
	for (legacy_u32 index = 0; index < SDL_arraysize(rectangles); index++) {
		const SDL_Rect *rectangle = &rectangles[index];
		set_test_viewport(rectangle->x * 2 + rectangle->w, rectangle->y * 2 + rectangle->h);
		for (enum TOUCH_CONTROL control = TOUCH_ESCAPE; control < TOUCH_CONTROL_COUNT; control++) {
			struct TOUCH_CIRCLE circle = sdl3_touch_circle(control, rectangle);
			assert(circle.x - circle.radius >=
				   rectangle->x + TEST_EDGE_MARGIN - TEST_COORDINATE_EPSILON);
			assert(circle.x + circle.radius <=
				   rectangle->x + rectangle->w - TEST_EDGE_MARGIN + TEST_COORDINATE_EPSILON);
			assert(circle.y - circle.radius >=
				   rectangle->y + TEST_EDGE_MARGIN - TEST_COORDINATE_EPSILON);
			assert(circle.y + circle.radius <=
				   rectangle->y + rectangle->h - TEST_EDGE_MARGIN + TEST_COORDINATE_EPSILON);
		}
		struct TOUCH_CIRCLE left = sdl3_touch_circle(TOUCH_LEFT, rectangle);
		struct TOUCH_CIRCLE right = sdl3_touch_circle(TOUCH_RIGHT, rectangle);
		struct TOUCH_CIRCLE up = sdl3_touch_circle(TOUCH_SHIFT_UP, rectangle);
		struct TOUCH_CIRCLE down = sdl3_touch_circle(TOUCH_SHIFT_DOWN, rectangle);
		struct TOUCH_CIRCLE rewind = sdl3_touch_circle(TOUCH_REWIND, rectangle);
		assert(SDL_fabsf(left.x - left.radius - (rectangle->x + TEST_EDGE_MARGIN)) <
			   TEST_COORDINATE_EPSILON);
		assert(left.y == right.y);
		assert(SDL_fabsf(up.x - (left.x + right.x) / 2.0f) < TEST_COORDINATE_EPSILON);
		assert(up.x == down.x && up.y < left.y && down.y > left.y);
		assert(
			SDL_fabsf(rewind.y + rewind.radius - (rectangle->y + rectangle->h - TEST_EDGE_MARGIN)) <
			TEST_COORDINATE_EPSILON);
		assert_top_right_geometry(rectangle);
	}
	set_test_viewport(TEST_WIDTH, TEST_HEIGHT);
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
	struct TOUCH_CIRCLE left = sdl3_touch_circle(TOUCH_LEFT, &area);
	event(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, TEST_EDGE_MARGIN / 2.0f, left.y);
	assert(!sdl3_touch_key_state(SDL_SCANCODE_LEFT) && key_count == 0 && mouse_count == 1);
	event(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, TEST_EDGE_MARGIN / 2.0f, left.y);
	assert(mouse_count == 2 && mouse_down[0] && !mouse_down[1]);
}

static void test_landscape_pillarboxes(legacy_int width, legacy_int height, legacy_int inset,
									   legacy_u8 expect_overlap)
{
	SDL_Rect safe = {inset, 0, width - inset * 2, height};
	SDL_FRect saved_viewport = game_viewport;
	set_test_viewport(width, height);
	clear();
	sdl3_touch_configure(1, 1, 1, 0);
	for (enum TOUCH_CONTROL control = TOUCH_ESCAPE; control < TOUCH_CONTROL_COUNT; control++) {
		struct TOUCH_CIRCLE circle = sdl3_touch_circle(control, &safe);
		assert(circle.x - circle.radius >= safe.x + TEST_EDGE_MARGIN - TEST_COORDINATE_EPSILON);
		assert(circle.x + circle.radius <=
			   safe.x + safe.w - TEST_EDGE_MARGIN + TEST_COORDINATE_EPSILON);
		assert(circle.y - circle.radius >= safe.y + TEST_EDGE_MARGIN - TEST_COORDINATE_EPSILON);
		assert(circle.y + circle.radius <=
			   safe.y + safe.h - TEST_EDGE_MARGIN + TEST_COORDINATE_EPSILON);
		for (enum TOUCH_CONTROL other = control + 1; other < TOUCH_CONTROL_COUNT; other++) {
			struct TOUCH_CIRCLE next = sdl3_touch_circle(other, &safe);
			legacy_f32 dx = circle.x - next.x;
			legacy_f32 dy = circle.y - next.y;
			assert(SDL_sqrtf(dx * dx + dy * dy) + TEST_COORDINATE_EPSILON >=
				   circle.radius + next.radius);
		}
		/* Stored float radii can round differently from x87 intermediates. */
		assert(SDL_fabsf(circle.radius - safe.h * TEST_RADIUS_HEIGHT) < TEST_COORDINATE_EPSILON);
	}
	struct TOUCH_CIRCLE left = sdl3_touch_circle(TOUCH_LEFT, &safe);
	struct TOUCH_CIRCLE right = sdl3_touch_circle(TOUCH_RIGHT, &safe);
	struct TOUCH_CIRCLE up = sdl3_touch_circle(TOUCH_SHIFT_UP, &safe);
	struct TOUCH_CIRCLE down = sdl3_touch_circle(TOUCH_SHIFT_DOWN, &safe);
	assert(left.y == right.y && right.x > left.x);
	assert(SDL_fabsf(up.x - (left.x + right.x) / 2.0f) < TEST_COORDINATE_EPSILON);
	assert(up.x == down.x && up.y < left.y && down.y > left.y);
	assert((right.x + right.radius > game_viewport.x) == expect_overlap);
	/* Touch coordinates cover the full canvas, including both black side areas. */
	SDL_TouchFingerEvent finger;
	SDL_zero(finger);
	finger.type = SDL_EVENT_FINGER_DOWN;
	finger.touchID = TEST_DEVICE;
	finger.fingerID = TEST_FIRST_FINGER;
	/* A NULL test window uses the area's right/bottom edges as the window size. */
	finger.x = left.x / (safe.x + safe.w);
	finger.y = left.y / (safe.y + safe.h);
	sdl3_touch_event(&finger, &safe);
	struct TOUCH_CIRCLE accelerate = sdl3_touch_circle(TOUCH_ACCELERATE, &safe);
	finger.fingerID = TEST_SECOND_FINGER;
	finger.x = accelerate.x / (safe.x + safe.w);
	finger.y = accelerate.y / (safe.y + safe.h);
	sdl3_touch_event(&finger, &safe);
	assert(sdl3_touch_key_state(SDL_SCANCODE_LEFT));
	assert(sdl3_touch_key_state(SDL_SCANCODE_UP));
	assert(key_count == 2 && queued_keys[0] == KEY_LEFT && queued_keys[1] == KEY_UP);
	/* Use a full-size outer point independent of the returned circle. Gutter-based
	 * shrinking on 16:9 screens used to turn this right-arrow tap into a mouse tap. */
	legacy_f32 radius = safe.h * TEST_RADIUS_HEIGHT;
	finger.fingerID = TEST_THIRD_FINGER;
	finger.x = (safe.x + TEST_EDGE_MARGIN +
				radius * (TEST_RIGHT_RADIUS_OFFSET + TEST_OUTER_RADIUS_OFFSET)) /
			   (safe.x + safe.w);
	finger.y = (safe.y + safe.h * TEST_STEERING_HEIGHT) / (safe.y + safe.h);
	sdl3_touch_event(&finger, &safe);
	assert(sdl3_touch_key_state(SDL_SCANCODE_RIGHT));
	assert(key_count == 3 && queued_keys[2] == KEY_RIGHT && mouse_count == 0);
	sdl3_touch_enable(0);
	assert(!sdl3_touch_key_state(SDL_SCANCODE_LEFT));
	assert(!sdl3_touch_key_state(SDL_SCANCODE_UP));
	assert(!sdl3_touch_key_state(SDL_SCANCODE_RIGHT));
	sdl3_touch_enable(1);
	game_viewport = saved_viewport;
	clear();
}

static void test_narrow_pillarboxes(void)
{
	SDL_FRect saved_viewport = game_viewport;
	game_viewport = (SDL_FRect){area.x, area.y, area.w, area.h};
	struct TOUCH_CIRCLE left = sdl3_touch_circle(TOUCH_LEFT, &area);
	assert(SDL_fabsf(left.radius - area.h * TEST_RADIUS_HEIGHT) < TEST_COORDINATE_EPSILON);
	game_viewport = saved_viewport;
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
	/* Tilt steering keeps the same touch pedals, gears and steering override. */
	driving_mode = SDL3_DRIVING_TILT;
	sdl3_touch_sync_game();
	assert(sdl3_touch_visible(TOUCH_LEFT) && sdl3_touch_visible(TOUCH_ACCELERATE));
	assert(!sdl3_touch_visible(TOUCH_SHIFT_UP) && !sdl3_touch_visible(TOUCH_SHIFT_DOWN));
	game_replay_mode = REPLAY_MODE_LIVE;
	is_in_replay = 0;
	sdl3_touch_sync_game();
	assert(sdl3_touch_visible(TOUCH_SHIFT_UP) && sdl3_touch_visible(TOUCH_SHIFT_DOWN));
	/* Last-used keyboard steering restores touch even if a legacy flag is stale. */
	mouse_driving_enabled = 1;
	driving_mode = SDL3_DRIVING_KEYBOARD;
	sdl3_touch_sync_game();
	assert(sdl3_touch_visible(TOUCH_LEFT) && sdl3_touch_visible(TOUCH_ESCAPE));
	mouse_driving_enabled = 0;
	sdl3_touch_set_game_active(0);
}

static void test_layout_visibility_and_taps(void)
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
	circle_event(SDL_EVENT_FINGER_DOWN, TEST_THIRD_FINGER, TOUCH_REWIND);
	assert(sdl3_touch_key_state(SDL_SCANCODE_LEFT) && sdl3_touch_key_state(SDL_SCANCODE_UP) &&
		   sdl3_touch_key_state(SDL_SCANCODE_Q));
	sdl3_touch_set_layout_visible(0);
	sdl3_touch_sync_game();
	for (enum TOUCH_CONTROL control = TOUCH_ESCAPE; control < TOUCH_CONTROL_COUNT; control++) {
		assert(!sdl3_touch_visible(control));
	}
	assert(!sdl3_touch_key_state(SDL_SCANCODE_LEFT) && !sdl3_touch_key_state(SDL_SCANCODE_UP) &&
		   !sdl3_touch_key_state(SDL_SCANCODE_Q));
	assert(sdl3_touch_take_shift_flags() == 0);
	circle_event(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, TOUCH_LEFT);
	circle_event(SDL_EVENT_FINGER_UP, TEST_SECOND_FINGER, TOUCH_ACCELERATE);
	circle_event(SDL_EVENT_FINGER_UP, TEST_THIRD_FINGER, TOUCH_REWIND);
	assert(mouse_count == 0);
	key_count = 0;

	/* Hidden circle positions become ordinary menu taps. Reapplying the preference
	 * must not interrupt a tap, while actually changing it must release the mouse. */
	sdl3_touch_set_game_active(0);
	circle_event(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, TOUCH_LEFT);
	sdl3_touch_update();
	assert(key_count == 0 && mouse_count == 1 && mouse_down[0]);
	sdl3_touch_set_layout_visible(0);
	assert(mouse_count == 1);
	circle_event(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, TOUCH_LEFT);
	assert(mouse_count == 2 && !mouse_down[1]);
	circle_event(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, TOUCH_ESCAPE);
	sdl3_touch_set_layout_visible(1);
	assert(mouse_count == 4 && mouse_down[2] && !mouse_down[3]);
	circle_event(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, TOUCH_ESCAPE);
	assert(mouse_count == 4 && key_count == 0);
	assert(sdl3_touch_visible(TOUCH_LEFT) && sdl3_touch_visible(TOUCH_ESCAPE));
	assert(!sdl3_touch_visible(TOUCH_SHIFT_UP) && !sdl3_touch_visible(TOUCH_REWIND));

	sdl3_touch_set_layout_visible(0);
	game_replay_mode = REPLAY_MODE_PLAYBACK;
	is_in_replay = 1;
	sdl3_touch_set_game_active(1);
	clear();
	event(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, TEST_VIEW_X, TEST_VIEW_Y);
	event(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, TEST_VIEW_X + TEST_SWIPE_X, TEST_VIEW_Y);
	assert(sdl3_touch_take_seek() == 1);
	event(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, TEST_VIEW_X, TEST_VIEW_Y);
	event(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, TEST_VIEW_X - TEST_SWIPE_X, TEST_VIEW_Y);
	assert(sdl3_touch_take_seek() == -1 && mouse_count == 0);
	event(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, TEST_TOOLBAR_X, TEST_TOOLBAR_Y);
	event(SDL_EVENT_FINGER_MOTION, TEST_FIRST_FINGER, TEST_TOOLBAR_X + TEST_SWIPE_X,
		  TEST_TOOLBAR_Y);
	event(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, TEST_TOOLBAR_X + TEST_SWIPE_X, TEST_TOOLBAR_Y);
	assert(sdl3_touch_take_seek() == 0 && key_count == 0 && mouse_count == 3);
	assert(mouse_down[0] && mouse_down[1] && !mouse_down[2]);

	/* Neither game-state synchronization nor an intro exit overrides the preference. */
	driving_mode = SDL3_DRIVING_MOUSE;
	sdl3_touch_sync_game();
	driving_mode = SDL3_DRIVING_KEYBOARD;
	sdl3_touch_sync_game();
	sdl3_touch_set_intro_active(1);
	sdl3_touch_set_intro_active(0);
	assert(!sdl3_touch_visible(TOUCH_LEFT));
	sdl3_touch_set_layout_visible(1);
	circle_event(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, TOUCH_LEFT);
	assert(sdl3_touch_key_state(SDL_SCANCODE_LEFT) && key_count == 1 && queued_keys[0] == KEY_LEFT);
	circle_event(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, TOUCH_LEFT);
	assert(!sdl3_touch_key_state(SDL_SCANCODE_LEFT));
	assert(!sdl3_touch_visible(TOUCH_SHIFT_UP) && !sdl3_touch_visible(TOUCH_REWIND));
	sdl3_touch_set_game_active(0);
	clear();
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

static void assert_hidden_rendering(SDL_Renderer *renderer, SDL_Window *window,
									const SDL_Rect *rectangle)
{
	sdl3_touch_draw(renderer, window);
	legacy_int width, height;
	SDL_RendererLogicalPresentation mode;
	assert(SDL_GetRenderLogicalPresentation(renderer, &width, &height, &mode));
	assert(width == SDL3_SCREEN_WIDTH && height == TEST_PRESENTATION_HEIGHT &&
		   mode == SDL_LOGICAL_PRESENTATION_LETTERBOX);
	/* Readback clips to the current viewport; sample the full window for overlay circles. */
	test_renderer = NULL;
	assert(SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED));
	SDL_Surface *hidden = SDL_RenderReadPixels(renderer, NULL);
	assert(hidden != NULL);
	for (enum TOUCH_CONTROL control = TOUCH_ESCAPE; control < TOUCH_CONTROL_COUNT; control++) {
		struct TOUCH_CIRCLE circle = sdl3_touch_circle(control, rectangle);
		legacy_u8 red, green, blue, alpha;
		assert(SDL_ReadSurfacePixel(hidden, (legacy_int)circle.x, (legacy_int)circle.y, &red,
									&green, &blue, &alpha));
		assert(red == 0 && green == 0 && blue == 0 && alpha == SDL_ALPHA_OPAQUE);
	}
	SDL_DestroySurface(hidden);
	assert(SDL_SetRenderLogicalPresentation(renderer, width, height, mode));
	test_renderer = renderer;
}

static void test_rendering(legacy_int window_width, legacy_int window_height,
						   const legacy_char *filename, legacy_u8 expect_overlap)
{
	SDL_Rect rectangle = {0, 0, window_width, window_height};
	SDL_FRect saved_viewport = game_viewport;
	set_test_viewport(window_width, window_height);
	assert(SDL_InitSubSystem(SDL_INIT_VIDEO));
	SDL_Window *window = SDL_CreateWindow("Touch layout", window_width, window_height, 0);
	assert(window != NULL);
	SDL_Renderer *renderer = SDL_CreateRenderer(window, "software");
	assert(renderer != NULL);
	test_renderer = renderer;
	assert(SDL_SetRenderLogicalPresentation(renderer, SDL3_SCREEN_WIDTH, TEST_PRESENTATION_HEIGHT,
											SDL_LOGICAL_PRESENTATION_LETTERBOX));
	assert(SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE));
	assert(SDL_RenderClear(renderer));
	sdl3_touch_configure(1, 1, 1, 0);
	sdl3_touch_set_intro_active(1);
	assert_hidden_rendering(renderer, window, &rectangle);
	sdl3_touch_set_intro_active(0);
	sdl3_touch_set_layout_visible(0);
	assert_hidden_rendering(renderer, window, &rectangle);
	sdl3_touch_set_layout_visible(1);
	legacy_int width, height;
	SDL_RendererLogicalPresentation mode;
	sdl3_touch_draw(renderer, window);
	assert(SDL_GetRenderLogicalPresentation(renderer, &width, &height, &mode));
	assert(width == SDL3_SCREEN_WIDTH && height == TEST_PRESENTATION_HEIGHT &&
		   mode == SDL_LOGICAL_PRESENTATION_LETTERBOX);
	struct TOUCH_CIRCLE circles[TOUCH_CONTROL_COUNT];
	for (enum TOUCH_CONTROL control = TOUCH_ESCAPE; control < TOUCH_CONTROL_COUNT; control++) {
		circles[control] = sdl3_touch_circle(control, &rectangle);
	}
	test_renderer = NULL;
	assert(SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED));
	SDL_Surface *image = SDL_RenderReadPixels(renderer, NULL);
	assert(image != NULL);
	struct TOUCH_CIRCLE left = circles[TOUCH_LEFT];
	legacy_u8 red, green, blue, alpha;
	assert(SDL_ReadSurfacePixel(image, (legacy_int)left.x, (legacy_int)left.y, &red, &green, &blue,
								&alpha));
	assert(red != 0 || green != 0 || blue != 0);
	assert(SDL_ReadSurfacePixel(image, (legacy_int)(TEST_EDGE_MARGIN / 2.0f), (legacy_int)left.y,
								&red, &green, &blue, &alpha));
	assert(red == 0 && green == 0 && blue == 0 && alpha == SDL_ALPHA_OPAQUE);
	for (enum TOUCH_CONTROL control = TOUCH_ESCAPE; control < TOUCH_CONTROL_COUNT; control++) {
		struct TOUCH_CIRCLE circle = circles[control];
		assert(SDL_ReadSurfacePixel(image, (legacy_int)circle.x, (legacy_int)circle.y, &red, &green,
									&blue, &alpha));
		assert(red > 0 && green > 0 && blue > 0);
	}
	legacy_u32 overlay_pixels = 0;
	/* Sample complete game pixels; fractional viewport edges can round past the surface. */
	legacy_int game_bottom =
		SDL_min(image->h, (legacy_int)SDL_floorf(game_viewport.y + game_viewport.h));
	legacy_int game_right =
		SDL_min(image->w, (legacy_int)SDL_floorf(game_viewport.x + game_viewport.w));
	for (legacy_int y = (legacy_int)SDL_ceilf(game_viewport.y); y < game_bottom; y += 8) {
		for (legacy_int x = (legacy_int)SDL_ceilf(game_viewport.x); x < game_right; x += 8) {
			assert(SDL_ReadSurfacePixel(image, x, y, &red, &green, &blue, &alpha));
			if (red != 0 || green != 0 || blue != 0) {
				++overlay_pixels;
			}
			if (!expect_overlap && (red != 0 || green != 0 || blue != 0)) {
				fprintf(
					stderr,
					"Overlay entered game at %d,%d in %dx%d; viewport %.3f..%.3f, RGB %u,%u,%u\n",
					x, y, window_width, window_height, game_viewport.x,
					game_viewport.x + game_viewport.w, red, green, blue);
			}
			assert(expect_overlap || (red == 0 && green == 0 && blue == 0));
		}
	}
	assert((overlay_pixels > 0) == expect_overlap);
	assert(SDL_SavePNG(image, filename));
	SDL_DestroySurface(image);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	game_viewport = saved_viewport;
}

legacy_int main(void)
{
	SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
	assert(SDL_Init(0));
	test_visibility_and_geometry();
	test_landscape_pillarboxes(TEST_WIDTH, TEST_HEIGHT, 0, 1);
	test_landscape_pillarboxes(TEST_WIDTH, TEST_HEIGHT, TEST_SAFE_INSET_X, 1);
	test_landscape_pillarboxes(TEST_WIDE_WIDTH, TEST_HEIGHT, 0, 0);
	test_landscape_pillarboxes(TEST_16X10_WIDTH, TEST_16X10_HEIGHT, 0, 1);
	test_landscape_pillarboxes(TEST_WIDTH * TEST_DOUBLE_SIZE, TEST_HEIGHT * TEST_DOUBLE_SIZE, 0, 1);
	/* Adjacent widths straddle the old discontinuous switch to smaller buttons. */
	test_landscape_pillarboxes(TEST_FORMER_SHRINK_THRESHOLD_WIDTH - 1, TEST_HEIGHT, 0, 1);
	test_landscape_pillarboxes(TEST_FORMER_SHRINK_THRESHOLD_WIDTH, TEST_HEIGHT, 0, 1);
	test_narrow_pillarboxes();
	test_holds_and_cancellation();
	test_single_tap_shifts();
	test_shift_tap_order();
	test_single_tap_shortcuts();
	test_gestures_and_pointer();
	test_game_state_and_rewind();
	test_layout_visibility_and_taps();
	test_intro_visibility_and_taps();
	test_rendering(TEST_WIDTH, TEST_HEIGHT, "touch-layout.png", 1);
	test_rendering(TEST_WIDE_WIDTH, TEST_HEIGHT, "touch-layout-wide.png", 0);
	test_rendering(TEST_16X10_WIDTH, TEST_16X10_HEIGHT, "touch-layout-16x10.png", 1);
	sdl3_touch_reset();
	SDL_Quit();
	puts("SDL3 touch layout, visibility, multitouch, tap shortcuts, shifting and replay gestures "
		 "passed.");
	return 0;
}
