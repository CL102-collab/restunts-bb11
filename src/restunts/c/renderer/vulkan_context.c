#include "vulkan_internal.h"
#include "gpu_raster_spirv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#define VULKAN_MAX_PHYSICAL_DEVICES 256U
#define VULKAN_MAX_QUEUE_FAMILIES 256U
#define VULKAN_QUEUE_PRIORITY 1.0f
#define VULKAN_NO_BASE_PIPELINE (-1)

struct VULKAN_CONTEXT vulkan_context;

legacy_s32 vulkan_error(legacy_char *error, size_t capacity, const legacy_char *operation,
						VkResult result)
{
	if (error != NULL && capacity != 0) {
		snprintf(error, capacity, "%s (Vulkan result %d)", operation, (legacy_int)result);
	}
	return 0;
}

static void *vulkan_library_open(void)
{
#ifdef _WIN32
	/* Load only the installed system loader. This also keeps Windows XP startup
	 * independent of newer Vulkan DLLs when Software is selected. */
	static const legacy_char name[] = "vulkan-1.dll";
	legacy_char path[MAX_PATH + sizeof(name) + 1U];
	UINT length = GetSystemDirectoryA(path, MAX_PATH);
	if (length == 0 || length >= MAX_PATH) {
		return NULL;
	}
	path[length] = '\\';
	memcpy(path + length + 1U, name, sizeof(name));
	return (void *)LoadLibraryA(path);
#elif defined(__ANDROID__)
	return dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
#else
	void *library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
	if (library == NULL) {
		library = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
	}
	return library;
#endif
}

static void vulkan_library_close(void *library)
{
	if (library == NULL) {
		return;
	}
#ifdef _WIN32
	FreeLibrary((HMODULE)library);
#else
	dlclose(library);
#endif
}

static PFN_vkGetInstanceProcAddr vulkan_loader_entry(void *library)
{
	PFN_vkGetInstanceProcAddr result = NULL;
#ifdef _WIN32
	FARPROC symbol = GetProcAddress((HMODULE)library, "vkGetInstanceProcAddr");
#else
	void *symbol = dlsym(library, "vkGetInstanceProcAddr");
#endif
	/* The native loader ABI uses equally sized object and function pointers.
	 * memcpy avoids a C object-pointer/function-pointer conversion diagnostic. */
	if (sizeof(result) == sizeof(symbol)) {
		memcpy(&result, &symbol, sizeof(result));
	}
	return result;
}

static void vulkan_close_instance(struct VULKAN_CONTEXT *context)
{
	if (context->instance != VK_NULL_HANDLE) {
		if (context->api.DestroyInstance == NULL) {
			/* A broken loader must not be unloaded beneath a live instance. */
			return;
		}
		context->api.DestroyInstance(context->instance, NULL);
	}
	vulkan_library_close(context->library);
	memset(context, 0, sizeof(*context));
}

