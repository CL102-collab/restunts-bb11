#include "sdl3.h"
#include "touch.h"
#include "soft_keyboard.h"
#include "../../c/platform.h"
#include "../../c/fatal.h"
#include "../../c/hires.h"
#include "../../c/frame_adaptive.h"
#include "../../c/frame_internal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#define SCREEN_BYTES (SDL3_SCREEN_WIDTH * SDL3_SCREEN_HEIGHT)
#define VGA_MEMORY_SEGMENT 0xA000U
#define PRESENT_INTERVAL_MS 10U
#define PRESENTATION_HEIGHT 240
#define WINDOW_INITIAL_SCALE 3
#define VGA_PALETTE_COLOR_COUNT 256U
#define VGA_PALETTE_CHANNEL_COUNT 3U
#define VGA_PALETTE_CHANNEL_MAX 63U
#define VGA_RETRACE_PERIOD_MS 14U
#define VGA_RETRACE_DURATION_MS 2U
#define VSYNC_EVERY_REFRESH 1
#define PRESENTATION_PAGE_COUNT 2U
#define SCREENSHOT_FIRST_NUMBER 1U
#define SCREENSHOT_LAST_NUMBER 9999U
#define SCREENSHOT_NAME_FORMAT "SHOT%04u.PNG"
#define SCREENSHOT_NAME_CAPACITY sizeof("SHOT0000.PNG")

struct PRESENTATION_PAGE {
	legacy_u32 *pixels;
	legacy_s32 width;
	legacy_s32 height;
};

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static struct PRESENTATION_PAGE presentation_pages[PRESENTATION_PAGE_COUNT];
static legacy_u32 front_page;
static SDL_Surface *frame_surface;
static legacy_s32 texture_width;
static legacy_s32 texture_height;
static legacy_u8 surface_output;
static legacy_u8 high_resolution_output;
static SDL_Rect surface_viewport;
static SDL_Color palette_colors[VGA_PALETTE_COLOR_COUNT];
static legacy_u32 palette_pixels[VGA_PALETTE_COLOR_COUNT];
static legacy_u8 previous_pixels[SCREEN_BYTES];
static legacy_u32 previous_generation;
static legacy_u64 last_present;
static legacy_u8 palette_changed = true;
static legacy_u8 drawing_frame;
static legacy_u8 adaptive_frame;
static legacy_u8 measured_frame;
static legacy_u64 render_frame_started;
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) && !defined(__DJGPP__)
static legacy_u8 startup_fullscreen_enabled = SDL3_STARTUP_FULLSCREEN_OFF;

void sdl3_video_set_startup_fullscreen(legacy_u8 enabled)
{
	startup_fullscreen_enabled =
		enabled != 0 ? SDL3_STARTUP_FULLSCREEN_ON : SDL3_STARTUP_FULLSCREEN_OFF;
}
#endif

#ifdef __EMSCRIPTEN__
EM_ASYNC_JS(legacy_int, save_browser_screenshot, (const legacy_char *path), {
	try {
		await Module['saveScreenshot'](UTF8ToString(path));
		return 1;
	} catch (error) {
		console.error('Cannot save screenshot:', error);
		return 0;
	}
});
#endif

static void video_fail(const legacy_char *operation)
{
	fprintf(stderr, "%s: %s\n", operation, SDL_GetError());
	dos_process_exit(1);
}

#if !defined(__DJGPP__) && !defined(__EMSCRIPTEN__)
static SDL_WindowFlags desktop_window_flags(void)
{
#ifdef __ANDROID__
	return SDL_WINDOW_FULLSCREEN;
#endif
	const legacy_char *setting = getenv("RESTUNTS_BORDERLESS");
	if (setting != NULL && *setting != '\0') {
		if (strcmp(setting, "1") == 0) {
			/* Keep ordinary window status and maximize within the usable desktop.
			 * Fullscreen status can select a different compositor presentation path. */
			return SDL_WINDOW_RESIZABLE | SDL_WINDOW_BORDERLESS | SDL_WINDOW_MAXIMIZED;
		}
		if (strcmp(setting, "0") != 0) {
			SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
						"Invalid RESTUNTS_BORDERLESS: use 0 or 1; using a normal window.");
		}
	}
	return SDL_WINDOW_RESIZABLE;
}

