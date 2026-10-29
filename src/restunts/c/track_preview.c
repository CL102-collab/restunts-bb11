#include "frame_internal.h"
#include "game_input.h"
#include "trackdata_layout.h"
#include "shape2d.h"
#include "shape3d.h"
#include "track_types.h"
#include "track_objects.h"
#include "scene_resources.h"
#include "skybox.h"
#include "externs.h"
#ifdef RESTUNTS_SDL3
#include "hires.h"
#include "extended_track.h"
#endif

#define TRACK_PREVIEW_HALF_SHIFT 1U
#define TRACK_PREVIEW_SCREEN_WIDTH 320
#define TRACK_PREVIEW_SCREEN_HEIGHT 200
#define TRACK_PREVIEW_SKYBOX_HEIGHT 100
#define TRACK_PREVIEW_TRANSFORM_DISTANCE 1024
#define TRACK_PREVIEW_GRID_SIZE 30U
#define TRACK_PREVIEW_PLACEHOLDER_MINIMUM 253U
#define TRACK_PREVIEW_WATER_TERRAIN_FIRST 1U
#define TRACK_PREVIEW_WATER_TERRAIN_LAST 5U
#define TRACK_PREVIEW_HILL_ROAD_TERRAIN_FIRST 7U
#define TRACK_PREVIEW_HILL_ROAD_TERRAIN_END 11U
#define TRACK_PREVIEW_BRIDGE_FIRST 105U
#define TRACK_PREVIEW_BRIDGE_LAST 108U
#define TRACK_PREVIEW_BRIDGE_QUADRANT_COUNT 4U
#define TRACK_PREVIEW_QUADRANT_COLUMN_BIT 1U
#define TRACK_PREVIEW_QUADRANT_ROW_BIT 2U
#define TRACK_PREVIEW_TRANSFORM_FLAGS 5U
#define TRACK_PREVIEW_OBJECT_TRANSFORM_FLAG 4U
#define TRACK_PREVIEW_ROTATE_CLIP 1

enum TRACK_PREVIEW_SKYBOX_SHAPE_INDEX {
	TRACK_PREVIEW_SKYBOX_LEFT = 2,
	TRACK_PREVIEW_SKYBOX_RIGHT = 3
};

enum TRACK_PREVIEW_HILL_BASE_SHAPE_INDEX {
	TRACK_PREVIEW_HILL_BASE_SINGLE_SHAPE = 43,
	TRACK_PREVIEW_HILL_BASE_VERTICAL_SHAPE = 91,
	TRACK_PREVIEW_HILL_BASE_HORIZONTAL_SHAPE = 92,
	TRACK_PREVIEW_HILL_BASE_QUAD_SHAPE = 93
};

static legacy_s16 track_preview_half(legacy_s16 value)
{
	legacy_u16 bits = (legacy_u16)value;
	return LEGACY_S16_FROM_BITS(LEGACY_U16_SAR(bits, TRACK_PREVIEW_HALF_SHIFT));
}

static void track_preview_draw_terrain(legacy_u8 terrain, legacy_u8 column, legacy_u8 row,
									   legacy_s16 height, legacy_s16 camera_x, legacy_s16 camera_y,
									   legacy_s16 camera_z, legacy_s16 use_high_detail,
									   struct TRANSFORMEDSHAPE3D *transformed)
{
	if (terrain == 0) {
		return;
	}
	struct TRACKOBJECT *terrain_object = &terrain_scene_objects[terrain];
	transformed->shapeptr =
		use_high_detail != 0 ? terrain_object->ss_shapePtr : terrain_object->ss_loShapePtr;
	transformed->pos.x =
		track_preview_half(LEGACY_S16_WRAP_SUB(track_column_centers[column], camera_x));
	transformed->pos.y = track_preview_half(LEGACY_S16_WRAP_SUB(height, camera_y));
	transformed->pos.z = track_preview_half(LEGACY_S16_WRAP_SUB(track_row_centers[row], camera_z));
	transformed->rotvec.z = terrain_object->ss_rotY;
	transformed->ts_flags = TRACK_PREVIEW_TRANSFORM_FLAGS;
	transformed->material = 0;
	shape3d_transform_and_queue(transformed);
}

