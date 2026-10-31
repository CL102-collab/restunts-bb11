#include "extended_track_preview.h"

#ifdef RESTUNTS_SDL3
#include "extended_track.h"
#include "extended_frame.h"
#include "hires.h"
#include "shape2d.h"
#include "shape3d.h"
#include "shape3d_hires.h"
#include "track_objects.h"
#include "trackdata_layout.h"
#include "skybox.h"
#include "platform.h"
#include "projection.h"
#include "externs.h"

#define TRACK_PREVIEW_SCREEN_WIDTH 320
#define TRACK_PREVIEW_SCREEN_HEIGHT 200

/* Keep the original surface map for the classic renderer. */
enum TRACK_PREVIEW_EXTENDED_COLOR {
	TRACK_PREVIEW_EXTENDED_GRASS_COLOR = 2,
	TRACK_PREVIEW_EXTENDED_HIGH_GRASS_COLOR = 10,
	TRACK_PREVIEW_EXTENDED_ICE_COLOR = 11,
	TRACK_PREVIEW_EXTENDED_DIRT_COLOR = 6,
	TRACK_PREVIEW_EXTENDED_WATER_COLOR = 9
};

#define TRACK_PREVIEW_EXTENDED_RAISED_GRASS_LEVEL 1U
#define TRACK_PREVIEW_EXTENDED_TOP 0
#define TRACK_PREVIEW_EXTENDED_HEADER_BOTTOM 40
#define TRACK_PREVIEW_EXTENDED_BOTTOM 166
#define TRACK_PREVIEW_EXTENDED_FOV 40
#define TRACK_PREVIEW_EXTENDED_SPIN_SECONDS 40UL
#define TRACK_PREVIEW_EXTENDED_SPIN_TICKS                                                          \
	(TRACK_PREVIEW_EXTENDED_SPIN_SECONDS * DOS_TIMER_REALTIME_TICKS_PER_SECOND)
#define TRACK_PREVIEW_EXTENDED_MINIMUM_LEVELS 12
#define TRACK_PREVIEW_EXTENDED_ORBIT_PITCH ANGLE_EIGHTH_TURN
#define TRACK_PREVIEW_EXTENDED_FIT_MARGIN 4
#define TRACK_PREVIEW_EXTENDED_RADIUS_SCALE 8
#define TRACK_PREVIEW_EXTENDED_RADIUS_ALLOWANCE 1.01
/* Authored road meshes rise at most 975 units above their support plane and
 * overhang a footprint by at most seven units. Include generated stunt meshes. */
#define TRACK_PREVIEW_EXTENDED_MODEL_HEIGHT CSTRK_TILE_WORLD_SIZE
#define TRACK_PREVIEW_EXTENDED_MODEL_MARGIN (CSTRK_TILE_WORLD_SIZE / 64)
#define TRACK_PREVIEW_EXTENDED_CORNER_AXIS 2

struct TRACK_PREVIEW_EXTENDED_BOUNDS {
	legacy_f64 min_x, max_x, min_y, max_y, min_z, max_z;
	legacy_u32 tiles;
};