static void configure_vsync(void)
{
	legacy_s32 interval = VSYNC_EVERY_REFRESH;
	const legacy_char *setting = getenv("RESTUNTS_VSYNC");
	if (setting != NULL && *setting != '\0') {
		if (strcmp(setting, "0") == 0) {
			interval = SDL_RENDERER_VSYNC_DISABLED;
		} else if (strcmp(setting, "1") != 0) {
			SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
						"Invalid RESTUNTS_VSYNC: use 0 or 1; enabling VSync.");
		}
	}
	if (!SDL_SetRenderVSync(renderer, interval)) {
		SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "Cannot %s VSync: %s",
					interval == SDL_RENDERER_VSYNC_DISABLED ? "disable" : "enable", SDL_GetError());
	}
}
#endif

#ifdef __DJGPP__
static legacy_s32 mode_viewport_width(const SDL_DisplayMode *mode)
{
	/* The legacy VGA mode has nonsquare pixels; VESA modes use a 4:3 viewport. */
	if (mode->w == SDL3_SCREEN_WIDTH && mode->h == SDL3_SCREEN_HEIGHT) {
		return SDL3_SCREEN_WIDTH;
	}
	return SDL_min(mode->w, mode->h * 4 / 3);
}

static legacy_u8 mode_is_better(const SDL_DisplayMode *candidate, const SDL_DisplayMode *current)
{
	if (current == NULL) {
		return true;
	}
	legacy_s32 candidate_width = mode_viewport_width(candidate);
	legacy_s32 current_width = mode_viewport_width(current);
	legacy_u8 candidate_fits = candidate_width >= HIRES_WIDTH;
	legacy_u8 current_fits = current_width >= HIRES_WIDTH;
	if (candidate_fits != current_fits) {
		return candidate_fits;
	}
	if (candidate_width != current_width) {
		return candidate_fits ? candidate_width < current_width : candidate_width > current_width;
	}
	legacy_s32 candidate_area = candidate->w * candidate->h;
	legacy_s32 current_area = current->w * current->h;
	if (candidate_area != current_area) {
		return candidate_area < current_area;
	}
	return SDL_BITSPERPIXEL(candidate->format) < SDL_BITSPERPIXEL(current->format);
}

static void select_dos_video_mode(legacy_u8 high_resolution)
{
	/* Match the output parameter type required by SDL. */
	legacy_int mode_count;
	SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(SDL_GetPrimaryDisplay(), &mode_count);
	legacy_u8 selected = false;
	legacy_s32 selected_width = 0;
	if (modes != NULL) {
		for (legacy_s32 attempt = 0; attempt < mode_count && !selected; attempt++) {
			legacy_s32 best = -1;
			for (legacy_s32 index = 0; index < mode_count; index++) {
				const SDL_DisplayMode *mode = modes[index];
				if (mode == NULL || (SDL_ISPIXELFORMAT_INDEXED(mode->format) &&
									 mode->format != SDL_PIXELFORMAT_INDEX8)) {
					continue;
				}
				if (!high_resolution &&
					(mode->w != SDL3_SCREEN_WIDTH || mode->h != SDL3_SCREEN_HEIGHT ||
					 mode->format != SDL_PIXELFORMAT_INDEX8)) {
					continue;
				}
				if (best < 0 || mode_is_better(mode, modes[best])) {
					best = index;
				}
			}
			if (best < 0) {
				break;
			}
			SDL_DestroyWindowSurface(window);
			selected = SDL_SetWindowFullscreenMode(window, modes[best]) && SDL_SyncWindow(window);
			selected_width = mode_viewport_width(modes[best]);
			modes[best] = NULL;
		}
		SDL_free(modes);
	}
	if (!selected) {
		video_fail("Select DOS video mode");
	}
	SDL_Surface *surface = SDL_GetWindowSurface(window);
	if (surface == NULL) {
		video_fail("Create DOS framebuffer");
	}
	surface_viewport.x = 0;
	surface_viewport.y = 0;
	surface_viewport.w = surface->w;
	surface_viewport.h = surface->h;
	if (surface->w != SDL3_SCREEN_WIDTH || surface->h != SDL3_SCREEN_HEIGHT) {
		if (surface->w * 3 > surface->h * 4) {
			surface_viewport.w = surface->h * 4 / 3;
		} else {
			surface_viewport.h = surface->w * 3 / 4;
		}
		surface_viewport.x = (surface->w - surface_viewport.w) / 2;
		surface_viewport.y = (surface->h - surface_viewport.h) / 2;
	}
	if (high_resolution && selected_width < HIRES_WIDTH) {
		SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
					"No VESA mode can display the full 1280x800 render at 4:3; scaling to %dx%d",
					surface_viewport.w, surface_viewport.h);
	}
	high_resolution_output = high_resolution;
	palette_changed = true;
}
#endif

