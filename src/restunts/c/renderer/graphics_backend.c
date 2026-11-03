#include "graphics_backend.h"
#include "vulkan_backend.h"
#include <stdio.h>
#include <stdlib.h>

static legacy_s32 selected_renderer = GRAPHICS_RENDERER_SOFTWARE;
static legacy_s32 selected_device = GRAPHICS_DEVICE_DEFAULT;
static legacy_u8 cleanup_registered;

static legacy_s32 graphics_error(legacy_char *error, size_t capacity, const legacy_char *message)
{
	if (error != NULL && capacity != 0) {
		snprintf(error, capacity, "%s", message);
	}
	return 0;
}

legacy_u32 graphics_enumerate_devices(struct GRAPHICS_DEVICE *devices, legacy_u32 capacity)
{
#ifdef RESTUNTS_NATIVE_VULKAN
	return vulkan_enumerate_devices(devices, capacity);
#else
	(void)devices;
	(void)capacity;
	return 0;
#endif
}

void graphics_shutdown(void)
{
#ifdef RESTUNTS_NATIVE_VULKAN
	vulkan_close();
#endif
	selected_renderer = GRAPHICS_RENDERER_SOFTWARE;
	selected_device = GRAPHICS_DEVICE_DEFAULT;
}

legacy_s32 graphics_select_renderer(legacy_s32 renderer, legacy_s32 device, legacy_char *error,
									size_t error_capacity)
{
	if (error != NULL && error_capacity != 0) {
		error[0] = 0;
	}
	if (renderer != GRAPHICS_RENDERER_SOFTWARE && renderer != GRAPHICS_RENDERER_VULKAN) {
		return graphics_error(error, error_capacity, "Unknown graphics renderer");
	}
	if (device < GRAPHICS_DEVICE_DEFAULT) {
		return graphics_error(error, error_capacity, "Invalid graphics device index");
	}
	if (renderer == GRAPHICS_RENDERER_SOFTWARE) {
		if (device != GRAPHICS_DEVICE_DEFAULT) {
			return graphics_error(error, error_capacity,
								  "A graphics device can only be selected for Vulkan");
		}
		graphics_shutdown();
		return 1;
	}
#ifdef RESTUNTS_NATIVE_VULKAN
	struct GRAPHICS_DEVICE devices[GRAPHICS_DEVICE_CAPACITY];
	legacy_u32 count = graphics_enumerate_devices(devices, GRAPHICS_DEVICE_CAPACITY);
	if (count == 0) {
		return graphics_error(error, error_capacity, "No compatible Vulkan device is available");
	}
	legacy_s32 index = device == GRAPHICS_DEVICE_DEFAULT ? devices[0].index : device;
	legacy_u8 found = 0;
	for (legacy_u32 entry = 0; entry < count; entry++) {
		found |= devices[entry].index == index;
	}
	if (!found) {
		return graphics_error(error, error_capacity, "The selected Vulkan device is unavailable");
	}
	if (!cleanup_registered) {
		if (atexit(graphics_shutdown) != 0) {
			return graphics_error(error, error_capacity, "Cannot register graphics cleanup");
		}
		cleanup_registered = 1;
	}
	if (!vulkan_open(index, error, error_capacity)) {
		selected_renderer = GRAPHICS_RENDERER_SOFTWARE;
		selected_device = GRAPHICS_DEVICE_DEFAULT;
		return 0;
	}
	selected_renderer = GRAPHICS_RENDERER_VULKAN;
	selected_device = index;
	return 1;
#else
	(void)cleanup_registered;
	return graphics_error(error, error_capacity, "Vulkan rendering is unavailable on this target");
#endif
}

legacy_s32 graphics_renderer(void)
{
	return selected_renderer;
}

legacy_s32 graphics_device(void)
{
	return selected_device;
}

legacy_s32 graphics_gpu_dispatch(const legacy_u32 *input, size_t input_words, legacy_u32 *output,
								 size_t output_words, legacy_u32 invocation_count,
								 legacy_char *error, size_t error_capacity)
{
#ifdef RESTUNTS_NATIVE_VULKAN
	if (selected_renderer == GRAPHICS_RENDERER_VULKAN) {
		return vulkan_dispatch(input, input_words, output, output_words, invocation_count, error,
							   error_capacity);
	}
#else
	(void)input;
	(void)input_words;
	(void)output;
	(void)output_words;
	(void)invocation_count;
#endif
	return graphics_error(error, error_capacity, "No GPU graphics renderer is active");
}
