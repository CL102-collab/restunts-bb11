#include "../c/renderer/vulkan_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Exercise the real transport with synthetic discrete/integrated memory heaps.
 * Fake commands check synchronization and execute copies and a small kernel;
 * this does not require a Vulkan driver or the full game. */
#include "../c/renderer/vulkan_dispatch.c"

#define TEST_OBJECT_CAPACITY 64U
#define TEST_MEMORY_TYPES 4U
#define TEST_PRIVATE_MEMORY 0U
#define TEST_UPLOAD_MEMORY 1U
#define TEST_READBACK_MEMORY 2U
#define TEST_BAR_MEMORY 3U
#define TEST_ALL_MEMORY_TYPES ((1U << TEST_MEMORY_TYPES) - 1U)
#define TEST_INPUT_WORDS 17U
#define TEST_OUTPUT_WORDS 130U
#define TEST_GROWN_WORDS 2051U
#define TEST_INITIAL_VALUE 200U
#define TEST_KERNEL_STRIDE 3U
#define TEST_HANDLE(type, value) ((type)(size_t)(value))

struct VULKAN_CONTEXT vulkan_context;

struct TEST_MEMORY {
	legacy_u8 *bytes;
	VkDeviceSize size;
	legacy_u32 type;
	legacy_s32 mapped;
};

struct TEST_BUFFER {
	VkDeviceSize size;
	VkBufferUsageFlags usage;
	VkDeviceMemory memory;
	VkAccessFlags access;
	legacy_s32 alive;
};

static struct TEST_MEMORY memories[TEST_OBJECT_CAPACITY];
static struct TEST_BUFFER buffers[TEST_OBJECT_CAPACITY];
static VkDescriptorBufferInfo bindings[VULKAN_STORAGE_BINDING_COUNT];
static legacy_u32 buffer_count, allocation_count, allocation_calls;
static legacy_u32 copy_count, map_count, flush_count, invalidate_count;
static legacy_u32 live_buffers, live_memories, fail_allocation, allowed_memory_types;
static VkResult wait_result;

static struct TEST_BUFFER *test_buffer(VkBuffer handle)
{
	size_t index = (size_t)handle - 1U;
	assert(index < buffer_count && buffers[index].alive);
	return &buffers[index];
}

static struct TEST_MEMORY *test_memory(VkDeviceMemory handle)
{
	size_t index = (size_t)handle - 1U;
	assert(index < allocation_count && memories[index].bytes != NULL);
	return &memories[index];
}

static legacy_u8 *test_bytes(VkBuffer handle)
{
	return test_memory(test_buffer(handle)->memory)->bytes;
}

legacy_s32 vulkan_error(legacy_char *error, size_t capacity, const legacy_char *operation,
						VkResult result)
{
	if (error != NULL && capacity != 0) {
		snprintf(error, capacity, "%s: %d", operation, (legacy_int)result);
	}
	return 0;
}

