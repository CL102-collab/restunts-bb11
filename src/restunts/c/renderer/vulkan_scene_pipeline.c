#include "vulkan_scene_internal.h"
#include "vulkan_scene_spirv.h"
#include <stddef.h>
#include <string.h>

#define SCENE_SHADER_COUNT 2U
#define SCENE_ATTACHMENT_COUNT 2U
#define SCENE_VERTEX_ATTRIBUTE_COUNT 4U
#define SCENE_PUSH_BYTES (4U * sizeof(legacy_u32))

static legacy_s32 scene_render_pass(legacy_u32 index, legacy_char *error, size_t capacity)
{
	VkAttachmentDescription attachments[SCENE_ATTACHMENT_COUNT] = {{0}};
	attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = index == VULKAN_SCENE_RENDER_CLEAR ? VK_ATTACHMENT_LOAD_OP_CLEAR
															   : VK_ATTACHMENT_LOAD_OP_LOAD;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = index == VULKAN_SCENE_RENDER_CLEAR
									   ? VK_IMAGE_LAYOUT_UNDEFINED
									   : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	attachments[1] = attachments[0];
	attachments[1].format = vulkan_scene.depth_format;
	attachments[1].initialLayout = index == VULKAN_SCENE_RENDER_CLEAR
									   ? VK_IMAGE_LAYOUT_UNDEFINED
									   : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	VkAttachmentReference color = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	VkAttachmentReference depth = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
	VkSubpassDescription subpass = {0};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color;
	subpass.pDepthStencilAttachment = &depth;
	VkSubpassDependency dependencies[2] = {{0}};
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
								   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
								   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	dependencies[0].dstStageMask =
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT |
									VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
									VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	dependencies[0].dstAccessMask =
		VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask =
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	dependencies[1].dstStageMask =
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
	dependencies[1].srcAccessMask =
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
	VkRenderPassCreateInfo create = {0};
	create.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	create.attachmentCount = SCENE_ATTACHMENT_COUNT;
	create.pAttachments = attachments;
	create.subpassCount = 1;
	create.pSubpasses = &subpass;
	create.dependencyCount = 2;
	create.pDependencies = dependencies;
	VkResult result = vulkan_scene.api.CreateRenderPass(vulkan_context.device, &create, NULL,
														&vulkan_scene.passes[index]);
	return result == VK_SUCCESS
			   ? 1
			   : vulkan_error(error, capacity, "Create Vulkan scene render pass", result);
}

static legacy_s32 scene_descriptors(legacy_char *error, size_t capacity)
{
	VkDescriptorSetLayoutBinding binding = {0};
	binding.binding = 0;
	binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	binding.descriptorCount = 1;
	binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	VkDescriptorSetLayoutCreateInfo descriptors = {0};
	descriptors.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descriptors.bindingCount = 1;
	descriptors.pBindings = &binding;
	VkResult result = vulkan_context.api.CreateDescriptorSetLayout(
		vulkan_context.device, &descriptors, NULL, &vulkan_scene.descriptors);
	if (result != VK_SUCCESS) {
		return vulkan_error(error, capacity, "Create Vulkan scene palette layout", result);
	}
	VkPushConstantRange push = {VK_SHADER_STAGE_VERTEX_BIT, 0, SCENE_PUSH_BYTES};
	VkPipelineLayoutCreateInfo layout = {0};
	layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layout.setLayoutCount = 1;
	layout.pSetLayouts = &vulkan_scene.descriptors;
	layout.pushConstantRangeCount = 1;
	layout.pPushConstantRanges = &push;
	result = vulkan_context.api.CreatePipelineLayout(vulkan_context.device, &layout, NULL,
													 &vulkan_scene.layout);
	if (result != VK_SUCCESS) {
		return vulkan_error(error, capacity, "Create Vulkan scene pipeline layout", result);
	}
	VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VULKAN_SCENE_SLOTS};
	VkDescriptorPoolCreateInfo pool = {0};
	pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool.maxSets = VULKAN_SCENE_SLOTS;
	pool.poolSizeCount = 1;
	pool.pPoolSizes = &pool_size;
	result = vulkan_context.api.CreateDescriptorPool(vulkan_context.device, &pool, NULL,
													 &vulkan_scene.descriptor_pool);
	if (result != VK_SUCCESS) {
		return vulkan_error(error, capacity, "Create Vulkan scene palette pool", result);
	}
	VkDescriptorSetLayout layouts[VULKAN_SCENE_SLOTS];
	VkDescriptorSet sets[VULKAN_SCENE_SLOTS];
	for (legacy_u32 slot = 0; slot < VULKAN_SCENE_SLOTS; slot++) {
		layouts[slot] = vulkan_scene.descriptors;
	}
	VkDescriptorSetAllocateInfo allocate = {0};
	allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocate.descriptorPool = vulkan_scene.descriptor_pool;
	allocate.descriptorSetCount = VULKAN_SCENE_SLOTS;
	allocate.pSetLayouts = layouts;
	result = vulkan_context.api.AllocateDescriptorSets(vulkan_context.device, &allocate, sets);
	if (result != VK_SUCCESS) {
		return vulkan_error(error, capacity, "Allocate Vulkan scene palettes", result);
	}
	for (legacy_u32 slot = 0; slot < VULKAN_SCENE_SLOTS; slot++) {
		vulkan_scene.slots[slot].descriptor = sets[slot];
	}
	return 1;
}

