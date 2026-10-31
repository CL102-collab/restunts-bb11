#include "extended_track_preview.h"

#ifdef RESTUNTS_SDL3
#include "extended_track.h"
#include "../hires/track_preview_track.h"
#include "../shape2d/shape2d.h"
#include "../shape3d/shape3d.h"
#include "../track/track_objects.h"
#include "../track/trackdata_layout.h"
#include "../platform.h"
#include "../externs.h"

/* Keep the original surface map for the classic renderer. */
enum TRACK_PREVIEW_EXTENDED_COLOR {
	TRACK_PREVIEW_EXTENDED_GRASS_COLOR = 2,
	TRACK_PREVIEW_EXTENDED_HIGH_GRASS_COLOR = 10,
	TRACK_PREVIEW_EXTENDED_ICE_COLOR = 11,
	TRACK_PREVIEW_EXTENDED_DIRT_COLOR = 6,
	TRACK_PREVIEW_EXTENDED_WATER_COLOR = 9
};

#define TRACK_PREVIEW_EXTENDED_RAISED_GRASS_LEVEL 1U
static legacy_u8 track_preview_extended_surface_color(legacy_u8 surface, legacy_u8 level)
{
	static const legacy_u8 colors[] = {
		TRACK_PREVIEW_EXTENDED_GRASS_COLOR, TRACK_PREVIEW_EXTENDED_ICE_COLOR,
		TRACK_PREVIEW_EXTENDED_DIRT_COLOR, TRACK_PREVIEW_EXTENDED_WATER_COLOR};
	if (surface == CSTRK_TERRAIN_GRASS && level > TRACK_PREVIEW_EXTENDED_RAISED_GRASS_LEVEL) {
		return TRACK_PREVIEW_EXTENDED_HIGH_GRASS_COLOR;
	}
	return colors[surface];
}

static void track_preview_draw_extended(void)
{
	const struct CSTRK_TRACK *track = extended_track_model();
	int pixel_width = 280, pixel_height = 130;
	if ((int)track->width * pixel_height > (int)track->height * pixel_width) {
		pixel_height = pixel_width * track->height / track->width;
	} else {
		pixel_width = pixel_height * track->width / track->height;
	}
	if (pixel_width < 1) {
		pixel_width = 1;
	}
	if (pixel_height < 1) {
		pixel_height = 1;
	}
	int left = (320 - pixel_width) / 2;
	int top = 35 + (130 - pixel_height) / 2;
	sprite_clear_target(0);
	for (unsigned row = 0; row < track->height; row++) {
		for (unsigned column = 0; column < track->width; column++) {
			const struct CSTRK_CELL *cell = &track->cells[row * track->width + column];
			int x1 = left + (int)column * pixel_width / track->width;
			int x2 = left + (int)(column + 1U) * pixel_width / track->width;
			int y1 = top + (int)row * pixel_height / track->height;
			int y2 = top + (int)(row + 1U) * pixel_height / track->height;
			unsigned level = track->corner_heights[row * (track->width + 1U) + column];
			legacy_u8 color = track_preview_extended_surface_color(cell->surface, (legacy_u8)level);
			sprite_fill_rect((legacy_s16)x1, (legacy_s16)y1, (legacy_s16)(x2 - x1),
							 (legacy_s16)(y2 - y1), color);
			if (cell->half_water_corner) {
				for (int y = y1; y < y2; y++) {
					for (int x = x1; x < x2; x++) {
						int dx = (x - x1) * 1024 / (x2 - x1), dy = (y - y1) * 1024 / (y2 - y1);
						legacy_u8 corner_half =
							cell->half_water_corner == CSTRK_WATER_NORTHWEST   ? dx + dy < 1024
							: cell->half_water_corner == CSTRK_WATER_NORTHEAST ? dx > dy
							: cell->half_water_corner == CSTRK_WATER_SOUTHEAST ? dx + dy >= 1024
																			   : dx <= dy;
						if (corner_half) {
							sprite_putpixel_clipped((legacy_s16)x, (legacy_s16)y,
													track_preview_extended_surface_color(
														cell->corner_surface, (legacy_u8)level));
						}
					}
				}
			}
			const struct CSTRK_OBJECT *object = extended_track_object_at((int)column, (int)row);
			if (object) {
				unsigned id = extended_track_object_id(object);
				int scenery = trkObjectList[id].ss_trkObjInfoPtr == 0;
				legacy_u8 road_color = scenery								? 14
									   : object->surface == CSTRK_ROAD_ICE	? 15
									   : object->surface == CSTRK_ROAD_DIRT ? 6
																			: 7;
				if (trkObjectList[id].ss_physicalModel == PHYSICAL_MODEL_START_FINISH) {
					road_color = 12;
				}
				sprite_fill_rect((legacy_s16)x1, (legacy_s16)y1, (legacy_s16)(x2 - x1),
								 (legacy_s16)(y2 - y1), road_color);
			}
		}
	}
	sprite_draw_rect_outline((legacy_s16)(left - 1), (legacy_s16)(top - 1),
							 (legacy_s16)(left + pixel_width), (legacy_s16)(top + pixel_height), 8);
}

legacy_s16 extended_track_preview_draw(void)
{
	if (!extended_track_active()) {
		return 0;
	}
	if (!hires_track_preview_draw()) {
		track_preview_draw_extended();
	}
	return 1;
}
#endif