SDL_Window *sdl3_video_window(void)
{
	return window;
}

void sdl3_video_toggle_fullscreen(void)
{
	/* DOS already uses native fullscreen modes. SDL saves and restores the
	 * desktop window's size and position; the logical 4:3 viewport persists. */
	if (window == NULL || surface_output) {
		return;
	}
	legacy_u8 fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
	if (!SDL_SetWindowFullscreen(window, !fullscreen)) {
		SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "Cannot change fullscreen mode: %s", SDL_GetError());
	}
}

void sdl3_video_window_to_game(legacy_f32 window_x, legacy_f32 window_y, legacy_f32 *x,
							   legacy_f32 *y)
{
	*x = window_x;
	*y = window_y;
	if (sdl3_soft_keyboard_active()) {
		SDL_FRect area = sdl3_soft_keyboard_game_area(window);
		*x = (window_x - area.x) * SDL3_SCREEN_WIDTH / area.w;
		*y = (window_y - area.y) * SDL3_SCREEN_HEIGHT / area.h;
		return;
	}
	if (renderer != NULL) {
		SDL_RenderCoordinatesFromWindow(renderer, window_x, window_y, x, y);
		*y *= (legacy_f32)SDL3_SCREEN_HEIGHT / PRESENTATION_HEIGHT;
	} else if (surface_output) {
		*x = (window_x - surface_viewport.x) * SDL3_SCREEN_WIDTH / surface_viewport.w;
		*y = (window_y - surface_viewport.y) * SDL3_SCREEN_HEIGHT / surface_viewport.h;
	}
}

void sdl3_video_game_to_window(legacy_f32 x, legacy_f32 y, legacy_f32 *window_x,
							   legacy_f32 *window_y)
{
	*window_x = x;
	*window_y = y;
	if (sdl3_soft_keyboard_active()) {
		SDL_FRect area = sdl3_soft_keyboard_game_area(window);
		*window_x = area.x + x * area.w / SDL3_SCREEN_WIDTH;
		*window_y = area.y + y * area.h / SDL3_SCREEN_HEIGHT;
		return;
	}
	if (renderer != NULL) {
		SDL_RenderCoordinatesToWindow(renderer, x,
									  y * ((legacy_f32)PRESENTATION_HEIGHT / SDL3_SCREEN_HEIGHT),
									  window_x, window_y);
	} else if (surface_output) {
		*window_x = surface_viewport.x + x * surface_viewport.w / SDL3_SCREEN_WIDTH;
		*window_y = surface_viewport.y + y * surface_viewport.h / SDL3_SCREEN_HEIGHT;
	}
}

