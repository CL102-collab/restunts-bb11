#include "soft_keyboard.h"
#include "touch.h"
#include "../../c/keyboard.h"
#include <string.h>

#define SOFT_KEYBOARD_TEXT_CAPACITY 65U
#define SOFT_KEYBOARD_LABEL_CAPACITY 16U
#define SOFT_KEYBOARD_ROWS 6U
#define SOFT_KEYBOARD_COLUMNS 10U
#define SOFT_KEYBOARD_MARGIN 8.0f
#define SOFT_KEYBOARD_PADDING 8.0f
#define SOFT_KEYBOARD_KEY_GAP 4.0f
#define SOFT_KEYBOARD_SHIFT 65535U

struct SOFT_KEY {
	legacy_u16 value;
	const legacy_char *label;
	legacy_u8 row;
	legacy_u8 column;
	legacy_u8 span;
};

#define SOFT_CHARACTER(value, row, column) {value, NULL, row, column, 1}
static const struct SOFT_KEY soft_keys[] = {SOFT_CHARACTER('0', 0, 0),
											SOFT_CHARACTER('1', 0, 1),
											SOFT_CHARACTER('2', 0, 2),
											SOFT_CHARACTER('3', 0, 3),
											SOFT_CHARACTER('4', 0, 4),
											SOFT_CHARACTER('5', 0, 5),
											SOFT_CHARACTER('6', 0, 6),
											SOFT_CHARACTER('7', 0, 7),
											SOFT_CHARACTER('8', 0, 8),
											SOFT_CHARACTER('9', 0, 9),
											SOFT_CHARACTER('q', 1, 0),
											SOFT_CHARACTER('w', 1, 1),
											SOFT_CHARACTER('e', 1, 2),
											SOFT_CHARACTER('r', 1, 3),
											SOFT_CHARACTER('t', 1, 4),
											SOFT_CHARACTER('y', 1, 5),
											SOFT_CHARACTER('u', 1, 6),
											SOFT_CHARACTER('i', 1, 7),
											SOFT_CHARACTER('o', 1, 8),
											SOFT_CHARACTER('p', 1, 9),
											SOFT_CHARACTER('a', 2, 0),
											SOFT_CHARACTER('s', 2, 1),
											SOFT_CHARACTER('d', 2, 2),
											SOFT_CHARACTER('f', 2, 3),
											SOFT_CHARACTER('g', 2, 4),
											SOFT_CHARACTER('h', 2, 5),
											SOFT_CHARACTER('j', 2, 6),
											SOFT_CHARACTER('k', 2, 7),
											SOFT_CHARACTER('l', 2, 8),
											SOFT_CHARACTER('_', 2, 9),
											{SOFT_KEYBOARD_SHIFT, "Shift", 3, 0, 2},
											SOFT_CHARACTER('z', 3, 2),
											SOFT_CHARACTER('x', 3, 3),
											SOFT_CHARACTER('c', 3, 4),
											SOFT_CHARACTER('v', 3, 5),
											SOFT_CHARACTER('b', 3, 6),
											SOFT_CHARACTER('n', 3, 7),
											SOFT_CHARACTER('m', 3, 8),
											SOFT_CHARACTER('-', 3, 9),
											SOFT_CHARACTER('.', 4, 0),
											SOFT_CHARACTER('/', 4, 1),
											{KEY_LEFT, "<", 4, 2, 1},
											{KEY_RIGHT, ">", 4, 3, 1},
											{KEY_SPACE, "Space", 4, 4, 3},
											{KEY_BACKSPACE, "Backspace", 4, 7, 3},
											{KEY_ESCAPE, "Cancel", 5, 0, 4},
											{KEY_HOME, "|<", 5, 4, 1},
											{KEY_END, ">|", 5, 5, 1},
											{KEY_ENTER, "Done", 5, 6, 4}};
#undef SOFT_CHARACTER

static legacy_u8 enabled;
static legacy_u8 active;
static legacy_u8 shifted;
static legacy_u8 dirty;
static legacy_char text_value[SOFT_KEYBOARD_TEXT_CAPACITY];
static legacy_char purpose[SOFT_KEYBOARD_LABEL_CAPACITY];
static legacy_u16 caret;
static legacy_u8 pointer_active;
static legacy_u8 pointer_touch;
static SDL_TouchID pointer_device;
static SDL_FingerID pointer_finger;
static legacy_s16 pointer_key = -1;

static void reset_pointer(void)
{
	pointer_active = false;
	pointer_key = -1;
	dirty = true;
}

