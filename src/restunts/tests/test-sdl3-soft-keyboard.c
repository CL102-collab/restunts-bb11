#include "../platform/sdl3/soft_keyboard.h"
#include "../c/keyboard.h"
#include <assert.h>
#include <stdio.h>

#define TEST_KEY_CAPACITY 8U
#define TEST_SAMPLE_STEP 10
#define TEST_FIRST_FINGER 11
#define TEST_SECOND_FINGER 12

struct KEY_POSITION {
	legacy_u16 value;
	SDL_FPoint point;
	legacy_u8 found;
};

static struct KEY_POSITION keys[] = {
	{.value = '0'},		  {.value = '1'},			{.value = '2'},		  {.value = '3'},
	{.value = '4'},		  {.value = '5'},			{.value = '6'},		  {.value = '7'},
	{.value = '8'},		  {.value = '9'},			{.value = 'a'},		  {.value = 'b'},
	{.value = 'c'},		  {.value = 'd'},			{.value = 'e'},		  {.value = 'f'},
	{.value = 'g'},		  {.value = 'h'},			{.value = 'i'},		  {.value = 'j'},
	{.value = 'k'},		  {.value = 'l'},			{.value = 'm'},		  {.value = 'n'},
	{.value = 'o'},		  {.value = 'p'},			{.value = 'q'},		  {.value = 'r'},
	{.value = 's'},		  {.value = 't'},			{.value = 'u'},		  {.value = 'v'},
	{.value = 'w'},		  {.value = 'x'},			{.value = 'y'},		  {.value = 'z'},
	{.value = '-'},		  {.value = '_'},			{.value = '.'},		  {.value = '/'},
	{.value = KEY_SPACE}, {.value = KEY_BACKSPACE}, {.value = KEY_LEFT},  {.value = KEY_RIGHT},
	{.value = KEY_HOME},  {.value = KEY_END},		{.value = KEY_ENTER}, {.value = KEY_ESCAPE}};
static legacy_u16 queued_keys[TEST_KEY_CAPACITY];
static legacy_u32 key_count;
static SDL_Window *window;
static legacy_int window_width;
static legacy_int window_height;

void sdl3_input_queue_key(legacy_u16 value)
{
	assert(key_count < TEST_KEY_CAPACITY);
	queued_keys[key_count++] = value;
}

static void begin(void)
{
	sdl3_soft_keyboard_end();
	sdl3_soft_keyboard_begin((const legacy_s8 *)"name", 4, "Track");
	key_count = 0;
}

static legacy_u8 finger(SDL_EventType type, SDL_FingerID id, SDL_FPoint point)
{
	SDL_Event event;
	SDL_zero(event);
	event.type = type;
	event.tfinger.windowID = SDL_GetWindowID(window);
	event.tfinger.touchID = 1;
	event.tfinger.fingerID = id;
	event.tfinger.x = point.x / window_width;
	event.tfinger.y = point.y / window_height;
	return sdl3_soft_keyboard_event(&event, window);
}

static void tap(SDL_FPoint point)
{
	assert(finger(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, point));
	assert(finger(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, point));
}

static struct KEY_POSITION *find_key(legacy_u16 value)
{
	for (legacy_u32 index = 0; index < SDL_arraysize(keys); index++) {
		if (keys[index].value == value) {
			return &keys[index];
		}
	}
	return NULL;
}

static SDL_FPoint point(legacy_u16 value)
{
	struct KEY_POSITION *key = find_key(value);
	assert(key != NULL && key->found);
	return key->point;
}

static SDL_FPoint shift_point(void)
{
	/* Shift occupies the left side of the same displayed row as Z. */
	return (SDL_FPoint){point('0').x, point('z').y};
}

static void assert_key(legacy_u16 value)
{
	assert(key_count == 1 && queued_keys[0] == value);
	key_count = 0;
}

