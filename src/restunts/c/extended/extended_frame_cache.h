#ifndef RESTUNTS_EXTENDED_FRAME_CACHE_H
#define RESTUNTS_EXTENDED_FRAME_CACHE_H
#include "../legacy.h"
#ifdef RESTUNTS_SDL3
struct FRAME_EXTENDED_TERRAIN;
struct FRAME_HIRES_EXTENDED_PRECISE_GROUND_FRAGMENT;
struct VECTOR;
struct HIRES_PRECISE_GROUND_CACHE_SLOT;
struct HIRES_PRECISE_GROUND_CACHE_FACE;
/* A miss can retain a tile lock until clipping either publishes or aborts.
 * Published fragments stay immutable until a joined cache reset. */
struct FRAME_HIRES_GROUND_CACHE_PENDING {
	struct HIRES_PRECISE_GROUND_CACHE_SLOT *slot;
	struct HIRES_PRECISE_GROUND_CACHE_FACE *face;
};
legacy_u8 hires_frame_extended_ground_cache_lookup(
	const struct FRAME_EXTENDED_TERRAIN *terrain, const struct VECTOR *source, legacy_u16 count,
	struct FRAME_HIRES_GROUND_CACHE_PENDING *pending,
	const struct FRAME_HIRES_EXTENDED_PRECISE_GROUND_FRAGMENT **fragments,
	legacy_u16 *fragment_count);
legacy_u8 hires_frame_extended_ground_cache_retain(
	struct FRAME_HIRES_GROUND_CACHE_PENDING *pending, const struct VECTOR *source, legacy_u16 count,
	struct FRAME_HIRES_EXTENDED_PRECISE_GROUND_FRAGMENT *fragments, legacy_u16 capacity,
	legacy_u16 fragment_count);
void hires_frame_extended_ground_cache_release(struct FRAME_HIRES_GROUND_CACHE_PENDING *pending);
#endif
#endif
