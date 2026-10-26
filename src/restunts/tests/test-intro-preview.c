#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../c/externs.h"
#include "../c/frame_internal.h"
#include "../c/shape2d.h"
#include "../c/scene_resources.h"
#include "../c/skybox.h"
#include "../c/projection.h"
#include "../c/menu_internal.h"
#include "../c/track_objects.h"
#include "../c/trackdata_layout.h"
#include "../c/platform.h"
#include "../c/fileio.h"
#include "../c/memmgr.h"
#include "../c/game_input.h"
#include "../c/video_frame.h"
#include "../c/shape3d.h"
#include "../c/keyboard.h"
#ifdef RESTUNTS_SDL3
#include "../c/hires.h"
#endif

#undef strcmp

struct SPRITE *render_window_sprite;

/* Draw-call traces cover the intro scene and full preview traversal. Geometry
 * and rectangle math use production code; rendering, resource and timer endpoints are stubbed.
 * Lifecycle cases also cover timing, camera phases, cancellation and cleanup. */
static legacy_u32 trace_hash;
static legacy_u32 queued_count, flush_count, pixel_count;
static struct SHAPE2D sky_images[4];
static legacy_u8 elements[900], terrain[900];
legacy_u8 supersight_enabled;

#ifdef RESTUNTS_SDL3
static legacy_u8 hires_active;

legacy_s32 hires_enabled(void)
{
	return hires_active;
}
#endif

enum {
	PREVIEW_SCENARIO_COUNT = 32,
	PREVIEW_WATER_TILE = TRACK_GRID_SIZE / 2,
	PREVIEW_WATER_OBJECT = 1,
	PREVIEW_WATER_BRIDGE_FIRST = 105,
	PREVIEW_WATER_BRIDGE_LAST = 108,
	PREVIEW_WATER_HIGH_SHAPE = 16,
	PREVIEW_WATER_LOW_SHAPE = 32,
	PREVIEW_WATER_FIRST = 1,
	PREVIEW_WATER_LAST = 5,
	PREVIEW_WATER_COUNT = PREVIEW_WATER_LAST - PREVIEW_WATER_FIRST + 1,
	PREVIEW_WATER_QUADRANTS = 4,
	PREVIEW_WATER_ALL_CELLS = (1U << PREVIEW_WATER_QUADRANTS) - 1U,
	PREVIEW_WATER_NORTHWEST = 1,
	PREVIEW_WATER_VERTICAL = 5,
	PREVIEW_WATER_HORIZONTAL = 3,
	PREVIEW_WATER_CAPTURE_CAPACITY = 8,
	PREVIEW_WATER_HALF_SCALE = 2,
	PREVIEW_WATER_FLAGS = 5,
	PREVIEW_WATER_CAMERA_HEIGHT = 256
};
static legacy_u8 preview_capture_active;
static legacy_u16 preview_captured_count;
static struct TRANSFORMEDSHAPE3D preview_captured[PREVIEW_WATER_CAPTURE_CAPACITY];

#define INTRO_TEST_STATUS_EXPIRY_INPUT 4U
#define INTRO_TEST_STATUS_EXPIRY_POLL 3U
#define INTRO_TEST_EXIT_POLL 5U
#define INTRO_TEST_CLEAR_PAGE_CASES 4U

legacy_u8 fps_display_enabled;
static legacy_u32 scripted_input, shortcut_count, opponent_updates;
static legacy_u32 fps_draw_count, fps_presented_count, fps_reset_count;
static legacy_u8 preset_status_active;
static legacy_s16 scripted_key;
static legacy_u64 scripted_now;
static legacy_u64 previous_input_time;
static legacy_u32 predicted_transforms;
static legacy_u8 drawing_frame, completed_frame;

void sdl3_video_begin_frame(void)
{
	assert(drawing_frame == 0);
	drawing_frame = 1;
	completed_frame = 0;
}

void sdl3_video_end_frame(void)
{
	assert(drawing_frame != 0 && completed_frame != 0);
	drawing_frame = 0;
}

legacy_u64 presentation_now(void)
{
	return scripted_now;
}
static struct RECTANGLE fps_bounds = {8, 81, 3, 12};
static struct RECTANGLE target_clip;
static struct RECTANGLE cleared_rects[8];
static legacy_u8 rendered_modes[8], rendered_fps[8];

legacy_s16 handle_ingame_kb_shortcuts(legacy_s16 key)
{
	assert(key == KEY_F11 || key == KEY_F12 || key == KEY_SHIFT_F12);
	if (key == KEY_F11) {
		fps_display_enabled = fps_display_enabled == FRAME_FPS_DISPLAY_OFF ? FRAME_FPS_DISPLAY_ON
																		   : FRAME_FPS_DISPLAY_OFF;
		frame_fps_reset();
	} else if (key == KEY_SHIFT_F12) {
		supersight_enabled = preset_status_active = 1;
	} else {
		supersight_enabled ^= 1U;
		preset_status_active = 1;
	}
	shortcut_count++;
	return 1;
}

legacy_s16 frame_display_overlay_active(void)
{
	return fps_display_enabled != 0 || preset_status_active != 0;
}

void frame_fps_reset(void)
{
	preset_status_active = 0;
	fps_reset_count++;
}

void frame_fps_record_presented(void)
{
#ifdef RESTUNTS_SDL3
	assert(drawing_frame != 0);
#endif
	completed_frame = 1;
	fps_presented_count++;
}

