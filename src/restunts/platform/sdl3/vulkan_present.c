#include "vulkan_present.h"
#include "sdl3.h"
#include "../../c/renderer/graphics_backend.h"
#include "../../c/renderer/gpu_overlay.h"
#include "../../c/renderer/vulkan_scene.h"
#include <string.h>
#include <stdlib.h>

#define VULKAN_PRESENT_NAME "vulkan"
#define VULKAN_PALETTE_COLOR_MASK 0x00FFFFFFU
#define VULKAN_ALPHA_SHIFT 24U
#define VULKAN_ALPHA_MAX 255U
#define VULKAN_PRESENTATION_WIDTH 320
#define VULKAN_PRESENTATION_HEIGHT 240

static SDL_Renderer *shared_renderer;
static SDL_Texture *scene_texture;
static legacy_u8 front_active;
static legacy_s32 front_width, front_height, front_x, front_y;

legacy_u8 sdl3_vulkan_present_requested(void)
{
	if (graphics_renderer() != GRAPHICS_RENDERER_VULKAN || sdl3_batch_mode) {
		return 0;
	}
	for (legacy_s32 index = 0; index < SDL_GetNumRenderDrivers(); index++) {
		const legacy_char *name = SDL_GetRenderDriver(index);
		if (name != NULL && strcmp(name, VULKAN_PRESENT_NAME) == 0) {
			return 1;
		}
	}
	return 0;
}

SDL_Renderer *sdl3_vulkan_present_create(SDL_Window *window)
{
	struct VULKAN_SCENE_HANDLES handles;
	legacy_char error[GRAPHICS_ERROR_CAPACITY] = {0};
	if (!vulkan_scene_get_handles(&handles, error, sizeof(error)) || !handles.presentable) {
		SDL_SetError("Vulkan presentation is unavailable: %s", error);
		return NULL;
	}
	SDL_PropertiesID properties = SDL_CreateProperties();
	if (properties == 0) {
		return NULL;
	}
	SDL_SetStringProperty(properties, SDL_PROP_RENDERER_CREATE_NAME_STRING, VULKAN_PRESENT_NAME);
	SDL_SetPointerProperty(properties, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, window);
	SDL_SetPointerProperty(properties, SDL_PROP_RENDERER_CREATE_VULKAN_INSTANCE_POINTER,
						   handles.instance);
	SDL_SetPointerProperty(properties, SDL_PROP_RENDERER_CREATE_VULKAN_PHYSICAL_DEVICE_POINTER,
						   handles.physical_device);
	SDL_SetPointerProperty(properties, SDL_PROP_RENDERER_CREATE_VULKAN_DEVICE_POINTER,
						   handles.device);
	SDL_SetNumberProperty(properties,
						  SDL_PROP_RENDERER_CREATE_VULKAN_GRAPHICS_QUEUE_FAMILY_INDEX_NUMBER,
						  handles.queue_family);
	SDL_SetNumberProperty(properties,
						  SDL_PROP_RENDERER_CREATE_VULKAN_PRESENT_QUEUE_FAMILY_INDEX_NUMBER,
						  handles.queue_family);
	SDL_Renderer *result = SDL_CreateRendererWithProperties(properties);
	SDL_DestroyProperties(properties);
	if (result == NULL) {
		return NULL;
	}
	properties = SDL_CreateProperties();
	SDL_SetNumberProperty(properties, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER,
						  SDL_PIXELFORMAT_RGBA32);
	SDL_SetNumberProperty(properties, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER,
						  SDL_TEXTUREACCESS_STATIC);
	SDL_SetNumberProperty(properties, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, handles.texture_width);
	SDL_SetNumberProperty(properties, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER,
						  handles.texture_height);
	SDL_SetNumberProperty(properties, SDL_PROP_TEXTURE_CREATE_VULKAN_TEXTURE_NUMBER,
						  (Sint64)handles.image);
	SDL_SetNumberProperty(properties, SDL_PROP_TEXTURE_CREATE_VULKAN_LAYOUT_NUMBER,
						  handles.image_layout);
	scene_texture = SDL_CreateTextureWithProperties(result, properties);
	SDL_DestroyProperties(properties);
	if (scene_texture == NULL || !SDL_SetTextureScaleMode(scene_texture, SDL_SCALEMODE_NEAREST) ||
		!SDL_SetTextureBlendMode(scene_texture, SDL_BLENDMODE_BLEND)) {
		SDL_DestroyTexture(scene_texture);
		scene_texture = NULL;
		SDL_DestroyRenderer(result);
		return NULL;
	}
	shared_renderer = result;
	vulkan_scene_set_active(1);
	return result;
}

void sdl3_vulkan_present_shutdown(void)
{
	vulkan_scene_set_active(0);
	SDL_DestroyTexture(scene_texture);
	scene_texture = NULL;
	shared_renderer = NULL;
	front_active = 0;
	gpu_overlay_reset();
}

void sdl3_vulkan_present_palette(const legacy_u32 *palette)
{
	if (graphics_renderer() == GRAPHICS_RENDERER_VULKAN) {
		vulkan_scene_palette(palette);
	}
}

