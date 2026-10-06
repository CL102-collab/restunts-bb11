#include "touch.h"
#include "controller_game.h"
#include "../../c/game_input.h"
#include "../../c/keyboard.h"
#include "../../c/platform.h"
#include <string.h>
#ifdef RESTUNTS_FULL
#include "../../c/externs.h"
#include "../../c/gamestate.h"
#include "../../c/race_graphics.h"
#endif

#define TOUCH_FINGER_COUNT 16U
#define INPUT_SHIFT_QUEUE_CAPACITY 64U
#define TOUCH_EDGE_MARGIN 10.0f
#define TOUCH_RADIUS_HEIGHT 0.075f
#define TOUCH_STEERING_HEIGHT 0.62f
#define TOUCH_RIGHT_RADIUS_OFFSET 3.5f
#define TOUCH_SHIFT_RADIUS_OFFSET 2.25f
#define TOUCH_VERTICAL_RADIUS_OFFSET 2.2f
#define TOUCH_ACCELERATE_HEIGHT 0.55f
#define TOUCH_BRAKE_HEIGHT 0.76f
#define TOUCH_SWIPE_HEIGHT 0.09f
#define TOUCH_SWIPE_HORIZONTAL_RATIO 1.5f
#define TOUCH_REPLAY_TOOLBAR_Y 151.0f
#define TOUCH_REPEAT_DELAY_MS 350U
#define TOUCH_REPEAT_INTERVAL_MS 100U
#define TOUCH_CIRCLE_SEGMENTS 48U
#define TOUCH_FULL_TURN 6.283185307179586f
#define TOUCH_FILL_ALPHA 38U
#define TOUCH_PRESSED_ALPHA 110U
#define TOUCH_OUTLINE_ALPHA 210U
#define TOUCH_LABEL_WIDTH 1.5f
#define TOUCH_SINGLE_LETTER_LABEL_HEIGHT 0.6f
#define TOUCH_ARROW_SCALE 0.36f
#define TOUCH_LABEL_OFFSET 0.2f
#define TOUCH_ESCAPE_CODE ((legacy_u16)KEY_ESCAPE)

enum TOUCH_POINTER_MODE {
	TOUCH_POINTER_CONTROL,
	TOUCH_POINTER_MOUSE,
	TOUCH_POINTER_GESTURE,
	TOUCH_POINTER_IGNORED
};
struct TOUCH_FINGER {
	SDL_TouchID device;
	SDL_FingerID id;
	legacy_u8 active;
	legacy_u8 pressed;
	enum TOUCH_POINTER_MODE mode;
	enum TOUCH_CONTROL control;
	legacy_f32 start_x;
	legacy_f32 start_y;
	legacy_f32 x;
	legacy_f32 y;
};

static struct TOUCH_FINGER fingers[TOUCH_FINGER_COUNT];
#ifdef __ANDROID__
static legacy_u8 enabled = 1;
#else
static legacy_u8 enabled;
#endif
static legacy_u8 layout_visible = true;
static legacy_u8 keyboard_selected = 1;
static legacy_u8 intro_active;
static legacy_u8 racing;
static legacy_u8 manual_gears;
static legacy_u8 replay_active;
static legacy_u8 game_active;
static legacy_u8 replay_interaction;
static legacy_u8 rewind_active;
static legacy_u8 shift_event_source;
static legacy_u16 shift_queue[INPUT_SHIFT_QUEUE_CAPACITY];
static legacy_u32 shift_read;
static legacy_u32 shift_count;
static legacy_s16 seek_direction;
static legacy_u64 repeat_at[TOUCH_CONTROL_COUNT];
static const SDL_Scancode control_scancodes[TOUCH_CONTROL_COUNT] = {
	SDL_SCANCODE_ESCAPE, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_A, SDL_SCANCODE_Z,
	SDL_SCANCODE_Q,		 SDL_SCANCODE_UP,	SDL_SCANCODE_DOWN,	SDL_SCANCODE_C, SDL_SCANCODE_T};
static const legacy_u16 control_keys[TOUCH_CONTROL_COUNT] = {
	TOUCH_ESCAPE_CODE, KEY_LEFT, KEY_RIGHT, 'a', 'z', 'q', KEY_UP, KEY_DOWN, 'c', 't'};

