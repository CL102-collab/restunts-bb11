#include "extended_horizon.h"

#ifdef RESTUNTS_SDL3
#include "extended_track.h"
#include "../shape2d/shape2d.h"
/* Host headers must precede the legacy CRT aliases in externs.h. */
#include <SDL3/SDL.h>
#include "../externs.h"

#define HORIZON_NO_IMAGES_FLAG 8U
#define HORIZON_GROUND_MATERIAL 16U
#define HORIZON_SKY_MATERIAL 17U
#define HORIZON_WATER_MATERIAL 100U

/* Keep the selected reference even when its file is missing and a built-in set
 * is displayed. The same numeric fallback can represent many custom images. */
static legacy_s8 selected_horizon[CSTRK_MAX_HORIZON_NAME + 1U];

legacy_s16 extended_horizon_skybox_release(void)
{
	legacy_s16 owned = extended_horizon_active();
	extended_horizon_unload();
	return owned;
}

legacy_s16 extended_horizon_skybox_prepare(legacy_s8 skybox_index)
{
	if ((legacy_u8)skybox_index & HORIZON_NO_IMAGES_FLAG) {
		return 0;
	}
	static const legacy_s8 no_custom[] = "";
	const legacy_s8 *name =
		extended_track_active() ? extended_track_model()->custom_horizon : no_custom;
	if (skybox_resources_loaded && loaded_skybox_index == skybox_index &&
		SDL_strcmp((const legacy_char *)name, (const legacy_char *)selected_horizon) == 0) {
		return extended_horizon_active();
	}
	if (extended_horizon_active() || name[0]) {
		unload_skybox();
	}
	SDL_strlcpy((legacy_char *)selected_horizon, (const legacy_char *)name,
				sizeof(selected_horizon));
	if (!name[0] || !extended_horizon_load(name)) {
		return 0;
	}
	loaded_skybox_index = skybox_index;
	skybox_res_ofs = NULL;
	skybox_resources_loaded = 1;
	for (legacy_u16 image = 0; image < SKYBOX_IMAGE_COUNT; image++) {
		skyboxes[image] = extended_horizon_shape((legacy_s16)image);
		skybox.heights[image] = shape2d_get_height(skyboxes[image]);
	}
	skybox.minimum_height = skybox.maximum_height = skybox.heights[0];
	skybox.sky_color = material_clrlist_ptr[HORIZON_SKY_MATERIAL];
	skybox.ground_color = material_clrlist_ptr[HORIZON_GROUND_MATERIAL];
	skybox.water_color = material_clrlist_ptr[HORIZON_WATER_MATERIAL];
	return 1;
}
#endif
