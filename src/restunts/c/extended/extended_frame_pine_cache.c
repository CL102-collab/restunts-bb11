#include "extended_frame_pine_cache.h"

#ifdef RESTUNTS_SDL3
#include "extended_track.h"
#include "extended_geometry.h"
#include "pine_trees.h"
#include <SDL3/SDL_atomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PINE_CACHE_SOURCE_COUNT 2U
#define PINE_CACHE_PAINT_COUNT 1U
#define PINE_CACHE_MASK_LIST_COUNT 2U
#define PINE_CACHE_TERMINATOR_BYTES 2U

struct HIRES_PINE_CACHE_ENTRY {
	const struct CSTRK_OBJECT *objects;
	const struct SHAPE3D *source_identity;
	struct CSTRK_OBJECT object;
	struct SHAPE3D source;
	legacy_u32 object_count, vertex_bytes;
	legacy_u16 width, height;
	struct SHAPE3D trees[CSTRK_PINE_GROUP_LARGE_COUNT];
	legacy_u8 source_bytes[];
};

struct HIRES_PINE_CACHE_SLOT {
	SDL_SpinLock lock;
	void *sources[PINE_CACHE_SOURCE_COUNT];
};

static struct HIRES_PINE_CACHE_SLOT pine_cache[CSTRK_MAX_OBJECTS];
static const legacy_u8 pine_cache_vertex_counts[SHAPE3D_PRIMITIVE_TYPE_COUNT] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 2, 6, 3, 0, 0};
static SDL_AtomicInt pine_cache_bytes;
static legacy_u32 pine_cache_budget =
	HIRES_FRAME_EXTENDED_PINE_CACHE_MAX_BYTES - sizeof(pine_cache);

static legacy_u8 pine_cache_reserve(legacy_u32 bytes)
{
	legacy_s32 previous = SDL_GetAtomicInt(&pine_cache_bytes);
	for (;;) {
		if (bytes > pine_cache_budget || (legacy_u32)previous > pine_cache_budget - bytes) {
			return 0;
		}
		if (SDL_CompareAndSwapAtomicInt(&pine_cache_bytes, previous,
										previous + (legacy_s32)bytes)) {
			return 1;
		}
		previous = SDL_GetAtomicInt(&pine_cache_bytes);
	}
}

static void pine_cache_free_entry(struct HIRES_PINE_CACHE_ENTRY *entry)
{
	for (legacy_u8 tree = 0; tree < entry->object.pine_count; tree++) {
		extended_geometry_free_shape(&entry->trees[tree]);
	}
	free(entry);
}

void hires_frame_extended_pine_cache_reset(void)
{
	for (legacy_u16 index = 0; index < CSTRK_MAX_OBJECTS; index++) {
		for (legacy_u8 source = 0; source < PINE_CACHE_SOURCE_COUNT; source++) {
			struct HIRES_PINE_CACHE_ENTRY *entry = pine_cache[index].sources[source];
			if (entry != NULL) {
				pine_cache_free_entry(entry);
				pine_cache[index].sources[source] = NULL;
			}
		}
	}
	SDL_SetAtomicInt(&pine_cache_bytes, 0);
}

void hires_frame_extended_pine_cache_set_budget(legacy_u32 bytes)
{
	hires_frame_extended_pine_cache_reset();
	legacy_u32 maximum = HIRES_FRAME_EXTENDED_PINE_CACHE_MAX_BYTES - sizeof(pine_cache);
	pine_cache_budget = bytes < maximum ? bytes : maximum;
}

legacy_u32 hires_frame_extended_pine_cache_bytes(void)
{
	return (legacy_u32)SDL_GetAtomicInt(&pine_cache_bytes);
}

/* Only the native byte-counted shape format is admitted. Larger or unsupported
 * custom descriptors retain their existing uncached behavior. */