static bool SDLCALL input_keep_unrelated_event(void *unused, SDL_Event *event)
{
	(void)unused;
	return event->type != SDL_EVENT_USER || event->user.data1 != &shift_event_source;
}

void sdl3_input_reset_shifts(void)
{
	shift_read = 0;
	shift_count = 0;
	/* Cancel requests that have not reached the game thread yet, without
	 * discarding other SDL user events. */
	SDL_FilterEvents(input_keep_unrelated_event, NULL);
}

legacy_u8 sdl3_input_request_shift(legacy_u8 up)
{
	SDL_Event event;
	SDL_zero(event);
	event.type = SDL_EVENT_USER;
	event.user.data1 = &shift_event_source;
	event.user.code = up ? INPUT_SHIFT_UP_FLAG : INPUT_SHIFT_DOWN_FLAG;
	return SDL_PushEvent(&event);
}

void sdl3_input_queue_shift(legacy_u16 flag)
{
	if ((flag != INPUT_SHIFT_UP_FLAG && flag != INPUT_SHIFT_DOWN_FLAG) || intro_active ||
		rewind_active) {
		return;
	}
	if (shift_count == INPUT_SHIFT_QUEUE_CAPACITY) {
		shift_read = (shift_read + 1U) % INPUT_SHIFT_QUEUE_CAPACITY;
		shift_count--;
	}
	shift_queue[(shift_read + shift_count++) % INPUT_SHIFT_QUEUE_CAPACITY] = flag;
}

void sdl3_input_shift_event(const SDL_UserEvent *event)
{
	if (event->data1 == &shift_event_source) {
		sdl3_input_queue_shift((legacy_u16)event->code);
	}
}

void sdl3_touch_reset(void)
{
	for (legacy_u32 i = 0; i < TOUCH_FINGER_COUNT; i++) {
		if (fingers[i].active && fingers[i].mode == TOUCH_POINTER_MOUSE) {
			sdl3_input_touch_mouse(fingers[i].x, fingers[i].y, false);
		}
	}
	memset(fingers, 0, sizeof(fingers));
	memset(repeat_at, 0, sizeof(repeat_at));
	sdl3_input_reset_shifts();
	seek_direction = 0;
}

void sdl3_touch_enable(legacy_u8 value)
{
	if (enabled != value) {
		sdl3_touch_reset();
	}
	enabled = value;
}

void sdl3_touch_configure(legacy_u8 keyboard, legacy_u8 live, legacy_u8 manual, legacy_u8 replay)
{
	if (keyboard_selected != keyboard || racing != live || manual_gears != manual ||
		replay_active != replay) {
		sdl3_touch_reset();
	}
	keyboard_selected = keyboard;
	racing = live;
	manual_gears = manual;
	replay_active = replay;
}

void sdl3_touch_set_layout_visible(legacy_u8 visible)
{
	visible = visible != 0;
	if (layout_visible != visible) {
		sdl3_touch_reset();
	}
	layout_visible = visible;
}

void sdl3_touch_set_intro_active(legacy_u8 active)
{
	if (intro_active != active) {
		sdl3_touch_reset();
	}
	intro_active = active;
}

void sdl3_touch_set_game_active(legacy_u8 active)
{
	game_active = active;
	if (!active) {
		rewind_active = false;
	}
	replay_interaction = active;
	sdl3_touch_sync_game();
}

void sdl3_touch_set_replay_active(legacy_u8 active)
{
	replay_interaction = active;
	sdl3_touch_sync_game();
}

void sdl3_touch_set_rewind_active(legacy_u8 active)
{
	if (rewind_active != active) {
		sdl3_input_reset_shifts();
	}
	rewind_active = active;
	sdl3_touch_sync_game();
}

