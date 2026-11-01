#if !defined(RESTUNTS_SDL3)
#include "../frame/frame_render_private.h"

legacy_s16 frame_supersight_enabled(void)
{
	return supersight_enabled != 0;
}

legacy_s16 frame_supersight_uses_screen_depth(void)
{
	return 0;
}

legacy_s16 frame_supersight_adaptive_enabled(void)
{
	return 0;
}

legacy_s16 frame_supersight_tile_detail(const struct FRAME_TILE_SELECTION *tiles,
										legacy_s16 tile_index, legacy_s8 *detail)
{
	if (supersight_enabled == 0) {
		return 0;
	}
	*detail = tiles->lookahead[tile_index].detail >= tiles->detail_threshold;
	return 1;
}

legacy_s16 frame_supersight_prepare_tile(struct FRAME_TILE *tile)
{
	(void)tile;
	return 1;
}

void frame_supersight_racing_line_flags(legacy_u8 *flags)
{
	*flags |= SHAPE3D_NO_SHADOW_RECEIVE_FLAG;
}

void frame_supersight_override_car_detail(const struct TRACKOBJECT *track_object,
										  legacy_s8 tile_detail, legacy_s16 *low_detail)
{
	(void)track_object;
	if (supersight_enabled != 0) {
		*low_detail = tile_detail != FRAME_TILE_DETAIL_FULL;
	}
}

#endif