static void video_record_render_work(void)
{
	if (measured_frame != 0) {
		legacy_u64 elapsed = SDL_GetTicksNS() - render_frame_started;
		frame_render_timing_record(elapsed);
		if (adaptive_frame != 0) {
			frame_adaptive_record(&frame_adaptive, elapsed);
		}
		measured_frame = 0;
		adaptive_frame = 0;
	}
}

static void present_surface(const legacy_u8 *pixels, const legacy_u32 *argb, legacy_s32 width,
							legacy_s32 height)
{
	SDL_Surface *surface = SDL_GetWindowSurface(window);
	if (surface == NULL) {
		video_fail("Get video surface");
	}
	const void *frame_pixels = argb != NULL ? (const void *)argb : pixels;
	SDL_PixelFormat format = argb != NULL ? SDL_PIXELFORMAT_ARGB8888 : SDL_PIXELFORMAT_INDEX8;
	legacy_u8 new_frame_surface = frame_surface == NULL || frame_surface->pixels != frame_pixels ||
								  frame_surface->format != format || frame_surface->w != width ||
								  frame_surface->h != height;
	if (new_frame_surface) {
		SDL_DestroySurface(frame_surface);
		frame_surface = SDL_CreateSurfaceFrom(width, height, format, (void *)frame_pixels,
											  width * SDL_BYTESPERPIXEL(format));
		if (frame_surface == NULL ||
			(argb == NULL && SDL_CreateSurfacePalette(frame_surface) == NULL)) {
			video_fail("Create presentation surface");
		}
	}
	SDL_Palette *palette = SDL_GetSurfacePalette(frame_surface);
	if (palette != NULL && (palette_changed || new_frame_surface) &&
		!SDL_SetPaletteColors(palette, palette_colors, 0, VGA_PALETTE_COLOR_COUNT)) {
		video_fail("Set video palette");
	}
	if (surface->format == SDL_PIXELFORMAT_INDEX8) {
		if (palette != NULL) {
			if (SDL_GetSurfacePalette(surface) != palette &&
				!SDL_SetSurfacePalette(surface, palette)) {
				video_fail("Set framebuffer palette");
			}
		} else {
			SDL_Palette *output_palette = SDL_GetSurfacePalette(surface);
			if (output_palette == NULL) {
				output_palette = SDL_CreateSurfacePalette(surface);
			}
			if (output_palette == NULL || !SDL_SetPaletteColors(output_palette, palette_colors, 0,
																SDL_arraysize(palette_colors))) {
				video_fail("Set framebuffer palette");
			}
		}
	}
	if ((surface_viewport.w != surface->w || surface_viewport.h != surface->h) &&
		!SDL_FillSurfaceRect(surface, NULL, SDL_MapSurfaceRGB(surface, 0, 0, 0))) {
		video_fail("Clear video borders");
	}
	if (!SDL_BlitSurfaceScaled(frame_surface, NULL, surface, &surface_viewport,
							   SDL_SCALEMODE_NEAREST)) {
		video_fail("Scale video surface");
	}
	video_record_render_work();
	if (!SDL_UpdateWindowSurface(window)) {
		video_fail("Present video surface");
	}
}

static void prepare_presentation_page(const legacy_u8 *legacy_pixels)
{
	legacy_u32 back_page = (front_page + 1U) % PRESENTATION_PAGE_COUNT;
	struct PRESENTATION_PAGE *page = &presentation_pages[back_page];
	legacy_s32 width = hires_enabled() ? hires_render_width() : SDL3_SCREEN_WIDTH;
	legacy_s32 height = hires_enabled() ? hires_render_height() : SDL3_SCREEN_HEIGHT;
	if (page->pixels == NULL || page->width != width || page->height != height) {
		legacy_u32 *pixels = SDL_realloc(page->pixels, (size_t)width * height * sizeof(*pixels));
		if (pixels == NULL) {
			SDL_OutOfMemory();
			video_fail("Allocate presentation page");
		}
		page->pixels = pixels;
		page->width = width;
		page->height = height;
	}
	/* Compose from the original address so high-resolution companion pixels
	 * remain associated with it. Bake the palette into the owned hidden page,
	 * then flip only after the entire composition is complete. */
	hires_copy_framebuffer_argb(legacy_pixels, palette_pixels, page->pixels,
								width * sizeof(*page->pixels));
	front_page = back_page;
}