void sdl3_soft_keyboard_enable(legacy_u8 value)
{
	enabled = value != 0;
	if (!enabled) {
		sdl3_soft_keyboard_end();
	}
}

legacy_u8 sdl3_soft_keyboard_enabled(void)
{
	return enabled;
}

legacy_u8 sdl3_soft_keyboard_active(void)
{
	return active;
}

legacy_u8 sdl3_soft_keyboard_take_redraw(void)
{
	legacy_u8 result = dirty;
	dirty = false;
	return result;
}

void sdl3_soft_keyboard_update(const legacy_s8 *text, legacy_u16 cursor)
{
	legacy_char next[SOFT_KEYBOARD_TEXT_CAPACITY];
	legacy_u32 length = 0;
	while (length + 1U < sizeof(next) && text[length] != 0) {
		next[length] = (legacy_char)text[length];
		length++;
	}
	next[length] = '\0';
	legacy_u16 next_caret = (legacy_u16)SDL_min(cursor, length);
	if (strcmp(text_value, next) != 0 || caret != next_caret) {
		memcpy(text_value, next, length + 1U);
		caret = next_caret;
		dirty = true;
	}
}

void sdl3_soft_keyboard_begin(const legacy_s8 *text, legacy_u16 cursor, const legacy_char *label)
{
	if (!enabled) {
		return;
	}
	active = true;
	shifted = false;
	SDL_strlcpy(purpose, label != NULL ? label : "Text", sizeof(purpose));
	sdl3_soft_keyboard_update(text, cursor);
	reset_pointer();
}

void sdl3_soft_keyboard_end(void)
{
	if (active) {
		active = false;
		reset_pointer();
	}
}

static void keyboard_layout(SDL_Window *window, SDL_FRect *game, SDL_FRect *keyboard)
{
	legacy_int window_width, window_height;
	SDL_GetWindowSize(window, &window_width, &window_height);
	SDL_Rect safe;
	if (!SDL_GetWindowSafeArea(window, &safe)) {
		safe = (SDL_Rect){0, 0, window_width, window_height};
	}
	legacy_f32 margin = SDL_min(SOFT_KEYBOARD_MARGIN, SDL_min(safe.w, safe.h) * 0.02f);
	SDL_FRect area = {safe.x + margin, safe.y + margin, SDL_max(1.0f, safe.w - margin * 2),
					  SDL_max(1.0f, safe.h - margin * 2)};
	SDL_FRect game_slot = area;
	if (area.w >= area.h * 1.7f) {
		game_slot.w = (area.w - SOFT_KEYBOARD_MARGIN) * 0.44f;
		*keyboard = (SDL_FRect){game_slot.x + game_slot.w + SOFT_KEYBOARD_MARGIN, area.y,
								area.w - game_slot.w - SOFT_KEYBOARD_MARGIN, area.h};
	} else {
		game_slot.h = SDL_min(area.h * 0.4f, area.w * 0.75f);
		*keyboard = (SDL_FRect){area.x, game_slot.y + game_slot.h + SOFT_KEYBOARD_MARGIN, area.w,
								area.h - game_slot.h - SOFT_KEYBOARD_MARGIN};
	}
	game->w = SDL_min(game_slot.w, game_slot.h * (4.0f / 3.0f));
	game->h = game->w * 0.75f;
	game->x = game_slot.x + (game_slot.w - game->w) * 0.5f;
	game->y = game_slot.y + (game_slot.h - game->h) * 0.5f;
}

SDL_FRect sdl3_soft_keyboard_game_area(SDL_Window *window)
{
	SDL_FRect game, keyboard;
	keyboard_layout(window, &game, &keyboard);
	return game;
}

static legacy_f32 header_height(const SDL_FRect *keyboard)
{
	return SDL_clamp(keyboard->h * 0.12f, 24.0f, 44.0f);
}

static SDL_FRect key_rect(const struct SOFT_KEY *key, const SDL_FRect *keyboard)
{
	legacy_f32 column = (keyboard->w - SOFT_KEYBOARD_PADDING * 2) / SOFT_KEYBOARD_COLUMNS;
	legacy_f32 row =
		(keyboard->h - header_height(keyboard) - SOFT_KEYBOARD_PADDING * 2) / SOFT_KEYBOARD_ROWS;
	return (SDL_FRect){keyboard->x + SOFT_KEYBOARD_PADDING + key->column * column +
						   SOFT_KEYBOARD_KEY_GAP * 0.5f,
					   keyboard->y + header_height(keyboard) + SOFT_KEYBOARD_PADDING +
						   key->row * row + SOFT_KEYBOARD_KEY_GAP * 0.5f,
					   column * key->span - SOFT_KEYBOARD_KEY_GAP, row - SOFT_KEYBOARD_KEY_GAP};
}

