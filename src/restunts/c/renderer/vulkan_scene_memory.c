#include "vulkan_scene_internal.h"
#include <string.h>

#define SCENE_NO_MEMORY_TYPE LEGACY_U32_MAX
#define SCENE_HOST_COHERENT_SCORE 4U
#define SCENE_HOST_CACHED_SCORE 2U
#define SCENE_DEVICE_LOCAL_SCORE 1U

static legacy_u32 scene_memory_type(legacy_u32 available, VkMemoryPropertyFlags required,
									legacy_s32 cached)
{
	legacy_u32 best = SCENE_NO_MEMORY_TYPE, best_score = 0;
	const VkPhysicalDeviceMemoryProperties *properties = &vulkan_context.memory_properties;
	for (legacy_u32 index = 0; index < properties->memoryTypeCount; index++) {
		VkMemoryPropertyFlags flags = properties->memoryTypes[index].propertyFlags;
		if ((available & (1U << index)) == 0 || (flags & required) != required) {
			continue;
		}
		legacy_u32 score =
			(flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0 ? SCENE_HOST_COHERENT_SCORE : 0;
		if (cached && (flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) != 0) {
			score += SCENE_HOST_CACHED_SCORE * SCENE_HOST_COHERENT_SCORE;
		}
		if ((flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0) {
			score += SCENE_DEVICE_LOCAL_SCORE;
		}
		if (best == SCENE_NO_MEMORY_TYPE || score > best_score) {
			best = index;
			best_score = score;
		}
	}
	return best;
}

void vulkan_scene_release_buffer(struct VULKAN_BUFFER *buffer)
{
	if (buffer->mapped != NULL) {
		vulkan_context.api.UnmapMemory(vulkan_context.device, buffer->memory);
	}
	if (buffer->buffer != VK_NULL_HANDLE) {
		vulkan_context.api.DestroyBuffer(vulkan_context.device, buffer->buffer, NULL);
	}
	if (buffer->memory != VK_NULL_HANDLE) {
		vulkan_context.api.FreeMemory(vulkan_context.device, buffer->memory, NULL);
	}
	memset(buffer, 0, sizeof(*buffer));
}

legacy_s32 vulkan_scene_buffer(struct VULKAN_BUFFER *buffer, size_t bytes, VkBufferUsageFlags usage,
							   legacy_s32 cached, legacy_char *error, size_t capacity)
{
	if (buffer->capacity >= bytes) {
		return 1;
	}
	VkDeviceSize next = buffer->capacity != 0 ? buffer->capacity : VULKAN_FIRST_BUFFER_BYTES;
	while (next < bytes && next <= (VkDeviceSize)(size_t)-1 / 2U) {
		next *= 2U;
	}
	struct VULKAN_BUFFER replacement = {0};
	VkBufferCreateInfo create = {0};
	create.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	create.size = next;
	create.usage = usage;
	create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VkResult result =
		vulkan_context.api.CreateBuffer(vulkan_context.device, &create, NULL, &replacement.buffer);
	if (result != VK_SUCCESS) {
		return vulkan_error(error, capacity, "Create Vulkan scene transfer buffer", result);
	}
	VkMemoryRequirements requirements;
	vulkan_context.api.GetBufferMemoryRequirements(vulkan_context.device, replacement.buffer,
												   &requirements);
	legacy_u32 type =
		scene_memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, cached);
	if (type == SCENE_NO_MEMORY_TYPE || requirements.size > (VkDeviceSize)(size_t)-1) {
		vulkan_scene_release_buffer(&replacement);
		return vulkan_error(error, capacity, "Find Vulkan scene transfer memory",
							VK_ERROR_FEATURE_NOT_PRESENT);
	}
	VkMemoryAllocateInfo allocate = {0};
	allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate.allocationSize = requirements.size;
	allocate.memoryTypeIndex = type;
	result = vulkan_context.api.AllocateMemory(vulkan_context.device, &allocate, NULL,
											   &replacement.memory);
	if (result == VK_SUCCESS) {
		result = vulkan_context.api.BindBufferMemory(vulkan_context.device, replacement.buffer,
													 replacement.memory, 0);
	}
	if (result == VK_SUCCESS) {
		result = vulkan_context.api.MapMemory(vulkan_context.device, replacement.memory, 0,
											  VK_WHOLE_SIZE, 0, &replacement.mapped);
	}
	if (result != VK_SUCCESS) {
		vulkan_scene_release_buffer(&replacement);
		return vulkan_error(error, capacity, "Allocate Vulkan scene transfer memory", result);
	}
	replacement.capacity = next;
	replacement.allocation_size = requirements.size;
	replacement.coherent = (vulkan_context.memory_properties.memoryTypes[type].propertyFlags &
							VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
	vulkan_scene_release_buffer(buffer);
	*buffer = replacement;
	return 1;
}

legacy_s32 vulkan_scene_flush(struct VULKAN_BUFFER *buffer, legacy_s32 invalidate,
							  legacy_char *error, size_t capacity)
{
	if (buffer->coherent) {
		return 1;
	}
	VkMappedMemoryRange range = {0};
	range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
	range.memory = buffer->memory;
	range.size = VK_WHOLE_SIZE;
	VkResult result =
		invalidate
			? vulkan_context.api.InvalidateMappedMemoryRanges(vulkan_context.device, 1, &range)
			: vulkan_context.api.FlushMappedMemoryRanges(vulkan_context.device, 1, &range);
	return result == VK_SUCCESS
			   ? 1
			   : vulkan_error(error, capacity, "Synchronize Vulkan scene transfer memory", result);
}

void vulkan_scene_release_image(struct VULKAN_SCENE_IMAGE *image)
{
	if (image->view != VK_NULL_HANDLE) {
		vulkan_scene.api.DestroyImageView(vulkan_context.device, image->view, NULL);
	}
	if (image->image != VK_NULL_HANDLE) {
		vulkan_scene.api.DestroyImage(vulkan_context.device, image->image, NULL);
	}
	if (image->memory != VK_NULL_HANDLE) {
		vulkan_context.api.FreeMemory(vulkan_context.device, image->memory, NULL);
	}
	memset(image, 0, sizeof(*image));
}

static legacy_s32 scene_create_image(struct VULKAN_SCENE_IMAGE *image, VkFormat format,
									 VkImageUsageFlags usage, VkImageAspectFlags aspect,
									 legacy_char *error, size_t capacity)
{
	VkImageCreateInfo create = {0};
	create.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	create.imageType = VK_IMAGE_TYPE_2D;
	create.format = format;
	create.extent = (VkExtent3D){VULKAN_SCENE_TEXTURE_WIDTH, VULKAN_SCENE_TEXTURE_HEIGHT, 1};
	create.mipLevels = 1;
	create.arrayLayers = 1;
	create.samples = VK_SAMPLE_COUNT_1_BIT;
	create.tiling = VK_IMAGE_TILING_OPTIMAL;
	create.usage = usage;
	create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VkResult result =
		vulkan_scene.api.CreateImage(vulkan_context.device, &create, NULL, &image->image);
	if (result != VK_SUCCESS) {
		return vulkan_error(error, capacity, "Create Vulkan scene image", result);
	}
	VkMemoryRequirements requirements;
	vulkan_scene.api.GetImageMemoryRequirements(vulkan_context.device, image->image, &requirements);
	legacy_u32 type =
		scene_memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
	if (type == SCENE_NO_MEMORY_TYPE) {
		return vulkan_error(error, capacity, "Find Vulkan scene image memory",
							VK_ERROR_FEATURE_NOT_PRESENT);
	}
	VkMemoryAllocateInfo allocate = {0};
	allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate.allocationSize = requirements.size;
	allocate.memoryTypeIndex = type;
	result =
		vulkan_context.api.AllocateMemory(vulkan_context.device, &allocate, NULL, &image->memory);
	if (result == VK_SUCCESS) {
		result =
			vulkan_scene.api.BindImageMemory(vulkan_context.device, image->image, image->memory, 0);
	}
	if (result != VK_SUCCESS) {
		return vulkan_error(error, capacity, "Allocate Vulkan scene image memory", result);
	}
	VkImageViewCreateInfo view = {0};
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.image = image->image;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = format;
	view.subresourceRange.aspectMask = aspect;
	view.subresourceRange.levelCount = 1;
	view.subresourceRange.layerCount = 1;
	result = vulkan_scene.api.CreateImageView(vulkan_context.device, &view, NULL, &image->view);
	return result == VK_SUCCESS ? 1
								: vulkan_error(error, capacity, "View Vulkan scene image", result);
}

legacy_s32 vulkan_scene_create_targets(legacy_char *error, size_t capacity)
{
	PFN_vkGetPhysicalDeviceFormatProperties properties =
		(PFN_vkGetPhysicalDeviceFormatProperties)vulkan_context.api.GetInstanceProcAddr(
			vulkan_context.instance, "vkGetPhysicalDeviceFormatProperties");
	if (properties == NULL) {
		return vulkan_error(error, capacity, "Query Vulkan depth formats",
							VK_ERROR_INITIALIZATION_FAILED);
	}
	VkFormatProperties format;
	properties(vulkan_context.physical_device, VK_FORMAT_D32_SFLOAT, &format);
	vulkan_scene.depth_format =
		(format.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0
			? VK_FORMAT_D32_SFLOAT
			: VK_FORMAT_D16_UNORM;
	return scene_create_image(&vulkan_scene.color, VK_FORMAT_R8G8B8A8_UNORM,
							  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
								  VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
							  VK_IMAGE_ASPECT_COLOR_BIT, error, capacity) &&
		   scene_create_image(&vulkan_scene.depth, vulkan_scene.depth_format,
							  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
							  VK_IMAGE_ASPECT_DEPTH_BIT, error, capacity);
}