void sdl3_video_reset_renderer(void)
{
	SDL_DestroyTexture(texture);
	texture = NULL;
}

static void present_texture(legacy_u8 new_frame)
{
	const struct PRESENTATION_PAGE *page = &presentation_pages[front_page];
	if (page->pixels == NULL) {
		return;
	}
	legacy_u8 upload = new_frame;
	if (texture == NULL || texture_width != page->width || texture_height != page->height) {
		SDL_DestroyTexture(texture);
		texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
									page->width, page->height);
		if (texture == NULL || !SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST)) {
			video_fail("Create presentation texture");
		}
		texture_width = page->width;
		texture_height = page->height;
		upload = true;
	}
	if (upload) {
		void *texture_pixels;
		/* Match the output parameter type required by SDL. */
		legacy_int pitch;
		if (!SDL_LockTexture(texture, NULL, &texture_pixels, &pitch)) {
			video_fail("Lock video texture");
		}
		for (legacy_s32 row = 0; row < page->height; row++) {
			memcpy((legacy_u8 *)texture_pixels + (size_t)row * pitch,
				   page->pixels + (size_t)row * page->width,
				   (size_t)page->width * sizeof(*page->pixels));
		}
		SDL_UnlockTexture(texture);
	}
	legacy_u8 keyboard_active = sdl3_soft_keyboard_active();
	legacy_int logical_w = 0, logical_h = 0;
	SDL_RendererLogicalPresentation presentation = SDL_LOGICAL_PRESENTATION_DISABLED;
	legacy_f32 scale_x = 1, scale_y = 1;
	SDL_FRect game_area;
	if (keyboard_active) {
		SDL_GetRenderLogicalPresentation(renderer, &logical_w, &logical_h, &presentation);
		SDL_GetRenderScale(renderer, &scale_x, &scale_y);
		SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
		legacy_int output_w, output_h, window_w, window_h;
		SDL_GetCurrentRenderOutputSize(renderer, &output_w, &output_h);
		SDL_GetWindowSize(window, &window_w, &window_h);
		SDL_SetRenderScale(renderer, (legacy_f32)output_w / window_w,
						   (legacy_f32)output_h / window_h);
		game_area = sdl3_soft_keyboard_game_area(window);
	}
	if (!SDL_RenderClear(renderer) ||
		!SDL_RenderTexture(renderer, texture, NULL, keyboard_active ? &game_area : NULL)) {
		video_fail("Render video texture");
	}
	/* Include CPU composition and submission, but exclude the presentation
	 * call, which can deliberately wait for display synchronization. */
	if (new_frame) {
		video_record_render_work();
	}
	if (keyboard_active) {
		sdl3_soft_keyboard_draw(renderer, window);
	} else {
		sdl3_touch_draw(renderer, window);
	}
	legacy_u64 present_started = SDL_GetTicksNS();
	if (!SDL_RenderPresent(renderer)) {
		video_fail("Present video");
	}
	if (keyboard_active) {
		SDL_SetRenderLogicalPresentation(renderer, logical_w, logical_h, presentation);
		SDL_SetRenderScale(renderer, scale_x, scale_y);
	}
#ifdef __EMSCRIPTEN__
	/* SDL's implicit Asyncify sleep is disabled. Keep long rendering loops
	 * cooperative without adding another wait after recent input/timer yields. */
	sdl3_browser_yield_if_due();
#endif
	if (measured_frame != 0) {
		/* An exposure can repaint the completed front page while another frame
		 * is being drawn. Its refresh wait is not rendering workload either. */
		render_frame_started += SDL_GetTicksNS() - present_started;
	}
}