static legacy_s16 hit_key(SDL_Window *window, legacy_f32 x, legacy_f32 y)
{
	SDL_FRect game, keyboard;
	keyboard_layout(window, &game, &keyboard);
	for (legacy_u32 index = 0; index < SDL_arraysize(soft_keys); index++) {
		SDL_FRect key = key_rect(&soft_keys[index], &keyboard);
		if (x >= key.x && x < key.x + key.w && y >= key.y && y < key.y + key.h) {
			return (legacy_s16)index;
		}
	}
	return -1;
}

static void release_pointer(SDL_Window *window, legacy_f32 x, legacy_f32 y, legacy_u8 cancelled)
{
	legacy_s16 index = pointer_key;
	legacy_u8 apply = !cancelled && index >= 0 && hit_key(window, x, y) == index;
	reset_pointer();
	if (!apply) {
		return;
	}
	legacy_u16 key = soft_keys[index].value;
	if (key == SOFT_KEYBOARD_SHIFT) {
		shifted = !shifted;
	} else {
		if (shifted && key >= 'a' && key <= 'z') {
			key = (legacy_u16)(key - 'a' + 'A');
		}
		sdl3_input_queue_key(key);
	}
}

legacy_u8 sdl3_soft_keyboard_event(const SDL_Event *event, SDL_Window *window)
{
	if (!active) {
		return false;
	}
	if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST ||
		event->type == SDL_EVENT_WILL_ENTER_BACKGROUND) {
		reset_pointer();
		return false;
	}
	if (event->type == SDL_EVENT_FINGER_DOWN || event->type == SDL_EVENT_FINGER_UP ||
		event->type == SDL_EVENT_FINGER_MOTION || event->type == SDL_EVENT_FINGER_CANCELED) {
		if (event->tfinger.touchID == SDL_MOUSE_TOUCHID) {
			return true;
		}
		legacy_int width, height;
		SDL_GetWindowSize(window, &width, &height);
		legacy_f32 x = event->tfinger.x * width;
		legacy_f32 y = event->tfinger.y * height;
		if (event->type == SDL_EVENT_FINGER_DOWN && !pointer_active) {
			pointer_active = true;
			pointer_touch = true;
			pointer_device = event->tfinger.touchID;
			pointer_finger = event->tfinger.fingerID;
			pointer_key = hit_key(window, x, y);
			dirty = true;
		} else if (pointer_active && pointer_touch && pointer_device == event->tfinger.touchID &&
				   pointer_finger == event->tfinger.fingerID) {
			if (event->type == SDL_EVENT_FINGER_UP || event->type == SDL_EVENT_FINGER_CANCELED) {
				release_pointer(window, x, y, event->type == SDL_EVENT_FINGER_CANCELED);
			} else if (pointer_key >= 0 && hit_key(window, x, y) != pointer_key) {
				pointer_key = -1;
				dirty = true;
			}
		}
		return true;
	}
	if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
		if (event->button.which != SDL_TOUCH_MOUSEID && event->button.button == SDL_BUTTON_LEFT) {
			if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && !pointer_active) {
				pointer_active = true;
				pointer_touch = false;
				pointer_key = hit_key(window, event->button.x, event->button.y);
				dirty = true;
			} else if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && pointer_active &&
					   !pointer_touch) {
				release_pointer(window, event->button.x, event->button.y, false);
			}
		}
		return true;
	}
	if (event->type == SDL_EVENT_MOUSE_MOTION) {
		if (event->motion.which != SDL_TOUCH_MOUSEID && pointer_active && !pointer_touch &&
			pointer_key >= 0 && hit_key(window, event->motion.x, event->motion.y) != pointer_key) {
			pointer_key = -1;
			dirty = true;
		}
		return true;
	}
	return event->type == SDL_EVENT_MOUSE_WHEEL;
}