static VKAPI_ATTR VkResult VKAPI_CALL test_create_buffer(VkDevice device,
														 const VkBufferCreateInfo *create,
														 const VkAllocationCallbacks *allocator,
														 VkBuffer *handle)
{
	(void)device;
	(void)allocator;
	assert(buffer_count < TEST_OBJECT_CAPACITY);
	struct TEST_BUFFER *buffer = &buffers[buffer_count++];
	buffer->size = create->size;
	buffer->usage = create->usage;
	buffer->alive = 1;
	*handle = TEST_HANDLE(VkBuffer, buffer_count);
	live_buffers++;
	return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL test_destroy_buffer(VkDevice device, VkBuffer handle,
													  const VkAllocationCallbacks *allocator)
{
	(void)device;
	(void)allocator;
	test_buffer(handle)->alive = 0;
	live_buffers--;
}

static VKAPI_ATTR void VKAPI_CALL test_requirements(VkDevice device, VkBuffer handle,
													VkMemoryRequirements *requirements)
{
	(void)device;
	requirements->size = test_buffer(handle)->size;
	requirements->alignment = sizeof(legacy_u32);
	requirements->memoryTypeBits = allowed_memory_types;
}

static VKAPI_ATTR VkResult VKAPI_CALL test_allocate(VkDevice device,
													const VkMemoryAllocateInfo *allocation,
													const VkAllocationCallbacks *allocator,
													VkDeviceMemory *handle)
{
	(void)device;
	(void)allocator;
	allocation_calls++;
	if (allocation_calls == fail_allocation) {
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	}
	assert(allocation_count < TEST_OBJECT_CAPACITY);
	assert(allocation->memoryTypeIndex < TEST_MEMORY_TYPES);
	struct TEST_MEMORY *memory = &memories[allocation_count++];
	memory->bytes = calloc(1, (size_t)allocation->allocationSize);
	assert(memory->bytes != NULL);
	memory->size = allocation->allocationSize;
	memory->type = allocation->memoryTypeIndex;
	*handle = TEST_HANDLE(VkDeviceMemory, allocation_count);
	live_memories++;
	return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL test_free(VkDevice device, VkDeviceMemory handle,
											const VkAllocationCallbacks *allocator)
{
	(void)device;
	(void)allocator;
	struct TEST_MEMORY *memory = test_memory(handle);
	assert(!memory->mapped);
	free(memory->bytes);
	memory->bytes = NULL;
	live_memories--;
}

static VKAPI_ATTR VkResult VKAPI_CALL test_bind(VkDevice device, VkBuffer handle,
												VkDeviceMemory memory, VkDeviceSize offset)
{
	(void)device;
	assert(offset == 0 && test_memory(memory)->size >= test_buffer(handle)->size);
	test_buffer(handle)->memory = memory;
	return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL test_map(VkDevice device, VkDeviceMemory handle,
											   VkDeviceSize offset, VkDeviceSize size,
											   VkMemoryMapFlags flags, void **pointer)
{
	(void)device;
	(void)flags;
	struct TEST_MEMORY *memory = test_memory(handle);
	assert(vulkan_context.memory_properties.memoryTypes[memory->type].propertyFlags &
		   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
	assert(offset == 0 && (size == VK_WHOLE_SIZE || size <= memory->size));
	memory->mapped = 1;
	*pointer = memory->bytes;
	map_count++;
	return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL test_unmap(VkDevice device, VkDeviceMemory handle)
{
	(void)device;
	assert(test_memory(handle)->mapped);
	test_memory(handle)->mapped = 0;
}

static VKAPI_ATTR VkResult VKAPI_CALL test_flush(VkDevice device, legacy_u32 count,
												 const VkMappedMemoryRange *ranges)
{
	(void)device;
	for (legacy_u32 index = 0; index < count; index++) {
		assert(test_memory(ranges[index].memory)->mapped);
		assert(ranges[index].offset == 0 && ranges[index].size == VK_WHOLE_SIZE);
	}
	flush_count += count;
	return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL test_invalidate(VkDevice device, legacy_u32 count,
													  const VkMappedMemoryRange *ranges)
{
	(void)device;
	for (legacy_u32 index = 0; index < count; index++) {
		assert(test_memory(ranges[index].memory)->mapped);
		assert(ranges[index].offset == 0 && ranges[index].size == VK_WHOLE_SIZE);
	}
	invalidate_count += count;
	return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL test_descriptors(VkDevice device, legacy_u32 count,
												   const VkWriteDescriptorSet *writes,
												   legacy_u32 copy_count,
												   const VkCopyDescriptorSet *copies)
{
	(void)device;
	(void)copies;
	assert(count == VULKAN_STORAGE_BINDING_COUNT && copy_count == 0);
	for (legacy_u32 index = 0; index < count; index++) {
		assert(writes[index].dstBinding < VULKAN_STORAGE_BINDING_COUNT);
		bindings[writes[index].dstBinding] = *writes[index].pBufferInfo;
		assert(test_buffer(bindings[writes[index].dstBinding].buffer)->usage &
			   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	}
}

static VKAPI_ATTR VkResult VKAPI_CALL test_reset_commands(VkCommandBuffer command,
														  VkCommandBufferResetFlags flags)
{
	(void)command;
	(void)flags;
	return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL test_begin_commands(VkCommandBuffer command,
														  const VkCommandBufferBeginInfo *begin)
{
	(void)command;
	(void)begin;
	for (legacy_u32 index = 0; index < buffer_count; index++) {
		buffers[index].access = 0;
	}
	return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL
test_barrier(VkCommandBuffer command, VkPipelineStageFlags source, VkPipelineStageFlags destination,
			 VkDependencyFlags flags, legacy_u32 memory_count, const VkMemoryBarrier *memory,
			 legacy_u32 buffer_barrier_count, const VkBufferMemoryBarrier *barriers,
			 legacy_u32 image_count, const VkImageMemoryBarrier *images)
{
	(void)command;
	(void)flags;
	(void)memory;
	(void)images;
	assert(memory_count == 0 && image_count == 0);
	for (legacy_u32 index = 0; index < buffer_barrier_count; index++) {
		const VkBufferMemoryBarrier *barrier = &barriers[index];
		assert(barrier->srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED &&
			   barrier->dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED);
		if (barrier->srcAccessMask & VK_ACCESS_HOST_WRITE_BIT) {
			assert(source & VK_PIPELINE_STAGE_HOST_BIT);
		}
		if (barrier->srcAccessMask & VK_ACCESS_SHADER_WRITE_BIT) {
			assert(source & VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		}
		if (barrier->srcAccessMask & VK_ACCESS_TRANSFER_WRITE_BIT) {
			assert(source & VK_PIPELINE_STAGE_TRANSFER_BIT);
		}
		if (barrier->dstAccessMask & VK_ACCESS_HOST_READ_BIT) {
			assert(destination & VK_PIPELINE_STAGE_HOST_BIT);
		}
		if (barrier->dstAccessMask & (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)) {
			assert(destination & VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		}
		if (barrier->dstAccessMask & VK_ACCESS_TRANSFER_READ_BIT) {
			assert(destination & VK_PIPELINE_STAGE_TRANSFER_BIT);
		}
		test_buffer(barrier->buffer)->access = barrier->dstAccessMask;
	}
}

static VKAPI_ATTR void VKAPI_CALL test_copy(VkCommandBuffer command, VkBuffer source,
											VkBuffer destination, legacy_u32 count,
											const VkBufferCopy *copies)
{
	(void)command;
	struct TEST_BUFFER *input = test_buffer(source), *output = test_buffer(destination);
	assert(input->usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
	assert(output->usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT);
	assert(input->access & VK_ACCESS_TRANSFER_READ_BIT);
	for (legacy_u32 index = 0; index < count; index++) {
		const VkBufferCopy *copy = &copies[index];
		assert(copy->srcOffset + copy->size <= input->size);
		assert(copy->dstOffset + copy->size <= output->size);
		memcpy(test_bytes(destination) + copy->dstOffset, test_bytes(source) + copy->srcOffset,
			   (size_t)copy->size);
	}
	output->access = 0;
	copy_count += count;
}

static VKAPI_ATTR void VKAPI_CALL test_bind_pipeline(VkCommandBuffer command,
													 VkPipelineBindPoint point, VkPipeline pipeline)
{
	(void)command;
	(void)pipeline;
	assert(point == VK_PIPELINE_BIND_POINT_COMPUTE);
}

static VKAPI_ATTR void VKAPI_CALL
test_bind_descriptors(VkCommandBuffer command, VkPipelineBindPoint point, VkPipelineLayout layout,
					  legacy_u32 first, legacy_u32 count, const VkDescriptorSet *sets,
					  legacy_u32 offset_count, const legacy_u32 *offsets)
{
	(void)command;
	(void)layout;
	(void)sets;
	(void)offsets;
	assert(point == VK_PIPELINE_BIND_POINT_COMPUTE && first == 0 && count == 1 &&
		   offset_count == 0);
}

static VKAPI_ATTR void VKAPI_CALL test_dispatch(VkCommandBuffer command, legacy_u32 groups,
												legacy_u32 height, legacy_u32 depth)
{
	(void)command;
	assert(groups != 0 && height == 1 && depth == 1);
	assert(test_buffer(bindings[VULKAN_INPUT_BINDING].buffer)->access & VK_ACCESS_SHADER_READ_BIT);
	struct TEST_BUFFER *output_buffer = test_buffer(bindings[VULKAN_OUTPUT_BINDING].buffer);
	assert((output_buffer->access & (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)) ==
		   (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT));
	legacy_u32 *input = (legacy_u32 *)test_bytes(bindings[VULKAN_INPUT_BINDING].buffer);
	legacy_u32 *output = (legacy_u32 *)test_bytes(bindings[VULKAN_OUTPUT_BINDING].buffer);
	size_t input_words = (size_t)bindings[VULKAN_INPUT_BINDING].range / sizeof(*input);
	size_t output_words = (size_t)bindings[VULKAN_OUTPUT_BINDING].range / sizeof(*output);
	for (size_t index = 0; index < output_words; index += TEST_KERNEL_STRIDE) {
		output[index] += input[index % input_words];
	}
	output_buffer->access = 0;
}

static VKAPI_ATTR VkResult VKAPI_CALL test_end_commands(VkCommandBuffer command)
{
	(void)command;
	return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL test_reset_fences(VkDevice device, legacy_u32 count,
														const VkFence *fences)
{
	(void)device;
	(void)fences;
	assert(count == 1);
	return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL test_submit(VkQueue queue, legacy_u32 count,
												  const VkSubmitInfo *submits, VkFence fence)
{
	(void)queue;
	(void)fence;
	assert(count == 1 && submits[0].commandBufferCount == 1);
	VkBuffer readback = vulkan_context.staged ? vulkan_context.readback.buffer
											  : bindings[VULKAN_OUTPUT_BINDING].buffer;
	assert(test_buffer(readback)->access & VK_ACCESS_HOST_READ_BIT);
	return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL test_wait(VkDevice device, legacy_u32 count,
												const VkFence *fences, VkBool32 all,
												uint64_t timeout)
{
	(void)device;
	(void)fences;
	assert(count == 1 && all && timeout == VULKAN_FENCE_TIMEOUT_NS);
	return wait_result;
}

static VKAPI_ATTR void VKAPI_CALL test_destroy_fence(VkDevice device, VkFence fence,
													 const VkAllocationCallbacks *allocator)
{
	(void)device;
	(void)fence;
	(void)allocator;
}

static VKAPI_ATTR void VKAPI_CALL test_destroy_pool(VkDevice device, VkCommandPool pool,
													const VkAllocationCallbacks *allocator)
{
	(void)device;
	(void)pool;
	(void)allocator;
}

static VKAPI_ATTR void VKAPI_CALL test_destroy_descriptors(VkDevice device, VkDescriptorPool pool,
														   const VkAllocationCallbacks *allocator)
{
	(void)device;
	(void)pool;
	(void)allocator;
}

static void test_context(VkPhysicalDeviceType type)
{
	assert(live_buffers == 0 && live_memories == 0);
	memset(&vulkan_context, 0, sizeof(vulkan_context));
	memset(memories, 0, sizeof(memories));
	memset(buffers, 0, sizeof(buffers));
	memset(bindings, 0, sizeof(bindings));
	buffer_count = allocation_count = allocation_calls = copy_count = 0;
	map_count = flush_count = invalidate_count = fail_allocation = 0;
	allowed_memory_types = TEST_ALL_MEMORY_TYPES;
	wait_result = VK_SUCCESS;
	struct VULKAN_CONTEXT *context = &vulkan_context;
	context->device = TEST_HANDLE(VkDevice, 1);
	context->pipeline = TEST_HANDLE(VkPipeline, 1);
	context->queue = TEST_HANDLE(VkQueue, 1);
	context->descriptor_set = TEST_HANDLE(VkDescriptorSet, 1);
	context->descriptor_pool = TEST_HANDLE(VkDescriptorPool, 1);
	context->command_pool = TEST_HANDLE(VkCommandPool, 1);
	context->command_buffer = TEST_HANDLE(VkCommandBuffer, 1);
	context->fence = TEST_HANDLE(VkFence, 1);
	context->properties.deviceType = type;
	context->properties.limits.maxStorageBufferRange = VULKAN_MINIMUM_STORAGE_BYTES;
	context->properties.limits.maxComputeWorkGroupCount[0] = LEGACY_U16_MAX;
	context->memory_properties.memoryTypeCount = TEST_MEMORY_TYPES;
	context->memory_properties.memoryTypes[TEST_PRIVATE_MEMORY].propertyFlags =
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	context->memory_properties.memoryTypes[TEST_UPLOAD_MEMORY].propertyFlags =
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	context->memory_properties.memoryTypes[TEST_READBACK_MEMORY].propertyFlags =
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
	context->memory_properties.memoryTypes[TEST_BAR_MEMORY].propertyFlags =
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
		VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
	context->api.CreateBuffer = test_create_buffer;
	context->api.DestroyBuffer = test_destroy_buffer;
	context->api.GetBufferMemoryRequirements = test_requirements;
	context->api.AllocateMemory = test_allocate;
	context->api.FreeMemory = test_free;
	context->api.BindBufferMemory = test_bind;
	context->api.MapMemory = test_map;
	context->api.UnmapMemory = test_unmap;
	context->api.FlushMappedMemoryRanges = test_flush;
	context->api.InvalidateMappedMemoryRanges = test_invalidate;
	context->api.UpdateDescriptorSets = test_descriptors;
	context->api.ResetCommandBuffer = test_reset_commands;
	context->api.BeginCommandBuffer = test_begin_commands;
	context->api.CmdPipelineBarrier = test_barrier;
	context->api.CmdCopyBuffer = test_copy;
	context->api.CmdBindPipeline = test_bind_pipeline;
	context->api.CmdBindDescriptorSets = test_bind_descriptors;
	context->api.CmdDispatch = test_dispatch;
	context->api.EndCommandBuffer = test_end_commands;
	context->api.ResetFences = test_reset_fences;
	context->api.QueueSubmit = test_submit;
	context->api.WaitForFences = test_wait;
	context->api.DestroyFence = test_destroy_fence;
	context->api.DestroyCommandPool = test_destroy_pool;
	context->api.DestroyDescriptorPool = test_destroy_descriptors;
}

static void test_run(size_t output_words)
{
	legacy_u32 input[TEST_INPUT_WORDS], output[TEST_GROWN_WORDS];
	assert(output_words <= TEST_GROWN_WORDS);
	for (size_t index = 0; index < TEST_INPUT_WORDS; index++) {
		input[index] = (legacy_u32)index + 1U;
	}
	for (size_t index = 0; index < output_words; index++) {
		output[index] = TEST_INITIAL_VALUE + (legacy_u32)index;
	}
	legacy_char error[GRAPHICS_ERROR_CAPACITY] = {0};
	assert(vulkan_dispatch(input, TEST_INPUT_WORDS, output, output_words, (legacy_u32)output_words,
						   error, sizeof(error)));
	for (size_t index = 0; index < output_words; index++) {
		legacy_u32 expected = TEST_INITIAL_VALUE + (legacy_u32)index;
		if (index % TEST_KERNEL_STRIDE == 0) {
			expected += input[index % TEST_INPUT_WORDS];
		}
		assert(output[index] == expected);
	}
	assert(!vulkan_context.pending && !vulkan_context.failed);
}

static void test_discrete_staging(void)
{
	test_context(VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
	test_run(TEST_OUTPUT_WORDS);
	assert(vulkan_context.staged && copy_count == 3 && map_count == 3);
	for (legacy_u32 index = 0; index < VULKAN_STORAGE_BINDING_COUNT; index++) {
		assert(test_memory(vulkan_context.buffers[index].memory)->type == TEST_PRIVATE_MEMORY);
		assert(vulkan_context.buffers[index].mapped == NULL);
		assert(test_memory(vulkan_context.uploads[index].memory)->type == TEST_UPLOAD_MEMORY);
	}
	assert(test_memory(vulkan_context.readback.memory)->type == TEST_READBACK_MEMORY);
	assert(flush_count == 0 && invalidate_count == 1);
	legacy_u32 original_allocations = allocation_count;
	test_run(TEST_OUTPUT_WORDS);
	assert(allocation_count == original_allocations && copy_count == 6);
	test_run(TEST_GROWN_WORDS);
	assert(allocation_count > original_allocations && copy_count == 9);
	vulkan_release_dispatch();
	assert(live_buffers == 0 && live_memories == 0);
}

static void test_integrated_direct(void)
{
	test_context(VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU);
	test_run(TEST_OUTPUT_WORDS);
	assert(!vulkan_context.staged && copy_count == 0 && map_count == 2);
	for (legacy_u32 index = 0; index < VULKAN_STORAGE_BINDING_COUNT; index++) {
		assert(test_memory(vulkan_context.buffers[index].memory)->type == TEST_BAR_MEMORY);
	}
	assert(flush_count == 0 && invalidate_count == 0);
	vulkan_release_dispatch();
	assert(live_buffers == 0 && live_memories == 0);
}

static void test_noncoherent_direct(void)
{
	test_context(VK_PHYSICAL_DEVICE_TYPE_CPU);
	vulkan_context.memory_properties.memoryTypes[TEST_BAR_MEMORY].propertyFlags &=
		~VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	test_run(TEST_OUTPUT_WORDS);
	assert(!vulkan_context.staged && copy_count == 0 && flush_count == 2 && invalidate_count == 1);
	vulkan_release_dispatch();
	assert(live_buffers == 0 && live_memories == 0);
}

static void test_staging_failure_cleanup(void)
{
	test_context(VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
	fail_allocation = 3;
	legacy_u32 input = 1, output = 2;
	legacy_char error[GRAPHICS_ERROR_CAPACITY] = {0};
	assert(!vulkan_dispatch(&input, 1, &output, 1, 1, error, sizeof(error)));
	assert(vulkan_context.failed && !vulkan_context.pending && error[0] != 0);
	vulkan_release_dispatch();
	assert(live_buffers == 0 && live_memories == 0);

	test_context(VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
	allowed_memory_types = 1U << TEST_UPLOAD_MEMORY;
	assert(!vulkan_dispatch(&input, 1, &output, 1, 1, error, sizeof(error)));
	assert(vulkan_context.failed && !vulkan_context.pending);
	vulkan_release_dispatch();
	assert(live_buffers == 0 && live_memories == 0);
}

static void test_timeout_retains_pending_storage(void)
{
	test_context(VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
	wait_result = VK_TIMEOUT;
	legacy_u32 input = 1, output = 2;
	legacy_char error[GRAPHICS_ERROR_CAPACITY] = {0};
	assert(!vulkan_dispatch(&input, 1, &output, 1, 1, error, sizeof(error)));
	assert(vulkan_context.failed && vulkan_context.pending && live_memories != 0);
	vulkan_release_dispatch();
	assert(live_memories != 0);
	/* The owner observes completion later; teardown can now release storage. */
	vulkan_context.pending = 0;
	vulkan_release_dispatch();
	assert(live_buffers == 0 && live_memories == 0);
}

legacy_int main(void)
{
	test_discrete_staging();
	test_integrated_direct();
	test_noncoherent_direct();
	test_staging_failure_cleanup();
	test_timeout_retains_pending_storage();
	puts("Vulkan discrete staging, shared-memory fast path, barriers, reuse and cleanup passed.");
	return 0;
}