void sdl3_video_present(void)
{
	if (window == NULL || drawing_frame) {
		return;
	}
#ifdef __DJGPP__
	if (high_resolution_output != (hires_enabled() != 0)) {
		select_dos_video_mode(hires_enabled() != 0);
	}
#endif
	const legacy_u8 *legacy_pixels = dos_memory_make_pointer(VGA_MEMORY_SEGMENT, 0);
	if (surface_output) {
		legacy_s32 width;
		legacy_s32 height;
		const legacy_u32 *argb = hires_framebuffer_argb(legacy_pixels, palette_pixels);
		const legacy_u8 *pixels = NULL;
		if (argb != NULL) {
			/* ARGB composition already expands every indexed sample. */
			width = hires_render_width();
			height = hires_render_height();
		} else {
			pixels = hires_framebuffer(legacy_pixels, &width, &height);
		}
		present_surface(pixels, argb, width, height);
	} else {
		prepare_presentation_page(legacy_pixels);
		present_texture(true);
	}
	memcpy(previous_pixels, legacy_pixels, SCREEN_BYTES);
	previous_generation = hires_generation();
	palette_changed = false;
	last_present = SDL_GetTicks();
}

void sdl3_video_redraw(void)
{
	if (window == NULL) {
		return;
	}
	if (surface_output) {
		/* DOS keeps its direct indexed/VESA path and frame guard. */
		sdl3_video_present();
		return;
	}
	/* Repaint only the published page. Drawing, palette edits, and retiring
	 * high-resolution buffers cannot change this snapshot before a flip. */
	present_texture(false);
	last_present = SDL_GetTicks();
}

void sdl3_video_screenshot(void)
{
	if (window == NULL) {
		return;
	}
	/* Use the completed frame, not renderer readback after RenderPresent or
	 * the live game buffers, which may already contain part of the next frame. */
	SDL_Surface *surface;
	if (surface_output) {
		surface = SDL_GetWindowSurface(window);
	} else {
		const struct PRESENTATION_PAGE *page = &presentation_pages[front_page];
		if (page->pixels == NULL) {
			return;
		}
		surface = SDL_CreateSurfaceFrom(page->width, page->height, SDL_PIXELFORMAT_ARGB8888,
										page->pixels, page->width * sizeof(*page->pixels));
	}
	if (surface == NULL) {
		SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "Cannot capture screenshot: %s", SDL_GetError());
		return;
	}
	legacy_u64 capture_started = SDL_GetTicksNS();
	legacy_char filename[SCREENSHOT_NAME_CAPACITY];
	legacy_u32 number;
	/* main selects the game data directory as the working directory. Keep
	 * names compatible with DOS and skip existing captures across restarts. */
	for (number = SCREENSHOT_FIRST_NUMBER; number <= SCREENSHOT_LAST_NUMBER; number++) {
		SDL_snprintf(filename, sizeof(filename), SCREENSHOT_NAME_FORMAT, (legacy_uint)number);
		if (!SDL_GetPathInfo(filename, NULL)) {
			break;
		}
	}
	if (number > SCREENSHOT_LAST_NUMBER) {
		SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
					"Cannot save screenshot: all SHOTxxxx.PNG names are in use");
	} else if (!SDL_SavePNG(surface, filename)) {
		SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "Cannot save screenshot %s: %s", filename,
					SDL_GetError());
	} else {
#ifdef __ANDROID__
		android_saved_file_written((const legacy_s8 *)filename);
#endif
#ifdef __EMSCRIPTEN__
		if (!save_browser_screenshot(filename)) {
			SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "Cannot save browser screenshot %s", filename);
		}
#else
		SDL_Log("Screenshot saved to %s", filename);
#endif
	}
	if (!surface_output) {
		SDL_DestroySurface(surface);
	}
	if (measured_frame != 0) {
		/* PNG compression and disk/browser writes are not rendering workload. */
		render_frame_started += SDL_GetTicksNS() - capture_started;
	}
}

