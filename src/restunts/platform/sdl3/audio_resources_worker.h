#ifndef RESTUNTS_SDL3_AUDIO_RESOURCES_WORKER_H
#define RESTUNTS_SDL3_AUDIO_RESOURCES_WORKER_H

#include "../../c/legacy.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_RESOURCE_USAGE_MUSIC 1U
#define AUDIO_RESOURCE_USAGE_ENGINE 2U
#define AUDIO_RESOURCE_WORKER_MEMORY_BYTES 0x100000UL
#define AUDIO_RESOURCE_WORKER_FIRST_SEGMENT 0x1000U
#define AUDIO_RESOURCE_WORKER_END_SEGMENT 0xA000U
#define AUDIO_RESOURCE_WORKER_CHUNK_COUNT 50U

/* Resource allocation, release and mapping belong to one audio worker. The
 * source is an unmodified game resource copied before the command completes.
 * Keys identify original allocations, not addresses within this private arena. */
void *sdl3_audio_resource_clone(const void *source, size_t bytes, legacy_u8 usage);
void *sdl3_audio_resource_clone_key(legacy_u64 key, const void *source, size_t bytes,
									legacy_u8 usage);

/* Quiesce runs synchronously before removing any group, while its bytes remain
 * valid. It must stop every borrowed sequence/instrument pointer in that group
 * and must not recursively allocate or release private resources. */
void sdl3_audio_resources_set_quiesce(void (*quiesce)(legacy_u8 usage));
void sdl3_audio_resource_release(const void *source);
void sdl3_audio_resource_release_key(legacy_u64 key);
void sdl3_audio_resources_reset(void);
size_t sdl3_audio_resource_remaining(const void *pointer);
const legacy_char *sdl3_audio_resources_error(void);

/* Enable only around private resource mapping and audio sequencing/rendering.
 * Native execution state is thread-local; browser instances have their own
 * realm-local state. File loading and ordinary game memory retain their realm.
 * Returns zero if thread-local state could not be allocated. */
legacy_int sdl3_audio_resources_set_execution(legacy_u8 enabled);
legacy_u8 sdl3_audio_resources_is_execution(void);

/* Used by the DOS address adapter; the entire fixed address space is stable. */
legacy_u8 *sdl3_audio_resources_arena(void);

#ifdef __cplusplus
}
#endif

#endif