struct RECTANGLE *frame_fps_draw_text(void)
{
	assert(frame_display_overlay_active() != 0);
	fps_draw_count++;
	return &fps_bounds;
}

static void record_word(legacy_u16 value)
{
	trace_hash = (trace_hash ^ value) * 16777619UL;
}

static void record_rect(const struct RECTANGLE *rect)
{
	record_word(rect->left);
	record_word(rect->right);
	record_word(rect->top);
	record_word(rect->bottom);
}

static legacy_u16 shape_id(const struct SHAPE3D *shape)
{
	if (shape == NULL) {
		return 0;
	}
	if (shape == &logoshape) {
		return 131;
	}
	if (shape == &logo2shape) {
		return 132;
	}
	if (shape == &bravshape) {
		return 133;
	}
	for (legacy_u32 index = 0; index < 130; index++) {
		if (shape == &game3dshapes[index]) {
			return index + 1;
		}
	}
	abort();
}

void fatal_error(const legacy_s8 *format, ...)
{
	(void)format;
	abort();
}

void sprite_set_target_clip_bounds(legacy_u16 left, legacy_u16 right, legacy_u16 top,
								   legacy_u16 bottom)
{
	record_word(1);
	record_word(left);
	record_word(right);
	record_word(top);
	record_word(bottom);
	target_clip.left = left;
	target_clip.right = right;
	target_clip.top = top;
	target_clip.bottom = bottom;
}

void sprite_clear_target(legacy_u8 color)
{
	record_word(2);
	record_word(color);
	if ((scripted_input == 1 || scripted_input == INTRO_TEST_STATUS_EXPIRY_INPUT) &&
		flush_count < 8) {
		cleared_rects[flush_count] = target_clip;
	}
}

void sprite_copy_image_at(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y)
{
	record_word(3);
	record_word((legacy_u16)(shape - sky_images));
	record_word(x);
	record_word(y);
}

void skybox_copy_image(legacy_s16 image, legacy_s16 x, legacy_s16 y)
{
	sprite_copy_image_at(skyboxes[image], x, y);
}

void sprite_putpixel_clipped(legacy_s16 x, legacy_s16 y, legacy_s16 color)
{
	record_word(4);
	record_word(x);
	record_word(y);
	record_word(color);
	pixel_count++;
}

legacy_u16 select_cliprect_rotate(legacy_s16 z, legacy_s16 x, legacy_s16 y, struct RECTANGLE *clip,
								  legacy_s16 half_scale)
{
	record_word(5);
	record_word(z);
	record_word(x);
	record_word(y);
	record_word(half_scale);
	record_rect(clip);
	mat_temp = *mat_rot_zxy(z, x, y, MATRIX_ROTATION_ORDER_ZXY);
	return 0;
}

legacy_u16 shape3d_transform_and_queue(struct TRANSFORMEDSHAPE3D *shape)
{
#ifdef RESTUNTS_SDL3
	if (hires_active != 0) {
		assert(flush_count == 0);
	}
#endif
	legacy_u16 id = shape_id(shape->shapeptr);
	if (preview_capture_active != 0) {
		assert(preview_captured_count < PREVIEW_WATER_CAPTURE_CAPACITY);
		preview_captured[preview_captured_count++] = *shape;
	}
	if (scripted_input == 3 && id == 132 &&
		shape->pos.x !=
			LEGACY_S16_WRAP_SUB(1024, position_to_word(state.opponentstate.car_position.lx))) {
		predicted_transforms++;
	}

	record_word(6);
	record_word(id);
	record_word(shape->pos.x);
	record_word(shape->pos.y);
	record_word(shape->pos.z);
	record_word(shape->rotvec.x);
	record_word(shape->rotvec.y);
	record_word(shape->rotvec.z);
	record_word(shape->culling_distance);
	record_word(shape->ts_flags);
	record_word(shape->material);
	struct POINT2D corner;
	if ((shape->ts_flags & 8) != 0) {
		assert(shape->rectptr != NULL);
		record_rect(shape->rectptr);
		corner.px = (legacy_s16)(id % 7 * 30 - 20);
		corner.py = (legacy_s16)(id % 5 * 20 - 10);
		rect_adjust_from_point(&corner, shape->rectptr);
		corner.px += 40;
		corner.py += 30;
		rect_adjust_from_point(&corner, shape->rectptr);
	}
	queued_count++;
	return 0;
}

void shape3d_render_queued_primitives(void)
{
	record_word(7);
	if ((scripted_input == 1 || scripted_input == INTRO_TEST_STATUS_EXPIRY_INPUT) &&
		flush_count < 8) {
		rendered_modes[flush_count] = supersight_enabled;
		rendered_fps[flush_count] = fps_display_enabled;
	}
	flush_count++;
}

struct TRACKOBJECT *frame_track_object_from_legacy_index(legacy_u8 index)
{
	record_word(8);
	record_word(index);
	if (index < 215) {
		return &trkObjectList[index];
	}
	assert(index < 234);
	return &terrain_scene_objects[index - 215];
}

static void reset_projection(void)
{
	video_x_alignment = 1;
	video_x_alignment_mask = -1;
	queued_count = 0;
	flush_count = 0;
	pixel_count = 0;
	projection_focal_length_x = 256;
	projection_focal_length_y = 192;
	projection_center_x = 160;
	projection_center_y = 100;
}

