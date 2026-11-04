#include "vulkan_internal.h"
#include <string.h>

#define VULKAN_DESCRIPTOR_SET_COUNT 1U
#define VULKAN_COMMAND_BUFFER_COUNT 1U
#define VULKAN_DISPATCH_Y 1U
#define VULKAN_DISPATCH_Z 1U
#define VULKAN_MEMORY_LOCAL_SCORE 8U
#define VULKAN_MEMORY_READBACK_CACHE_SCORE 16U
#define VULKAN_MEMORY_PRIMARY_SCORE 4U
#define VULKAN_MEMORY_SECONDARY_SCORE 1U
#define VULKAN_MEMORY_STORAGE 0U
#define VULKAN_MEMORY_UPLOAD 1U
#define VULKAN_MEMORY_READBACK 2U
#define VULKAN_NO_MEMORY_TYPE LEGACY_U32_MAX

static legacy_s32 vulkan_dispatch_error(legacy_char *error, size_t capacity,
										const legacy_char *operation, VkResult result)
{
	vulkan_context.failed = 1;
	if (result == VK_ERROR_DEVICE_LOST) {
		vulkan_context.device_lost = 1;
		vulkan_context.pending = 0;
	}
	return vulkan_error(error, capacity, operation, result);
}

static void vulkan_buffer_release(struct VULKAN_BUFFER *buffer)
{
	struct VULKAN_CONTEXT *context = &vulkan_context;
	if (buffer->mapped != NULL) {
		context->api.UnmapMemory(context->device, buffer->memory);
	}
	if (buffer->buffer != VK_NULL_HANDLE) {
		context->api.DestroyBuffer(context->device, buffer->buffer, NULL);
	}
	if (buffer->memory != VK_NULL_HANDLE) {
		context->api.FreeMemory(context->device, buffer->memory, NULL);
	}
	memset(buffer, 0, sizeof(*buffer));
}

void vulkan_release_dispatch(void)
{
	struct VULKAN_CONTEXT *context = &vulkan_context;
	/* A timed-out submission may still be executing. Its resources must remain
	 * alive until the owner observes fence completion or device loss. */
	if (context->device == VK_NULL_HANDLE || (context->pending && !context->device_lost)) {
		return;
	}
	for (legacy_u32 index = 0; index < VULKAN_STORAGE_BINDING_COUNT; index++) {
		vulkan_buffer_release(&context->buffers[index]);
		vulkan_buffer_release(&context->uploads[index]);
	}
	vulkan_buffer_release(&context->readback);
	if (context->fence != VK_NULL_HANDLE) {
		context->api.DestroyFence(context->device, context->fence, NULL);
		context->fence = VK_NULL_HANDLE;
	}
	if (context->command_pool != VK_NULL_HANDLE) {
		context->api.DestroyCommandPool(context->device, context->command_pool, NULL);
		context->command_pool = VK_NULL_HANDLE;
		context->command_buffer = VK_NULL_HANDLE;
	}
	if (context->descriptor_pool != VK_NULL_HANDLE) {
		context->api.DestroyDescriptorPool(context->device, context->descriptor_pool, NULL);
		context->descriptor_pool = VK_NULL_HANDLE;
		context->descriptor_set = VK_NULL_HANDLE;
	}
}

