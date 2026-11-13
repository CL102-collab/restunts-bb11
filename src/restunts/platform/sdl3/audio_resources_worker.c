#include "audio_resources_worker.h"
#include <SDL3/SDL.h>
#include <stdint.h>
#include <string.h>

#define AUDIO_RESOURCE_PARAGRAPH_BYTES 16U
#define AUDIO_RESOURCE_ERROR_BYTES 160U
#define AUDIO_RESOURCE_ALL_USAGES (AUDIO_RESOURCE_USAGE_MUSIC | AUDIO_RESOURCE_USAGE_ENGINE)
#define AUDIO_RESOURCE_FIRST_BYTE                                                                  \
	(AUDIO_RESOURCE_WORKER_FIRST_SEGMENT * AUDIO_RESOURCE_PARAGRAPH_BYTES)
#define AUDIO_RESOURCE_END_BYTE (AUDIO_RESOURCE_WORKER_END_SEGMENT * AUDIO_RESOURCE_PARAGRAPH_BYTES)

struct AUDIO_PRIVATE_RESOURCE {
	legacy_u64 key;
	size_t offset;
	size_t length;
	size_t reserved;
	legacy_u8 usage;
};

static legacy_u8 audio_resource_memory[AUDIO_RESOURCE_WORKER_MEMORY_BYTES];
static struct AUDIO_PRIVATE_RESOURCE audio_resources[AUDIO_RESOURCE_WORKER_CHUNK_COUNT];
static void (*audio_resources_quiesce)(legacy_u8 usage);
static legacy_char audio_resource_error[AUDIO_RESOURCE_ERROR_BYTES];

#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
static legacy_u8 audio_resource_execution;
#else
static SDL_TLSID audio_resource_execution;
static const legacy_u8 audio_resource_execution_token;
#endif

legacy_int sdl3_audio_resources_set_execution(legacy_u8 enabled)
{
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
	audio_resource_execution = enabled != 0;
	return 1;
#else
	if (!SDL_SetTLS(&audio_resource_execution, enabled ? &audio_resource_execution_token : NULL,
					NULL)) {
		SDL_strlcpy(audio_resource_error, "Cannot allocate the audio worker memory context.",
					sizeof(audio_resource_error));
		return 0;
	}
	return 1;
#endif
}

legacy_u8 sdl3_audio_resources_is_execution(void)
{
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
	return audio_resource_execution;
#else
	return SDL_GetTLS(&audio_resource_execution) != NULL;
#endif
}

legacy_u8 *sdl3_audio_resources_arena(void)
{
	return audio_resource_memory;
}

const legacy_char *sdl3_audio_resources_error(void)
{
	return audio_resource_error;
}

void sdl3_audio_resources_set_quiesce(void (*quiesce)(legacy_u8 usage))
{
	audio_resources_quiesce = quiesce;
}

static size_t audio_resource_find_space(size_t bytes)
{
	size_t candidate = AUDIO_RESOURCE_FIRST_BYTE;
	while (candidate <= AUDIO_RESOURCE_END_BYTE - bytes) {
		size_t next = candidate;
		for (size_t index = 0; index < AUDIO_RESOURCE_WORKER_CHUNK_COUNT; ++index) {
			const struct AUDIO_PRIVATE_RESOURCE *resource = &audio_resources[index];
			if (resource->usage != 0 && candidate < resource->offset + resource->reserved &&
				resource->offset < candidate + bytes &&
				resource->offset + resource->reserved > next) {
				next = resource->offset + resource->reserved;
			}
		}
		if (candidate == next) {
			return candidate;
		}
		candidate = next;
	}
	return 0;
}