static void draw_label(SDL_Renderer *renderer, const SDL_FRect *rect, const legacy_char *label,
					   legacy_f32 base_scale_x, legacy_f32 base_scale_y)
{
	legacy_f32 width = (legacy_f32)SDL_strlen(label) * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
	legacy_f32 scale = SDL_min(
		2.0f, SDL_min((rect->w - 6) / width, (rect->h - 6) / SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE));
	scale = SDL_max(0.5f, scale);
	SDL_SetRenderScale(renderer, base_scale_x * scale, base_scale_y * scale);
	SDL_RenderDebugText(
		renderer, (rect->x + (rect->w - width * scale) * 0.5f) / scale,
		(rect->y + (rect->h - SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * scale) * 0.5f) / scale, label);
	SDL_SetRenderScale(renderer, base_scale_x, base_scale_y);
}

void sdl3_soft_keyboard_draw(SDL_Renderer *renderer, SDL_Window *window)
{
	if (!active) {
		return;
	}
	SDL_FRect game, keyboard;
	keyboard_layout(window, &game, &keyboard);
	legacy_f32 scale_x, scale_y;
	SDL_BlendMode blend;
	legacy_u8 red, green, blue, alpha;
	SDL_GetRenderScale(renderer, &scale_x, &scale_y);
	SDL_GetRenderDrawBlendMode(renderer, &blend);
	SDL_GetRenderDrawColor(renderer, &red, &green, &blue, &alpha);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
	SDL_SetRenderDrawColor(renderer, 24, 30, 40, SDL_ALPHA_OPAQUE);
	SDL_RenderFillRect(renderer, &keyboard);
	legacy_f32 header = header_height(&keyboard);
	legacy_f32 text_scale = SDL_min(2.0f, (header - 8) / SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE);
	legacy_f32 character_width = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * text_scale;
	legacy_u32 label_length = (legacy_u32)SDL_strlen(purpose) + 2U;
	legacy_u32 columns =
		(legacy_u32)SDL_max(1.0f, (keyboard.w - SOFT_KEYBOARD_PADDING * 2) / character_width);
	legacy_u32 available = columns > label_length ? columns - label_length : 1U;
	legacy_u32 start = caret >= available ? caret - available + 1U : 0U;
	legacy_char line[SOFT_KEYBOARD_TEXT_CAPACITY + SOFT_KEYBOARD_LABEL_CAPACITY + 2U];
	SDL_snprintf(line, sizeof(line), "%s: %.*s", purpose, (legacy_int)available,
				 text_value + start);
	SDL_SetRenderDrawColor(renderer, 240, 244, 250, SDL_ALPHA_OPAQUE);
	SDL_SetRenderScale(renderer, scale_x * text_scale, scale_y * text_scale);
	SDL_RenderDebugText(
		renderer, (keyboard.x + SOFT_KEYBOARD_PADDING) / text_scale,
		(keyboard.y + (header - SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * text_scale) * 0.5f) /
			text_scale,
		line);
	SDL_SetRenderScale(renderer, scale_x, scale_y);
	legacy_f32 caret_x =
		keyboard.x + SOFT_KEYBOARD_PADDING + (label_length + caret - start) * character_width;
	SDL_RenderLine(renderer, caret_x, keyboard.y + 4, caret_x, keyboard.y + header - 4);
	for (legacy_u32 index = 0; index < SDL_arraysize(soft_keys); index++) {
		const struct SOFT_KEY *key = &soft_keys[index];
		SDL_FRect rect = key_rect(key, &keyboard);
		legacy_u8 selected = pointer_active && pointer_key == (legacy_s16)index;
		if (key->value == SOFT_KEYBOARD_SHIFT && shifted) {
			selected = true;
		}
		SDL_SetRenderDrawColor(renderer, selected ? 57 : 42, selected ? 105 : 53,
							   selected ? 156 : 70, SDL_ALPHA_OPAQUE);
		SDL_RenderFillRect(renderer, &rect);
		SDL_SetRenderDrawColor(renderer, 118, 142, 167, SDL_ALPHA_OPAQUE);
		SDL_RenderRect(renderer, &rect);
		legacy_char character[2] = {(legacy_char)key->value, '\0'};
		if (shifted && key->value >= 'a' && key->value <= 'z') {
			character[0] = (legacy_char)(key->value - 'a' + 'A');
		}
		SDL_SetRenderDrawColor(renderer, 240, 244, 250, SDL_ALPHA_OPAQUE);
		draw_label(renderer, &rect, key->label != NULL ? key->label : character, scale_x, scale_y);
	}
	SDL_SetRenderScale(renderer, scale_x, scale_y);
	SDL_SetRenderDrawBlendMode(renderer, blend);
	SDL_SetRenderDrawColor(renderer, red, green, blue, alpha);
}
