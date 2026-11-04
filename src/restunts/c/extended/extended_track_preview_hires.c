#include "extended_track_preview_hires.h"
#ifdef RESTUNTS_SDL3
#include "extended_track.h"
#include "extended_frame.h"
#include "../hires/hires.h"
#include "../shape2d/shape2d.h"
#include "../shape3d/shape3d.h"
#include "../hires/shape3d_hires.h"
#include "../hires/shape3d_pipeline.h"
#include "../track/track_objects.h"
#include "../track/trackdata_layout.h"
#include "../skybox.h"
#include "../platform.h"
#include "../math/projection.h"
#include "../externs.h"
#define HIRES_TRACK_PREVIEW_SCREEN_WIDTH 320
#define HIRES_TRACK_PREVIEW_SCREEN_HEIGHT 200

#define HIRES_TRACK_PREVIEW_EXTENDED_TOP 0
#define HIRES_TRACK_PREVIEW_EXTENDED_HEADER_BOTTOM 40
#define HIRES_TRACK_PREVIEW_EXTENDED_BOTTOM 166
#define HIRES_TRACK_PREVIEW_EXTENDED_FOV 40
#define HIRES_TRACK_PREVIEW_EXTENDED_SPIN_SECONDS 40UL
#define HIRES_TRACK_PREVIEW_EXTENDED_SPIN_TICKS                                                    \
	(HIRES_TRACK_PREVIEW_EXTENDED_SPIN_SECONDS * DOS_TIMER_REALTIME_TICKS_PER_SECOND)
#define HIRES_TRACK_PREVIEW_EXTENDED_MINIMUM_LEVELS 12
#define HIRES_TRACK_PREVIEW_EXTENDED_ORBIT_PITCH ANGLE_EIGHTH_TURN
#define HIRES_TRACK_PREVIEW_EXTENDED_FIT_MARGIN 4
#define HIRES_TRACK_PREVIEW_EXTENDED_RADIUS_SCALE 8
#define HIRES_TRACK_PREVIEW_EXTENDED_RADIUS_ALLOWANCE 1.01
/* Authored road meshes rise at most 975 units above their support plane and
 * overhang a footprint by at most seven units. Include generated stunt meshes. */
#define HIRES_TRACK_PREVIEW_EXTENDED_MODEL_HEIGHT CSTRK_TILE_WORLD_SIZE
#define HIRES_TRACK_PREVIEW_EXTENDED_MODEL_MARGIN (CSTRK_TILE_WORLD_SIZE / 64)
#define HIRES_TRACK_PREVIEW_EXTENDED_CORNER_AXIS 2
#define HIRES_TRACK_PREVIEW_GEOMETRY_GROUP_TILES 8

static legacy_u32 hires_track_preview_last_primitive_count;

struct HIRES_TRACK_PREVIEW_EXTENDED_BOUNDS {
	legacy_f64 min_x, max_x, min_y, max_y, min_z, max_z;
	legacy_u32 tiles;
};