void sdl3_touch_sync_game(void)
{
#ifdef RESTUNTS_FULL
	sdl3_touch_configure(
		sdl3_input_driving_mode() == SDL3_DRIVING_KEYBOARD,
		game_active && replay_interaction &&
			(rewind_active || (game_replay_mode == REPLAY_MODE_LIVE && is_in_replay == 0)),
		gameconfig.game_playertransmission == TRANSMISSION_MANUAL,
		game_active && !rewind_active && game_replay_mode == REPLAY_MODE_PLAYBACK &&
			replay_interaction);
#endif
}

legacy_u8 sdl3_touch_visible(enum TOUCH_CONTROL control)
{
	if (!enabled || !layout_visible || intro_active || !keyboard_selected ||
		control >= TOUCH_CONTROL_COUNT) {
		return false;
	}
	if (control == TOUCH_SHIFT_UP || control == TOUCH_SHIFT_DOWN) {
		return racing && manual_gears && !rewind_active;
	}
	return control != TOUCH_REWIND || racing;
}

struct TOUCH_CIRCLE sdl3_touch_circle(enum TOUCH_CONTROL control, const SDL_Rect *area)
{
	struct TOUCH_CIRCLE circle;
	circle.radius = area->h * TOUCH_RADIUS_HEIGHT;
	circle.x = area->x + TOUCH_EDGE_MARGIN + circle.radius;
	circle.y = area->y + area->h * TOUCH_STEERING_HEIGHT;
	switch (control) {
		case TOUCH_ESCAPE:
			circle.y = area->y + TOUCH_EDGE_MARGIN + circle.radius;
			break;
		case TOUCH_RIGHT:
			circle.x = area->x + TOUCH_EDGE_MARGIN + circle.radius * TOUCH_RIGHT_RADIUS_OFFSET;
			break;
		case TOUCH_SHIFT_UP:
		case TOUCH_SHIFT_DOWN:
			circle.x = area->x + TOUCH_EDGE_MARGIN + circle.radius * TOUCH_SHIFT_RADIUS_OFFSET;
			circle.y += circle.radius * TOUCH_VERTICAL_RADIUS_OFFSET *
						(control == TOUCH_SHIFT_UP ? -1.0f : 1.0f);
			break;
		case TOUCH_CAMERA:
		case TOUCH_FOLLOW:
			circle.x = area->x + area->w - TOUCH_EDGE_MARGIN - circle.radius;
			circle.y = area->y + TOUCH_EDGE_MARGIN + circle.radius;
			if (control == TOUCH_FOLLOW) {
				circle.y += circle.radius * TOUCH_VERTICAL_RADIUS_OFFSET;
			}
			break;
		case TOUCH_REWIND:
			circle.y = area->y + area->h - TOUCH_EDGE_MARGIN - circle.radius;
			break;
		case TOUCH_ACCELERATE:
		case TOUCH_BRAKE:
			circle.x = area->x + area->w - TOUCH_EDGE_MARGIN - circle.radius;
			circle.y = area->y + area->h * (control == TOUCH_ACCELERATE ? TOUCH_ACCELERATE_HEIGHT
																		: TOUCH_BRAKE_HEIGHT);
			break;
		default:
			break;
	}
	return circle;
}

static legacy_u8 circle_contains(struct TOUCH_CIRCLE circle, legacy_f32 x, legacy_f32 y)
{
	legacy_f32 dx = x - circle.x;
	legacy_f32 dy = y - circle.y;
	return dx * dx + dy * dy <= circle.radius * circle.radius;
}

static legacy_u8 control_pressed(enum TOUCH_CONTROL control)
{
	for (legacy_u32 i = 0; i < TOUCH_FINGER_COUNT; i++) {
		if (fingers[i].active && fingers[i].pressed && fingers[i].mode == TOUCH_POINTER_CONTROL &&
			fingers[i].control == control) {
			return true;
		}
	}
	return false;
}

legacy_u8 sdl3_touch_key_state(SDL_Scancode scancode)
{
	if (!enabled) {
		return false;
	}
	for (enum TOUCH_CONTROL control = TOUCH_ESCAPE; control < TOUCH_CONTROL_COUNT; control++) {
		if (control != TOUCH_SHIFT_UP && control != TOUCH_SHIFT_DOWN &&
			sdl3_touch_visible(control) && control_scancodes[control] == scancode &&
			control_pressed(control)) {
			return true;
		}
	}
	return false;
}

