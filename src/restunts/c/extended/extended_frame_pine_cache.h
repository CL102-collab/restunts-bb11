#ifndef RESTUNTS_EXTENDED_FRAME_PINE_CACHE_H
#define RESTUNTS_EXTENDED_FRAME_PINE_CACHE_H

#include "../legacy.h"

#ifdef RESTUNTS_SDL3
struct CSTRK_TRACK;
struct CSTRK_OBJECT;
struct SHAPE3D;

#define HIRES_FRAME_EXTENDED_PINE_CACHE_MAX_BYTES (16U * 1024U * 1024U)

/* Returns pine_count immutable meshes, one per tree, or NULL for the uncached
 * path. Only objects in the active track's owned array can be retained. Source
 * descriptors and authored bytes are checked on every hit, including custom
 * resources changed in place. Inputs remain frozen while render jobs run.
 * Borrowed meshes live until reset; callers must neither mutate nor free them. */
const struct SHAPE3D *hires_frame_extended_pine_cache_lookup(const struct CSTRK_TRACK *track,
															 const struct CSTRK_OBJECT *object,
															 const struct SHAPE3D *source);

/* Reset after all render jobs join, before replacing track/native resources.
 * The heap budget is capped so retained data plus fixed slots fit the maximum.
 * Zero disables retention and supplies an exact uncached diagnostic reference. */
void hires_frame_extended_pine_cache_reset(void);
void hires_frame_extended_pine_cache_set_budget(legacy_u32 bytes);
legacy_u32 hires_frame_extended_pine_cache_bytes(void);
#endif

#endif