static legacy_u8 pine_cache_source_size(const struct SHAPE3D *source, legacy_u32 *primitive_bytes,
										legacy_u32 *cooked_bytes)
{
	if (source->shape3d_numverts == 0 || source->shape3d_numverts > LEGACY_U8_MAX ||
		source->shape3d_numprimitives > LEGACY_U8_MAX || source->shape3d_numpaints == 0 ||
		source->shape3d_numpaints > LEGACY_U8_MAX || source->shape3d_vertex_bytes == NULL ||
		source->shape3d_primitives == NULL) {
		return 0;
	}
	*primitive_bytes = 0;
	*cooked_bytes = (legacy_u32)source->shape3d_numverts * SHAPE3D_VERTEX_SIZE +
					(legacy_u32)source->shape3d_numprimitives * SHAPE3D_VISIBILITY_MASK_SIZE *
						PINE_CACHE_MASK_LIST_COUNT +
					PINE_CACHE_TERMINATOR_BYTES;
	const legacy_u8 *primitive = source->shape3d_primitives;
	for (legacy_u16 index = 0; index < source->shape3d_numprimitives; index++) {
		legacy_u8 type = primitive[0];
		if (type >= SHAPE3D_PRIMITIVE_TYPE_COUNT || pine_cache_vertex_counts[type] == 0) {
			return 0;
		}
		legacy_u32 bytes = SHAPE3D_PRIMITIVE_HEADER_SIZE + source->shape3d_numpaints +
						   pine_cache_vertex_counts[type];
		*primitive_bytes += bytes;
		*cooked_bytes +=
			SHAPE3D_PRIMITIVE_HEADER_SIZE + PINE_CACHE_PAINT_COUNT + pine_cache_vertex_counts[type];
		primitive += bytes;
	}
	return 1;
}

static legacy_u8 pine_cache_object_matches(const struct HIRES_PINE_CACHE_ENTRY *entry,
										   const struct CSTRK_TRACK *track,
										   const struct CSTRK_OBJECT *object)
{
	return entry->objects == track->objects && entry->object_count == track->object_count &&
		   entry->width == track->width && entry->height == track->height &&
		   memcmp(&entry->object, object, sizeof(*object)) == 0;
}

static legacy_u8 pine_cache_source_matches(const struct HIRES_PINE_CACHE_ENTRY *entry,
										   const struct SHAPE3D *source)
{
	if (memcmp(&entry->source, source, sizeof(*source)) != 0 ||
		memcmp(entry->source_bytes, source->shape3d_vertex_bytes, entry->vertex_bytes) != 0) {
		return 0;
	}
	const legacy_u8 *saved = entry->source_bytes + entry->vertex_bytes;
	const legacy_u8 *current = source->shape3d_primitives;
	for (legacy_u16 index = 0; index < source->shape3d_numprimitives; index++) {
		/* A rewritten custom resource may keep its descriptor and buffer
		 * address while shortening a record. Check its type before using the
		 * saved record's extent; equal types have equal byte counts. */
		if (current[0] != saved[0]) {
			return 0;
		}
		legacy_u32 bytes = SHAPE3D_PRIMITIVE_HEADER_SIZE + source->shape3d_numpaints +
						   pine_cache_vertex_counts[saved[0]];
		if (memcmp(current, saved, bytes) != 0) {
			return 0;
		}
		current += bytes;
		saved += bytes;
	}
	return 1;
}