static struct SHAPE3D_HIRES_VECTOR
track_preview_extended_center(const struct CSTRK_TRACK *track,
							  struct TRACK_PREVIEW_EXTENDED_BOUNDS *bounds)
{
	struct SHAPE3D_HIRES_VECTOR center = {0};
	const legacy_u8 *lap_tiles = extended_track_lap_tiles();
	for (legacy_s16 row = 0; row < track->height; row++) {
		for (legacy_s16 column = 0; column < track->width; column++) {
			if (!lap_tiles || !lap_tiles[row * track->width + column]) {
				continue;
			}
			const struct CSTRK_OBJECT *object = extended_track_object_at(column, row);
			if (!object || !trkObjectList[extended_track_object_id(object)].ss_trkObjInfoPtr) {
				continue;
			}
			legacy_s32 left = column * CSTRK_TILE_WORLD_SIZE;
			legacy_s32 south = (track->height - row - 1) * CSTRK_TILE_WORLD_SIZE;
			legacy_s16 height = extended_track_elevation(column, row, CSTRK_TILE_WORLD_SIZE / 2,
														 CSTRK_TILE_WORLD_SIZE / 2);
			legacy_s16 low = height, high = height;
			for (legacy_u16 y = 0; y < TRACK_PREVIEW_EXTENDED_CORNER_AXIS; y++) {
				for (legacy_u16 x = 0; x < TRACK_PREVIEW_EXTENDED_CORNER_AXIS; x++) {
					legacy_s16 corner =
						track->corner_heights[(row + y) * (track->width + 1U) + column + x] *
						CSTRK_LEVEL_WORLD_HEIGHT;
					if (corner < low) {
						low = corner;
					}
					if (corner > high) {
						high = corner;
					}
				}
			}
			if (!bounds->tiles || left < bounds->min_x) {
				bounds->min_x = left;
			}
			if (!bounds->tiles || left + CSTRK_TILE_WORLD_SIZE > bounds->max_x) {
				bounds->max_x = left + CSTRK_TILE_WORLD_SIZE;
			}
			if (!bounds->tiles || south < bounds->min_z) {
				bounds->min_z = south;
			}
			if (!bounds->tiles || south + CSTRK_TILE_WORLD_SIZE > bounds->max_z) {
				bounds->max_z = south + CSTRK_TILE_WORLD_SIZE;
			}
			if (!bounds->tiles || low < bounds->min_y) {
				bounds->min_y = low;
			}
			if (!bounds->tiles || high > bounds->max_y) {
				bounds->max_y = high;
			}
			/* Only tiles on a route from start back to finish contribute. Each
			 * occupied tile has one vote, regardless of its piece or route count. */
			center.x += left + CSTRK_TILE_WORLD_SIZE / 2;
			center.y += height;
			center.z += south + CSTRK_TILE_WORLD_SIZE / 2;
			bounds->tiles++;
		}
	}
	if (bounds->tiles) {
		center.x /= bounds->tiles;
		center.y /= bounds->tiles;
		center.z /= bounds->tiles;
	} else {
		center.x = track->width * CSTRK_TILE_WORLD_SIZE / 2;
		center.z = track->height * CSTRK_TILE_WORLD_SIZE / 2;
	}
	return center;
}

static legacy_f64
track_preview_extended_orbit_radius(const struct TRACK_PREVIEW_EXTENDED_BOUNDS *bounds,
									const struct SHAPE3D_HIRES_VECTOR *center)
{
	legacy_f64 radius = TRACK_PREVIEW_EXTENDED_MINIMUM_LEVELS * CSTRK_LEVEL_WORLD_HEIGHT;
	if (!bounds->tiles) {
		return radius;
	}
	legacy_f64 extent_x = bounds->max_x - center->x;
	if (extent_x < center->x - bounds->min_x) {
		extent_x = center->x - bounds->min_x;
	}
	legacy_f64 extent_z = bounds->max_z - center->z;
	if (extent_z < center->z - bounds->min_z) {
		extent_z = center->z - bounds->min_z;
	}
	extent_x += TRACK_PREVIEW_EXTENDED_MODEL_MARGIN;
	extent_z += TRACK_PREVIEW_EXTENDED_MODEL_MARGIN;
	/* Normalize the largest supported diagonal into word range; allow for
	 * integer radius and angle rounding before fitting the camera frustum. */
	legacy_f64 reach =
		(polarRadius2D((legacy_s16)(extent_x / TRACK_PREVIEW_EXTENDED_RADIUS_SCALE),
					   (legacy_s16)(extent_z / TRACK_PREVIEW_EXTENDED_RADIUS_SCALE)) *
			 TRACK_PREVIEW_EXTENDED_RADIUS_SCALE +
		 TRACK_PREVIEW_EXTENDED_MODEL_MARGIN) *
		TRACK_PREVIEW_EXTENDED_RADIUS_ALLOWANCE;
	legacy_f64 low = bounds->min_y - TRACK_PREVIEW_EXTENDED_MODEL_MARGIN - center->y;
	legacy_f64 high = bounds->max_y + TRACK_PREVIEW_EXTENDED_MODEL_HEIGHT - center->y;
	legacy_f64 cosine = cos_fast(TRACK_PREVIEW_EXTENDED_ORBIT_PITCH) / (legacy_f64)TRIG_FIXED_ONE;
	legacy_f64 horizontal =
		projection_focal_length_x /
		(legacy_f64)(TRACK_PREVIEW_SCREEN_WIDTH / 2 - TRACK_PREVIEW_EXTENDED_FIT_MARGIN);
	legacy_f64 top = projection_focal_length_y /
					 (legacy_f64)(projection_center_y - TRACK_PREVIEW_EXTENDED_HEADER_BOTTOM -
								  TRACK_PREVIEW_EXTENDED_FIT_MARGIN);
	legacy_f64 bottom = projection_focal_length_y /
						(legacy_f64)(TRACK_PREVIEW_EXTENDED_BOTTOM - projection_center_y -
									 TRACK_PREVIEW_EXTENDED_FIT_MARGIN);
	/* At 45 degrees the orbit radius equals its height above the target.
	 * Bound each frustum plane over every heading, keeping the entire lap
	 * below its header without zooming as the camera goes around the circle. */
	legacy_f64 required = (reach * (1 + horizontal / cosine) + high) / 2;
	if (radius < required) {
		radius = required;
	}
	legacy_f64 top_reach = top > 1 ? top - 1 : 1 - top;
	required = ((top + 1) * high + top_reach * reach) / 2;
	if (radius < required) {
		radius = required;
	}
	required = ((1 - bottom) * low + (1 + bottom) * reach) / 2;
	if (radius < required) {
		radius = required;
	}
	return radius;
}

