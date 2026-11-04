#include "vulkan_scene_internal.h"
#include <stdlib.h>
#include <string.h>

#define SCENE_PALETTE_OPAQUE 0xFF000000U
#define SCENE_PALETTE_RED_SHIFT 16U
#define SCENE_PALETTE_GREEN_SHIFT 8U
#define SCENE_TRIANGLE_VERTICES 3U

struct VULKAN_SCENE_STATE vulkan_scene;

static legacy_s32 scene_error(legacy_char *error, size_t capacity, const legacy_char *operation,
							  VkResult result)
{
	vulkan_scene.failed = 1;
	return vulkan_error(error, capacity, operation, result);
}

static legacy_s32 scene_wait(struct VULKAN_SCENE_SLOT *slot, legacy_char *error, size_t capacity)
{
	if (!slot->pending) {
		return 1;
	}
	VkResult result = vulkan_context.api.GetFenceStatus(vulkan_context.device, slot->fence);
	if (result == VK_NOT_READY) {
		result = vulkan_context.api.WaitForFences(vulkan_context.device, 1, &slot->fence, VK_TRUE,
												  VULKAN_FENCE_TIMEOUT_NS);
	}
	if (result == VK_SUCCESS || result == VK_ERROR_DEVICE_LOST) {
		slot->pending = 0;
	}
	return result == VK_SUCCESS
			   ? 1
			   : scene_error(error, capacity, "Wait for Vulkan scene completion", result);
}

static struct VULKAN_SCENE_SLOT *scene_next(legacy_char *error, size_t capacity)
{
	struct VULKAN_SCENE_SLOT *slot = &vulkan_scene.slots[vulkan_scene.next_slot];
	if (!scene_wait(slot, error, capacity)) {
		return NULL;
	}
	vulkan_scene.next_slot = (vulkan_scene.next_slot + 1U) % VULKAN_SCENE_SLOTS;
	VkResult result = vulkan_context.api.ResetCommandBuffer(slot->command, 0);
	if (result != VK_SUCCESS) {
		scene_error(error, capacity, "Reset Vulkan scene commands", result);
		return NULL;
	}
	VkCommandBufferBeginInfo begin = {0};
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = vulkan_context.api.BeginCommandBuffer(slot->command, &begin);
	if (result != VK_SUCCESS) {
		scene_error(error, capacity, "Begin Vulkan scene commands", result);
		return NULL;
	}
	return slot;
}

static legacy_s32 scene_queue(struct VULKAN_SCENE_SLOT *slot, legacy_char *error, size_t capacity)
{
	VkResult result = vulkan_context.api.EndCommandBuffer(slot->command);
	if (result == VK_SUCCESS) {
		result = vulkan_context.api.ResetFences(vulkan_context.device, 1, &slot->fence);
	}
	if (result != VK_SUCCESS) {
		return scene_error(error, capacity, "Finish Vulkan scene commands", result);
	}
	VkSubmitInfo submit = {0};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &slot->command;
	result = vulkan_context.api.QueueSubmit(vulkan_context.queue, 1, &submit, slot->fence);
	if (result != VK_SUCCESS) {
		return scene_error(error, capacity, "Submit Vulkan scene", result);
	}
	slot->pending = 1;
	return 1;
}