static legacy_s32 scene_shader(VkShaderModule *shader, const legacy_u32 *words, size_t size,
							   legacy_char *error, size_t capacity)
{
	VkShaderModuleCreateInfo module = {0};
	module.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	module.codeSize = size;
	module.pCode = words;
	VkResult result =
		vulkan_context.api.CreateShaderModule(vulkan_context.device, &module, NULL, shader);
	return result == VK_SUCCESS
			   ? 1
			   : vulkan_error(error, capacity, "Create Vulkan scene shader", result);
}

static legacy_s32 scene_graphics_pipeline(legacy_u32 index, legacy_char *error, size_t capacity)
{
	VkPipelineShaderStageCreateInfo stages[SCENE_SHADER_COUNT] = {{0}};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vulkan_scene.vertex_shader;
	stages[0].pName = "main";
	stages[1] = stages[0];
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = vulkan_scene.fragment_shader;
	VkVertexInputBindingDescription binding = {0, sizeof(struct VULKAN_SCENE_GPU_VERTEX),
											   VK_VERTEX_INPUT_RATE_VERTEX};
	VkVertexInputAttributeDescription attributes[SCENE_VERTEX_ATTRIBUTE_COUNT] = {
		{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(struct VULKAN_SCENE_VERTEX, x)},
		{1, 0, VK_FORMAT_R32G32B32A32_UINT, offsetof(struct VULKAN_SCENE_VERTEX, color)},
		{2, 0, VK_FORMAT_R32_UINT, offsetof(struct VULKAN_SCENE_VERTEX, flags)},
		{3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(struct VULKAN_SCENE_GPU_VERTEX, plane)}};
	VkPipelineVertexInputStateCreateInfo input = {0};
	input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	input.vertexBindingDescriptionCount = 1;
	input.pVertexBindingDescriptions = &binding;
	input.vertexAttributeDescriptionCount = SCENE_VERTEX_ATTRIBUTE_COUNT;
	input.pVertexAttributeDescriptions = attributes;
	VkPipelineInputAssemblyStateCreateInfo assembly = {0};
	assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	VkPipelineViewportStateCreateInfo viewport = {0};
	viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport.viewportCount = 1;
	viewport.scissorCount = 1;
	VkPipelineRasterizationStateCreateInfo raster = {0};
	raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster.polygonMode = VK_POLYGON_MODE_FILL;
	raster.cullMode = VK_CULL_MODE_NONE;
	raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	raster.lineWidth = 1.0f;
	VkPipelineMultisampleStateCreateInfo samples = {0};
	samples.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	VkPipelineDepthStencilStateCreateInfo depth = {0};
	depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depth.depthTestEnable = index != VULKAN_SCENE_PAINTER_PIPELINE;
	depth.depthWriteEnable = index == VULKAN_SCENE_DEPTH_PIPELINE;
	depth.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
	VkPipelineColorBlendAttachmentState blend = {0};
	blend.blendEnable = VK_TRUE;
	blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blend.colorBlendOp = VK_BLEND_OP_ADD;
	blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blend.alphaBlendOp = VK_BLEND_OP_ADD;
	blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
						   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	VkPipelineColorBlendStateCreateInfo color = {0};
	color.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	color.attachmentCount = 1;
	color.pAttachments = &blend;
	VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynamic = {0};
	dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic.dynamicStateCount = sizeof(dynamic_states) / sizeof(*dynamic_states);
	dynamic.pDynamicStates = dynamic_states;
	VkGraphicsPipelineCreateInfo pipeline = {0};
	pipeline.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipeline.stageCount = SCENE_SHADER_COUNT;
	pipeline.pStages = stages;
	pipeline.pVertexInputState = &input;
	pipeline.pInputAssemblyState = &assembly;
	pipeline.pViewportState = &viewport;
	pipeline.pRasterizationState = &raster;
	pipeline.pMultisampleState = &samples;
	pipeline.pDepthStencilState = &depth;
	pipeline.pColorBlendState = &color;
	pipeline.pDynamicState = &dynamic;
	pipeline.layout = vulkan_scene.layout;
	pipeline.renderPass = vulkan_scene.passes[VULKAN_SCENE_RENDER_CLEAR];
	pipeline.basePipelineIndex = -1;
	VkResult result = vulkan_scene.api.CreateGraphicsPipelines(
		vulkan_context.device, VK_NULL_HANDLE, 1, &pipeline, NULL, &vulkan_scene.pipelines[index]);
	return result == VK_SUCCESS
			   ? 1
			   : vulkan_error(error, capacity, "Create Vulkan scene graphics pipeline", result);
}