legacy_s32 vulkan_prepare_dispatch(legacy_char *error, size_t capacity)
{
	struct VULKAN_CONTEXT *context = &vulkan_context;
	/* Discrete adapters must execute from VRAM. Host-visible storage can live
	 * across PCIe, including on Maxwell devices with no host-visible VRAM. */
	context->staged = context->properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
					  context->properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU;
	if (context->descriptor_set != VK_NULL_HANDLE && context->command_pool != VK_NULL_HANDLE &&
		context->fence != VK_NULL_HANDLE) {
		return 1;
	}
	if (context->device == VK_NULL_HANDLE || context->descriptor_layout == VK_NULL_HANDLE ||
		context->descriptor_pool != VK_NULL_HANDLE || context->command_pool != VK_NULL_HANDLE ||
		context->fence != VK_NULL_HANDLE) {
		return vulkan_error(error, capacity, "Incomplete Vulkan dispatch initialization",
							VK_ERROR_INITIALIZATION_FAILED);
	}
	VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
									  VULKAN_STORAGE_BINDING_COUNT};
	VkDescriptorPoolCreateInfo pool = {0};
	pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool.maxSets = VULKAN_DESCRIPTOR_SET_COUNT;
	pool.poolSizeCount = 1;
	pool.pPoolSizes = &pool_size;
	VkResult result =
		context->api.CreateDescriptorPool(context->device, &pool, NULL, &context->descriptor_pool);
	if (result != VK_SUCCESS) {
		context->descriptor_pool = VK_NULL_HANDLE;
		return vulkan_dispatch_error(error, capacity, "Create Vulkan descriptor pool", result);
	}
	VkDescriptorSetAllocateInfo sets = {0};
	sets.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	sets.descriptorPool = context->descriptor_pool;
	sets.descriptorSetCount = VULKAN_DESCRIPTOR_SET_COUNT;
	sets.pSetLayouts = &context->descriptor_layout;
	result = context->api.AllocateDescriptorSets(context->device, &sets, &context->descriptor_set);
	if (result != VK_SUCCESS) {
		return vulkan_dispatch_error(error, capacity, "Allocate Vulkan descriptor set", result);
	}
	VkCommandPoolCreateInfo command_pool = {0};
	command_pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	command_pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	command_pool.queueFamilyIndex = context->queue_family;
	result = context->api.CreateCommandPool(context->device, &command_pool, NULL,
											&context->command_pool);
	if (result != VK_SUCCESS) {
		context->command_pool = VK_NULL_HANDLE;
		return vulkan_dispatch_error(error, capacity, "Create Vulkan command pool", result);
	}
	VkCommandBufferAllocateInfo commands = {0};
	commands.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	commands.commandPool = context->command_pool;
	commands.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	commands.commandBufferCount = VULKAN_COMMAND_BUFFER_COUNT;
	result =
		context->api.AllocateCommandBuffers(context->device, &commands, &context->command_buffer);
	if (result != VK_SUCCESS) {
		return vulkan_dispatch_error(error, capacity, "Allocate Vulkan command buffer", result);
	}
	VkFenceCreateInfo fence = {0};
	fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	result = context->api.CreateFence(context->device, &fence, NULL, &context->fence);
	if (result != VK_SUCCESS) {
		context->fence = VK_NULL_HANDLE;
		return vulkan_dispatch_error(error, capacity, "Create Vulkan completion fence", result);
	}
	return 1;
}