static struct HIRES_PINE_CACHE_ENTRY *pine_cache_create(const struct CSTRK_TRACK *track,
														const struct CSTRK_OBJECT *object,
														const struct SHAPE3D *source)
{
	legacy_u32 primitive_bytes, cooked_bytes;
	if (!pine_cache_source_size(source, &primitive_bytes, &cooked_bytes)) {
		return NULL;
	}
	legacy_u32 vertex_bytes = (legacy_u32)source->shape3d_numverts * SHAPE3D_VERTEX_SIZE;
	legacy_u32 bytes = (legacy_u32)sizeof(struct HIRES_PINE_CACHE_ENTRY) + vertex_bytes +
					   primitive_bytes + object->pine_count * cooked_bytes;
	if (!pine_cache_reserve(bytes)) {
		return NULL;
	}
	struct HIRES_PINE_CACHE_ENTRY *entry =
		calloc(1, sizeof(*entry) + vertex_bytes + primitive_bytes);
	if (entry == NULL) {
		SDL_AddAtomicInt(&pine_cache_bytes, -(legacy_s32)bytes);
		return NULL;
	}
	entry->objects = track->objects;
	entry->source_identity = source;
	entry->object = *object;
	entry->source = *source;
	entry->object_count = track->object_count;
	entry->width = track->width;
	entry->height = track->height;
	entry->vertex_bytes = vertex_bytes;
	memcpy(entry->source_bytes, source->shape3d_vertex_bytes, vertex_bytes);
	memcpy(entry->source_bytes + vertex_bytes, source->shape3d_primitives, primitive_bytes);
	for (legacy_u8 tree = 0; tree < object->pine_count; tree++) {
		if (!pine_trees_create_mesh(source, &entry->trees[tree], object, tree)) {
			pine_cache_free_entry(entry);
			SDL_AddAtomicInt(&pine_cache_bytes, -(legacy_s32)bytes);
			return NULL;
		}
	}
	return entry;
}

/* Published entries are immutable. Atomic loads keep hits read-only; only a
 * miss needs the slot lock, followed by a second lookup before publication. */
static legacy_u8 pine_cache_find(struct HIRES_PINE_CACHE_SLOT *slot,
								 const struct CSTRK_TRACK *track, const struct CSTRK_OBJECT *object,
								 const struct SHAPE3D *source, const struct SHAPE3D **result,
								 void ***empty)
{
	*empty = NULL;
	for (legacy_u8 candidate = 0; candidate < PINE_CACHE_SOURCE_COUNT; candidate++) {
		struct HIRES_PINE_CACHE_ENTRY *entry = SDL_GetAtomicPointer(&slot->sources[candidate]);
		if (entry == NULL) {
			if (*empty == NULL) {
				*empty = &slot->sources[candidate];
			}
			continue;
		}
		if (!pine_cache_object_matches(entry, track, object)) {
			/* Old meshes may still be borrowed. Retain them until joined reset. */
			return 1;
		}
		if (entry->source_identity == source ||
			memcmp(&entry->source, source, sizeof(*source)) == 0) {
			if (pine_cache_source_matches(entry, source)) {
				*result = entry->trees;
			}
			return 1;
		}
	}
	return 0;
}

const struct SHAPE3D *hires_frame_extended_pine_cache_lookup(const struct CSTRK_TRACK *track,
															 const struct CSTRK_OBJECT *object,
															 const struct SHAPE3D *source)
{
	if (!pine_cache_budget || track == NULL || track != extended_track_model() || object == NULL ||
		source == NULL || track->objects == NULL || track->object_count > CSTRK_MAX_OBJECTS ||
		object->legacy_id != CSTRK_PINE_TREE_ID ||
		(object->pine_count != CSTRK_PINE_GROUP_SMALL_COUNT &&
		 object->pine_count != CSTRK_PINE_GROUP_LARGE_COUNT)) {
		return NULL;
	}
	uintptr_t first = (uintptr_t)track->objects, address = (uintptr_t)object;
	if (address < first || address - first >= (size_t)track->object_count * sizeof(*object) ||
		(address - first) % sizeof(*object) != 0) {
		return NULL;
	}
	legacy_u32 index = (legacy_u32)((address - first) / sizeof(*object));
	struct HIRES_PINE_CACHE_SLOT *slot = &pine_cache[index];
	void **empty;
	const struct SHAPE3D *result = NULL;
	if (pine_cache_find(slot, track, object, source, &result, &empty) || empty == NULL) {
		return result;
	}
	if (!SDL_TryLockSpinlock(&slot->lock)) {
		return NULL;
	}
	if (!pine_cache_find(slot, track, object, source, &result, &empty) && empty != NULL) {
		struct HIRES_PINE_CACHE_ENTRY *entry = pine_cache_create(track, object, source);
		if (entry != NULL) {
			SDL_SetAtomicPointer(empty, entry);
			result = entry->trees;
		}
	}
	SDL_UnlockSpinlock(&slot->lock);
	return result;
}
#endif
