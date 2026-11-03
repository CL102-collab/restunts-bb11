#include "metal_backend.h"
#include "gpu_raster_metal.h"
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#define METAL_LOCAL_SIZE 64U
#define METAL_MINIMUM_STORAGE_BYTES (128U * 1024U * 1024U)
#define METAL_FIRST_BUFFER_BYTES 4096U
#define METAL_COMPLETION_TIMEOUT_NS 10000000000LL
#define METAL_INPUT_BINDING 0U
#define METAL_OUTPUT_BINDING 1U
#define METAL_DISPATCH_Y 1U
#define METAL_DISPATCH_Z 1U

/* ARC keeps an unfinished command and all storage alive after a bounded wait
 * expires. The owner reports the failure; shutdown must never wait forever. */
@interface RestuntsMetalContext : NSObject {
	@public
	id<MTLDevice> device;
	id<MTLCommandQueue> queue;
	id<MTLComputePipelineState> pipeline;
	id<MTLBuffer> input_buffer;
	id<MTLBuffer> output_buffer;
	id<MTLCommandBuffer> pending;
	legacy_u8 failed;
}
@end

@implementation RestuntsMetalContext
@end

static RestuntsMetalContext *metal_context;

static legacy_s32 metal_error(legacy_char *error, size_t capacity, const legacy_char *operation,
							  NSError *failure)
{
	if (error != NULL && capacity != 0) {
		const legacy_char *detail = failure.localizedDescription.UTF8String;
		if (detail != NULL) {
			snprintf(error, capacity, "%s: %s", operation, detail);
		} else {
			snprintf(error, capacity, "%s", operation);
		}
	}
	return 0;
}

static NSArray<id<MTLDevice>> *metal_devices(void)
{
	/* The framework is weak-linked. Software startup remains valid when Metal
	 * is absent, and probing must not call through an unresolved weak symbol. */
	if (dlsym(RTLD_DEFAULT, "MTLCopyAllDevices") == NULL) {
		return nil;
	}
	return MTLCopyAllDevices();
}

static legacy_s32 metal_compatible_device(id<MTLDevice> device)
{
	return device != nil && device.maxThreadsPerThreadgroup.width >= METAL_LOCAL_SIZE &&
		   device.maxBufferLength >= METAL_MINIMUM_STORAGE_BYTES;
}

legacy_u32 metal_enumerate_devices(struct GRAPHICS_DEVICE *devices, legacy_u32 capacity)
{
	if (devices == NULL || capacity == 0) {
		return 0;
	}
	if (capacity > GRAPHICS_DEVICE_CAPACITY) {
		capacity = GRAPHICS_DEVICE_CAPACITY;
	}
	@autoreleasepool {
		legacy_u32 found = 0;
		for (id<MTLDevice> device in metal_devices()) {
			if (!metal_compatible_device(device)) {
				continue;
			}
			struct GRAPHICS_DEVICE *output = &devices[found];
			memset(output, 0, sizeof(*output));
			output->index = (legacy_s32)found;
			/* Metal exposes a registry identifier, rather than PCI vendor/device
			 * properties. Selection uses the compatible-device index and name. */
			output->device_id = (legacy_u32)device.registryID;
			const legacy_char *name = device.name.UTF8String;
			snprintf(output->name, sizeof(output->name), "%s",
					 name != NULL ? name : "Metal device");
			for (size_t character = 0; output->name[character] != 0; character++) {
				if ((legacy_u8)output->name[character] < ' ' ||
					(legacy_u8)output->name[character] == LEGACY_S8_MAX) {
					output->name[character] = ' ';
				}
			}
			if (++found == capacity) {
				break;
			}
		}
		return found;
	}
}

static id<MTLDevice> metal_select_device(legacy_s32 selected)
{
	legacy_s32 found = 0;
	if (selected == GRAPHICS_DEVICE_DEFAULT) {
		selected = 0;
	}
	for (id<MTLDevice> device in metal_devices()) {
		if (!metal_compatible_device(device)) {
			continue;
		}
		if (found++ == selected) {
			return device;
		}
		if (found >= (legacy_s32)GRAPHICS_DEVICE_CAPACITY) {
			break;
		}
	}
	return nil;
}

void metal_close(void)
{
	@autoreleasepool {
		RestuntsMetalContext *context = metal_context;
		if (context != nil && context->pending != nil) {
			MTLCommandBufferStatus status = context->pending.status;
			if (status != MTLCommandBufferStatusCompleted &&
				status != MTLCommandBufferStatusError) {
				return;
			}
		}
		metal_context = nil;
	}
}