static void intro_case(legacy_u32 scenario)
{
	reset_projection();
	slow_video_mgmt_copy = scenario & 1;
	legacy_u32 draw_car = (scenario >> 1) & 1;
	legacy_u32 logo = (scenario >> 2) & 1;
	legacy_u32 rotation = (scenario >> 3) % 4;
	static const legacy_s16 offsets[] = {0, 200, -200, 32767, -32768};
	legacy_s16 camera_x = offsets[(scenario >> 5) % 5];
	legacy_s16 camera_y = offsets[((scenario >> 5) + 1) % 5];
	legacy_s16 camera_z = offsets[((scenario >> 5) + 2) % 5];
	legacy_s16 old_count = scenario % 3 == 0 ? 0 : (scenario % 3 == 1 ? 3 : 100);
	legacy_s16 count = old_count;
	intro_palette_color_count = scenario % 2 ? 16 : 3;
	intro_colorvalue = intro_palette_color_count - 1;
	intro_redraw_cliprect = intro_cliprect;
	struct RECTANGLE shape_rect = {30, 110, 25, 75};
	struct RECTANGLE previous_rect = {-40, 360, -20, 220};
	if (scenario % 3 == 0) {
		previous_rect.left = shape_rect.left = 400;
		previous_rect.right = shape_rect.right = 420;
	}
	state.opponentstate.car_position.lx = 16385;
	state.opponentstate.car_position.ly = -321;
	state.opponentstate.car_position.lz = -327681;
	state.opponentstate.car_rotate.x = (legacy_s16)(scenario * 97);
	struct VECTOR stars[100];
	struct POINT2D previous_points[100];
	for (legacy_u32 index = 0; index < 100; index++) {
		stars[index].x = LEGACY_S16_WRAP_ADD(camera_x, (legacy_s16)(index * 173 - 8000));
		stars[index].y = LEGACY_S16_WRAP_ADD(camera_y, (legacy_s16)(index * 113 - 5000));
		stars[index].z = LEGACY_S16_WRAP_ADD(camera_z, (legacy_s16)(index * 73 - 2400));
		previous_points[index].px = (legacy_s16)(index * 7 - 30);
		previous_points[index].py = (legacy_s16)(index * 3 - 20);
	}
	stars[0].z = LEGACY_S16_WRAP_ADD(camera_z, 199);
	stars[1].z = LEGACY_S16_WRAP_ADD(camera_z, 200);
	stars[2].z = LEGACY_S16_WRAP_ADD(camera_z, 201);
	struct RECTANGLE combined_rect = {1, 2, 3, 4};
	intro_render_scene(camera_x, camera_y, camera_z, rotation * 256, 0, draw_car, logo, stars,
					   previous_points, &count, previous_rect, &shape_rect, &combined_rect);
	assert(queued_count == 1 + draw_car);
	assert(flush_count == 1);
	assert(count >= 0 && count <= 100);
	if (slow_video_mgmt_copy != 0) {
		assert(pixel_count == (legacy_u32)(old_count + count));
	} else {
		assert(count == old_count);
	}
	record_word(count);
	record_word(intro_colorvalue);
	record_rect(&shape_rect);
	record_rect(&combined_rect);
	for (legacy_u32 index = 0; index < 100; index++) {
		record_word(previous_points[index].px);
		record_word(previous_points[index].py);
	}
}

static void preview_map(legacy_u32 variant)
{
	track_element_map = elements;
	track_terrain_map = terrain;
	for (legacy_u32 row = 0; row < 30; row++) {
		trackrows[row] = row * 30;
		terrainrows[row] = (29 - row) * 30;
		track_column_positions[row] = row * 1024;
		track_column_centers[row] = row * 1024 + 512;
		track_row_positions[row] = (30 - row) * 1024;
		track_row_centers[row] = (29 - row) * 1024 + 512;
	}
	for (legacy_u32 row = 0; row < 30; row++) {
		for (legacy_u32 column = 0; column < 30; column++) {
			legacy_u32 tile = (row * 30 + column + variant * 43) % 215;
			if ((column == 29 && (trkObjectList[tile].ss_multiTileFlag & 2) != 0) ||
				(row == 29 && tile >= 105 && tile <= 108)) {
				tile = 0;
			}
			if (column == 28 && row % 3 == 0) {
				tile = 253 + (row / 3) % 3;
			}
			elements[trackrows[row] + column] = tile;
			terrain[terrainrows[row] + column] =
				variant == 0 ? 0 : (variant == 1 ? 6 : (row + column * 3 + variant) % 19);
		}
	}
}

static void preview_case(legacy_u32 scenario)
{
	reset_projection();
	preview_map(scenario % 4);
	track_preview_camera_x = (legacy_s16)(scenario * 1027 - 16000);
	track_preview_camera_y = (legacy_s16)(scenario * 337 - 8000);
	track_preview_camera_z = (legacy_s16)(17000 - scenario * 919);
	track_preview_target_x = 15360;
	track_preview_target_y = 100;
	track_preview_target_z = 15360;
	track_preview_horizon_vector.x = 0;
	track_preview_horizon_vector.y = scenario & 1 ? -400 : 400;
	track_preview_horizon_vector.z = 1000;
	skybox.minimum_height = 20;
	skybox.sky_color = 3;
	skybox.ground_color = 6;
	for (legacy_u32 index = 0; index < 4; index++) {
		skybox.heights[index] = 20 + index * 5;
		skyboxes[index] = &sky_images[index];
	}
	draw_track_preview();
	legacy_u32 expected_flushes = TRACK_GRID_SIZE * TRACK_GRID_SIZE;
#ifdef RESTUNTS_SDL3
	if (hires_active != 0) {
		expected_flushes = 1;
	}
#endif
	assert(flush_count == expected_flushes);
	assert(queued_count > 0);
	record_word(queued_count);
}