static void track_preview_prepare(const struct VECTOR *camera)
{
	legacy_s16 camera_radius =
		(legacy_s16)polarRadius2D(LEGACY_S16_WRAP_SUB(track_preview_target_x, camera->x),
								  LEGACY_S16_WRAP_SUB(track_preview_target_z, camera->z));
	legacy_s16 camera_angle = (legacy_s16)polarAngle(
		LEGACY_S16_WRAP_SUB(track_preview_target_y, camera->y), camera_radius);
	struct MATRIX *rotation = mat_rot_zxy(0, camera_angle, 0, MATRIX_ROTATION_ORDER_YXZ);
	struct VECTOR projected_vector;
	mat_mul_vector(&track_preview_horizon_vector, rotation, &projected_vector);
	struct POINT2D projected_point;
	vector_to_point(&projected_vector, &projected_point);
	legacy_s16 horizon = (legacy_s16)projected_point.py;
	if (horizon < 0) {
		horizon = 0;
	}

	sprite_set_target_clip_bounds(0, TRACK_PREVIEW_SCREEN_WIDTH, 0,
								  LEGACY_S16_WRAP_SUB(horizon, skybox.minimum_height));
	sprite_clear_target((legacy_u8)skybox.sky_color);
	sprite_set_target_clip_bounds(0, TRACK_PREVIEW_SCREEN_WIDTH, 0, TRACK_PREVIEW_SKYBOX_HEIGHT);
	skybox_copy_image(TRACK_PREVIEW_SKYBOX_LEFT, 0,
					  LEGACY_S16_WRAP_SUB(horizon, skybox.heights[TRACK_PREVIEW_SKYBOX_LEFT]));
	skybox_copy_image(TRACK_PREVIEW_SKYBOX_RIGHT, TRACK_PREVIEW_SCREEN_WIDTH,
					  LEGACY_S16_WRAP_SUB(horizon, skybox.heights[TRACK_PREVIEW_SKYBOX_RIGHT]));
	sprite_set_target_clip_bounds(0, TRACK_PREVIEW_SCREEN_WIDTH, horizon,
								  TRACK_PREVIEW_SCREEN_HEIGHT);
	sprite_clear_target((legacy_u8)skybox.ground_color);
	sprite_set_target_clip_bounds(0, TRACK_PREVIEW_SCREEN_WIDTH, 0, TRACK_PREVIEW_SCREEN_HEIGHT);
	select_cliprect_rotate(0, camera_angle, 0, &trackpreview_cliprect, TRACK_PREVIEW_ROTATE_CLIP);
}

static void track_preview_draw_bridge_terrain(legacy_u8 column, legacy_u8 row,
											  const struct VECTOR *camera,
											  struct TRANSFORMEDSHAPE3D *transformed)
{
	for (legacy_u8 quadrant = 0; quadrant < TRACK_PREVIEW_BRIDGE_QUADRANT_COUNT; quadrant++) {
		legacy_u8 adjacent_column =
			(legacy_u8)(column + ((quadrant & TRACK_PREVIEW_QUADRANT_COLUMN_BIT) != 0 ? 1U : 0U));
		legacy_u8 adjacent_row =
			(legacy_u8)(row + ((quadrant & TRACK_PREVIEW_QUADRANT_ROW_BIT) != 0 ? 1U : 0U));
		legacy_u8 terrain =
			track_terrain_map[LEGACY_U16_WRAP_ADD(terrainrows[adjacent_row], adjacent_column)];
		track_preview_draw_terrain(terrain, adjacent_column, adjacent_row, 0, camera->x, camera->y,
								   camera->z, 1, transformed);
	}
}

/* Queue covered water with its owner so later continuation cells cannot paint
 * over the road after the owner's primitives have been flushed. */