static struct SHAPE3D_HIRES_VECTOR
hires_track_preview_extended_center(const struct CSTRK_TRACK *track,
									struct HIRES_TRACK_PREVIEW_EXTENDED_BOUNDS *bounds)
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
			for (legacy_u16 y = 0; y < HIRES_TRACK_PREVIEW_EXTENDED_CORNER_AXIS; y++) {
				for (legacy_u16 x = 0; x < HIRES_TRACK_PREVIEW_EXTENDED_CORNER_AXIS; x++) {
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
hires_track_preview_extended_orbit_radius(const struct HIRES_TRACK_PREVIEW_EXTENDED_BOUNDS *bounds,
										  const struct SHAPE3D_HIRES_VECTOR *center)
{
	legacy_f64 radius = HIRES_TRACK_PREVIEW_EXTENDED_MINIMUM_LEVELS * CSTRK_LEVEL_WORLD_HEIGHT;
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
	extent_x += HIRES_TRACK_PREVIEW_EXTENDED_MODEL_MARGIN;
	extent_z += HIRES_TRACK_PREVIEW_EXTENDED_MODEL_MARGIN;
	/* Normalize the largest supported diagonal into word range; allow for
	 * integer radius and angle rounding before fitting the camera frustum. */
	legacy_f64 reach =
		(polarRadius2D((legacy_s16)(extent_x / HIRES_TRACK_PREVIEW_EXTENDED_RADIUS_SCALE),
					   (legacy_s16)(extent_z / HIRES_TRACK_PREVIEW_EXTENDED_RADIUS_SCALE)) *
			 HIRES_TRACK_PREVIEW_EXTENDED_RADIUS_SCALE +
		 HIRES_TRACK_PREVIEW_EXTENDED_MODEL_MARGIN) *
		HIRES_TRACK_PREVIEW_EXTENDED_RADIUS_ALLOWANCE;
	legacy_f64 low = bounds->min_y - HIRES_TRACK_PREVIEW_EXTENDED_MODEL_MARGIN - center->y;
	legacy_f64 high = bounds->max_y + HIRES_TRACK_PREVIEW_EXTENDED_MODEL_HEIGHT - center->y;
	legacy_f64 cosine =
		cos_fast(HIRES_TRACK_PREVIEW_EXTENDED_ORBIT_PITCH) / (legacy_f64)TRIG_FIXED_ONE;
	legacy_f64 horizontal =
		projection_focal_length_x / (legacy_f64)(HIRES_TRACK_PREVIEW_SCREEN_WIDTH / 2 -
												 HIRES_TRACK_PREVIEW_EXTENDED_FIT_MARGIN);
	legacy_f64 top = projection_focal_length_y /
					 (legacy_f64)(projection_center_y - HIRES_TRACK_PREVIEW_EXTENDED_HEADER_BOTTOM -
								  HIRES_TRACK_PREVIEW_EXTENDED_FIT_MARGIN);
	legacy_f64 bottom = projection_focal_length_y /
						(legacy_f64)(HIRES_TRACK_PREVIEW_EXTENDED_BOTTOM - projection_center_y -
									 HIRES_TRACK_PREVIEW_EXTENDED_FIT_MARGIN);
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

struct HIRES_TRACK_PREVIEW_GEOMETRY_FRAME {
	const struct CSTRK_TRACK *track;
	const struct SHAPE3D_HIRES_VECTOR *camera;
};

static void hires_track_preview_build_geometry(void *argument, legacy_s32 index)
{
	const struct HIRES_TRACK_PREVIEW_GEOMETRY_FRAME *frame = argument;
	legacy_s32 first = index * HIRES_TRACK_PREVIEW_GEOMETRY_GROUP_TILES;
	legacy_s32 end = first + HIRES_TRACK_PREVIEW_GEOMETRY_GROUP_TILES;
	legacy_s32 tile_count = (legacy_s32)frame->track->width * frame->track->height;
	if (end > tile_count) {
		end = tile_count;
	}
	legacy_s16 column = (legacy_s16)(first % frame->track->width);
	legacy_s16 row = (legacy_s16)(first / frame->track->width);
	for (legacy_s32 tile = first; tile < end; tile++) {
		/* Private geometry scenes retain the same row-major tile order. The
		 * precise origin belongs to the current job's thread-local context. */
		legacy_s32 center_x = column * CSTRK_TILE_WORLD_SIZE + CSTRK_TILE_WORLD_SIZE / 2;
		legacy_s32 center_z =
			(frame->track->height - row) * CSTRK_TILE_WORLD_SIZE - CSTRK_TILE_WORLD_SIZE / 2;
		struct SHAPE3D_HIRES_VECTOR origin = {center_x - frame->camera->x, -frame->camera->y,
											  center_z - frame->camera->z};
		shape3d_hires_set_origin(&origin);
		extended_frame_draw_preview_tile(column, row);
		if (++column == frame->track->width) {
			column = 0;
			row++;
		}
	}
}

static void hires_track_preview_draw_extended_3d(void)
{
	const struct CSTRK_TRACK *track = extended_track_model();
	struct HIRES_TRACK_PREVIEW_EXTENDED_BOUNDS bounds = {0};
	struct SHAPE3D_HIRES_VECTOR camera = hires_track_preview_extended_center(track, &bounds);
	legacy_u16 heading =
		(legacy_u16)((dos_timer_get_realtime_counter() % HIRES_TRACK_PREVIEW_EXTENDED_SPIN_TICKS) *
					 ANGLE_FULL_TURN / HIRES_TRACK_PREVIEW_EXTENDED_SPIN_TICKS);
	set_projection(HIRES_TRACK_PREVIEW_EXTENDED_FOV, HIRES_TRACK_PREVIEW_EXTENDED_FOV,
				   HIRES_TRACK_PREVIEW_SCREEN_WIDTH, HIRES_TRACK_PREVIEW_SCREEN_HEIGHT);
	projection_set_origin(0, (HIRES_TRACK_PREVIEW_EXTENDED_HEADER_BOTTOM +
							  HIRES_TRACK_PREVIEW_EXTENDED_BOTTOM -
							  HIRES_TRACK_PREVIEW_SCREEN_HEIGHT) /
								 2);
	legacy_f64 radius = hires_track_preview_extended_orbit_radius(&bounds, &camera);
	camera.x += radius * sin_fast(heading) / TRIG_FIXED_ONE;
	camera.y += radius;
	camera.z -= radius * cos_fast(heading) / TRIG_FIXED_ONE;
	legacy_s16 horizon = (legacy_s16)projection_center_y -
						 (legacy_s16)((legacy_s32)projection_focal_length_y *
									  sin_fast(HIRES_TRACK_PREVIEW_EXTENDED_ORBIT_PITCH) /
									  cos_fast(HIRES_TRACK_PREVIEW_EXTENDED_ORBIT_PITCH));
	struct RECTANGLE viewport = {0, HIRES_TRACK_PREVIEW_SCREEN_WIDTH,
								 HIRES_TRACK_PREVIEW_EXTENDED_TOP,
								 HIRES_TRACK_PREVIEW_EXTENDED_BOTTOM};
	skybox_render_level_rect(&viewport, (legacy_s16)heading, horizon);
	sprite_set_target_clip_bounds(0, HIRES_TRACK_PREVIEW_SCREEN_WIDTH,
								  HIRES_TRACK_PREVIEW_EXTENDED_TOP,
								  HIRES_TRACK_PREVIEW_EXTENDED_BOTTOM);
	struct RECTANGLE clip = {0, HIRES_TRACK_PREVIEW_SCREEN_WIDTH - 1,
							 HIRES_TRACK_PREVIEW_EXTENDED_TOP,
							 HIRES_TRACK_PREVIEW_EXTENDED_BOTTOM - 1};
	select_cliprect_rotate(0, -HIRES_TRACK_PREVIEW_EXTENDED_ORBIT_PITCH, (legacy_s16)heading, &clip,
						   0);
	struct HIRES_TRACK_PREVIEW_GEOMETRY_FRAME geometry = {track, &camera};
	legacy_s32 tile_count = (legacy_s32)track->width * track->height;
	legacy_s32 groups = (tile_count + HIRES_TRACK_PREVIEW_GEOMETRY_GROUP_TILES - 1) /
						HIRES_TRACK_PREVIEW_GEOMETRY_GROUP_TILES;
	/* No dynamic serial geometry interrupts the overview, so all immutable
	 * tile groups can build independently and merge once in source order. */
	shape3d_hires_geometry_run(groups, hires_track_preview_build_geometry, NULL, NULL, &geometry);
	shape3d_hires_set_origin(NULL);
	hires_track_preview_last_primitive_count = shape3d_hires_scene_count();
	shape3d_render_queued_primitives();
	projection_set_origin(0, 0);
	sprite_set_target_clip_bounds(0, HIRES_TRACK_PREVIEW_SCREEN_WIDTH, 0,
								  HIRES_TRACK_PREVIEW_SCREEN_HEIGHT);
}

legacy_u32 extended_track_preview_hires_primitive_count(void)
{
	return hires_track_preview_last_primitive_count;
}

legacy_s16 extended_track_preview_hires_draw(void)
{
	hires_track_preview_last_primitive_count = 0;
	if (!hires_enabled()) {
		return 0;
	}
	hires_track_preview_draw_extended_3d();
	return 1;
}
#endif
