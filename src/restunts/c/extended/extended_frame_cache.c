#ifdef RESTUNTS_SDL3
#include "extended_frame_internal.h"
#include "extended_frame_cache.h"
#include <SDL3/SDL_atomic.h>

/* Only local road-cut fragments are retained. Painting, shoreline splitting,
 * camera transforms and queue submission still run for every frame. Entries are
 * immutable once published; load/unload/reset must run after render jobs join. */
#define HIRES_PRECISE_GROUND_CACHE_FACE_COUNT 16U
#define HIRES_PRECISE_GROUND_CACHE_MAX_BYTES (32U * 1024U * 1024U)

struct HIRES_PRECISE_GROUND_CACHE_OBJECT {
	struct CSTRK_OBJECT object;
	legacy_s16 model, yaw;
	legacy_u8 graded;
};

struct HIRES_PRECISE_GROUND_CACHE_FACE {
	struct VECTOR source[SHAPE3D_POLYGON_MAX_VERTICES];
	struct FRAME_HIRES_EXTENDED_PRECISE_GROUND_FRAGMENT *fragments;
	legacy_u16 source_count, fragment_count;
};

struct HIRES_PRECISE_GROUND_CACHE_TILE {
	struct HIRES_PRECISE_GROUND_CACHE_FACE faces[HIRES_PRECISE_GROUND_CACHE_FACE_COUNT];
	legacy_u16 object_count;
	legacy_u8 base_height;
	struct HIRES_PRECISE_GROUND_CACHE_OBJECT objects[];
};

struct HIRES_PRECISE_GROUND_CACHE_SLOT {
	SDL_SpinLock lock;
	struct HIRES_PRECISE_GROUND_CACHE_TILE *tile;
};

static struct HIRES_PRECISE_GROUND_CACHE_SLOT ground_cache[EXTENDED_TRACK_MAX_CELLS];
static SDL_AtomicInt ground_cache_bytes;
static legacy_u32 ground_cache_budget = HIRES_PRECISE_GROUND_CACHE_MAX_BYTES - sizeof(ground_cache);

static legacy_u8 ground_cache_reserve(legacy_u32 bytes)
{
	legacy_s32 previous = SDL_GetAtomicInt(&ground_cache_bytes);
	for (;;) {
		if (bytes > ground_cache_budget || (legacy_u32)previous > ground_cache_budget - bytes) {
			return 0;
		}
		if (SDL_CompareAndSwapAtomicInt(&ground_cache_bytes, previous,
										previous + (legacy_s32)bytes)) {
			return 1;
		}
		previous = SDL_GetAtomicInt(&ground_cache_bytes);
	}
}

void hires_frame_extended_ground_cache_reset(void)
{
	for (legacy_u16 index = 0; index < EXTENDED_TRACK_MAX_CELLS; index++) {
		struct HIRES_PRECISE_GROUND_CACHE_TILE *tile = ground_cache[index].tile;
		if (!tile) {
			continue;
		}
		for (legacy_u16 face = 0; face < HIRES_PRECISE_GROUND_CACHE_FACE_COUNT; face++) {
			free(tile->faces[face].fragments);
		}
		free(tile);
		ground_cache[index].tile = NULL;
	}
	SDL_SetAtomicInt(&ground_cache_bytes, 0);
}

/* A zero budget also provides an exact uncached reference for diagnostics. */
void hires_frame_extended_ground_cache_set_budget(legacy_u32 bytes)
{
	hires_frame_extended_ground_cache_reset();
	legacy_u32 maximum = HIRES_PRECISE_GROUND_CACHE_MAX_BYTES - sizeof(ground_cache);
	ground_cache_budget = bytes < maximum ? bytes : maximum;
}

legacy_u32 hires_frame_extended_ground_cache_bytes(void)
{
	return (legacy_u32)SDL_GetAtomicInt(&ground_cache_bytes);
}

static legacy_u8 ground_cache_matches(const struct HIRES_PRECISE_GROUND_CACHE_TILE *tile,
									  const struct FRAME_EXTENDED_TERRAIN *terrain)
{
	if (tile->base_height != terrain->profile.base_height ||
		tile->object_count != terrain->object_count) {
		return 0;
	}
	for (legacy_u16 index = 0; index < terrain->object_count; index++) {
		const struct CSTRK_OBJECT *object = terrain->objects[index];
		const struct HIRES_PRECISE_GROUND_CACHE_OBJECT *key = &tile->objects[index];
		const struct TRACKOBJECT *definition = &trkObjectList[extended_track_object_id(object)];
		if (memcmp(&key->object, object, sizeof(*object)) != 0 ||
			key->model != definition->ss_physicalModel || key->yaw != definition->ss_rotY ||
			key->graded != (legacy_u8)frame_extended_uses_grade(object)) {
			return 0;
		}
	}
	return 1;
}

/* Keep the tile try-lock until a miss has been computed and published. A second
 * worker can always use the original clipping path without waiting on a fill. */