static void track_preview_draw_covered_water(legacy_u8 track, legacy_u8 terrain, legacy_u8 column,
											 legacy_u8 row, const struct VECTOR *camera,
											 struct TRANSFORMEDSHAPE3D *transformed)
{
	if (track == 0 || (track >= TRACK_PREVIEW_BRIDGE_FIRST && track <= TRACK_PREVIEW_BRIDGE_LAST &&
					   terrain != TERRAIN_RAISED_TILE)) {
		/* Elevated corners already draw every cell's terrain. */
		return;
	}
	legacy_u8 footprint = trkObjectList[track].ss_multiTileFlag;
	if (footprint < MULTI_TILE_ROW_EDGE_FLAG ||
		footprint > (MULTI_TILE_ROW_EDGE_FLAG | MULTI_TILE_COLUMN_EDGE_FLAG)) {
		return;
	}
	legacy_u8 last_column = column + ((footprint & MULTI_TILE_COLUMN_EDGE_FLAG) != 0);
	legacy_u8 last_row = row + ((footprint & MULTI_TILE_ROW_EDGE_FLAG) != 0);
	for (legacy_u8 adjacent_row = row;
		 adjacent_row <= last_row && adjacent_row < TRACK_PREVIEW_GRID_SIZE; adjacent_row++) {
		for (legacy_u8 adjacent_column = column;
			 adjacent_column <= last_column && adjacent_column < TRACK_PREVIEW_GRID_SIZE;
			 adjacent_column++) {
			if (adjacent_column == column && adjacent_row == row) {
				continue;
			}
			legacy_u8 covered_terrain =
				track_terrain_map[LEGACY_U16_WRAP_ADD(terrainrows[adjacent_row], adjacent_column)];
			if (covered_terrain >= TRACK_PREVIEW_WATER_TERRAIN_FIRST &&
				covered_terrain <= TRACK_PREVIEW_WATER_TERRAIN_LAST) {
				track_preview_draw_terrain(covered_terrain, adjacent_column, adjacent_row, 0,
										   camera->x, camera->y, camera->z, 0, transformed);
			}
		}
	}
}

static void track_preview_draw_hill_base(const struct TRACKOBJECT *track_object,
										 const struct VECTOR *position,
										 struct TRANSFORMEDSHAPE3D *transformed)
{
	switch (track_object->ss_multiTileFlag) {
		case TRACK_PREVIEW_QUADRANT_COLUMN_BIT:
			transformed->shapeptr = &game3dshapes[TRACK_PREVIEW_HILL_BASE_VERTICAL_SHAPE];
			break;
		case TRACK_PREVIEW_QUADRANT_ROW_BIT:
			transformed->shapeptr = &game3dshapes[TRACK_PREVIEW_HILL_BASE_HORIZONTAL_SHAPE];
			break;
		case TRACK_PREVIEW_QUADRANT_COLUMN_BIT | TRACK_PREVIEW_QUADRANT_ROW_BIT:
			transformed->shapeptr = &game3dshapes[TRACK_PREVIEW_HILL_BASE_QUAD_SHAPE];
			break;
		default:
			transformed->shapeptr = &game3dshapes[TRACK_PREVIEW_HILL_BASE_SINGLE_SHAPE];
			break;
	}
	transformed->pos = *position;
	transformed->rotvec.z = 0;
	transformed->ts_flags = TRACK_PREVIEW_TRANSFORM_FLAGS;
	transformed->material = 0;
	shape3d_transform_and_queue(transformed);
}

static void track_preview_draw_object(legacy_u8 track, legacy_u8 column, legacy_u8 row,
									  legacy_s16 terrain_height, const struct VECTOR *camera,
									  struct TRANSFORMEDSHAPE3D *transformed)
{
	struct TRACKOBJECT *track_object = &trkObjectList[track];
	struct VECTOR track_position;
	track_position.x = track_preview_half(
		LEGACY_S16_WRAP_SUB(track_object_base_x(track_object, column), camera->x));
	track_position.y = track_preview_half(LEGACY_S16_WRAP_SUB(terrain_height, camera->y));
	track_position.z =
		track_preview_half(LEGACY_S16_WRAP_SUB(track_object_base_z(track_object, row), camera->z));

	if (terrain_height != 0) {
		track_preview_draw_hill_base(track_object, &track_position, transformed);
	}

	struct TRACKOBJECT *overlay_object;
	if (track_object->ss_ssOvelay != 0) {
		overlay_object = frame_track_object_from_legacy_index(track_object->ss_ssOvelay);
		if (overlay_object->ss_loShapePtr != 0) {
			transformed->shapeptr = overlay_object->ss_loShapePtr;
			transformed->pos = track_position;
			transformed->rotvec.z = track_object->ss_rotY;
			transformed->ts_flags = TRACK_PREVIEW_TRANSFORM_FLAGS;
			transformed->material =
				(legacy_s8)overlay_object->ss_surfaceType < 0 ? 0 : overlay_object->ss_surfaceType;
			shape3d_transform_and_queue(transformed);
		}
	}