#ifdef RESTUNTS_SDL3
static void test_preview_batching(void)
{
	for (legacy_u32 scenario = 0; scenario < PREVIEW_SCENARIO_COUNT; scenario++) {
		preview_case(scenario);
		legacy_u32 classic_queued_count = queued_count;
		hires_active = 1;
		preview_case(scenario);
		assert(queued_count == classic_queued_count);
		hires_active = 0;
		preview_case(scenario);
		assert(queued_count == classic_queued_count);
	}
}
#endif

struct PREVIEW_WATER_FIXTURE {
	legacy_u8 track, footprint, cells, column, row;
};

static void preview_water_case(const struct PREVIEW_WATER_FIXTURE *fixture, legacy_u8 wet_cells,
							   legacy_u8 water, legacy_u8 raised)
{
	static const legacy_u8 continuations[PREVIEW_WATER_QUADRANTS] = {
		0, TRACK_TILE_CONTINUATION_EAST, TRACK_TILE_CONTINUATION_SOUTH,
		TRACK_TILE_CONTINUATION_SOUTHEAST};
	reset_projection();
	memset(elements, 0, sizeof(elements));
	memset(terrain, 0, sizeof(terrain));
	memset(&trkObjectList[fixture->track], 0, sizeof(trkObjectList[fixture->track]));
	trkObjectList[fixture->track].ss_multiTileFlag = fixture->footprint;
	trkObjectList[fixture->track].ss_loShapePtr = &game3dshapes[0];
	legacy_u8 expected = wet_cells & fixture->cells;
	legacy_u8 bridge = fixture->track >= PREVIEW_WATER_BRIDGE_FIRST &&
					   fixture->track <= PREVIEW_WATER_BRIDGE_LAST && raised == 0;
	if (supersight_enabled == 0 && bridge == 0) {
		expected &= PREVIEW_WATER_NORTHWEST;
	}
	if (raised != 0) {
		expected &= ~PREVIEW_WATER_NORTHWEST;
	}
	for (legacy_u8 quadrant = 0; quadrant < PREVIEW_WATER_QUADRANTS; quadrant++) {
		legacy_u8 column = fixture->column + (quadrant & 1U);
		legacy_u8 row = fixture->row + (quadrant >> 1U);
		legacy_u8 cell = 1U << quadrant;
		if (column >= TRACK_GRID_SIZE || row >= TRACK_GRID_SIZE) {
			expected &= ~cell;
			continue;
		}
		if ((fixture->cells & cell) == 0) {
			continue;
		}
		elements[trackrows[row] + column] =
			quadrant == 0 ? fixture->track : continuations[quadrant];
		terrain[terrainrows[row] + column] =
			(wet_cells & cell) != 0
				? PREVIEW_WATER_FIRST +
					  (water - PREVIEW_WATER_FIRST + quadrant) % PREVIEW_WATER_COUNT
				: 0;
		if (quadrant == 0 && raised != 0) {
			terrain[terrainrows[row] + column] = TERRAIN_RAISED_TILE;
		}
	}
	preview_captured_count = 0;
	preview_capture_active = 1;
	draw_track_preview();
	preview_capture_active = 0;
	assert(flush_count == TRACK_GRID_SIZE * TRACK_GRID_SIZE);
	legacy_u8 seen = 0, object_drawn = 0;
	for (legacy_u16 index = 0; index < preview_captured_count; index++) {
		const struct TRANSFORMEDSHAPE3D *shape = &preview_captured[index];
		if (shape->shapeptr == &game3dshapes[0]) {
			assert(object_drawn++ == 0);
			continue;
		}
		legacy_u8 cell = 0;
		for (legacy_u8 quadrant = 0; quadrant < PREVIEW_WATER_QUADRANTS; quadrant++) {
			legacy_u8 column = fixture->column + (quadrant & 1U);
			legacy_u8 row = fixture->row + (quadrant >> 1U);
			if (column >= TRACK_GRID_SIZE || row >= TRACK_GRID_SIZE ||
				(expected & (1U << quadrant)) == 0) {
				continue;
			}
			legacy_u8 tile_terrain = terrain[terrainrows[row] + column];
			legacy_u16 shape_base =
				bridge != 0 ? PREVIEW_WATER_HIGH_SHAPE : PREVIEW_WATER_LOW_SHAPE;
			if (shape->shapeptr != &game3dshapes[shape_base + tile_terrain]) {
				continue;
			}
			assert(object_drawn == 0);
			assert(shape->pos.x == (track_column_centers[column] - track_preview_camera_x) /
									   PREVIEW_WATER_HALF_SCALE);
			assert(shape->pos.z ==
				   (track_row_centers[row] - track_preview_camera_z) / PREVIEW_WATER_HALF_SCALE);
			assert(shape->pos.y == -PREVIEW_WATER_CAMERA_HEIGHT / PREVIEW_WATER_HALF_SCALE);
			assert(shape->rotvec.z == terrain_scene_objects[tile_terrain].ss_rotY);
			assert(shape->rotvec.x == 0 && shape->rotvec.y == 0);
			assert(shape->ts_flags == PREVIEW_WATER_FLAGS && shape->material == 0);
			cell = 1U << quadrant;
		}
		if (cell != 0) {
			assert((seen & cell) == 0);
			seen |= cell;
		} else {
			/* Raised owners retain their existing solid hill foundation. */
			assert(raised != 0 && object_drawn == 0);
			assert(shape->pos.y ==
				   (hillHeightConsts[TERRAIN_RAISED_HEIGHT_INDEX] - PREVIEW_WATER_CAMERA_HEIGHT) /
					   PREVIEW_WATER_HALF_SCALE);
		}
	}
	assert(seen == expected && object_drawn == 1);
}