void sdl3_video_begin_frame(void)
{
	hires_set_render_scale(frame_adaptive.preset == FRAME_ADAPTIVE_PRESET_AUTO
							   ? HIRES_SCALE
							   : frame_adaptive_render_scale(&frame_adaptive));
	drawing_frame = true;
	adaptive_frame = 0;
	measured_frame = 1;
	render_frame_started = SDL_GetTicksNS();
}

void sdl3_video_begin_track_frame(legacy_u8 adaptive)
{
	drawing_frame = true;
	adaptive_frame = adaptive != 0 && frame_adaptive.preset == FRAME_ADAPTIVE_PRESET_AUTO;
	measured_frame = 1;
	render_frame_started = SDL_GetTicksNS();
}

void sdl3_video_add_render_work(legacy_u64 elapsed_ns)
{
	if (measured_frame != 0) {
		render_frame_started -= elapsed_ns;
	}
}

void sdl3_video_end_frame(void)
{
	drawing_frame = false;
	sdl3_video_present();
	adaptive_frame = 0;
	measured_frame = 0;
}

void sdl3_video_refresh(void)
{
	if (window != NULL && !drawing_frame && SDL_GetTicks() - last_present >= PRESENT_INTERVAL_MS) {
		const legacy_u8 *pixels = dos_memory_make_pointer(VGA_MEMORY_SEGMENT, 0);
		if (palette_changed || hires_generation() != previous_generation ||
			memcmp(pixels, previous_pixels, SCREEN_BYTES) != 0) {
			sdl3_video_present();
		}
		/* Also throttle unchanged screens, including idle menus. */
		last_present = SDL_GetTicks();
	}
}

void sdl3_video_shutdown(void)
{
	SDL_DestroySurface(frame_surface);
	SDL_DestroyTexture(texture);
	for (legacy_u32 index = 0; index < PRESENTATION_PAGE_COUNT; index++) {
		SDL_free(presentation_pages[index].pixels);
		memset(&presentation_pages[index], 0, sizeof(presentation_pages[index]));
	}
	front_page = 0;
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	surface_output = false;
	high_resolution_output = false;
	drawing_frame = false;
	adaptive_frame = 0;
	measured_frame = 0;
	render_frame_started = 0;
	frame_render_timing_reset();
	frame_surface = NULL;
	texture = NULL;
	texture_width = 0;
	texture_height = 0;
	renderer = NULL;
	window = NULL;
}