legacy_s32 metal_open(legacy_s32 selected, legacy_char *error, size_t error_capacity)
{
	@autoreleasepool {
		metal_close();
		if (metal_context != nil) {
			return metal_error(error, error_capacity, "Cannot reopen an unfinished Metal context",
							   nil);
		}
		RestuntsMetalContext *context = [RestuntsMetalContext new];
		if (context == nil) {
			return metal_error(error, error_capacity, "Cannot allocate Metal renderer context",
							   nil);
		}
		context->device = metal_select_device(selected);
		if (context->device == nil) {
			return metal_error(error, error_capacity,
							   "Selected Metal compute device is unavailable", nil);
		}
		context->queue = [context->device newCommandQueue];
		if (context->queue == nil) {
			return metal_error(error, error_capacity, "Cannot create Metal command queue", nil);
		}
		MTLCompileOptions *options = [MTLCompileOptions new];
		options.languageVersion = MTLLanguageVersion1_2;
		options.fastMathEnabled = NO;
		NSString *source = [NSString stringWithUTF8String:gpu_raster_metal_source];
		if (options == nil || source == nil) {
			return metal_error(error, error_capacity, "Cannot allocate Metal shader source", nil);
		}
		NSError *failure = nil;
		id<MTLLibrary> library = [context->device newLibraryWithSource:source
															   options:options
																 error:&failure];
		if (library == nil) {
			return metal_error(error, error_capacity, "Cannot compile Metal raster shader",
							   failure);
		}
		id<MTLFunction> function = [library newFunctionWithName:@"gpu_raster"];
		if (function == nil) {
			return metal_error(error, error_capacity, "Metal raster kernel is unavailable", nil);
		}
		context->pipeline = [context->device newComputePipelineStateWithFunction:function
																		   error:&failure];
		if (context->pipeline == nil) {
			return metal_error(error, error_capacity, "Cannot create Metal raster pipeline",
							   failure);
		}
		if (context->pipeline.maxTotalThreadsPerThreadgroup < METAL_LOCAL_SIZE) {
			return metal_error(error, error_capacity,
							   "Metal raster kernel requires larger threadgroups", nil);
		}
		metal_context = context;
		return 1;
	}
}

static id<MTLBuffer> metal_buffer_reserve(id<MTLBuffer> buffer, size_t bytes, id<MTLDevice> device)
{
	if (buffer != nil && buffer.length >= bytes) {
		return buffer;
	}
	NSUInteger maximum = device.maxBufferLength;
	NSUInteger next = buffer != nil ? buffer.length : METAL_FIRST_BUFFER_BYTES;
	while (next < bytes && next <= maximum / 2U) {
		next *= 2U;
	}
	if (next < bytes || next > maximum) {
		next = bytes;
	}
	return [device newBufferWithLength:next options:MTLResourceStorageModeShared];
}

legacy_s32 metal_dispatch(const legacy_u32 *input, size_t input_words, legacy_u32 *output,
						  size_t output_words, legacy_u32 invocation_count, legacy_char *error,
						  size_t error_capacity)
{
	@autoreleasepool {
		RestuntsMetalContext *context = metal_context;
		if (context == nil || context->failed || context->pending != nil) {
			return metal_error(error, error_capacity, "Metal renderer is not ready", nil);
		}
		if (invocation_count == 0) {
			return 1;
		}
		if (input == NULL || output == NULL || input_words == 0 || output_words == 0 ||
			input_words > (size_t)-1 / sizeof(*input) ||
			output_words > (size_t)-1 / sizeof(*output)) {
			return metal_error(error, error_capacity, "Invalid Metal dispatch buffers", nil);
		}
		size_t input_bytes = input_words * sizeof(*input);
		size_t output_bytes = output_words * sizeof(*output);
		if (input_bytes > context->device.maxBufferLength ||
			output_bytes > context->device.maxBufferLength) {
			return metal_error(error, error_capacity, "Metal scene exceeds device storage limits",
							   nil);
		}
		context->input_buffer =
			metal_buffer_reserve(context->input_buffer, input_bytes, context->device);
		context->output_buffer =
			metal_buffer_reserve(context->output_buffer, output_bytes, context->device);
		if (context->input_buffer == nil || context->output_buffer == nil ||
			context->input_buffer.contents == NULL || context->output_buffer.contents == NULL) {
			return metal_error(error, error_capacity, "Cannot allocate Metal shared storage", nil);
		}
		memcpy(context->input_buffer.contents, input, input_bytes);
		memcpy(context->output_buffer.contents, output, output_bytes);
		id<MTLCommandBuffer> command = [context->queue commandBuffer];
		id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
		if (command == nil || encoder == nil) {
			return metal_error(error, error_capacity, "Cannot create Metal raster commands", nil);
		}
		[encoder setComputePipelineState:context->pipeline];
		[encoder setBuffer:context->input_buffer offset:0 atIndex:METAL_INPUT_BINDING];
		[encoder setBuffer:context->output_buffer offset:0 atIndex:METAL_OUTPUT_BINDING];
		NSUInteger groups = invocation_count / METAL_LOCAL_SIZE;
		if (invocation_count % METAL_LOCAL_SIZE != 0) {
			groups++;
		}
		[encoder dispatchThreadgroups:MTLSizeMake(groups, METAL_DISPATCH_Y, METAL_DISPATCH_Z)
				threadsPerThreadgroup:MTLSizeMake(METAL_LOCAL_SIZE, METAL_DISPATCH_Y,
												  METAL_DISPATCH_Z)];
		[encoder endEncoding];
		dispatch_semaphore_t completion = dispatch_semaphore_create(0);
		if (completion == NULL) {
			return metal_error(error, error_capacity, "Cannot create Metal completion signal", nil);
		}
		[command addCompletedHandler:^(id<MTLCommandBuffer> finished) {
		  (void)finished;
		  dispatch_semaphore_signal(completion);
		}];
		context->pending = command;
		[command commit];
		if (dispatch_semaphore_wait(
				completion, dispatch_time(DISPATCH_TIME_NOW, METAL_COMPLETION_TIMEOUT_NS)) != 0) {
			context->failed = 1;
			return metal_error(error, error_capacity,
							   "Timed out waiting for Metal raster completion", nil);
		}
		if (command.status != MTLCommandBufferStatusCompleted) {
			context->failed = 1;
			context->pending = nil;
			return metal_error(error, error_capacity, "Metal raster command failed", command.error);
		}
		context->pending = nil;
		/* Shared storage is CPU-visible after command completion on both Intel
		 * and Apple Silicon. No managed-resource synchronize blit is needed. */
		memcpy(output, context->output_buffer.contents, output_bytes);
		return 1;
	}
}