static void test_preview_covered_water(void)
{
	static const legacy_u8 coverage[] = {PREVIEW_WATER_NORTHWEST, PREVIEW_WATER_VERTICAL,
										 PREVIEW_WATER_HORIZONTAL, PREVIEW_WATER_ALL_CELLS};
	preview_map(0);
	track_preview_camera_x = track_column_centers[PREVIEW_WATER_TILE];
	track_preview_camera_y = PREVIEW_WATER_CAMERA_HEIGHT;
	track_preview_camera_z = track_row_centers[PREVIEW_WATER_TILE];
	for (legacy_u8 water = PREVIEW_WATER_FIRST; water <= PREVIEW_WATER_LAST; water++) {
		terrain_scene_objects[water].ss_shapePtr = &game3dshapes[PREVIEW_WATER_HIGH_SHAPE + water];
		terrain_scene_objects[water].ss_loShapePtr = &game3dshapes[PREVIEW_WATER_LOW_SHAPE + water];
	}
	/* Exercise the full preview traversal, so continuation suppression and any
	 * duplicate or late terrain submissions are covered along with tile drawing. */
	for (legacy_u8 footprint = 0; footprint < sizeof(coverage) / sizeof(coverage[0]); footprint++) {
		struct PREVIEW_WATER_FIXTURE fixture = {PREVIEW_WATER_OBJECT, footprint,
												coverage[footprint], PREVIEW_WATER_TILE,
												PREVIEW_WATER_TILE};
		for (legacy_u8 water = PREVIEW_WATER_FIRST; water <= PREVIEW_WATER_LAST; water++) {
			for (legacy_u8 wet = 0; wet <= PREVIEW_WATER_ALL_CELLS; wet++) {
				for (legacy_u8 raised = 0; raised <= 1; raised++) {
					for (supersight_enabled = 0; supersight_enabled <= 1; supersight_enabled++) {
						preview_water_case(&fixture, wet, water, raised);
					}
				}
			}
		}
	}
	struct PREVIEW_WATER_FIXTURE fixture = {
		PREVIEW_WATER_BRIDGE_FIRST, MULTI_TILE_ROW_EDGE_FLAG | MULTI_TILE_COLUMN_EDGE_FLAG,
		PREVIEW_WATER_ALL_CELLS, PREVIEW_WATER_TILE, PREVIEW_WATER_TILE};
	/* Elevated corners already draw four high-detail terrain cells. */
	for (; fixture.track <= PREVIEW_WATER_BRIDGE_LAST; fixture.track++) {
		for (legacy_u8 raised = 0; raised <= 1; raised++) {
			for (supersight_enabled = 0; supersight_enabled <= 1; supersight_enabled++) {
				preview_water_case(&fixture, PREVIEW_WATER_ALL_CELLS, PREVIEW_WATER_FIRST, raised);
			}
		}
	}
	fixture.track = PREVIEW_WATER_OBJECT;
	supersight_enabled = 1;
	for (legacy_u8 edge = 0; edge < PREVIEW_WATER_QUADRANTS; edge++) {
		fixture.column = (edge & 1U) != 0 ? TRACK_GRID_LAST_INDEX : PREVIEW_WATER_TILE;
		fixture.row = (edge & 2U) != 0 ? TRACK_GRID_LAST_INDEX : PREVIEW_WATER_TILE;
		preview_water_case(&fixture, PREVIEW_WATER_ALL_CELLS, PREVIEW_WATER_FIRST, 0);
	}
	supersight_enabled = 0;
}

static legacy_s8 title_data[3];
static struct SPRITE intro_sprite;
static legacy_u32 input_polls, cancel_after, copy_backbuffer;
static legacy_u16 random_value;
static legacy_u32 random_calls, timer_reads;

