#ifndef RESTUNTS_VULKAN_BACKEND_H
#define RESTUNTS_VULKAN_BACKEND_H

#include "graphics_backend.h"

legacy_u32 vulkan_enumerate_devices(struct GRAPHICS_DEVICE *devices, legacy_u32 capacity);
legacy_s32 vulkan_open(legacy_s32 device, legacy_char *error, size_t error_capacity);
legacy_s32 vulkan_dispatch(const legacy_u32 *input, size_t input_words, legacy_u32 *output,
						   size_t output_words, legacy_u32 invocation_count, legacy_char *error,
						   size_t error_capacity);
void vulkan_close(void);

#endif