legacy_u16 sdl3_touch_take_shift_flags(void)
{
	if (shift_count == 0) {
		return 0;
	}
	legacy_u16 result = shift_queue[shift_read];
	shift_read = (shift_read + 1U) % INPUT_SHIFT_QUEUE_CAPACITY;
	shift_count--;
	return result;
}

legacy_s16 sdl3_touch_take_seek(void)
{
	legacy_s16 result = seek_direction;
	seek_direction = 0;
	return result;
}

static void control_press(enum TOUCH_CONTROL control)
{
	if (control == TOUCH_SHIFT_UP || control == TOUCH_SHIFT_DOWN) {
		sdl3_input_queue_shift(control == TOUCH_SHIFT_UP ? INPUT_SHIFT_UP_FLAG
														 : INPUT_SHIFT_DOWN_FLAG);
	} else {
		sdl3_input_queue_key(control_keys[control]);
		repeat_at[control] = SDL_GetTicks() + TOUCH_REPEAT_DELAY_MS;
	}
}

void sdl3_touch_update(void)
{
	legacy_u64 now = SDL_GetTicks();
	for (enum TOUCH_CONTROL control = TOUCH_LEFT; control < TOUCH_CONTROL_COUNT; control++) {
		if ((control == TOUCH_LEFT || control == TOUCH_RIGHT || control == TOUCH_ACCELERATE ||
			 control == TOUCH_BRAKE) &&
			control_pressed(control) && !racing && now >= repeat_at[control]) {
			sdl3_input_queue_key(control_keys[control]);
			repeat_at[control] = now + TOUCH_REPEAT_INTERVAL_MS;
		}
	}
}

void sdl3_touch_event(const SDL_TouchFingerEvent *event, const SDL_Rect *area)
{
	if (!enabled) {
		return;
	}
	struct TOUCH_FINGER *finger = NULL;
	for (legacy_u32 i = 0; i < TOUCH_FINGER_COUNT; i++) {
		if (fingers[i].active && fingers[i].id == event->fingerID &&
			fingers[i].device == event->touchID) {
			finger = &fingers[i];
			break;
		}
	}
	SDL_Window *window = sdl3_video_window();
	legacy_int width = area->x + area->w;
	legacy_int height = area->y + area->h;
	if (window != NULL) {
		SDL_GetWindowSize(window, &width, &height);
	}
	legacy_f32 x = event->x * width;
	legacy_f32 y = event->y * height;
	if (event->type == SDL_EVENT_FINGER_DOWN) {
		if (finger != NULL) {
			return;
		}
		for (legacy_u32 i = 0; i < TOUCH_FINGER_COUNT; i++) {
			if (!fingers[i].active) {
				finger = &fingers[i];
				break;
			}
		}
		if (finger == NULL) {
			return;
		}
		memset(finger, 0, sizeof(*finger));
		finger->active = true;
		finger->id = event->fingerID;
		finger->device = event->touchID;
		finger->start_x = finger->x = x;
		finger->start_y = finger->y = y;
		finger->control = TOUCH_CONTROL_COUNT;
		for (enum TOUCH_CONTROL control = TOUCH_ESCAPE; control < TOUCH_CONTROL_COUNT; control++) {
			if (sdl3_touch_visible(control) &&
				circle_contains(sdl3_touch_circle(control, area), x, y)) {
				finger->mode = TOUCH_POINTER_CONTROL;
				finger->control = control;
				finger->pressed = true;
				control_press(control);
				return;
			}
		}
		/* The legacy UI has one mouse pointer. Extra fingers must not move or release it. */
		for (legacy_u32 i = 0; i < TOUCH_FINGER_COUNT; i++) {
			if (&fingers[i] != finger && fingers[i].active &&
				(fingers[i].mode == TOUCH_POINTER_MOUSE ||
				 fingers[i].mode == TOUCH_POINTER_GESTURE)) {
				finger->mode = TOUCH_POINTER_IGNORED;
				return;
			}
		}
		legacy_f32 game_x, game_y;
		sdl3_video_window_to_game(x, y, &game_x, &game_y);
		finger->mode = replay_active && game_y < TOUCH_REPLAY_TOOLBAR_Y ? TOUCH_POINTER_GESTURE
																		: TOUCH_POINTER_MOUSE;
		if (finger->mode == TOUCH_POINTER_MOUSE) {
			sdl3_input_touch_mouse(x, y, true);
		}
		return;
	}
	if (finger == NULL) {
		return;
	}
	finger->x = x;
	finger->y = y;
	if (event->type == SDL_EVENT_FINGER_MOTION) {
		if (finger->mode == TOUCH_POINTER_CONTROL) {
			legacy_u8 pressed = circle_contains(sdl3_touch_circle(finger->control, area), x, y);
			if (pressed && !finger->pressed && finger->control != TOUCH_SHIFT_UP &&
				finger->control != TOUCH_SHIFT_DOWN && finger->control != TOUCH_CAMERA &&
				finger->control != TOUCH_FOLLOW) {
				control_press(finger->control);
			}
			finger->pressed = pressed;
		} else if (finger->mode == TOUCH_POINTER_MOUSE) {
			sdl3_input_touch_mouse(x, y, true);
		}
		return;
	}
	if (event->type == SDL_EVENT_FINGER_UP || event->type == SDL_EVENT_FINGER_CANCELED) {
		if (finger->mode == TOUCH_POINTER_GESTURE && event->type == SDL_EVENT_FINGER_UP) {
			legacy_f32 dx = x - finger->start_x;
			legacy_f32 dy = y - finger->start_y;
			if (SDL_fabsf(dx) >= area->h * TOUCH_SWIPE_HEIGHT &&
				SDL_fabsf(dx) >= SDL_fabsf(dy) * TOUCH_SWIPE_HORIZONTAL_RATIO) {
				seek_direction = dx < 0 ? -1 : 1;
			} else {
				sdl3_input_touch_mouse(x, y, true);
				sdl3_input_touch_mouse(x, y, false);
			}
		} else if (finger->mode == TOUCH_POINTER_MOUSE) {
			sdl3_input_touch_mouse(x, y, false);
		}
		finger->active = false;
	}
}