void *file_load_3dres(const legacy_s8 *name)
{
	assert(strcmp((const legacy_char *)name, "title") == 0);
	record_word(20);
	return title_data;
}
void locate_many_resources(legacy_s8 *data, const legacy_s8 *names, legacy_s8 **result)
{
	assert(data == title_data);
	assert(strcmp((const legacy_char *)names, "logolog2brav") == 0);
	record_word(21);
	for (legacy_u32 index = 0; index < 3; index++) {
		result[index] = &title_data[index];
	}
}
void shape3d_init_shape(legacy_s8 *source, struct SHAPE3D *destination)
{
	record_word(22);
	record_word(source - title_data);
	record_word(shape_id(destination));
}
struct SPRITE *sprite_make_wnd(legacy_u16 width, legacy_u16 height, legacy_u16 color)
{
	record_word(23);
	record_word(width);
	record_word(height);
	record_word(color);
	intro_sprite.sprite_bitmapptr = &sky_images[0];
	return &intro_sprite;
}
void sprite_free_wnd(struct SPRITE *sprite)
{
	assert(sprite == &intro_sprite);
	record_word(24);
}
legacy_s16 get_kevinrandom(void)
{
	random_calls++;
	random_value = (legacy_u16)(random_value * 25173U + 13849U);
	record_word(25);
	record_word(random_value & 255U);
	return random_value & 255U;
}
void set_projection(legacy_s16 horizontal, legacy_s16 vertical, legacy_s16 width, legacy_s16 height)
{
	record_word(26);
	record_word(horizontal);
	record_word(vertical);
	record_word(width);
	record_word(height);
}
void *file_load_resfile(const legacy_s8 *name)
{
	assert(strcmp((const legacy_char *)name, "carcoun") == 0);
	record_word(27);
	return title_data;
}
void setup_aero_trackdata(void *resource, legacy_s16 opponent)
{
	assert(resource == title_data);
	record_word(28);
	record_word(opponent);
}
void unload_resource(void *resource)
{
	assert(resource == title_data);
	record_word(29);
}
void *mmgr_free(legacy_s8 *resource)
{
	assert(resource == title_data);
	record_word(30);
	return 0;
}
void init_plantrak(void)
{
	record_word(31);
}
legacy_u32 timer_get_delta(void)
{
	timer_reads++;
	record_word(32);
	if (scripted_input == 3) {
		scripted_now += 1000000ULL;
		return scripted_now % 10000000ULL == 0;
	}
	scripted_now += 20000000ULL;
	/* Pause simulation on the first poll after each display toggle. The
	 * renderer still has to refresh immediately at the new resolution. */
	if (scripted_input == 1 && (timer_reads == 3 || timer_reads == 5)) {
		return 0;
	}
	return 2;
}
void update_opponent(void)
{
	opponent_updates++;
	record_word(33);
	state.opponentstate.car_position.lx += 128;
	state.opponentstate.car_position.lz += 64;
	state.opponentstate.car_rotate.x = LEGACY_S16_WRAP_ADD(state.opponentstate.car_rotate.x, 3);
}
legacy_s16 input_do_checking(legacy_s16 delta)
{
	record_word(34);
	record_word(delta);
	input_polls++;
	assert(drawing_frame == 0);
	if (scripted_input == 3) {
		if (supersight_enabled != 0) {
			assert(scripted_now - previous_input_time >= 10000000ULL);
		}
		previous_input_time = scripted_now;
		if (scripted_key == KEY_F12 && ((shortcut_count == 0 && scripted_now >= 400000000ULL) ||
										(shortcut_count == 1 && scripted_now >= 650000000ULL))) {
			return KEY_F12;
		}
		return scripted_now >= 1000000000ULL ? KEY_ESCAPE : 0;
	}
	if (scripted_input != 0) {
		if (scripted_input == INTRO_TEST_STATUS_EXPIRY_INPUT &&
			input_polls == INTRO_TEST_STATUS_EXPIRY_POLL) {
			preset_status_active = 0;
		}
		if (input_polls == 1 || (scripted_input == 1 && input_polls == 3)) {
			return scripted_key;
		}
		if ((scripted_input == 1 || scripted_input == INTRO_TEST_STATUS_EXPIRY_INPUT) &&
			input_polls >= INTRO_TEST_EXIT_POLL) {
			return KEY_ESCAPE;
		}
		return 0;
	}
	return cancel_after != 0 && input_polls >= cancel_after;
}
void sprite_select_mcga_backbuffer(void)
{
	record_word(35);
}
void sprite_select_render_window(void)
{
	record_word(36);
}
void sprite_select_screen_compat(void)
{
	record_word(37);
}
void sprite_present_mcga_backbuffer(void)
{
	record_word(38);
}
void mouse_draw_opaque_check(void)
{
	record_word(39);
}
void mouse_draw_transparent_check(void)
{
	record_word(40);
}
void sprite_putimage(struct SHAPE2D *shape)
{
	assert(shape == &sky_images[0]);
	record_word(41);
}
legacy_s16 video_backbuffer_copy_required(void)
{
	record_word(42);
	return copy_backbuffer;
}
void sprite_copy_rect_shifted(legacy_s16 x, legacy_s16 y, legacy_s16 width, legacy_s16 height,
							  legacy_s16 shift)
{
	record_word(43);
	record_word(x);
	record_word(y);
	record_word(width);
	record_word(height);
	record_word(shift);
}
/* Full lifecycle uses full redraw: the original setup routine leaves its first
 * dirty rectangle uninitialized. Standalone scene cases cover dirty rectangles
 * with explicit previous-frame inputs instead. */
static void lifecycle_case(legacy_u32 scenario)
{
	reset_projection();
	memset(&state, 0, sizeof(state));
	framespersec = 20;
	timer_ticks_per_frame = 1;
	intro_elapsed_ticks = 0;
	intro_colorvalue = 1;
	intro_palette_color_count = 16;
	slow_video_mgmt = 0;
	video_uses_page_flipping = scenario & 1;
	copy_backbuffer = (scenario >> 1) & 1;
	static const legacy_u32 cancellations[] = {0, 1, 80, 160};
	cancel_after = cancellations[(scenario >> 2) & 3];
	input_polls = 0;
	random_value = 1;
	random_calls = timer_reads = 0;
	state.opponentstate.car_position.lx = 64000;
	state.opponentstate.car_position.ly = 1280;
	state.opponentstate.car_position.lz = 64000;
	legacy_s8 interrupted = setup_intro();
	assert(interrupted == (cancel_after != 0));
	assert(input_polls > 0);
	/* One RNG draw per axis of each of the 100 stars, and one elapsed-time
	 * read per frame after the initial timer reset. */
	assert(random_calls == 300U);
	assert(timer_reads == input_polls + 1U);
	record_word(interrupted);
	record_word(input_polls);
	record_word(random_value);
	record_word(intro_elapsed_ticks);
	record_word(intro_colorvalue);
}

