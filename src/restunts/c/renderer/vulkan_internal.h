#ifndef RESTUNTS_VULKAN_INTERNAL_H
#define RESTUNTS_VULKAN_INTERNAL_H

#include "vulkan_backend.h"
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan_core.h>

#define VULKAN_STORAGE_BINDING_COUNT 2U
#define VULKAN_INPUT_BINDING 0U
#define VULKAN_OUTPUT_BINDING 1U
#define VULKAN_LOCAL_SIZE 64U
#define VULKAN_MINIMUM_STORAGE_BYTES (128U * 1024U * 1024U)
#define VULKAN_FENCE_TIMEOUT_NS 10000000000ULL
#define VULKAN_FIRST_BUFFER_BYTES 4096U

#define VULKAN_INSTANCE_FUNCTIONS(X)                                                               \
	X(DestroyInstance)                                                                             \
	X(EnumeratePhysicalDevices)                                                                    \
	X(GetPhysicalDeviceProperties)                                                                 \
	X(GetPhysicalDeviceQueueFamilyProperties)                                                      \
	X(GetPhysicalDeviceMemoryProperties)                                                           \
	X(CreateDevice)                                                                                \
	X(GetDeviceProcAddr)

#define VULKAN_DEVICE_FUNCTIONS(X)                                                                 \
	X(DestroyDevice)                                                                               \
	X(GetDeviceQueue)                                                                              \
	X(CreateShaderModule)                                                                          \
	X(DestroyShaderModule)                                                                         \
	X(CreateDescriptorSetLayout)                                                                   \
	X(DestroyDescriptorSetLayout)                                                                  \
	X(CreatePipelineLayout)                                                                        \
	X(DestroyPipelineLayout)                                                                       \
	X(CreateComputePipelines)                                                                      \
	X(DestroyPipeline)                                                                             \
	X(CreateDescriptorPool)                                                                        \
	X(DestroyDescriptorPool)                                                                       \
	X(AllocateDescriptorSets)                                                                      \
	X(UpdateDescriptorSets)                                                                        \
	X(CreateBuffer)                                                                                \
	X(DestroyBuffer)                                                                               \
	X(GetBufferMemoryRequirements)                                                                 \
	X(AllocateMemory)                                                                              \
	X(FreeMemory)                                                                                  \
	X(BindBufferMemory)                                                                            \
	X(MapMemory)                                                                                   \
	X(UnmapMemory)                                                                                 \
	X(FlushMappedMemoryRanges)                                                                     \
	X(InvalidateMappedMemoryRanges)                                                                \
	X(CreateCommandPool)                                                                           \
	X(DestroyCommandPool)                                                                          \
	X(AllocateCommandBuffers)                                                                      \
	X(ResetCommandBuffer)                                                                          \
	X(BeginCommandBuffer)                                                                          \
	X(EndCommandBuffer)                                                                            \
	X(CmdPipelineBarrier)                                                                          \
	X(CmdBindPipeline)                                                                             \
	X(CmdBindDescriptorSets)                                                                       \
	X(CmdCopyBuffer)                                                                               \
	X(CmdDispatch)                                                                                 \
	X(CreateFence)                                                                                 \
	X(DestroyFence)                                                                                \
	X(ResetFences)                                                                                 \
	X(GetFenceStatus)                                                                              \
	X(WaitForFences)                                                                               \
	X(QueueSubmit)

struct VULKAN_FUNCTIONS {
	PFN_vkGetInstanceProcAddr GetInstanceProcAddr;
	PFN_vkCreateInstance CreateInstance;
#define VULKAN_DECLARE_FUNCTION(name) PFN_vk##name name;
	VULKAN_INSTANCE_FUNCTIONS(VULKAN_DECLARE_FUNCTION)
	VULKAN_DEVICE_FUNCTIONS(VULKAN_DECLARE_FUNCTION)
#undef VULKAN_DECLARE_FUNCTION
};

struct VULKAN_BUFFER {
	VkBuffer buffer;
	VkDeviceMemory memory;
	VkDeviceSize capacity;
	VkDeviceSize allocation_size;
	void *mapped;
	legacy_s32 coherent;
};

struct VULKAN_CONTEXT {
	void *library;
	struct VULKAN_FUNCTIONS api;
	VkInstance instance;
	VkPhysicalDevice physical_device;
	VkPhysicalDeviceProperties properties;
	VkPhysicalDeviceMemoryProperties memory_properties;
	VkDevice device;
	VkQueue queue;
	legacy_u32 queue_family;
	VkDescriptorSetLayout descriptor_layout;
	VkPipelineLayout pipeline_layout;
	VkShaderModule shader;
	VkPipeline pipeline;
	VkDescriptorPool descriptor_pool;
	VkDescriptorSet descriptor_set;
	VkCommandPool command_pool;
	VkCommandBuffer command_buffer;
	VkFence fence;
	struct VULKAN_BUFFER buffers[VULKAN_STORAGE_BINDING_COUNT];
	struct VULKAN_BUFFER uploads[VULKAN_STORAGE_BINDING_COUNT];
	struct VULKAN_BUFFER readback;
	legacy_s32 staged;
	legacy_s32 graphics_enabled;
	legacy_s32 presentation_enabled;
	legacy_s32 pending;
	legacy_s32 device_lost;
	legacy_s32 failed;
};

extern struct VULKAN_CONTEXT vulkan_context;

legacy_s32 vulkan_error(legacy_char *error, size_t capacity, const legacy_char *operation,
						VkResult result);
legacy_s32 vulkan_prepare_dispatch(legacy_char *error, size_t capacity);
void vulkan_release_dispatch(void);

#endif
