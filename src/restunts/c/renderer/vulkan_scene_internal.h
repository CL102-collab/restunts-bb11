#ifndef RESTUNTS_VULKAN_SCENE_INTERNAL_H
#define RESTUNTS_VULKAN_SCENE_INTERNAL_H

#include "vulkan_internal.h"
#include "vulkan_scene.h"

#define VULKAN_SCENE_SLOTS 3U
#define VULKAN_SCENE_PIPELINES 3U
#define VULKAN_SCENE_DEPTH_PIPELINE 0U
#define VULKAN_SCENE_PAINTER_PIPELINE 1U
#define VULKAN_SCENE_SHADOW_PIPELINE 2U
#define VULKAN_SCENE_SHADOW_MODE 3U
#define VULKAN_SCENE_FIRST_VERTICES 4096U
#define VULKAN_SCENE_PALETTE_BYTES (VULKAN_SCENE_PALETTE_COLORS * sizeof(legacy_u32))
#define VULKAN_SCENE_RENDER_CLEAR 0U
#define VULKAN_SCENE_RENDER_LOAD 1U

#define VULKAN_SCENE_FUNCTIONS(X)                                                                  \
	X(CreateImage)                                                                                 \
	X(DestroyImage)                                                                                \
	X(GetImageMemoryRequirements)                                                                  \
	X(BindImageMemory)                                                                             \
	X(CreateImageView)                                                                             \
	X(DestroyImageView)                                                                            \
	X(CreateRenderPass)                                                                            \
	X(DestroyRenderPass)                                                                           \
	X(CreateFramebuffer)                                                                           \
	X(DestroyFramebuffer)                                                                          \
	X(CreateGraphicsPipelines)                                                                     \
	X(CmdBeginRenderPass)                                                                          \
	X(CmdEndRenderPass)                                                                            \
	X(CmdSetViewport)                                                                              \
	X(CmdSetScissor)                                                                               \
	X(CmdBindVertexBuffers)                                                                        \
	X(CmdPushConstants)                                                                            \
	X(CmdDraw)                                                                                     \
	X(CmdCopyImageToBuffer)

/* Private vertex layout; retained/public geometry keeps the 32-byte ABI.
 * Plane stores affine coefficients A/B/C and the largest vertex depth. */
struct VULKAN_SCENE_GPU_VERTEX {
	struct VULKAN_SCENE_VERTEX base;
	legacy_f32 plane[4];
};

struct VULKAN_SCENE_API {
#define VULKAN_SCENE_DECLARE(name) PFN_vk##name name;
	VULKAN_SCENE_FUNCTIONS(VULKAN_SCENE_DECLARE)
#undef VULKAN_SCENE_DECLARE
};

struct VULKAN_SCENE_IMAGE {
	VkImage image;
	VkDeviceMemory memory;
	VkImageView view;
};

struct VULKAN_SCENE_SLOT {
	VkCommandBuffer command;
	VkFence fence;
	VkDescriptorSet descriptor;
	struct VULKAN_BUFFER vertices, palette;
	legacy_s32 pending;
};

struct VULKAN_SCENE_BATCH {
	size_t first, count;
	legacy_s32 width, height, left, right, top, bottom, preserve, classic;
};

struct VULKAN_SCENE_STATE {
	struct VULKAN_SCENE_API api;
	struct VULKAN_SCENE_IMAGE color, depth;
	VkFormat depth_format;
	VkRenderPass passes[2];
	VkFramebuffer framebuffer;
	VkDescriptorSetLayout descriptors;
	VkPipelineLayout layout;
	VkPipeline pipelines[VULKAN_SCENE_PIPELINES];
	VkShaderModule vertex_shader, fragment_shader;
	VkDescriptorPool descriptor_pool;
	VkCommandPool command_pool;
	struct VULKAN_SCENE_SLOT slots[VULKAN_SCENE_SLOTS];
	struct VULKAN_BUFFER readback;
	legacy_u32 palette[VULKAN_SCENE_PALETTE_COLORS];
	struct VULKAN_SCENE_VERTEX *cached_vertices;
	struct VULKAN_SCENE_BATCH *cached_batches;
	size_t cached_vertex_count, cached_vertex_capacity, cached_batch_count, cached_batch_capacity;
	legacy_u32 palette_generation, image_palette_generation;
	legacy_u32 next_slot;
	legacy_s32 initialized, active, failed, replaying;
};

extern struct VULKAN_SCENE_STATE vulkan_scene;

void vulkan_scene_depth_reset(void);
void vulkan_scene_depth_shutdown(void);
legacy_s32 vulkan_scene_write_vertices(struct VULKAN_SCENE_GPU_VERTEX *output,
									   const struct VULKAN_SCENE_VERTEX *vertices, size_t count,
									   legacy_s32 classic, legacy_char *error, size_t capacity);

legacy_s32 vulkan_scene_initialize(legacy_char *error, size_t capacity);
legacy_s32 vulkan_scene_create_targets(legacy_char *error, size_t capacity);
legacy_s32 vulkan_scene_create_pipeline(legacy_char *error, size_t capacity);
legacy_s32 vulkan_scene_create_commands(legacy_char *error, size_t capacity);
legacy_s32 vulkan_scene_buffer(struct VULKAN_BUFFER *buffer, size_t bytes, VkBufferUsageFlags usage,
							   legacy_s32 cached, legacy_char *error, size_t capacity);
legacy_s32 vulkan_scene_flush(struct VULKAN_BUFFER *buffer, legacy_s32 invalidate,
							  legacy_char *error, size_t capacity);
void vulkan_scene_release_buffer(struct VULKAN_BUFFER *buffer);
void vulkan_scene_release_image(struct VULKAN_SCENE_IMAGE *image);
legacy_s32 vulkan_scene_cache(const struct VULKAN_SCENE_VERTEX *vertices, size_t count,
							  legacy_s32 width, legacy_s32 height, legacy_s32 left,
							  legacy_s32 right, legacy_s32 top, legacy_s32 bottom,
							  legacy_s32 preserve, legacy_s32 classic, legacy_char *error,
							  size_t capacity);

#endif