static void assert_full_clear(legacy_u32 index)
{
	assert(cleared_rects[index].left == intro_cliprect.left);
	assert(cleared_rects[index].right == intro_cliprect.right);
	assert(cleared_rects[index].top == intro_cliprect.top);
	assert(cleared_rects[index].bottom == intro_cliprect.bottom);
}

static void display_toggle_case(legacy_u32 scenario)
{
	reset_projection();
	memset(&state, 0, sizeof(state));
	memset(cleared_rects, 0, sizeof(cleared_rects));
	memset(rendered_modes, 0, sizeof(rendered_modes));
	memset(rendered_fps, 0, sizeof(rendered_fps));
	framespersec = 20;
	timer_ticks_per_frame = 1;
	intro_elapsed_ticks = 0;
	intro_colorvalue = 1;
	intro_palette_color_count = 16;
	video_uses_page_flipping = scenario & 1;
	slow_video_mgmt = (scenario >> 1) & 1;
	legacy_u8 initial_mode = (scenario >> 2) & 1;
	legacy_u8 initial_fps = (scenario >> 3) & 1;
	scripted_key = (scenario & 16) != 0 ? KEY_F11 : KEY_F12;
	supersight_enabled = initial_mode;
	fps_display_enabled = initial_fps;
	copy_backbuffer = 0;
	cancel_after = 0;
	input_polls = timer_reads = shortcut_count = opponent_updates = 0;
	fps_draw_count = fps_presented_count = fps_reset_count = 0;
	random_value = 1;
	scripted_input = 1;
	assert(setup_intro() == 1);
	assert(input_polls == 5);
	assert(shortcut_count == 2);
	assert(supersight_enabled == initial_mode);
	assert(fps_display_enabled == initial_fps);
	assert(opponent_updates == 5);
	assert(flush_count == 5);
	legacy_u32 expected_fps_draws = 0;
	for (legacy_u32 frame = 0; frame < flush_count; frame++) {
		legacy_u8 toggled = frame == 1 || frame == 2;
		legacy_u8 expected_mode = initial_mode ^ (toggled && scripted_key == KEY_F12);
		legacy_u8 expected_fps = initial_fps ^ (toggled && scripted_key == KEY_F11);
		assert(rendered_modes[frame] == expected_mode);
		assert(rendered_fps[frame] == expected_fps);
		expected_fps_draws += expected_fps || (scripted_key == KEY_F12 && frame != 0);
		/* Dirty rendering must clear the previous FPS text on the same page,
		 * including when F12 leaves the counter enabled throughout. */
		legacy_u32 page_age = video_uses_page_flipping != 0 ? 2 : 1;
		if (slow_video_mgmt != 0 && frame >= page_age && rendered_fps[frame - page_age]) {
			assert(cleared_rects[frame].left <= fps_bounds.left);
			assert(cleared_rects[frame].right >= fps_bounds.right);
			assert(cleared_rects[frame].top <= fps_bounds.top);
			assert(cleared_rects[frame].bottom >= fps_bounds.bottom);
		}
	}
	assert(fps_draw_count == expected_fps_draws);
	assert(fps_presented_count == flush_count);
	assert(fps_reset_count == (scripted_key == KEY_F11 ? 4U : 2U));
	/* The first frame and each toggle must clear old 3D pixels throughout
	 * the viewport. Page flipping must also clear its other stale page. */
	assert_full_clear(0);
	assert_full_clear(1);
	assert_full_clear(3);
	if (video_uses_page_flipping != 0) {
		assert_full_clear(2);
		assert_full_clear(4);
	}
	scripted_input = 0;
}

#ifdef RESTUNTS_SDL3
static void predictive_presentation_case(void)
{
	struct GAMESTATE reference;
	legacy_u32 reference_polls = 0;
	for (legacy_u16 mode = 0; mode < 3; mode++) {
		reset_projection();
		memset(&state, 0, sizeof(state));
		framespersec = 20;
		timer_ticks_per_frame = 5;
		intro_elapsed_ticks = 0;
		intro_colorvalue = 1;
		intro_palette_color_count = 16;
		slow_video_mgmt = video_uses_page_flipping = 0;
		supersight_enabled = mode == 1;
		fps_display_enabled = 0;
		input_polls = timer_reads = shortcut_count = opponent_updates = 0;
		fps_draw_count = fps_presented_count = fps_reset_count = 0;
		predicted_transforms = 0;
		scripted_now = previous_input_time = 0;
		scripted_input = 3;
		scripted_key = mode == 2 ? KEY_F12 : 0;
		copy_backbuffer = cancel_after = 0;
		random_value = 1;
		assert(setup_intro() == 1);
		assert(timer_reads >= 1000 && timer_reads <= 1010);
		assert(opponent_updates == 19);
		if (mode == 0) {
			reference = state;
			reference_polls = input_polls;
			assert(reference_polls == 999);
			assert(flush_count == 20);
			assert(predicted_transforms == 0);
		} else {
			assert(memcmp(&reference, &state, sizeof(state)) == 0);
			assert(input_polls <= reference_polls);
			assert(predicted_transforms > 0);
			assert(flush_count > 20);
		}
		if (mode == 1) {
			assert(input_polls == 100);
			/* Include the initial presentation and the deadline at one second. */
			assert(flush_count == 61);
		}
	}
	scripted_input = 0;
}