void sdl3_vulkan_present_prepare(const legacy_u8 *base, legacy_u32 *pixels, legacy_s32 width,
								 legacy_s32 height)
{
	(void)pixels;
	front_active = shared_renderer != NULL &&
				   gpu_overlay_mask(base, width, height, &front_x, &front_y) != NULL;
	front_width = width;
	front_height = height;
}

legacy_u8 sdl3_vulkan_present_active(void)
{
	return shared_renderer != NULL && front_active;
}

legacy_s32 sdl3_vulkan_present_draw(SDL_Renderer *renderer, SDL_Texture *cpu,
									const SDL_FRect *destination)
{
	if (renderer != shared_renderer) {
		return 0;
	}
	if (!SDL_SetTextureBlendMode(cpu, SDL_BLENDMODE_NONE) ||
		!SDL_RenderTexture(renderer, cpu, NULL, destination)) {
		return -1;
	}
	if (!front_active) {
		return 1;
	}
	SDL_FRect source = {0, 0, (legacy_f32)front_width, (legacy_f32)front_height};
	SDL_FRect translated;
	const SDL_FRect *output = destination;
	if (front_x != 0 || front_y != 0) {
		translated = destination != NULL
						 ? *destination
						 : (SDL_FRect){0, 0, VULKAN_PRESENTATION_WIDTH, VULKAN_PRESENTATION_HEIGHT};
		translated.x += front_x * translated.w / front_width;
		translated.y += front_y * translated.h / front_height;
		output = &translated;
	}
	legacy_char error[GRAPHICS_ERROR_CAPACITY];
	if (!vulkan_scene_refresh_palette(error, sizeof(error))) {
		SDL_SetError("Cannot refresh Vulkan palette: %s", error);
		return -1;
	}
	if (!SDL_RenderTexture(renderer, scene_texture, &source, output) ||
		!SDL_SetTextureBlendMode(cpu, SDL_BLENDMODE_BLEND) ||
		!SDL_RenderTexture(renderer, cpu, NULL, destination) ||
		!SDL_SetTextureBlendMode(cpu, SDL_BLENDMODE_NONE)) {
		return -1;
	}
	return 1;
}

static legacy_u32 present_blend(legacy_u32 over, legacy_u32 under, legacy_u32 alpha)
{
	if (alpha == VULKAN_ALPHA_MAX) {
		return over | (VULKAN_ALPHA_MAX << VULKAN_ALPHA_SHIFT);
	}
	if (alpha == 0) {
		return under;
	}
	legacy_u32 result = VULKAN_ALPHA_MAX << VULKAN_ALPHA_SHIFT;
	for (legacy_u32 shift = 0; shift < VULKAN_ALPHA_SHIFT; shift += LEGACY_BYTE_BITS) {
		legacy_u32 color = (((over >> shift) & VULKAN_ALPHA_MAX) * alpha +
							((under >> shift) & VULKAN_ALPHA_MAX) * (VULKAN_ALPHA_MAX - alpha)) /
						   VULKAN_ALPHA_MAX;
		result |= color << shift;
	}
	return result;
}

SDL_Surface *sdl3_vulkan_present_capture(const legacy_u32 *pixels, legacy_s32 width,
										 legacy_s32 height)
{
	legacy_u32 *gpu = malloc((size_t)width * height * sizeof(*gpu));
	if (gpu == NULL) {
		SDL_OutOfMemory();
		return NULL;
	}
	legacy_char error[GRAPHICS_ERROR_CAPACITY];
	if (!vulkan_scene_readback(gpu, width, height, error, sizeof(error))) {
		free(gpu);
		SDL_SetError("Cannot capture Vulkan scene: %s", error);
		return NULL;
	}
	SDL_Surface *surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_ARGB8888);
	if (surface == NULL) {
		free(gpu);
		return NULL;
	}
	for (legacy_s32 y = 0; y < height; y++) {
		legacy_u32 *out = (legacy_u32 *)((legacy_u8 *)surface->pixels + (size_t)y * surface->pitch);
		for (legacy_s32 x = 0; x < width; x++) {
			legacy_u32 index = (legacy_u32)y * width + x;
			legacy_u32 cpu = pixels[index];
			legacy_u32 base = cpu | (VULKAN_ALPHA_MAX << VULKAN_ALPHA_SHIFT);
			legacy_s32 sx = x - front_x, sy = y - front_y;
			if (sx >= 0 && sy >= 0 && sx < width && sy < height) {
				const legacy_u8 *rgba = (const legacy_u8 *)(gpu + (size_t)sy * width + sx);
				legacy_u32 color = ((legacy_u32)rgba[0] << LEGACY_WORD_BITS) |
								   ((legacy_u32)rgba[1] << LEGACY_BYTE_BITS) | rgba[2];
				base = present_blend(color, base, rgba[3]);
			}
			out[x] = present_blend(cpu, base, cpu >> VULKAN_ALPHA_SHIFT);
		}
	}
	free(gpu);
	return surface;
}
