#ifndef RESTUNTS_METAL_BACKEND_H
#define RESTUNTS_METAL_BACKEND_H

#include "graphics_backend.h"

legacy_u32 metal_enumerate_devices(struct GRAPHICS_DEVICE *devices, legacy_u32 capacity);
legacy_s32 metal_open(legacy_s32 device, legacy_char *error, size_t error_capacity);
legacy_s32 metal_dispatch(const legacy_u32 *input, size_t input_words, legacy_u32 *output,
						  size_t output_words, legacy_u32 invocation_count, legacy_char *error,
						  size_t error_capacity);
void metal_close(void);

#endif
