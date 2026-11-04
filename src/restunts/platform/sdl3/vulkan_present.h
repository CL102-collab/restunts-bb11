#ifndef RESTUNTS_SDL3_VULKAN_PRESENT_H
#define RESTUNTS_SDL3_VULKAN_PRESENT_H

#include "../../c/legacy.h"
#include <SDL3/SDL.h>

#if defined(RESTUNTS_VULKAN_SCENE) && !defined(__APPLE__) && !defined(__EMSCRIPTEN__) &&           \
	!defined(__DJGPP__)
#define RESTUNTS_VULKAN_PRESENT 1
legacy_u8 sdl3_vulkan_present_requested(void);
SDL_Renderer *sdl3_vulkan_present_create(SDL_Window *window);
void sdl3_vulkan_present_shutdown(void);
void sdl3_vulkan_present_palette(const legacy_u32 *palette);
void sdl3_vulkan_present_prepare(const legacy_u8 *base, legacy_u32 *pixels, legacy_s32 width,
								 legacy_s32 height);
legacy_s32 sdl3_vulkan_present_draw(SDL_Renderer *renderer, SDL_Texture *cpu,
									const SDL_FRect *destination);
legacy_u8 sdl3_vulkan_present_active(void);
SDL_Surface *sdl3_vulkan_present_capture(const legacy_u32 *pixels, legacy_s32 width,
										 legacy_s32 height);
#endif

#endif