void sdl3_touch_draw(SDL_Renderer *renderer, SDL_Window *window)
{
	if (!enabled || !layout_visible || intro_active || !keyboard_selected) {
		return;
	}
	SDL_Rect area;
	if (!SDL_GetWindowSafeArea(window, &area)) {
		return;
	}
	legacy_int logical_w, logical_h;
	SDL_RendererLogicalPresentation presentation;
	SDL_GetRenderLogicalPresentation(renderer, &logical_w, &logical_h, &presentation);
	legacy_f32 scale_x, scale_y;
	SDL_GetRenderScale(renderer, &scale_x, &scale_y);
	SDL_BlendMode blend;
	SDL_GetRenderDrawBlendMode(renderer, &blend);
	SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
	legacy_int output_w, output_h, window_w, window_h;
	SDL_GetRenderOutputSize(renderer, &output_w, &output_h);
	SDL_GetWindowSize(window, &window_w, &window_h);
	SDL_SetRenderScale(renderer, (legacy_f32)output_w / window_w, (legacy_f32)output_h / window_h);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	for (enum TOUCH_CONTROL control = TOUCH_ESCAPE; control < TOUCH_CONTROL_COUNT; control++) {
		if (!sdl3_touch_visible(control)) {
			continue;
		}
		struct TOUCH_CIRCLE circle = sdl3_touch_circle(control, &area);
		SDL_Vertex vertices[TOUCH_CIRCLE_SEGMENTS + 1U];
		legacy_int indices[TOUCH_CIRCLE_SEGMENTS * 3U];
		SDL_FPoint outline[TOUCH_CIRCLE_SEGMENTS + 1U];
		legacy_f32 alpha = (control_pressed(control) ? TOUCH_PRESSED_ALPHA : TOUCH_FILL_ALPHA) /
						   (legacy_f32)SDL_ALPHA_OPAQUE;
		vertices[0] = (SDL_Vertex){{circle.x, circle.y}, {1, 1, 1, alpha}, {0, 0}};
		for (legacy_u32 i = 0; i < TOUCH_CIRCLE_SEGMENTS; i++) {
			legacy_f32 angle = TOUCH_FULL_TURN * i / TOUCH_CIRCLE_SEGMENTS;
			outline[i] = (SDL_FPoint){circle.x + SDL_cosf(angle) * circle.radius,
									  circle.y + SDL_sinf(angle) * circle.radius};
			vertices[i + 1U] = (SDL_Vertex){outline[i], {1, 1, 1, alpha}, {0, 0}};
			indices[i * 3U] = 0;
			indices[i * 3U + 1U] = (legacy_int)i + 1;
			indices[i * 3U + 2U] = (legacy_int)((i + 1U) % TOUCH_CIRCLE_SEGMENTS) + 1;
		}
		outline[TOUCH_CIRCLE_SEGMENTS] = outline[0];
		SDL_RenderGeometry(renderer, NULL, vertices, SDL_arraysize(vertices), indices,
						   SDL_arraysize(indices));
		SDL_SetRenderDrawColor(renderer, SDL_ALPHA_OPAQUE, SDL_ALPHA_OPAQUE, SDL_ALPHA_OPAQUE,
							   TOUCH_OUTLINE_ALPHA);
		SDL_RenderLines(renderer, outline, SDL_arraysize(outline));
		const legacy_char *label = control == TOUCH_ESCAPE		 ? "ESC"
								   : control == TOUCH_REWIND	 ? "Q"
								   : control == TOUCH_CAMERA	 ? "C"
								   : control == TOUCH_FOLLOW	 ? "T"
								   : control == TOUCH_SHIFT_UP	 ? "SHIFT+"
								   : control == TOUCH_SHIFT_DOWN ? "SHIFT-"
																 : NULL;
		if (label != NULL) {
			legacy_f32 label_scale =
				SDL_strlen(label) == 1U
					? circle.radius * TOUCH_SINGLE_LETTER_LABEL_HEIGHT /
						  SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE
					: circle.radius * TOUCH_LABEL_WIDTH /
						  (SDL_strlen(label) * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE);
			SDL_SetRenderScale(renderer, (legacy_f32)output_w / window_w * label_scale,
							   (legacy_f32)output_h / window_h * label_scale);
			SDL_RenderDebugText(
				renderer,
				(circle.x -
				 SDL_strlen(label) * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * label_scale / 2.0f) /
					label_scale,
				((control == TOUCH_SHIFT_UP || control == TOUCH_SHIFT_DOWN
					  ? circle.y + circle.radius * TOUCH_LABEL_OFFSET
					  : circle.y - SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * label_scale / 2.0f)) /
					label_scale,
				label);
			SDL_SetRenderScale(renderer, (legacy_f32)output_w / window_w,
							   (legacy_f32)output_h / window_h);
		}
		if (label == NULL || control == TOUCH_SHIFT_UP || control == TOUCH_SHIFT_DOWN) {
			legacy_f32 length = circle.radius * TOUCH_ARROW_SCALE;
			legacy_f32 dx = control == TOUCH_LEFT ? -length : control == TOUCH_RIGHT ? length : 0;
			legacy_f32 dy = control == TOUCH_SHIFT_UP || control == TOUCH_ACCELERATE ? -length
							: control == TOUCH_SHIFT_DOWN || control == TOUCH_BRAKE	 ? length
																					 : 0;
			legacy_f32 cy =
				label != NULL ? circle.y - circle.radius * TOUCH_LABEL_OFFSET : circle.y;
			SDL_RenderLine(renderer, circle.x - dx, cy - dy, circle.x + dx, cy + dy);
			SDL_RenderLine(renderer, circle.x + dx, cy + dy, circle.x + dy / 2.0f, cy - dx / 2.0f);
			SDL_RenderLine(renderer, circle.x + dx, cy + dy, circle.x - dy / 2.0f, cy + dx / 2.0f);
		}
	}
	SDL_SetRenderLogicalPresentation(renderer, logical_w, logical_h, presentation);
	SDL_SetRenderScale(renderer, scale_x, scale_y);
	SDL_SetRenderDrawBlendMode(renderer, blend);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
}