void *sdl3_audio_resource_clone_key(legacy_u64 key, const void *source, size_t bytes,
									legacy_u8 usage)
{
	audio_resource_error[0] = 0;
	if (key == 0 || source == NULL || bytes == 0 ||
		bytes > AUDIO_RESOURCE_END_BYTE - AUDIO_RESOURCE_FIRST_BYTE || usage == 0 ||
		(usage & ~AUDIO_RESOURCE_ALL_USAGES) != 0) {
		SDL_strlcpy(audio_resource_error, "Invalid private audio resource.",
					sizeof(audio_resource_error));
		return NULL;
	}

	struct AUDIO_PRIVATE_RESOURCE *available = NULL;
	for (size_t index = 0; index < AUDIO_RESOURCE_WORKER_CHUNK_COUNT; ++index) {
		struct AUDIO_PRIVATE_RESOURCE *resource = &audio_resources[index];
		if (resource->usage != 0 && resource->key == key) {
			if (resource->length != bytes) {
				SDL_strlcpy(audio_resource_error,
							"An audio resource allocation changed before it was released.",
							sizeof(audio_resource_error));
				return NULL;
			}
			resource->usage |= usage;
			return audio_resource_memory + resource->offset;
		}
		if (resource->usage == 0 && available == NULL) {
			available = resource;
		}
	}
	if (available == NULL) {
		SDL_strlcpy(audio_resource_error, "Too many private audio resources.",
					sizeof(audio_resource_error));
		return NULL;
	}

	size_t reserved = (bytes + AUDIO_RESOURCE_PARAGRAPH_BYTES - 1U) &
					  ~(size_t)(AUDIO_RESOURCE_PARAGRAPH_BYTES - 1U);
	size_t offset = audio_resource_find_space(reserved);
	if (offset == 0) {
		SDL_strlcpy(audio_resource_error, "Not enough private audio resource memory.",
					sizeof(audio_resource_error));
		return NULL;
	}
	memcpy(audio_resource_memory + offset, source, bytes);
	memset(audio_resource_memory + offset + bytes, 0, reserved - bytes);
	available->key = key;
	available->offset = offset;
	available->length = bytes;
	available->reserved = reserved;
	available->usage = usage;
	return audio_resource_memory + offset;
}

void *sdl3_audio_resource_clone(const void *source, size_t bytes, legacy_u8 usage)
{
	return sdl3_audio_resource_clone_key((legacy_u64)(uintptr_t)source, source, bytes, usage);
}

static void audio_resource_release_usages(legacy_u8 usage)
{
	if (usage == 0) {
		return;
	}
	if (audio_resources_quiesce != NULL) {
		audio_resources_quiesce(usage);
	}
	for (size_t index = 0; index < AUDIO_RESOURCE_WORKER_CHUNK_COUNT; ++index) {
		struct AUDIO_PRIVATE_RESOURCE *resource = &audio_resources[index];
		resource->usage &= (legacy_u8)~usage;
		if (resource->usage == 0) {
			memset(resource, 0, sizeof(*resource));
		}
	}
}

void sdl3_audio_resource_release_key(legacy_u64 key)
{
	for (size_t index = 0; index < AUDIO_RESOURCE_WORKER_CHUNK_COUNT; ++index) {
		const struct AUDIO_PRIVATE_RESOURCE *resource = &audio_resources[index];
		if (resource->usage != 0 && resource->key == key) {
			audio_resource_release_usages(resource->usage);
			return;
		}
	}
}

void sdl3_audio_resource_release(const void *source)
{
	sdl3_audio_resource_release_key((legacy_u64)(uintptr_t)source);
}

void sdl3_audio_resources_reset(void)
{
	legacy_u8 usage = 0;
	for (size_t index = 0; index < AUDIO_RESOURCE_WORKER_CHUNK_COUNT; ++index) {
		usage |= audio_resources[index].usage;
	}
	audio_resource_release_usages(usage);
	audio_resource_error[0] = 0;
}

size_t sdl3_audio_resource_remaining(const void *pointer)
{
	uintptr_t address = (uintptr_t)pointer;
	uintptr_t base = (uintptr_t)audio_resource_memory;
	if (address < base || address - base >= AUDIO_RESOURCE_WORKER_MEMORY_BYTES) {
		return 0;
	}
	size_t offset = (size_t)(address - base);
	for (size_t index = 0; index < AUDIO_RESOURCE_WORKER_CHUNK_COUNT; ++index) {
		const struct AUDIO_PRIVATE_RESOURCE *resource = &audio_resources[index];
		if (resource->usage != 0 && offset >= resource->offset &&
			offset - resource->offset < resource->length) {
			return resource->length - (offset - resource->offset);
		}
	}
	return 0;
}