void dos_video_set_mode_13h(void)
{
	if (sdl3_batch_mode) {
		hires_forget(dos_memory_make_pointer(VGA_MEMORY_SEGMENT, 0));
		memset(dos_memory_make_pointer(VGA_MEMORY_SEGMENT, 0), 0, SCREEN_BYTES);
		return;
	}
	sdl3_video_shutdown();
#ifdef __EMSCRIPTEN__
	/* Keep keyboard capture on the game canvas, leaving page controls usable. */
	SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#canvas");
	/* Own browser suspension centrally; SDL's swap sleep would add a nested
	 * zero-delay timer on top of the game's explicit pacing waits. */
	SDL_SetHint(SDL_HINT_EMSCRIPTEN_ASYNCIFY, "0");
#endif
	if (SDL_WasInit(SDL_INIT_VIDEO) == 0 && !SDL_InitSubSystem(SDL_INIT_VIDEO)) {
		video_fail("Initialize video");
	}
#ifdef __DJGPP__
	/* The direct framebuffer supports indexed and truecolour VESA modes. */
	SDL_SetHint(SDL_HINT_DOS_ALLOW_DIRECT_FRAMEBUFFER, "1");
	window = SDL_CreateWindow("Chocolate Stunts", SDL3_SCREEN_WIDTH, SDL3_SCREEN_HEIGHT,
							  SDL_WINDOW_FULLSCREEN);
#else
	SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE;
#ifndef __EMSCRIPTEN__
	window_flags = desktop_window_flags();
#ifndef __ANDROID__
	if (startup_fullscreen_enabled != SDL3_STARTUP_FULLSCREEN_OFF) {
		window_flags |= SDL_WINDOW_FULLSCREEN;
	}
#endif
#endif
	window = SDL_CreateWindow("Chocolate Stunts", SDL3_SCREEN_WIDTH * WINDOW_INITIAL_SCALE,
							  PRESENTATION_HEIGHT * WINDOW_INITIAL_SCALE, window_flags);
#endif
	if (window == NULL) {
		video_fail("Create game window");
	}
	hires_forget(dos_memory_make_pointer(VGA_MEMORY_SEGMENT, 0));
	memset(dos_memory_make_pointer(VGA_MEMORY_SEGMENT, 0), 0, SCREEN_BYTES);
#ifdef __DJGPP__
	select_dos_video_mode(hires_enabled() != 0);
	surface_output = true;
#else
	renderer = SDL_CreateRenderer(window, NULL);
	if (renderer == NULL) {
		video_fail("Create renderer");
	}
	/* Browser presentation is composited on its own refresh cycle. SDL
	 * implements swap intervals through an Emscripten main-loop callback,
	 * whereas this backend preserves the nested game loops with Asyncify. */
#ifndef __EMSCRIPTEN__
	configure_vsync();
#endif
	if (!SDL_SetRenderLogicalPresentation(renderer, SDL3_SCREEN_WIDTH, PRESENTATION_HEIGHT,
										  SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
		video_fail("Configure framebuffer scaling");
	}
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
#endif
	SDL_HideCursor();
	palette_changed = true;
	sdl3_video_present();
}

legacy_s16 dos_video_get_status(void)
{
	/* Preserve polling loops that wait for both phases of VGA retrace. */
	sdl3_platform_pump();
	return (SDL_GetTicks() % VGA_RETRACE_PERIOD_MS) < VGA_RETRACE_DURATION_MS
			   ? DOS_VIDEO_RETRACE_STATUS_BIT
			   : 0;
}

legacy_s16 video_get_status(void)
{
	return dos_video_get_status();
}

void dos_video_set_palette(legacy_u16 start, legacy_u16 count, legacy_u8 *palette)
{
	for (legacy_u32 index = start;
		 index < VGA_PALETTE_COLOR_COUNT && index < (legacy_u32)start + count; index++) {
		SDL_Color *color = &palette_colors[index];
		color->r = (legacy_u8)((palette[0] & VGA_PALETTE_CHANNEL_MAX) * SDL_ALPHA_OPAQUE /
							   VGA_PALETTE_CHANNEL_MAX);
		color->g = (legacy_u8)((palette[1] & VGA_PALETTE_CHANNEL_MAX) * SDL_ALPHA_OPAQUE /
							   VGA_PALETTE_CHANNEL_MAX);
		color->b = (legacy_u8)((palette[2] & VGA_PALETTE_CHANNEL_MAX) * SDL_ALPHA_OPAQUE /
							   VGA_PALETTE_CHANNEL_MAX);
		color->a = SDL_ALPHA_OPAQUE;
		palette_pixels[index] = ((legacy_u32)SDL_ALPHA_OPAQUE << LEGACY_THREE_BYTE_BITS) |
								((legacy_u32)color->r << LEGACY_WORD_BITS) |
								((legacy_u32)color->g << LEGACY_BYTE_BITS) | color->b;
		palette += VGA_PALETTE_CHANNEL_COUNT;
	}
	palette_changed = true;
}

legacy_u8 dos_video_enable_planar_pages(void)
{
	/* SDL consumes the game's packed framebuffer; VGA planes are unnecessary. */
	return 0;
}

void dos_video_set_write_planes(legacy_u8 mask)
{
	(void)mask;
}

void dos_video_set_read_plane(legacy_u8 plane)
{
	(void)plane;
}

void dos_video_show_page(legacy_u16 address)
{
	(void)address;
	sdl3_video_present();
}

void dos_video_set_mode4(void)
{
	dos_video_set_mode_13h();
}

void dos_video_set_mode7(void)
{
	sdl3_video_shutdown();
}