static struct HIRES_PRECISE_GROUND_CACHE_SLOT *
ground_cache_acquire(const struct FRAME_EXTENDED_TERRAIN *terrain)
{
	if (!ground_cache_budget || terrain->column < 0 || terrain->row < 0 ||
		(legacy_u16)terrain->column >= EXTENDED_TRACK_MAX_DIMENSION ||
		(legacy_u16)terrain->row >= EXTENDED_TRACK_MAX_DIMENSION) {
		return NULL;
	}
	legacy_u16 index = (legacy_u16)(terrain->row * EXTENDED_TRACK_MAX_DIMENSION + terrain->column);
	struct HIRES_PRECISE_GROUND_CACHE_SLOT *slot = &ground_cache[index];
	if (!SDL_TryLockSpinlock(&slot->lock)) {
		return NULL;
	}
	if (slot->tile) {
		if (ground_cache_matches(slot->tile, terrain)) {
			return slot;
		}
		/* Mutable editor/resource inputs can invalidate an individual tile. Keep
		 * its old entries alive for readers until the next joined reset. */
		SDL_UnlockSpinlock(&slot->lock);
		return NULL;
	}
	legacy_u32 bytes = sizeof(*slot->tile) + terrain->object_count * sizeof(slot->tile->objects[0]);
	if (!ground_cache_reserve(bytes)) {
		SDL_UnlockSpinlock(&slot->lock);
		return NULL;
	}
	struct HIRES_PRECISE_GROUND_CACHE_TILE *tile = calloc(1, bytes);
	if (!tile) {
		SDL_AddAtomicInt(&ground_cache_bytes, -(legacy_s32)bytes);
		SDL_UnlockSpinlock(&slot->lock);
		return NULL;
	}
	tile->object_count = terrain->object_count;
	tile->base_height = terrain->profile.base_height;
	for (legacy_u16 object_index = 0; object_index < terrain->object_count; object_index++) {
		const struct CSTRK_OBJECT *object = terrain->objects[object_index];
		const struct TRACKOBJECT *definition = &trkObjectList[extended_track_object_id(object)];
		tile->objects[object_index].object = *object;
		tile->objects[object_index].model = definition->ss_physicalModel;
		tile->objects[object_index].yaw = definition->ss_rotY;
		tile->objects[object_index].graded = (legacy_u8)frame_extended_uses_grade(object);
	}
	slot->tile = tile;
	return slot;
}

legacy_u8 hires_frame_extended_ground_cache_lookup(
	const struct FRAME_EXTENDED_TERRAIN *terrain, const struct VECTOR *source, legacy_u16 count,
	struct FRAME_HIRES_GROUND_CACHE_PENDING *pending,
	const struct FRAME_HIRES_EXTENDED_PRECISE_GROUND_FRAGMENT **fragments,
	legacy_u16 *fragment_count)
{
	pending->slot = NULL;
	pending->face = NULL;
	struct HIRES_PRECISE_GROUND_CACHE_SLOT *slot = ground_cache_acquire(terrain);
	if (!slot) {
		return 0;
	}
	for (legacy_u16 face = 0; face < HIRES_PRECISE_GROUND_CACHE_FACE_COUNT; face++) {
		struct HIRES_PRECISE_GROUND_CACHE_FACE *candidate = &slot->tile->faces[face];
		if (!candidate->source_count) {
			pending->slot = slot;
			pending->face = candidate;
			return 0;
		}
		if (candidate->source_count == count &&
			memcmp(candidate->source, source, count * sizeof(*source)) == 0) {
			*fragments = candidate->fragments;
			*fragment_count = candidate->fragment_count;
			SDL_UnlockSpinlock(&slot->lock);
			return 1;
		}
	}
	SDL_UnlockSpinlock(&slot->lock);
	return 0;
}

void hires_frame_extended_ground_cache_release(struct FRAME_HIRES_GROUND_CACHE_PENDING *pending)
{
	if (pending->slot) {
		SDL_UnlockSpinlock(&pending->slot->lock);
	}
	pending->slot = NULL;
	pending->face = NULL;
}

legacy_u8 hires_frame_extended_ground_cache_retain(
	struct FRAME_HIRES_GROUND_CACHE_PENDING *pending, const struct VECTOR *source, legacy_u16 count,
	struct FRAME_HIRES_EXTENDED_PRECISE_GROUND_FRAGMENT *fragments, legacy_u16 capacity,
	legacy_u16 fragment_count)
{
	if (!pending->slot) {
		return 0;
	}
	legacy_u8 retained = 0;
	legacy_u32 bytes = capacity * sizeof(*fragments);
	if (ground_cache_reserve(bytes)) {
		memcpy(pending->face->source, source, count * sizeof(*source));
		pending->face->source_count = count;
		pending->face->fragment_count = fragment_count;
		pending->face->fragments = fragments;
		retained = 1;
	}
	hires_frame_extended_ground_cache_release(pending);
	return retained;
}

#endif