	transformed->shapeptr = track_object->ss_loShapePtr;
	transformed->pos = track_position;
	transformed->rotvec.z = track_object->ss_rotY;
	transformed->ts_flags =
		(legacy_u8)(track_object->ss_ignoreZBias | TRACK_PREVIEW_OBJECT_TRANSFORM_FLAG);
	transformed->material =
		(legacy_s8)track_object->ss_surfaceType < 0 ? 0 : track_object->ss_surfaceType;
	shape3d_transform_and_queue(transformed);
}

static void track_preview_draw_cell(legacy_u8 column, legacy_u8 row, const struct VECTOR *camera,
									struct TRANSFORMEDSHAPE3D *transformed)
{
	legacy_u8 track = track_element_map[LEGACY_U16_WRAP_ADD(trackrows[row], column)];
	legacy_u8 terrain = track_terrain_map[LEGACY_U16_WRAP_ADD(terrainrows[row], column)];
	if (track != 0 && terrain >= TRACK_PREVIEW_HILL_ROAD_TERRAIN_FIRST &&
		terrain < TRACK_PREVIEW_HILL_ROAD_TERRAIN_END) {
		track = subst_hillroad_track(terrain, track);
		terrain = 0;
	}
	if (track >= TRACK_PREVIEW_PLACEHOLDER_MINIMUM) {
		track = 0;
		terrain = 0;
	}

	if (supersight_enabled != 0) {
		track_preview_draw_covered_water(track, terrain, column, row, camera, transformed);
	}

	legacy_s16 terrain_height = 0;
	if (terrain == TERRAIN_RAISED_TILE) {
		terrain_height = hillHeightConsts[TERRAIN_RAISED_HEIGHT_INDEX];
		if (track != 0) {
			terrain = 0;
		}
	} else if (track >= TRACK_PREVIEW_BRIDGE_FIRST && track <= TRACK_PREVIEW_BRIDGE_LAST) {
		track_preview_draw_bridge_terrain(column, row, camera, transformed);
		terrain = 0;
	}

	track_preview_draw_terrain(terrain, column, row, terrain_height, camera->x, camera->y,
							   camera->z, 0, transformed);
	if (track != 0) {
		track_preview_draw_object(track, column, row, terrain_height, camera, transformed);
	}
}

#ifdef RESTUNTS_SDL3
/* An overview must show the complete extended map. The original perspective
 * preview's word coordinates and camera fit only thirty tiles. A surface map
 * avoids wrapping distant cells and also makes stacked terrain legible. */
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
#endif

void draw_track_preview(void)
{
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		track_preview_draw_extended();
		return;
	}
#endif
	struct VECTOR camera;
	camera.x = (legacy_s16)track_preview_camera_x;
	camera.y = (legacy_s16)track_preview_camera_y;
	camera.z = (legacy_s16)track_preview_camera_z;
	track_preview_prepare(&camera);
	struct TRANSFORMEDSHAPE3D transformed;
	transformed.rotvec.x = 0;
	transformed.rotvec.y = 0;
	transformed.culling_distance = TRACK_PREVIEW_TRANSFORM_DISTANCE;
	legacy_s32 batch_scene = 0;
#ifdef RESTUNTS_SDL3
	batch_scene = hires_enabled();
#endif
	/* HyperVision resolves the whole scene by depth. Starting a full-screen
	 * raster for every tile repeatedly clears its depth buffer and starves
	 * music playback while the preview is built. Keep per-tile flushing for
	 * the classic renderer, whose fixed queue and painter order require it. */
	for (legacy_u8 row = 0; row < TRACK_PREVIEW_GRID_SIZE; row++) {
		for (legacy_u8 column = 0; column < TRACK_PREVIEW_GRID_SIZE; column++) {
			track_preview_draw_cell(column, row, &camera, &transformed);
			if (!batch_scene) {
				shape3d_render_queued_primitives();
			}
		}
	}
	if (batch_scene) {
		shape3d_render_queued_primitives();
	}
}
