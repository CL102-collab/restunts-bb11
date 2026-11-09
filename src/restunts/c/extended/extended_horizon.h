#ifndef RESTUNTS_EXTENDED_HORIZON_H
#define RESTUNTS_EXTENDED_HORIZON_H

#include "../legacy.h"
#include "../skybox.h"

#define EXTENDED_HORIZON_WIDTH 4096
#define EXTENDED_HORIZON_MIN_HEIGHT 64
#define EXTENDED_HORIZON_MAX_HEIGHT 120
#define EXTENDED_HORIZON_SAMPLE_SCALE 4
#define EXTENDED_HORIZON_SKY_RED 93U
#define EXTENDED_HORIZON_SKY_GREEN 255U
#define EXTENDED_HORIZON_SKY_BLUE 255U

struct SDL_Surface;
struct SHAPE2D;

#ifdef RESTUNTS_SDL3
/* Only horizons/ in the selected game-data directory is searched. Source names
 * are UTF-8 basenames; ambiguous ASCII-casefolded spellings are rejected.
 * The module owns its panorama, strip views and derived classic sprites. */
legacy_s16 extended_horizon_load(const legacy_s8 *name);
void extended_horizon_unload(void);
void extended_horizon_palette_changed(void);
legacy_s16 extended_horizon_active(void);
legacy_s16 extended_horizon_matches(const legacy_s8 *name);
struct SDL_Surface *extended_horizon_image(legacy_s16 image);
struct SHAPE2D *extended_horizon_shape(legacy_s16 image);

/* Shared skybox-resource hooks keep SDL allocations outside the legacy arena. */
legacy_s16 extended_horizon_skybox_prepare(legacy_s8 skybox_index);
legacy_s16 extended_horizon_skybox_release(void);
#endif

#endif