static void track_preview_draw_extended_3d(void)
{
	const struct CSTRK_TRACK *track = extended_track_model();
	struct TRACK_PREVIEW_EXTENDED_BOUNDS bounds = {0};
	struct SHAPE3D_HIRES_VECTOR camera = track_preview_extended_center(track, &bounds);
	legacy_u16 heading =
		(legacy_u16)((dos_timer_get_realtime_counter() % TRACK_PREVIEW_EXTENDED_SPIN_TICKS) *
					 ANGLE_FULL_TURN / TRACK_PREVIEW_EXTENDED_SPIN_TICKS);
	set_projection(TRACK_PREVIEW_EXTENDED_FOV, TRACK_PREVIEW_EXTENDED_FOV,
				   TRACK_PREVIEW_SCREEN_WIDTH, TRACK_PREVIEW_SCREEN_HEIGHT);
	projection_set_origin(0, (TRACK_PREVIEW_EXTENDED_HEADER_BOTTOM + TRACK_PREVIEW_EXTENDED_BOTTOM -
							  TRACK_PREVIEW_SCREEN_HEIGHT) /
								 2);
	legacy_f64 radius = track_preview_extended_orbit_radius(&bounds, &camera);
	camera.x += radius * sin_fast(heading) / TRIG_FIXED_ONE;
	camera.y += radius;
	camera.z -= radius * cos_fast(heading) / TRIG_FIXED_ONE;
	legacy_s16 horizon = (legacy_s16)projection_center_y -
						 (legacy_s16)((legacy_s32)projection_focal_length_y *
									  sin_fast(TRACK_PREVIEW_EXTENDED_ORBIT_PITCH) /
									  cos_fast(TRACK_PREVIEW_EXTENDED_ORBIT_PITCH));
	struct RECTANGLE viewport = {0, TRACK_PREVIEW_SCREEN_WIDTH, TRACK_PREVIEW_EXTENDED_TOP,
								 TRACK_PREVIEW_EXTENDED_BOTTOM};
	skybox_render_level_rect(&viewport, (legacy_s16)heading, horizon);
	sprite_set_target_clip_bounds(0, TRACK_PREVIEW_SCREEN_WIDTH, TRACK_PREVIEW_EXTENDED_TOP,
								  TRACK_PREVIEW_EXTENDED_BOTTOM);
	struct RECTANGLE clip = {0, TRACK_PREVIEW_SCREEN_WIDTH - 1, TRACK_PREVIEW_EXTENDED_TOP,
							 TRACK_PREVIEW_EXTENDED_BOTTOM - 1};
	select_cliprect_rotate(0, -TRACK_PREVIEW_EXTENDED_ORBIT_PITCH, (legacy_s16)heading, &clip, 0);
	for (legacy_s16 row = 0; row < track->height; row++) {
		for (legacy_s16 column = 0; column < track->width; column++) {
			/* Existing meshes use a tile-local wrapped camera. Restore its full
			 * world position here before HyperVision rotates precise vertices. */
			struct SHAPE3D_HIRES_VECTOR origin = {
				column * CSTRK_TILE_WORLD_SIZE + CSTRK_TILE_WORLD_SIZE / 2 - camera.x, -camera.y,
				(track->height - row) * CSTRK_TILE_WORLD_SIZE - CSTRK_TILE_WORLD_SIZE / 2 -
					camera.z};
			shape3d_hires_set_origin(&origin);
			extended_frame_draw_preview_tile(column, row);
		}
	}
	shape3d_hires_set_origin(NULL);
	shape3d_render_queued_primitives();
	projection_set_origin(0, 0);
	sprite_set_target_clip_bounds(0, TRACK_PREVIEW_SCREEN_WIDTH, 0, TRACK_PREVIEW_SCREEN_HEIGHT);
}

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
	if (hires_enabled()) {
		track_preview_draw_extended_3d();
	} else {
		track_preview_draw_extended();
	}
	return 1;
}
#endif