legacy_s32 vulkan_scene_create_pipeline(legacy_char *error, size_t capacity)
{
	if (!scene_render_pass(VULKAN_SCENE_RENDER_CLEAR, error, capacity) ||
		!scene_render_pass(VULKAN_SCENE_RENDER_LOAD, error, capacity) ||
		!scene_descriptors(error, capacity) ||
		!scene_shader(&vulkan_scene.vertex_shader, vulkan_scene_vertex_spirv,
					  vulkan_scene_vertex_spirv_size, error, capacity) ||
		!scene_shader(&vulkan_scene.fragment_shader, vulkan_scene_fragment_spirv,
					  vulkan_scene_fragment_spirv_size, error, capacity)) {
		return 0;
	}
	VkImageView views[SCENE_ATTACHMENT_COUNT] = {vulkan_scene.color.view, vulkan_scene.depth.view};
	VkFramebufferCreateInfo framebuffer = {0};
	framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebuffer.renderPass = vulkan_scene.passes[VULKAN_SCENE_RENDER_CLEAR];
	framebuffer.attachmentCount = SCENE_ATTACHMENT_COUNT;
	framebuffer.pAttachments = views;
	framebuffer.width = VULKAN_SCENE_TEXTURE_WIDTH;
	framebuffer.height = VULKAN_SCENE_TEXTURE_HEIGHT;
	framebuffer.layers = 1;
	VkResult result = vulkan_scene.api.CreateFramebuffer(vulkan_context.device, &framebuffer, NULL,
														 &vulkan_scene.framebuffer);
	if (result != VK_SUCCESS) {
		return vulkan_error(error, capacity, "Create Vulkan scene framebuffer", result);
	}
	for (legacy_u32 index = 0; index < VULKAN_SCENE_PIPELINES; index++) {
		if (!scene_graphics_pipeline(index, error, capacity)) {
			return 0;
		}
	}
	return 1;
}

legacy_s32 vulkan_scene_create_commands(legacy_char *error, size_t capacity)
{
	VkCommandPoolCreateInfo pool = {0};
	pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pool.queueFamilyIndex = vulkan_context.queue_family;
	VkResult result = vulkan_context.api.CreateCommandPool(vulkan_context.device, &pool, NULL,
														   &vulkan_scene.command_pool);
	if (result != VK_SUCCESS) {
		return vulkan_error(error, capacity, "Create Vulkan scene command pool", result);
	}
	VkCommandBufferAllocateInfo allocate = {0};
	allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocate.commandPool = vulkan_scene.command_pool;
	allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocate.commandBufferCount = VULKAN_SCENE_SLOTS;
	VkCommandBuffer commands[VULKAN_SCENE_SLOTS];
	result = vulkan_context.api.AllocateCommandBuffers(vulkan_context.device, &allocate, commands);
	if (result != VK_SUCCESS) {
		return vulkan_error(error, capacity, "Allocate Vulkan scene commands", result);
	}
	for (legacy_u32 slot = 0; slot < VULKAN_SCENE_SLOTS; slot++) {
		vulkan_scene.slots[slot].command = commands[slot];
		VkFenceCreateInfo fence = {0};
		fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
		result = vulkan_context.api.CreateFence(vulkan_context.device, &fence, NULL,
												&vulkan_scene.slots[slot].fence);
		if (result != VK_SUCCESS) {
			return vulkan_error(error, capacity, "Create Vulkan scene completion fence", result);
		}
	}
	return 1;
}