static void discover_keys(void)
{
	for (legacy_u32 index = 0; index < SDL_arraysize(keys); index++) {
		keys[index].found = false;
	}
	begin();
	/* Discover the actual hit regions rather than copying the layout formulas.
	 * Every emitted value must belong to the supported alphabet or editing keys. */
	for (legacy_int y = TEST_SAMPLE_STEP / 2; y < window_height; y += TEST_SAMPLE_STEP) {
		for (legacy_int x = TEST_SAMPLE_STEP / 2; x < window_width; x += TEST_SAMPLE_STEP) {
			key_count = 0;
			tap((SDL_FPoint){x, y});
			assert(key_count <= 1);
			if (key_count != 0) {
				legacy_u16 value = queued_keys[0];
				if (value >= 'A' && value <= 'Z') {
					value = (legacy_u16)(value - 'A' + 'a');
				}
				struct KEY_POSITION *key = find_key(value);
				assert(key != NULL);
				if (!key->found) {
					key->point = (SDL_FPoint){x, y};
					key->found = true;
				}
			}
		}
	}
	for (legacy_u32 index = 0; index < SDL_arraysize(keys); index++) {
		assert(keys[index].found);
	}
	begin();
	for (legacy_u32 index = 0; index < SDL_arraysize(keys); index++) {
		tap(keys[index].point);
		assert_key(keys[index].value);
	}
}

static void test_shift_and_sessions(void)
{
	begin();
	tap(shift_point());
	assert(key_count == 0);
	tap(point('a'));
	assert_key('A');
	tap(point('z'));
	assert_key('Z');
	tap(point('1'));
	assert_key('1');
	tap(shift_point());
	tap(point('a'));
	assert_key('a');
	tap(shift_point());
	assert(finger(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, point('a')));
	begin();
	assert(finger(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, point('a')));
	assert(key_count == 0);
	tap(point('a'));
	assert_key('a');
}

static void test_release_and_cancellation(void)
{
	begin();
	/* The finger that opened the editor must not press a keyboard key on release. */
	assert(finger(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, point(KEY_ENTER)));
	assert(key_count == 0);
	assert(finger(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, point('a')));
	assert(key_count == 0);
	assert(finger(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, point('a')));
	assert_key('a');
	assert(finger(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, point('a')));
	assert(finger(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, point('b')));
	assert(key_count == 0);
	assert(finger(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, point('a')));
	assert(finger(SDL_EVENT_FINGER_MOTION, TEST_FIRST_FINGER, point('b')));
	assert(finger(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, point('a')));
	assert(key_count == 0);
	assert(finger(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, point('a')));
	assert(finger(SDL_EVENT_FINGER_CANCELED, TEST_FIRST_FINGER, point('a')));
	assert(finger(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, point('a')));
	assert(key_count == 0);
	assert(finger(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, point('a')));
	assert(finger(SDL_EVENT_FINGER_DOWN, TEST_SECOND_FINGER, point('b')));
	assert(finger(SDL_EVENT_FINGER_UP, TEST_SECOND_FINGER, point('b')));
	assert(key_count == 0);
	assert(finger(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, point('a')));
	assert_key('a');
	SDL_FRect game = sdl3_soft_keyboard_game_area(window);
	tap((SDL_FPoint){game.x + game.w / 2, game.y + game.h / 2});
	assert(key_count == 0);
}

static void test_mouse_and_disabled_keyboard(void)
{
	begin();
	SDL_Event event;
	SDL_zero(event);
	event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
	event.button.button = SDL_BUTTON_LEFT;
	event.button.x = point('a').x;
	event.button.y = point('a').y;
	assert(sdl3_soft_keyboard_event(&event, window));
	assert(key_count == 0);
	event.type = SDL_EVENT_MOUSE_BUTTON_UP;
	assert(sdl3_soft_keyboard_event(&event, window));
	assert_key('a');
	event.button.which = SDL_TOUCH_MOUSEID;
	event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
	assert(sdl3_soft_keyboard_event(&event, window));
	event.type = SDL_EVENT_MOUSE_BUTTON_UP;
	assert(sdl3_soft_keyboard_event(&event, window));
	assert(key_count == 0);
	event.type = SDL_EVENT_KEY_DOWN;
	assert(!sdl3_soft_keyboard_event(&event, window));
	sdl3_soft_keyboard_enable(false);
	assert(!sdl3_soft_keyboard_enabled() && !sdl3_soft_keyboard_active());
	begin();
	assert(!sdl3_soft_keyboard_active());
	assert(!finger(SDL_EVENT_FINGER_DOWN, TEST_FIRST_FINGER, point('a')));
	assert(!finger(SDL_EVENT_FINGER_UP, TEST_FIRST_FINGER, point('a')));
	assert(key_count == 0);
	sdl3_soft_keyboard_enable(true);
}