static legacy_u32 vulkan_memory_type(legacy_u32 available, legacy_u32 purpose)
{
	const struct VULKAN_CONTEXT *context = &vulkan_context;
	const VkPhysicalDeviceMemoryProperties *properties = &context->memory_properties;
	VkMemoryPropertyFlags required = purpose == VULKAN_MEMORY_STORAGE && context->staged
										 ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
										 : VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
	legacy_u32 selected = VULKAN_NO_MEMORY_TYPE, selected_score = 0;
	for (legacy_u32 index = 0; index < properties->memoryTypeCount; index++) {
		VkMemoryPropertyFlags flags = properties->memoryTypes[index].propertyFlags;
		if ((available & (1U << index)) == 0 || (flags & required) != required) {
			continue;
		}
		legacy_u32 score = 0;
		if (purpose == VULKAN_MEMORY_STORAGE && context->staged) {
			/* Avoid consuming the small PCIe BAR aperture when private VRAM is
			 * available. The shader never needs CPU access to these allocations. */
			score =
				(flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0 ? VULKAN_MEMORY_LOCAL_SCORE : 0;
		} else {
			VkMemoryPropertyFlags primary = purpose == VULKAN_MEMORY_UPLOAD
												? VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
												: VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
			VkMemoryPropertyFlags secondary = purpose == VULKAN_MEMORY_UPLOAD
												  ? VK_MEMORY_PROPERTY_HOST_CACHED_BIT
												  : VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
			if ((flags & primary) != 0) {
				score += purpose == VULKAN_MEMORY_READBACK ? VULKAN_MEMORY_READBACK_CACHE_SCORE
														   : VULKAN_MEMORY_PRIMARY_SCORE;
			}
			if ((flags & secondary) != 0) {
				score += VULKAN_MEMORY_SECONDARY_SCORE;
			}
			if ((purpose == VULKAN_MEMORY_STORAGE &&
				 (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) ||
				(purpose != VULKAN_MEMORY_STORAGE && context->staged &&
				 (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == 0)) {
				score += VULKAN_MEMORY_LOCAL_SCORE;
			}
		}
		if (selected == VULKAN_NO_MEMORY_TYPE || score > selected_score) {
			selected = index;
			selected_score = score;
		}
	}
	return selected;
}

static legacy_s32 vulkan_buffer_reserve(struct VULKAN_BUFFER *buffer, VkDeviceSize bytes,
										legacy_u32 purpose, VkBufferUsageFlags usage,
										legacy_char *error, size_t capacity)
{
	struct VULKAN_CONTEXT *context = &vulkan_context;
	if (buffer->capacity >= bytes) {
		return 1;
	}
	VkDeviceSize maximum = context->properties.limits.maxStorageBufferRange;
	VkDeviceSize next = buffer->capacity != 0 ? buffer->capacity : VULKAN_FIRST_BUFFER_BYTES;
	while (next < bytes && next <= maximum / 2U) {
		next *= 2U;
	}
	if (next < bytes || next > maximum) {
		next = bytes;
	}
	struct VULKAN_BUFFER replacement = {0};
	VkBufferCreateInfo create = {0};
	create.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	create.size = next;
	create.usage = usage;
	create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VkResult result =
		context->api.CreateBuffer(context->device, &create, NULL, &replacement.buffer);
	if (result != VK_SUCCESS) {
		return vulkan_dispatch_error(error, capacity, "Create Vulkan raster buffer", result);
	}
	VkMemoryRequirements requirements;
	context->api.GetBufferMemoryRequirements(context->device, replacement.buffer, &requirements);
	legacy_u32 memory_type = vulkan_memory_type(requirements.memoryTypeBits, purpose);
	if (memory_type == VULKAN_NO_MEMORY_TYPE || requirements.size > (VkDeviceSize)(size_t)-1) {
		vulkan_buffer_release(&replacement);
		return vulkan_dispatch_error(error, capacity, "Find Vulkan raster memory",
									 VK_ERROR_FEATURE_NOT_PRESENT);
	}
	VkMemoryAllocateInfo allocation = {0};
	allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocation.allocationSize = requirements.size;
	allocation.memoryTypeIndex = memory_type;
	result = context->api.AllocateMemory(context->device, &allocation, NULL, &replacement.memory);
	if (result != VK_SUCCESS) {
		replacement.memory = VK_NULL_HANDLE;
		vulkan_buffer_release(&replacement);
		return vulkan_dispatch_error(error, capacity, "Allocate Vulkan raster memory", result);
	}
	result =
		context->api.BindBufferMemory(context->device, replacement.buffer, replacement.memory, 0);
	legacy_s32 mapped = purpose != VULKAN_MEMORY_STORAGE || !context->staged;
	if (result == VK_SUCCESS && mapped) {
		result = context->api.MapMemory(context->device, replacement.memory, 0, VK_WHOLE_SIZE, 0,
										&replacement.mapped);
	}
	if (result != VK_SUCCESS) {
		replacement.mapped = NULL;
		vulkan_buffer_release(&replacement);
		return vulkan_dispatch_error(error, capacity, "Bind and map Vulkan raster memory", result);
	}
	replacement.capacity = next;
	replacement.allocation_size = requirements.size;
	replacement.coherent = (context->memory_properties.memoryTypes[memory_type].propertyFlags &
							VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
	vulkan_buffer_release(buffer);
	*buffer = replacement;
	return 1;
}

static legacy_s32 vulkan_reserve_buffers(const VkDeviceSize bytes[VULKAN_STORAGE_BINDING_COUNT],
										 legacy_char *error, size_t capacity)
{
	struct VULKAN_CONTEXT *context = &vulkan_context;
	for (legacy_u32 index = 0; index < VULKAN_STORAGE_BINDING_COUNT; index++) {
		VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		if (context->staged) {
			usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			if (index == VULKAN_OUTPUT_BINDING) {
				usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			}
			if (!vulkan_buffer_reserve(&context->uploads[index], bytes[index], VULKAN_MEMORY_UPLOAD,
									   VK_BUFFER_USAGE_TRANSFER_SRC_BIT, error, capacity)) {
				return 0;
			}
		}
		if (!vulkan_buffer_reserve(&context->buffers[index], bytes[index], VULKAN_MEMORY_STORAGE,
								   usage, error, capacity)) {
			return 0;
		}
	}
	return !context->staged ||
		   vulkan_buffer_reserve(&context->readback, bytes[VULKAN_OUTPUT_BINDING],
								 VULKAN_MEMORY_READBACK, VK_BUFFER_USAGE_TRANSFER_DST_BIT, error,
								 capacity);
}

static legacy_s32 vulkan_flush_buffers(legacy_char *error, size_t capacity)
{
	struct VULKAN_CONTEXT *context = &vulkan_context;
	VkMappedMemoryRange ranges[VULKAN_STORAGE_BINDING_COUNT] = {{0}};
	legacy_u32 count = 0;
	for (legacy_u32 index = 0; index < VULKAN_STORAGE_BINDING_COUNT; index++) {
		const struct VULKAN_BUFFER *buffer =
			context->staged ? &context->uploads[index] : &context->buffers[index];
		if (!buffer->coherent) {
			VkMappedMemoryRange *range = &ranges[count++];
			range->sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
			range->memory = buffer->memory;
			range->size = VK_WHOLE_SIZE;
		}
	}
	if (count != 0) {
		VkResult result = context->api.FlushMappedMemoryRanges(context->device, count, ranges);
		if (result != VK_SUCCESS) {
			return vulkan_dispatch_error(error, capacity, "Flush Vulkan raster uploads", result);
		}
	}
	return 1;
}

static VkBufferMemoryBarrier vulkan_buffer_barrier(const struct VULKAN_BUFFER *buffer,
												   VkDeviceSize bytes, VkAccessFlags source,
												   VkAccessFlags destination)
{
	VkBufferMemoryBarrier barrier = {0};
	barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	barrier.srcAccessMask = source;
	barrier.dstAccessMask = destination;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.buffer = buffer->buffer;
	barrier.size = bytes;
	return barrier;
}

static legacy_s32 vulkan_record_dispatch(const VkDeviceSize bytes[VULKAN_STORAGE_BINDING_COUNT],
										 legacy_u32 groups, legacy_char *error, size_t capacity)
{
	struct VULKAN_CONTEXT *context = &vulkan_context;
	VkDescriptorBufferInfo buffers[VULKAN_STORAGE_BINDING_COUNT] = {{0}};
	VkWriteDescriptorSet writes[VULKAN_STORAGE_BINDING_COUNT] = {{0}};
	VkBufferMemoryBarrier barriers[VULKAN_STORAGE_BINDING_COUNT] = {{0}};
	for (legacy_u32 index = 0; index < VULKAN_STORAGE_BINDING_COUNT; index++) {
		buffers[index].buffer = context->buffers[index].buffer;
		buffers[index].range = bytes[index];
		writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[index].dstSet = context->descriptor_set;
		writes[index].dstBinding = index;
		writes[index].descriptorCount = 1;
		writes[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[index].pBufferInfo = &buffers[index];
		const struct VULKAN_BUFFER *upload =
			context->staged ? &context->uploads[index] : &context->buffers[index];
		barriers[index] = vulkan_buffer_barrier(upload, bytes[index], VK_ACCESS_HOST_WRITE_BIT,
												context->staged ? VK_ACCESS_TRANSFER_READ_BIT
																: VK_ACCESS_SHADER_READ_BIT |
																	  VK_ACCESS_SHADER_WRITE_BIT);
	}
	context->api.UpdateDescriptorSets(context->device, VULKAN_STORAGE_BINDING_COUNT, writes, 0,
									  NULL);
	VkResult result = context->api.ResetCommandBuffer(context->command_buffer, 0);
	if (result != VK_SUCCESS) {
		return vulkan_dispatch_error(error, capacity, "Reset Vulkan command buffer", result);
	}
	VkCommandBufferBeginInfo begin = {0};
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = context->api.BeginCommandBuffer(context->command_buffer, &begin);
	if (result != VK_SUCCESS) {
		return vulkan_dispatch_error(error, capacity, "Begin Vulkan command buffer", result);
	}
	context->api.CmdPipelineBarrier(context->command_buffer, VK_PIPELINE_STAGE_HOST_BIT,
									context->staged ? VK_PIPELINE_STAGE_TRANSFER_BIT
													: VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
									0, 0, NULL, VULKAN_STORAGE_BINDING_COUNT, barriers, 0, NULL);
	if (context->staged) {
		for (legacy_u32 index = 0; index < VULKAN_STORAGE_BINDING_COUNT; index++) {
			VkBufferCopy copy = {0, 0, bytes[index]};
			context->api.CmdCopyBuffer(context->command_buffer, context->uploads[index].buffer,
									   context->buffers[index].buffer, 1, &copy);
			barriers[index] = vulkan_buffer_barrier(
				&context->buffers[index], bytes[index], VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
		}
		context->api.CmdPipelineBarrier(context->command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
										VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL,
										VULKAN_STORAGE_BINDING_COUNT, barriers, 0, NULL);
	}
	context->api.CmdBindPipeline(context->command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
								 context->pipeline);
	context->api.CmdBindDescriptorSets(context->command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
									   context->pipeline_layout, 0, VULKAN_DESCRIPTOR_SET_COUNT,
									   &context->descriptor_set, 0, NULL);
	context->api.CmdDispatch(context->command_buffer, groups, VULKAN_DISPATCH_Y, VULKAN_DISPATCH_Z);
	VkBufferMemoryBarrier output;
	if (context->staged) {
		/* Include initialization copies: clipped pixels can be untouched by the
		 * shader but their caller-provided output words must still survive. */
		output = vulkan_buffer_barrier(
			&context->buffers[VULKAN_OUTPUT_BINDING], bytes[VULKAN_OUTPUT_BINDING],
			VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
		context->api.CmdPipelineBarrier(
			context->command_buffer,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1, &output, 0, NULL);
		VkBufferCopy copy = {0, 0, bytes[VULKAN_OUTPUT_BINDING]};
		context->api.CmdCopyBuffer(context->command_buffer,
								   context->buffers[VULKAN_OUTPUT_BINDING].buffer,
								   context->readback.buffer, 1, &copy);
		output = vulkan_buffer_barrier(&context->readback, bytes[VULKAN_OUTPUT_BINDING],
									   VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT);
		context->api.CmdPipelineBarrier(context->command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
										VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &output, 0,
										NULL);
	} else {
		output = vulkan_buffer_barrier(&context->buffers[VULKAN_OUTPUT_BINDING],
									   bytes[VULKAN_OUTPUT_BINDING], VK_ACCESS_SHADER_WRITE_BIT,
									   VK_ACCESS_HOST_READ_BIT);
		context->api.CmdPipelineBarrier(
			context->command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &output, 0, NULL);
	}
	result = context->api.EndCommandBuffer(context->command_buffer);
	if (result != VK_SUCCESS) {
		return vulkan_dispatch_error(error, capacity, "Finish Vulkan command buffer", result);
	}
	return 1;
}

legacy_s32 vulkan_dispatch(const legacy_u32 *input, size_t input_words, legacy_u32 *output,
						   size_t output_words, legacy_u32 invocation_count, legacy_char *error,
						   size_t error_capacity)
{
	struct VULKAN_CONTEXT *context = &vulkan_context;
	if (context->device == VK_NULL_HANDLE || context->pipeline == VK_NULL_HANDLE ||
		context->failed || context->pending || context->device_lost) {
		return vulkan_error(error, error_capacity, "Vulkan renderer is not ready",
							VK_ERROR_INITIALIZATION_FAILED);
	}
	if (invocation_count == 0) {
		return 1;
	}
	if (input == NULL || output == NULL || input_words == 0 || output_words == 0 ||
		input_words > (size_t)-1 / sizeof(*input) || output_words > (size_t)-1 / sizeof(*output)) {
		return vulkan_error(error, error_capacity, "Invalid Vulkan dispatch buffers",
							VK_ERROR_INITIALIZATION_FAILED);
	}
	VkDeviceSize bytes[VULKAN_STORAGE_BINDING_COUNT] = {
		(VkDeviceSize)(input_words * sizeof(*input)),
		(VkDeviceSize)(output_words * sizeof(*output))};
	legacy_u32 groups = invocation_count / VULKAN_LOCAL_SIZE;
	if (invocation_count % VULKAN_LOCAL_SIZE != 0) {
		groups++;
	}
	if (bytes[VULKAN_INPUT_BINDING] > context->properties.limits.maxStorageBufferRange ||
		bytes[VULKAN_OUTPUT_BINDING] > context->properties.limits.maxStorageBufferRange ||
		groups > context->properties.limits.maxComputeWorkGroupCount[0]) {
		return vulkan_error(error, error_capacity, "Vulkan scene exceeds device storage limits",
							VK_ERROR_FEATURE_NOT_PRESENT);
	}
	if (!vulkan_prepare_dispatch(error, error_capacity) ||
		!vulkan_reserve_buffers(bytes, error, error_capacity)) {
		return 0;
	}
	struct VULKAN_BUFFER *uploads = context->staged ? context->uploads : context->buffers;
	memcpy(uploads[VULKAN_INPUT_BINDING].mapped, input, (size_t)bytes[VULKAN_INPUT_BINDING]);
	memcpy(uploads[VULKAN_OUTPUT_BINDING].mapped, output, (size_t)bytes[VULKAN_OUTPUT_BINDING]);
	if (!vulkan_flush_buffers(error, error_capacity) ||
		!vulkan_record_dispatch(bytes, groups, error, error_capacity)) {
		return 0;
	}
	VkResult result = context->api.ResetFences(context->device, 1, &context->fence);
	if (result != VK_SUCCESS) {
		return vulkan_dispatch_error(error, error_capacity, "Reset Vulkan completion fence",
									 result);
	}
	VkSubmitInfo submit = {0};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = VULKAN_COMMAND_BUFFER_COUNT;
	submit.pCommandBuffers = &context->command_buffer;
	result = context->api.QueueSubmit(context->queue, 1, &submit, context->fence);
	if (result != VK_SUCCESS) {
		return vulkan_dispatch_error(error, error_capacity, "Submit Vulkan raster commands",
									 result);
	}
	context->pending = 1;
	result = context->api.WaitForFences(context->device, 1, &context->fence, VK_TRUE,
										VULKAN_FENCE_TIMEOUT_NS);
	if (result != VK_SUCCESS) {
		return vulkan_dispatch_error(error, error_capacity, "Wait for Vulkan raster completion",
									 result);
	}
	context->pending = 0;
	const struct VULKAN_BUFFER *readback =
		context->staged ? &context->readback : &context->buffers[VULKAN_OUTPUT_BINDING];
	if (!readback->coherent) {
		VkMappedMemoryRange invalidate = {0};
		invalidate.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		invalidate.memory = readback->memory;
		invalidate.size = VK_WHOLE_SIZE;
		result = context->api.InvalidateMappedMemoryRanges(context->device, 1, &invalidate);
		if (result != VK_SUCCESS) {
			return vulkan_dispatch_error(error, error_capacity, "Invalidate Vulkan readback memory",
										 result);
		}
	}
	memcpy(output, readback->mapped, (size_t)bytes[VULKAN_OUTPUT_BINDING]);
	return 1;
}