legacy_s32 vulkan_scene_initialize(legacy_char *error, size_t capacity)
{
	if (vulkan_scene.initialized) {
		return !vulkan_scene.failed;
	}
	if (vulkan_context.device == VK_NULL_HANDLE || !vulkan_context.graphics_enabled) {
		return vulkan_error(error, capacity, "Vulkan graphics queue is unavailable",
							VK_ERROR_FEATURE_NOT_PRESENT);
	}
#define SCENE_LOAD(name)                                                                           \
	vulkan_scene.api.name =                                                                        \
		(PFN_vk##name)vulkan_context.api.GetDeviceProcAddr(vulkan_context.device, "vk" #name);     \
	if (vulkan_scene.api.name == NULL) {                                                           \
		return scene_error(error, capacity, "Missing Vulkan graphics function vk" #name,           \
						   VK_ERROR_INITIALIZATION_FAILED);                                        \
	}
	VULKAN_SCENE_FUNCTIONS(SCENE_LOAD)
#undef SCENE_LOAD
	if (!vulkan_scene_create_targets(error, capacity) ||
		!vulkan_scene_create_pipeline(error, capacity) ||
		!vulkan_scene_create_commands(error, capacity)) {
		vulkan_scene_shutdown();
		return 0;
	}
	legacy_u32 combined = 0;
	for (legacy_u32 index = 0; index < VULKAN_SCENE_PALETTE_COLORS; index++) {
		combined |= vulkan_scene.palette[index];
	}
	if (combined == 0) {
		for (legacy_u32 index = 0; index < VULKAN_SCENE_PALETTE_COLORS; index++) {
			vulkan_scene.palette[index] = SCENE_PALETTE_OPAQUE |
										  (index << SCENE_PALETTE_RED_SHIFT) |
										  (index << SCENE_PALETTE_GREEN_SHIFT) | index;
		}
	}
	vulkan_scene.initialized = 1;
	return vulkan_scene_submit(NULL, 0, VULKAN_SCENE_TEXTURE_WIDTH, VULKAN_SCENE_TEXTURE_HEIGHT, 0,
							   VULKAN_SCENE_TEXTURE_WIDTH, 0, VULKAN_SCENE_TEXTURE_HEIGHT, 0, 0,
							   error, capacity);
}

legacy_s32 vulkan_scene_get_handles(struct VULKAN_SCENE_HANDLES *handles, legacy_char *error,
									size_t error_capacity)
{
	if (handles == NULL) {
		return vulkan_error(error, error_capacity, "Missing Vulkan scene handle destination",
							VK_ERROR_INITIALIZATION_FAILED);
	}
	memset(handles, 0, sizeof(*handles));
	if (!vulkan_scene_initialize(error, error_capacity)) {
		return 0;
	}
	handles->instance = vulkan_context.instance;
	handles->physical_device = vulkan_context.physical_device;
	handles->device = vulkan_context.device;
	handles->queue_family = vulkan_context.queue_family;
	/* Vulkan non-dispatchable handles may be pointers or uint64 depending on
	 * platform ABI; memcpy preserves either representation without truncation. */
	memcpy(&handles->image, &vulkan_scene.color.image, sizeof(vulkan_scene.color.image));
	handles->image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	handles->image_format = VK_FORMAT_R8G8B8A8_UNORM;
	handles->texture_width = VULKAN_SCENE_TEXTURE_WIDTH;
	handles->texture_height = VULKAN_SCENE_TEXTURE_HEIGHT;
	handles->presentable = vulkan_context.presentation_enabled;
	return 1;
}

void vulkan_scene_set_active(legacy_s32 active)
{
	vulkan_scene.active = active != 0 && vulkan_scene.initialized && !vulkan_scene.failed;
}

legacy_s32 vulkan_scene_active(void)
{
	return vulkan_scene.active && vulkan_scene.initialized && !vulkan_scene.failed;
}

void vulkan_scene_palette(const legacy_u32 *argb)
{
	if (argb != NULL && memcmp(vulkan_scene.palette, argb, sizeof(vulkan_scene.palette)) != 0) {
		memcpy(vulkan_scene.palette, argb, sizeof(vulkan_scene.palette));
		vulkan_scene.palette_generation++;
	}
}

static legacy_s32 scene_upload(struct VULKAN_SCENE_SLOT *slot,
							   const struct VULKAN_SCENE_VERTEX *vertices, size_t count,
							   legacy_s32 classic, legacy_char *error, size_t capacity)
{
	size_t bytes = count * sizeof(struct VULKAN_SCENE_GPU_VERTEX);
	if (!vulkan_scene_buffer(&slot->vertices, bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 0, error,
							 capacity) ||
		!vulkan_scene_buffer(&slot->palette, VULKAN_SCENE_PALETTE_BYTES,
							 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0, error, capacity)) {
		return 0;
	}
	if (!vulkan_scene_write_vertices(slot->vertices.mapped, vertices, count, classic, error,
									 capacity)) {
		return 0;
	}
	memcpy(slot->palette.mapped, vulkan_scene.palette, VULKAN_SCENE_PALETTE_BYTES);
	if (!vulkan_scene_flush(&slot->vertices, 0, error, capacity) ||
		!vulkan_scene_flush(&slot->palette, 0, error, capacity)) {
		return 0;
	}
	VkDescriptorBufferInfo buffer = {slot->palette.buffer, 0, VULKAN_SCENE_PALETTE_BYTES};
	VkWriteDescriptorSet write = {0};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = slot->descriptor;
	write.dstBinding = 0;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	write.pBufferInfo = &buffer;
	vulkan_context.api.UpdateDescriptorSets(vulkan_context.device, 1, &write, 0, NULL);
	VkMemoryBarrier barrier = {0};
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
	vulkan_context.api.CmdPipelineBarrier(slot->command, VK_PIPELINE_STAGE_HOST_BIT,
										  VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
											  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
										  0, 1, &barrier, 0, NULL, 0, NULL);
	return 1;
}

legacy_s32 vulkan_scene_submit(const struct VULKAN_SCENE_VERTEX *vertices, size_t count,
							   legacy_s32 width, legacy_s32 height, legacy_s32 left,
							   legacy_s32 right, legacy_s32 top, legacy_s32 bottom,
							   legacy_s32 preserve, legacy_s32 classic, legacy_char *error,
							   size_t error_capacity)
{
	if (width <= 0 || height <= 0 || width > (legacy_s32)VULKAN_SCENE_TEXTURE_WIDTH ||
		height > (legacy_s32)VULKAN_SCENE_TEXTURE_HEIGHT || left < 0 || top < 0 || right > width ||
		bottom > height || left >= right || top >= bottom || count > LEGACY_U32_MAX ||
		count > (size_t)-1 / sizeof(struct VULKAN_SCENE_GPU_VERTEX) ||
		count % SCENE_TRIANGLE_VERTICES != 0 || (count != 0 && vertices == NULL)) {
		return vulkan_error(error, error_capacity, "Invalid Vulkan scene geometry or viewport",
							VK_ERROR_INITIALIZATION_FAILED);
	}
	if (!vulkan_scene_initialize(error, error_capacity)) {
		return 0;
	}
	if (!vulkan_scene_cache(vertices, count, width, height, left, right, top, bottom, preserve,
							classic, error, error_capacity)) {
		return 0;
	}
	if (!preserve) {
		vulkan_scene_depth_reset();
	}
	struct VULKAN_SCENE_SLOT *slot = scene_next(error, error_capacity);
	if (slot == NULL ||
		(count != 0 && !scene_upload(slot, vertices, count, classic, error, error_capacity))) {
		return 0;
	}
	VkClearValue clear[2] = {{{{0, 0, 0, 0}}}, {.depthStencil = {0.0f, 0}}};
	VkRenderPassBeginInfo pass = {0};
	pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	pass.renderPass =
		vulkan_scene.passes[preserve ? VULKAN_SCENE_RENDER_LOAD : VULKAN_SCENE_RENDER_CLEAR];
	pass.framebuffer = vulkan_scene.framebuffer;
	pass.renderArea.extent = (VkExtent2D){(legacy_u32)width, (legacy_u32)height};
	pass.clearValueCount = 2;
	pass.pClearValues = clear;
	vulkan_scene.api.CmdBeginRenderPass(slot->command, &pass, VK_SUBPASS_CONTENTS_INLINE);
	if (count != 0) {
		legacy_u32 pipeline = classic ? VULKAN_SCENE_PAINTER_PIPELINE
							  : vertices[0].mode == VULKAN_SCENE_SHADOW_MODE
								  ? VULKAN_SCENE_SHADOW_PIPELINE
								  : VULKAN_SCENE_DEPTH_PIPELINE;
		vulkan_context.api.CmdBindPipeline(slot->command, VK_PIPELINE_BIND_POINT_GRAPHICS,
										   vulkan_scene.pipelines[pipeline]);
		VkViewport viewport = {0, 0, (legacy_f32)width, (legacy_f32)height, 0, 1};
		VkRect2D scissor = {{left, top}, {(legacy_u32)(right - left), (legacy_u32)(bottom - top)}};
		vulkan_scene.api.CmdSetViewport(slot->command, 0, 1, &viewport);
		vulkan_scene.api.CmdSetScissor(slot->command, 0, 1, &scissor);
		VkDeviceSize offset = 0;
		vulkan_scene.api.CmdBindVertexBuffers(slot->command, 0, 1, &slot->vertices.buffer, &offset);
		vulkan_context.api.CmdBindDescriptorSets(slot->command, VK_PIPELINE_BIND_POINT_GRAPHICS,
												 vulkan_scene.layout, 0, 1, &slot->descriptor, 0,
												 NULL);
		struct {
			legacy_f32 width, height, depth_scale;
			legacy_u32 reserved;
		} push = {(legacy_f32)width, (legacy_f32)height, 1.0f, 0};
		vulkan_scene.api.CmdPushConstants(slot->command, vulkan_scene.layout,
										  VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
		vulkan_scene.api.CmdDraw(slot->command, (legacy_u32)count, 1, 0, 0);
	}
	vulkan_scene.api.CmdEndRenderPass(slot->command);
	return scene_queue(slot, error, error_capacity);
}

static void scene_read_barrier(VkCommandBuffer command, legacy_s32 before)
{
	VkImageMemoryBarrier image = {0};
	image.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	image.srcAccessMask = before ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT
								 : VK_ACCESS_TRANSFER_READ_BIT;
	image.dstAccessMask = before ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_SHADER_READ_BIT;
	image.oldLayout =
		before ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	image.newLayout =
		before ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	image.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	image.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	image.image = vulkan_scene.color.image;
	image.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image.subresourceRange.levelCount = 1;
	image.subresourceRange.layerCount = 1;
	vulkan_context.api.CmdPipelineBarrier(
		command,
		before
			? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
			: VK_PIPELINE_STAGE_TRANSFER_BIT,
		before ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL,
		0, NULL, 1, &image);
}

legacy_s32 vulkan_scene_readback(legacy_u32 *rgba, legacy_s32 width, legacy_s32 height,
								 legacy_char *error, size_t error_capacity)
{
	if (rgba == NULL || width <= 0 || height <= 0 ||
		width > (legacy_s32)VULKAN_SCENE_TEXTURE_WIDTH ||
		height > (legacy_s32)VULKAN_SCENE_TEXTURE_HEIGHT ||
		!vulkan_scene_initialize(error, error_capacity)) {
		return vulkan_error(error, error_capacity, "Invalid Vulkan scene readback",
							VK_ERROR_INITIALIZATION_FAILED);
	}
	size_t bytes = (size_t)width * height * sizeof(*rgba);
	if (!vulkan_scene_buffer(&vulkan_scene.readback, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, 1,
							 error, error_capacity)) {
		return 0;
	}
	struct VULKAN_SCENE_SLOT *slot = scene_next(error, error_capacity);
	if (slot == NULL) {
		return 0;
	}
	scene_read_barrier(slot->command, 1);
	VkBufferImageCopy copy = {0};
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.layerCount = 1;
	copy.imageExtent = (VkExtent3D){(legacy_u32)width, (legacy_u32)height, 1};
	vulkan_scene.api.CmdCopyImageToBuffer(slot->command, vulkan_scene.color.image,
										  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
										  vulkan_scene.readback.buffer, 1, &copy);
	scene_read_barrier(slot->command, 0);
	VkMemoryBarrier memory = {0};
	memory.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	memory.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	memory.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	vulkan_context.api.CmdPipelineBarrier(slot->command, VK_PIPELINE_STAGE_TRANSFER_BIT,
										  VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &memory, 0, NULL, 0,
										  NULL);
	if (!scene_queue(slot, error, error_capacity) || !scene_wait(slot, error, error_capacity) ||
		!vulkan_scene_flush(&vulkan_scene.readback, 1, error, error_capacity)) {
		return 0;
	}
	memcpy(rgba, vulkan_scene.readback.mapped, bytes);
	return 1;
}

legacy_s32 vulkan_scene_shutdown(void)
{
	vulkan_scene.active = 0;
	if (vulkan_context.device == VK_NULL_HANDLE) {
		vulkan_scene_depth_shutdown();
		memset(&vulkan_scene, 0, sizeof(vulkan_scene));
		return 1;
	}
	for (legacy_u32 slot = 0; slot < VULKAN_SCENE_SLOTS; slot++) {
		if (!scene_wait(&vulkan_scene.slots[slot], NULL, 0) && vulkan_scene.slots[slot].pending) {
			return 0;
		}
	}
	for (legacy_u32 slot = 0; slot < VULKAN_SCENE_SLOTS; slot++) {
		vulkan_scene_release_buffer(&vulkan_scene.slots[slot].vertices);
		vulkan_scene_release_buffer(&vulkan_scene.slots[slot].palette);
		if (vulkan_scene.slots[slot].fence != VK_NULL_HANDLE) {
			vulkan_context.api.DestroyFence(vulkan_context.device, vulkan_scene.slots[slot].fence,
											NULL);
		}
	}
	vulkan_scene_release_buffer(&vulkan_scene.readback);
	if (vulkan_scene.command_pool != VK_NULL_HANDLE) {
		vulkan_context.api.DestroyCommandPool(vulkan_context.device, vulkan_scene.command_pool,
											  NULL);
	}
	for (legacy_u32 index = 0; index < VULKAN_SCENE_PIPELINES; index++) {
		if (vulkan_scene.pipelines[index] != VK_NULL_HANDLE) {
			vulkan_context.api.DestroyPipeline(vulkan_context.device, vulkan_scene.pipelines[index],
											   NULL);
		}
	}
	if (vulkan_scene.vertex_shader != VK_NULL_HANDLE) {
		vulkan_context.api.DestroyShaderModule(vulkan_context.device, vulkan_scene.vertex_shader,
											   NULL);
	}
	if (vulkan_scene.fragment_shader != VK_NULL_HANDLE) {
		vulkan_context.api.DestroyShaderModule(vulkan_context.device, vulkan_scene.fragment_shader,
											   NULL);
	}
	if (vulkan_scene.layout != VK_NULL_HANDLE) {
		vulkan_context.api.DestroyPipelineLayout(vulkan_context.device, vulkan_scene.layout, NULL);
	}
	if (vulkan_scene.descriptor_pool != VK_NULL_HANDLE) {
		vulkan_context.api.DestroyDescriptorPool(vulkan_context.device,
												 vulkan_scene.descriptor_pool, NULL);
	}
	if (vulkan_scene.descriptors != VK_NULL_HANDLE) {
		vulkan_context.api.DestroyDescriptorSetLayout(vulkan_context.device,
													  vulkan_scene.descriptors, NULL);
	}
	if (vulkan_scene.framebuffer != VK_NULL_HANDLE) {
		vulkan_scene.api.DestroyFramebuffer(vulkan_context.device, vulkan_scene.framebuffer, NULL);
	}
	for (legacy_u32 index = 0; index < 2U; index++) {
		if (vulkan_scene.passes[index] != VK_NULL_HANDLE) {
			vulkan_scene.api.DestroyRenderPass(vulkan_context.device, vulkan_scene.passes[index],
											   NULL);
		}
	}
	vulkan_scene_release_image(&vulkan_scene.color);
	vulkan_scene_release_image(&vulkan_scene.depth);
	free(vulkan_scene.cached_vertices);
	free(vulkan_scene.cached_batches);
	vulkan_scene_depth_shutdown();
	memset(&vulkan_scene, 0, sizeof(vulkan_scene));
	return 1;
}