static legacy_s32 vulkan_create_instance(struct VULKAN_CONTEXT *context, legacy_char *error,
										 size_t capacity)
{
	context->library = vulkan_library_open();
	if (context->library == NULL) {
		return vulkan_error(error, capacity, "Vulkan loader is unavailable",
							VK_ERROR_INCOMPATIBLE_DRIVER);
	}
	context->api.GetInstanceProcAddr = vulkan_loader_entry(context->library);
	if (context->api.GetInstanceProcAddr == NULL) {
		vulkan_close_instance(context);
		return vulkan_error(error, capacity, "Vulkan loader entry point is unavailable",
							VK_ERROR_INCOMPATIBLE_DRIVER);
	}
	context->api.CreateInstance =
		(PFN_vkCreateInstance)context->api.GetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");
	if (context->api.CreateInstance == NULL) {
		vulkan_close_instance(context);
		return vulkan_error(error, capacity, "Vulkan instance entry point is unavailable",
							VK_ERROR_INCOMPATIBLE_DRIVER);
	}
	VkApplicationInfo application = {0};
	application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application.pApplicationName = "Chocolate Stunts";
	application.pEngineName = "Restunts";
	application.apiVersion = VK_API_VERSION_1_0;
	VkInstanceCreateInfo create = {0};
	create.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	create.pApplicationInfo = &application;
	VkResult result = context->api.CreateInstance(&create, NULL, &context->instance);
	if (result != VK_SUCCESS) {
		context->instance = VK_NULL_HANDLE;
		vulkan_close_instance(context);
		return vulkan_error(error, capacity, "Cannot create a Vulkan 1.0 instance", result);
	}
#define VULKAN_LOAD_INSTANCE(name)                                                                 \
	context->api.name =                                                                            \
		(PFN_vk##name)context->api.GetInstanceProcAddr(context->instance, "vk" #name);             \
	if (context->api.name == NULL) {                                                               \
		vulkan_close_instance(context);                                                            \
		return vulkan_error(error, capacity, "Missing Vulkan instance function vk" #name,          \
							VK_ERROR_INITIALIZATION_FAILED);                                       \
	}
	VULKAN_INSTANCE_FUNCTIONS(VULKAN_LOAD_INSTANCE)
#undef VULKAN_LOAD_INSTANCE
	return 1;
}

static legacy_u32 vulkan_physical_devices(struct VULKAN_CONTEXT *context, VkPhysicalDevice *devices)
{
	legacy_u32 count = 0;
	VkResult result = context->api.EnumeratePhysicalDevices(context->instance, &count, NULL);
	if (result != VK_SUCCESS || count == 0) {
		return 0;
	}
	if (count > VULKAN_MAX_PHYSICAL_DEVICES) {
		count = VULKAN_MAX_PHYSICAL_DEVICES;
	}
	result = context->api.EnumeratePhysicalDevices(context->instance, &count, devices);
	return result == VK_SUCCESS || result == VK_INCOMPLETE ? count : 0;
}

static legacy_s32 vulkan_compatible_device(struct VULKAN_CONTEXT *context, VkPhysicalDevice device,
										   VkPhysicalDeviceProperties *properties,
										   legacy_u32 *queue_family)
{
	context->api.GetPhysicalDeviceProperties(device, properties);
	const VkPhysicalDeviceLimits *limits = &properties->limits;
	if (properties->apiVersion < VK_API_VERSION_1_0 ||
		limits->maxComputeWorkGroupInvocations < VULKAN_LOCAL_SIZE ||
		limits->maxComputeWorkGroupSize[0] < VULKAN_LOCAL_SIZE ||
		limits->maxComputeWorkGroupCount[0] == 0 ||
		limits->maxPerStageDescriptorStorageBuffers < VULKAN_STORAGE_BINDING_COUNT ||
		limits->maxDescriptorSetStorageBuffers < VULKAN_STORAGE_BINDING_COUNT ||
		limits->maxStorageBufferRange < VULKAN_MINIMUM_STORAGE_BYTES) {
		return 0;
	}
	legacy_u32 count = 0;
	context->api.GetPhysicalDeviceQueueFamilyProperties(device, &count, NULL);
	if (count == 0) {
		return 0;
	}
	if (count > VULKAN_MAX_QUEUE_FAMILIES) {
		count = VULKAN_MAX_QUEUE_FAMILIES;
	}
	VkQueueFamilyProperties queues[VULKAN_MAX_QUEUE_FAMILIES];
	context->api.GetPhysicalDeviceQueueFamilyProperties(device, &count, queues);
	for (legacy_u32 index = 0; index < count; index++) {
		if (queues[index].queueCount != 0 && (queues[index].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
			*queue_family = index;
			return 1;
		}
	}
	return 0;
}

legacy_u32 vulkan_enumerate_devices(struct GRAPHICS_DEVICE *devices, legacy_u32 capacity)
{
	/* Probing can happen on Android's launcher worker while a game context is
	 * active. Never reuse its instance, function table or dispatch resources. */
	struct VULKAN_CONTEXT inventory = {0};
	if (capacity == 0 || devices == NULL || !vulkan_create_instance(&inventory, NULL, 0)) {
		return 0;
	}
	VkPhysicalDevice physical[VULKAN_MAX_PHYSICAL_DEVICES];
	legacy_u32 count = vulkan_physical_devices(&inventory, physical);
	legacy_u32 found = 0;
	if (capacity > GRAPHICS_DEVICE_CAPACITY) {
		capacity = GRAPHICS_DEVICE_CAPACITY;
	}
	for (legacy_u32 index = 0; index < count && found < capacity; index++) {
		VkPhysicalDeviceProperties properties;
		legacy_u32 queue_family;
		if (!vulkan_compatible_device(&inventory, physical[index], &properties, &queue_family)) {
			continue;
		}
		struct GRAPHICS_DEVICE *output = &devices[found];
		memset(output, 0, sizeof(*output));
		output->index = (legacy_s32)found;
		output->vendor_id = properties.vendorID;
		output->device_id = properties.deviceID;
		memcpy(output->name, properties.deviceName, sizeof(output->name) - 1U);
		for (size_t character = 0; output->name[character] != 0; character++) {
			if ((legacy_u8)output->name[character] < ' ') {
				output->name[character] = ' ';
			}
		}
		found++;
	}
	vulkan_close_instance(&inventory);
	return found;
}

static legacy_s32 vulkan_select_physical_device(legacy_s32 selected, legacy_char *error,
												size_t capacity)
{
	VkPhysicalDevice physical[VULKAN_MAX_PHYSICAL_DEVICES];
	legacy_u32 count = vulkan_physical_devices(&vulkan_context, physical);
	legacy_s32 found = 0;
	if (selected == GRAPHICS_DEVICE_DEFAULT) {
		selected = 0;
	}
	for (legacy_u32 index = 0; index < count && found < (legacy_s32)GRAPHICS_DEVICE_CAPACITY;
		 index++) {
		VkPhysicalDeviceProperties properties;
		legacy_u32 family;
		if (!vulkan_compatible_device(&vulkan_context, physical[index], &properties, &family)) {
			continue;
		}
		if (found++ == selected) {
			vulkan_context.physical_device = physical[index];
			vulkan_context.properties = properties;
			vulkan_context.queue_family = family;
			vulkan_context.api.GetPhysicalDeviceMemoryProperties(physical[index],
																 &vulkan_context.memory_properties);
			return 1;
		}
	}
	return vulkan_error(error, capacity, "Selected Vulkan compute device is unavailable",
						VK_ERROR_FEATURE_NOT_PRESENT);
}

static legacy_s32 vulkan_create_device(legacy_char *error, size_t capacity)
{
	legacy_f32 priority = VULKAN_QUEUE_PRIORITY;
	VkDeviceQueueCreateInfo queue = {0};
	queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue.queueFamilyIndex = vulkan_context.queue_family;
	queue.queueCount = 1;
	queue.pQueuePriorities = &priority;
	VkDeviceCreateInfo create = {0};
	create.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	create.queueCreateInfoCount = 1;
	create.pQueueCreateInfos = &queue;
	VkResult result = vulkan_context.api.CreateDevice(vulkan_context.physical_device, &create, NULL,
													  &vulkan_context.device);
	if (result != VK_SUCCESS) {
		vulkan_context.device = VK_NULL_HANDLE;
		return vulkan_error(error, capacity, "Cannot create Vulkan compute device", result);
	}
#define VULKAN_LOAD_DEVICE(name)                                                                   \
	vulkan_context.api.name =                                                                      \
		(PFN_vk##name)vulkan_context.api.GetDeviceProcAddr(vulkan_context.device, "vk" #name);     \
	if (vulkan_context.api.name == NULL) {                                                         \
		return vulkan_error(error, capacity, "Missing Vulkan device function vk" #name,            \
							VK_ERROR_INITIALIZATION_FAILED);                                       \
	}
	VULKAN_DEVICE_FUNCTIONS(VULKAN_LOAD_DEVICE)
#undef VULKAN_LOAD_DEVICE
	vulkan_context.api.GetDeviceQueue(vulkan_context.device, vulkan_context.queue_family, 0,
									  &vulkan_context.queue);
	if (vulkan_context.queue == VK_NULL_HANDLE) {
		return vulkan_error(error, capacity, "Vulkan compute queue is unavailable",
							VK_ERROR_INITIALIZATION_FAILED);
	}
	return 1;
}

static legacy_s32 vulkan_create_pipeline(legacy_char *error, size_t capacity)
{
	VkDescriptorSetLayoutBinding bindings[VULKAN_STORAGE_BINDING_COUNT] = {{0}};
	for (legacy_u32 binding = 0; binding < VULKAN_STORAGE_BINDING_COUNT; binding++) {
		bindings[binding].binding = binding;
		bindings[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		bindings[binding].descriptorCount = 1;
		bindings[binding].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	}
	VkDescriptorSetLayoutCreateInfo descriptors = {0};
	descriptors.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descriptors.bindingCount = VULKAN_STORAGE_BINDING_COUNT;
	descriptors.pBindings = bindings;
	VkResult result = vulkan_context.api.CreateDescriptorSetLayout(
		vulkan_context.device, &descriptors, NULL, &vulkan_context.descriptor_layout);
	if (result != VK_SUCCESS) {
		vulkan_context.descriptor_layout = VK_NULL_HANDLE;
		return vulkan_error(error, capacity, "Cannot create Vulkan storage layout", result);
	}
	VkPipelineLayoutCreateInfo layout = {0};
	layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layout.setLayoutCount = 1;
	layout.pSetLayouts = &vulkan_context.descriptor_layout;
	result = vulkan_context.api.CreatePipelineLayout(vulkan_context.device, &layout, NULL,
													 &vulkan_context.pipeline_layout);
	if (result != VK_SUCCESS) {
		vulkan_context.pipeline_layout = VK_NULL_HANDLE;
		return vulkan_error(error, capacity, "Cannot create Vulkan pipeline layout", result);
	}
	VkShaderModuleCreateInfo module = {0};
	module.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	module.codeSize = sizeof(gpu_raster_spirv);
	module.pCode = gpu_raster_spirv;
	result = vulkan_context.api.CreateShaderModule(vulkan_context.device, &module, NULL,
												   &vulkan_context.shader);
	if (result != VK_SUCCESS) {
		vulkan_context.shader = VK_NULL_HANDLE;
		return vulkan_error(error, capacity, "Cannot create Vulkan raster shader", result);
	}
	VkComputePipelineCreateInfo pipeline = {0};
	pipeline.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipeline.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	pipeline.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	pipeline.stage.module = vulkan_context.shader;
	pipeline.stage.pName = "main";
	pipeline.layout = vulkan_context.pipeline_layout;
	pipeline.basePipelineIndex = VULKAN_NO_BASE_PIPELINE;
	result = vulkan_context.api.CreateComputePipelines(vulkan_context.device, VK_NULL_HANDLE, 1,
													   &pipeline, NULL, &vulkan_context.pipeline);
	return result == VK_SUCCESS
			   ? 1
			   : vulkan_error(error, capacity, "Cannot create Vulkan raster pipeline", result);
}

legacy_s32 vulkan_open(legacy_s32 device, legacy_char *error, size_t error_capacity)
{
	vulkan_close();
	if (vulkan_context.instance != VK_NULL_HANDLE || device < GRAPHICS_DEVICE_DEFAULT) {
		return vulkan_error(error, error_capacity, "Cannot reopen an unfinished Vulkan context",
							VK_NOT_READY);
	}
	if (!vulkan_create_instance(&vulkan_context, error, error_capacity) ||
		!vulkan_select_physical_device(device, error, error_capacity) ||
		!vulkan_create_device(error, error_capacity) ||
		!vulkan_create_pipeline(error, error_capacity) ||
		!vulkan_prepare_dispatch(error, error_capacity)) {
		vulkan_close();
		return 0;
	}
	return 1;
}

void vulkan_close(void)
{
	if (vulkan_context.pending && !vulkan_context.device_lost) {
		VkResult result =
			vulkan_context.api.GetFenceStatus(vulkan_context.device, vulkan_context.fence);
		if (result == VK_SUCCESS) {
			vulkan_context.pending = 0;
		} else if (result == VK_ERROR_DEVICE_LOST) {
			vulkan_context.device_lost = 1;
		} else {
			/* A finite fence timeout is fatal to the renderer. Keep its objects
			 * alive for process teardown instead of blocking on a hung driver
			 * or destroying storage still referenced by submitted commands. */
			return;
		}
	}
	vulkan_release_dispatch();
	if (vulkan_context.device != VK_NULL_HANDLE) {
		if (vulkan_context.api.DestroyDevice == NULL) {
			return;
		}
		if (vulkan_context.pipeline != VK_NULL_HANDLE) {
			vulkan_context.api.DestroyPipeline(vulkan_context.device, vulkan_context.pipeline,
											   NULL);
		}
		if (vulkan_context.shader != VK_NULL_HANDLE) {
			vulkan_context.api.DestroyShaderModule(vulkan_context.device, vulkan_context.shader,
												   NULL);
		}
		if (vulkan_context.pipeline_layout != VK_NULL_HANDLE) {
			vulkan_context.api.DestroyPipelineLayout(vulkan_context.device,
													 vulkan_context.pipeline_layout, NULL);
		}
		if (vulkan_context.descriptor_layout != VK_NULL_HANDLE) {
			vulkan_context.api.DestroyDescriptorSetLayout(vulkan_context.device,
														  vulkan_context.descriptor_layout, NULL);
		}
		vulkan_context.api.DestroyDevice(vulkan_context.device, NULL);
	}
	vulkan_close_instance(&vulkan_context);
}