#endif

static void display_status_expiry_case(legacy_u32 scenario)
{
	reset_projection();
	framespersec = GAME_FRAME_RATE_NORMAL;
	timer_ticks_per_frame = 1;
	intro_elapsed_ticks = 0;
	intro_colorvalue = 1;
	intro_palette_color_count = 16;
	slow_video_mgmt = (scenario >> 1) & 1U;
	video_uses_page_flipping = scenario & 1U;
	supersight_enabled = fps_display_enabled = 0;
	input_polls = timer_reads = shortcut_count = 0;
	fps_draw_count = fps_presented_count = fps_reset_count = 0;
	copy_backbuffer = cancel_after = 0;
	scripted_key = KEY_F12;
	scripted_input = INTRO_TEST_STATUS_EXPIRY_INPUT;
	assert(setup_intro() == 1);
	assert(shortcut_count == 1 && supersight_enabled != 0);
	assert(fps_draw_count == INTRO_TEST_STATUS_EXPIRY_POLL - 1U);
	assert(fps_presented_count == flush_count);
	legacy_u32 page_age = video_uses_page_flipping != 0 ? 2U : 1U;
	for (legacy_u32 frame = INTRO_TEST_STATUS_EXPIRY_POLL; frame < flush_count; frame++) {
		if (slow_video_mgmt != 0 && frame < INTRO_TEST_STATUS_EXPIRY_POLL + page_age) {
			/* Status expiry must restore each page that still contains the OSD. */
			assert(cleared_rects[frame].left <= fps_bounds.left);
			assert(cleared_rects[frame].right >= fps_bounds.right);
			assert(cleared_rects[frame].top <= fps_bounds.top);
			assert(cleared_rects[frame].bottom >= fps_bounds.bottom);
		}
	}
	scripted_input = 0;
}

static void display_toggle_completion_case(legacy_s16 key)
{
	reset_projection();
	framespersec = 20;
	timer_ticks_per_frame = 1;
	intro_elapsed_ticks = 0;
	intro_colorvalue = 1;
	intro_palette_color_count = 16;
	slow_video_mgmt = video_uses_page_flipping = 0;
	supersight_enabled = fps_display_enabled = 0;
	input_polls = timer_reads = shortcut_count = 0;
	fps_draw_count = fps_presented_count = fps_reset_count = 0;
	copy_backbuffer = cancel_after = 0;
	scripted_key = key;
	scripted_input = 2;
	assert(setup_intro() == 0);
	assert(input_polls > 1);
	assert(shortcut_count == 1);
	assert(supersight_enabled == (key != KEY_F11));
	assert(fps_display_enabled == (key == KEY_F11));
	assert(fps_draw_count == flush_count - 1U);
	assert(fps_presented_count == flush_count);
	assert(fps_reset_count == (key == KEY_F11 ? 3U : 2U));
	scripted_input = 0;
}

int main(void)
{
	trace_hash = 2166136261UL;
	for (legacy_u32 scenario = 0; scenario < 480; scenario++) {
		record_word(scenario);
		intro_case(scenario);
	}
	legacy_u32 intro_hash = trace_hash;
	trace_hash = 2166136261UL;
	for (legacy_u32 scenario = 0; scenario < PREVIEW_SCENARIO_COUNT; scenario++) {
		record_word(scenario);
		preview_case(scenario);
	}
	legacy_u32 preview_hash = trace_hash;
	trace_hash = 2166136261UL;
	for (legacy_u32 scenario = 0; scenario < 16; scenario++) {
		record_word(scenario);
		lifecycle_case(scenario);
	}
	legacy_u32 lifecycle_hash = trace_hash;
#ifdef INTRO_PREVIEW_RECORD_BASELINE
	fprintf(stdout,
			"intro=0x%08" LEGACY_PRIx32 " preview=0x%08" LEGACY_PRIx32
			" lifecycle=0x%08" LEGACY_PRIx32 "\n",
			intro_hash, preview_hash, lifecycle_hash);
#else
	/* Captured from unmodified production at 43fba20d. */
	assert(intro_hash == 0xcdcdbe61UL);
	assert(preview_hash == 0x8ae71f07UL);
	/* Lifecycle now evaluates each RNG and timer operation exactly once. */
	assert(lifecycle_hash == 0xa3183e31UL);
#endif
#ifdef RESTUNTS_SDL3
	test_preview_batching();
#endif
	for (legacy_u32 scenario = 0; scenario < 32; scenario++) {
		display_toggle_case(scenario);
	}
	for (legacy_u32 scenario = 0; scenario < INTRO_TEST_CLEAR_PAGE_CASES; scenario++) {
		display_status_expiry_case(scenario);
	}
	display_toggle_completion_case(KEY_F12);
	display_toggle_completion_case(KEY_F11);
#ifdef RESTUNTS_SDL3
	display_toggle_completion_case(KEY_SHIFT_F12);
	predictive_presentation_case();
#endif
	test_preview_covered_water();
	return 0;
}