static void test_drawing(SDL_Renderer *renderer)
{
	begin();
	SDL_FRect game = sdl3_soft_keyboard_game_area(window);
	assert(game.w > 0 && game.h > 0 && game.x >= 0 && game.y >= 0);
	assert(game.x + game.w <= window_width && game.y + game.h <= window_height);
	assert(SDL_fabsf(game.w / game.h - 4.0f / 3.0f) < 0.001f);
	assert(SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED));
	assert(SDL_SetRenderDrawColor(renderer, 35, 61, 93, SDL_ALPHA_OPAQUE));
	assert(SDL_RenderClear(renderer));
	sdl3_soft_keyboard_update((const legacy_s8 *)"edited_name", 6);
	sdl3_soft_keyboard_draw(renderer, window);
	legacy_u8 red, green, blue, alpha;
	assert(SDL_GetRenderDrawColor(renderer, &red, &green, &blue, &alpha));
	assert(red == 35 && green == 61 && blue == 93 && alpha == SDL_ALPHA_OPAQUE);
	SDL_Surface *image = SDL_RenderReadPixels(renderer, NULL);
	assert(image != NULL);
	legacy_u32 keyboard_pixels = 0;
	for (legacy_int y = 1; y < image->h; y += 3) {
		for (legacy_int x = 1; x < image->w; x += 3) {
			assert(SDL_ReadSurfacePixel(image, x, y, &red, &green, &blue, &alpha));
			if (x > game.x + 1 && x < game.x + game.w - 1 && y > game.y + 1 &&
				y < game.y + game.h - 1) {
				if (red != 35 || green != 61 || blue != 93) {
					fprintf(stderr,
							"Keyboard changed game pixel %d,%d to %u,%u,%u in %dx%d; game "
							"%.2f,%.2f %.2fx%.2f\n",
							x, y, red, green, blue, window_width, window_height, game.x, game.y,
							game.w, game.h);
				}
				assert(red == 35 && green == 61 && blue == 93);
			} else if ((x < game.x || x >= game.x + game.w || y < game.y || y >= game.y + game.h) &&
					   (red != 35 || green != 61 || blue != 93)) {
				keyboard_pixels++;
			}
		}
	}
	assert(keyboard_pixels > 100);
	SDL_DestroySurface(image);
	sdl3_soft_keyboard_end();
	assert(SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE));
	assert(SDL_RenderClear(renderer));
	sdl3_soft_keyboard_draw(renderer, window);
	image = SDL_RenderReadPixels(renderer, NULL);
	assert(image != NULL);
	assert(SDL_ReadSurfacePixel(image, (legacy_int)point('a').x, (legacy_int)point('a').y, &red,
								&green, &blue, &alpha));
	assert(red == 0 && green == 0 && blue == 0);
	SDL_DestroySurface(image);
}

legacy_int main(void)
{
	SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
	assert(SDL_Init(SDL_INIT_VIDEO));
	static const legacy_int sizes[][2] = {{390, 844}, {844, 390}, {640, 480}};
	for (legacy_u32 index = 0; index < SDL_arraysize(sizes); index++) {
		window_width = sizes[index][0];
		window_height = sizes[index][1];
		window = SDL_CreateWindow("In-game keyboard", window_width, window_height, 0);
		assert(window != NULL);
		SDL_Renderer *renderer = SDL_CreateRenderer(window, "software");
		assert(renderer != NULL);
		sdl3_soft_keyboard_enable(true);
		discover_keys();
		test_shift_and_sessions();
		test_release_and_cancellation();
		test_mouse_and_disabled_keyboard();
		test_drawing(renderer);
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);
	}
	SDL_Quit();
	puts("SDL3 in-game keyboard alphabet, editing keys, release handling, session reset and layout "
		 "passed.");
	return 0;
}
