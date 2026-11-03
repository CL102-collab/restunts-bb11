#ifndef RESTUNTS_GRAPHICS_BACKEND_H
#define RESTUNTS_GRAPHICS_BACKEND_H

#include "../legacy.h"
#include <stddef.h>

/* Scene raster selection is independent of the classic/HyperVision view mode. */
#if (defined(RESTUNTS_SDL3) && !defined(__APPLE__) && !defined(__DJGPP__)) ||                      \
	defined(RESTUNTS_GRAPHICS_LAUNCHER)
#define RESTUNTS_RENDERER_SELECTION 1
#endif

#define GRAPHICS_DEVICE_NAME_CAPACITY 256U
#define GRAPHICS_DEVICE_CAPACITY 64U
#define GRAPHICS_ERROR_CAPACITY 512U
#define GRAPHICS_DEVICE_DEFAULT (-1)
#define GRAPHICS_DEVICE_REPORT_HEADER "RESTUNTS_RENDER_DEVICES 1\n"

enum GRAPHICS_RENDERER { GRAPHICS_RENDERER_SOFTWARE, GRAPHICS_RENDERER_VULKAN };

struct GRAPHICS_DEVICE {
	legacy_s32 index;
	legacy_u32 vendor_id;
	legacy_u32 device_id;
	legacy_char name[GRAPHICS_DEVICE_NAME_CAPACITY];
};

#ifdef __cplusplus
extern "C" {
#endif

/* No window, game assets or SDL initialization are needed for this inventory. */
legacy_u32 graphics_enumerate_devices(struct GRAPHICS_DEVICE *devices, legacy_u32 capacity);
legacy_s32 graphics_select_renderer(legacy_s32 renderer, legacy_s32 device, legacy_char *error,
									size_t error_capacity);
legacy_s32 graphics_renderer(void);
legacy_s32 graphics_device(void);
void graphics_shutdown(void);

/* One invocation per output pixel. Both storage buffers contain 32-bit words;
 * output is initialized by the caller and read back before this call returns. */
legacy_s32 graphics_gpu_dispatch(const legacy_u32 *input, size_t input_words, legacy_u32 *output,
								 size_t output_words, legacy_u32 invocation_count,
								 legacy_char *error, size_t error_capacity);

#ifdef __cplusplus
}
#endif

#endif
