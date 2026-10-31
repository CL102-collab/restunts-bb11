#include <assert.h>
#include <string.h>

#include "../c/externs.h"
#include "../c/extended_track.h"
#include "../c/extended_frame.h"
#include "../c/frame_internal.h"
#include "../c/game_input.h"
#include "../c/hires.h"
#include "../c/menu_internal.h"
#include "../c/platform.h"
#include "../c/projection.h"
#include "../c/shape2d.h"
#include "../c/shape3d_hires.h"
#include "../c/skybox.h"
#include "../c/track_objects.h"
#include "../c/trackdata_layout.h"

#define PREVIEW_TEST_OBJECT_CAPACITY 8U
#define PREVIEW_TEST_SPIN_TICKS 4000U
#define PREVIEW_TEST_QUARTER_SPIN_TICKS (PREVIEW_TEST_SPIN_TICKS / 4U)
#define PREVIEW_TEST_MINIMUM_ORBIT_HEIGHT (12 * CSTRK_LEVEL_WORLD_HEIGHT)
#define PREVIEW_TEST_DEGREES_PER_TURN 360U
#define PREVIEW_TEST_FIT_TOLERANCE .1
#define PREVIEW_TEST_HEADER_BOTTOM 40
#define PREVIEW_TEST_FIT_MARGIN 4
#define PREVIEW_TEST_PRECISION .000001
#define PREVIEW_TEST_SCREEN_WIDTH 320
#define PREVIEW_TEST_SCREEN_HEIGHT 200

/* Render endpoints capture the production preview's world coordinates. The
 * camera is recovered independently from each tile's translated origin, so a
 * wrapped far-edge tile or a stale per-tile origin fails immediately. */
static struct CSTRK_TRACK preview_track;
static struct CSTRK_CELL preview_cells[CSTRK_MAX_OBJECTS];
static legacy_u8 preview_heights[(CSTRK_MAX_DIMENSION + 1U) * (CSTRK_MAX_DIMENSION + 1U)];
static struct CSTRK_OBJECT preview_objects[PREVIEW_TEST_OBJECT_CAPACITY];
static const struct CSTRK_OBJECT *preview_owners[CSTRK_MAX_OBJECTS];
static legacy_s16 preview_elevations[CSTRK_MAX_OBJECTS];
static legacy_u8 visited_tiles[CSTRK_MAX_OBJECTS];
static legacy_u8 lap_tiles[CSTRK_MAX_OBJECTS];
static legacy_u32 preview_ticks;
static legacy_s32 preview_hires;
legacy_u8 supersight_enabled;

static legacy_u32 fill_count, outline_count, clear_count;
static legacy_u32 tile_count, flush_count, origin_count, reset_count;
static legacy_u32 projection_count;
static legacy_u32 background_count;
static legacy_s16 current_yaw, current_pitch;
static legacy_s16 background_heading;
static legacy_u16 projection_origin_x, projection_origin_y;
static struct RECTANGLE current_clip, background_clip;
static struct SHAPE3D_HIRES_VECTOR current_origin, captured_camera;
static legacy_u8 origin_active;
static struct MATRIX captured_rotation;
static legacy_u16 captured_center_x, captured_center_y;
static legacy_u16 captured_focal_x, captured_focal_y;

struct PREVIEW_TEST_BOUNDS {
	legacy_f64 min_x, max_x, min_y, max_y, min_z, max_z;
};

static void assert_close(legacy_f64 actual, legacy_f64 expected)
{
	legacy_f64 difference = actual - expected;
	assert(difference > -PREVIEW_TEST_PRECISION && difference < PREVIEW_TEST_PRECISION);
}

int extended_track_active(void)
{
	return 1;
}

const legacy_u8 *extended_track_lap_tiles(void)
{
	return lap_tiles;
}

const struct CSTRK_TRACK *extended_track_model(void)
{
	return &preview_track;
}

const struct CSTRK_OBJECT *extended_track_object_at(legacy_int column, legacy_int row)
{
	assert(column >= 0 && column < preview_track.width);
	assert(row >= 0 && row < preview_track.height);
	return preview_owners[row * preview_track.width + column];
}

legacy_uint extended_track_object_id(const struct CSTRK_OBJECT *object)
{
	return object->legacy_id;
}

legacy_s16 extended_track_elevation(legacy_int column, legacy_int row, legacy_int local_x,
									legacy_int local_y)
{
	assert(column >= 0 && column < preview_track.width);
	assert(row >= 0 && row < preview_track.height);
	assert(local_x >= 0 && local_x <= CSTRK_TILE_WORLD_SIZE);
	assert(local_y >= 0 && local_y <= CSTRK_TILE_WORLD_SIZE);
	return preview_elevations[row * preview_track.width + column];
}

legacy_s32 hires_enabled(void)
{
	return preview_hires;
}

legacy_u32 dos_timer_get_realtime_counter(void)
{
	return preview_ticks;
}

void shape3d_hires_set_origin(const struct SHAPE3D_HIRES_VECTOR *origin)
{
	if (origin == NULL) {
		origin_active = 0;
		memset(&current_origin, 0, sizeof(current_origin));
		reset_count++;
		return;
	}
	assert(preview_hires != 0);
	current_origin = *origin;
	origin_active = 1;
	origin_count++;
}

void extended_frame_draw_preview_tile(legacy_s16 column, legacy_s16 row)
{
	assert(preview_hires != 0 && origin_active != 0);
	assert(current_clip.top == 0 && current_clip.bottom < PREVIEW_TEST_SCREEN_HEIGHT);
	assert(background_count == 1);
	assert(current_clip.left == background_clip.left &&
		   current_clip.right == background_clip.right);
	assert(current_clip.top == background_clip.top &&
		   current_clip.bottom == background_clip.bottom);
	assert(column >= 0 && column < preview_track.width);
	assert(row >= 0 && row < preview_track.height);
	legacy_u32 index = (legacy_u32)row * preview_track.width + column;
	assert(visited_tiles[index] == 0);
	visited_tiles[index] = 1;
	struct SHAPE3D_HIRES_VECTOR camera;
	camera.x = ((legacy_f64)column + .5) * CSTRK_TILE_WORLD_SIZE - current_origin.x;
	camera.y = -current_origin.y;
	camera.z =
		((legacy_f64)preview_track.height - row - .5) * CSTRK_TILE_WORLD_SIZE - current_origin.z;
	if (tile_count == 0) {
		captured_camera = camera;
	} else {
		assert_close(camera.x, captured_camera.x);
		assert_close(camera.y, captured_camera.y);
		assert_close(camera.z, captured_camera.z);
	}
	tile_count++;
}

void shape3d_render_queued_primitives(void)
{
	assert(tile_count == (legacy_u32)preview_track.width * preview_track.height);
	flush_count++;
}

legacy_u16 shape3d_transform_and_queue(struct TRANSFORMEDSHAPE3D *shape)
{
	(void)shape;
	assert(!"Extended preview must use the complete extended geometry path");
	return 0;
}

void set_projection(legacy_s16 horizontal_fov, legacy_s16 vertical_fov, legacy_s16 width,
					legacy_s16 height)
{
	assert(horizontal_fov > 0 && vertical_fov > 0);
	assert(width == PREVIEW_TEST_SCREEN_WIDTH);
	assert(height > 0 && height <= PREVIEW_TEST_SCREEN_HEIGHT);
	projection_center_x = (legacy_u16)width / 2;
	projection_center_y = (legacy_u16)height / 2;
	legacy_u16 horizontal_angle =
		(legacy_u32)horizontal_fov * ANGLE_FULL_TURN / PREVIEW_TEST_DEGREES_PER_TURN;
	legacy_u16 vertical_angle =
		(legacy_u32)vertical_fov * ANGLE_FULL_TURN / PREVIEW_TEST_DEGREES_PER_TURN;
	projection_focal_length_x =
		(legacy_s32)cos_fast(horizontal_angle) * (width / 2) / sin_fast(horizontal_angle);
	projection_focal_length_y =
		(legacy_s32)cos_fast(vertical_angle) * (height / 2) / sin_fast(vertical_angle);
	projection_count++;
}

void projection_set_origin(legacy_u16 x, legacy_u16 y)
{
	projection_origin_x = x;
	projection_origin_y = y;
	projection_center_y = PREVIEW_TEST_SCREEN_HEIGHT / 2 + y;
}

legacy_u16 select_cliprect_rotate(legacy_s16 roll, legacy_s16 pitch, legacy_s16 yaw,
								  struct RECTANGLE *clip, legacy_s16 half_scale)
{
	assert(roll == 0 && half_scale == 0);
	assert(clip->left >= 0 && clip->right <= PREVIEW_TEST_SCREEN_WIDTH);
	assert(clip->top >= 0 && clip->bottom <= PREVIEW_TEST_SCREEN_HEIGHT);
	assert(clip->left < clip->right && clip->top < clip->bottom);
	current_yaw = yaw;
	current_pitch = pitch;
	captured_rotation = *mat_rot_zxy(roll, pitch, yaw, MATRIX_ROTATION_ORDER_YXZ);
	captured_center_x = projection_center_x;
	captured_center_y = projection_center_y;
	captured_focal_x = projection_focal_length_x;
	captured_focal_y = projection_focal_length_y;
	return 0;
}

void sprite_set_target_clip_bounds(legacy_u16 left, legacy_u16 right, legacy_u16 top,
								   legacy_u16 bottom)
{
	current_clip.left = (legacy_s16)left;
	current_clip.right = (legacy_s16)right;
	current_clip.top = (legacy_s16)top;
	current_clip.bottom = (legacy_s16)bottom;
}

void sprite_clear_target(legacy_u8 color)
{
	(void)color;
	clear_count++;
}

void sprite_fill_rect(legacy_s16 x, legacy_s16 y, legacy_s16 width, legacy_s16 height,
					  legacy_s16 color)
{
	assert(preview_hires == 0);
	assert(x >= 0 && x + width <= PREVIEW_TEST_SCREEN_WIDTH);
	assert(y >= 0 && y + height <= PREVIEW_TEST_SCREEN_HEIGHT);
	assert(width >= 0 && height >= 0);
	assert(color >= 0 && color < 16);
	fill_count++;
}

void sprite_draw_rect_outline(legacy_s16 x1, legacy_s16 y1, legacy_s16 x2, legacy_s16 y2,
							  legacy_s16 color)
{
	assert(preview_hires == 0);
	assert(x1 < x2 && y1 < y2 && color >= 0 && color < 16);
	outline_count++;
}

void sprite_putpixel_clipped(legacy_s16 x, legacy_s16 y, legacy_s16 color)
{
	(void)x;
	(void)y;
	(void)color;
	assert(!"These fixtures have no split-material terrain");
}

void skybox_copy_image(legacy_s16 image, legacy_s16 x, legacy_s16 y)
{
	(void)image;
	(void)x;
	(void)y;
	assert(!"Extended preview has no classic horizon strip");
}

void skybox_render_level_rect(struct RECTANGLE *rect, legacy_s16 heading, legacy_s16 horizon)
{
	assert(preview_hires != 0);
	assert(rect->left == 0 && rect->right == PREVIEW_TEST_SCREEN_WIDTH);
	assert(rect->top == 0 && rect->bottom < PREVIEW_TEST_SCREEN_HEIGHT);
	assert(rect->top < rect->bottom);
	assert(horizon < (legacy_s16)projection_center_y);
	background_clip = *rect;
	background_heading = heading;
	background_count++;
}

struct TRACKOBJECT *frame_track_object_from_legacy_index(legacy_u8 index)
{
	assert(index < 215U);
	return &trkObjectList[index];
}

static void reset_capture(void)
{
	fill_count = outline_count = clear_count = 0;
	tile_count = flush_count = origin_count = reset_count = 0;
	projection_count = 0;
	background_count = 0;
	memset(visited_tiles, 0, sizeof(visited_tiles));
}

static void make_track(legacy_u16 width, legacy_u16 height)
{
	memset(&preview_track, 0, sizeof(preview_track));
	memset(preview_cells, 0, sizeof(preview_cells));
	memset(preview_heights, 0, sizeof(preview_heights));
	memset(preview_objects, 0, sizeof(preview_objects));
	memset(preview_owners, 0, sizeof(preview_owners));
	memset(preview_elevations, 0, sizeof(preview_elevations));
	memset(lap_tiles, 0, sizeof(lap_tiles));
	preview_track.version = CSTRK_VERSION;
	preview_track.width = width;
	preview_track.height = height;
	preview_track.cells = preview_cells;
	preview_track.corner_heights = preview_heights;
	preview_track.objects = preview_objects;
}

static void add_object(legacy_u16 id, legacy_u8 x, legacy_u8 y, legacy_u8 width, legacy_u8 height,
					   legacy_u8 mask)
{
	assert(preview_track.object_count < PREVIEW_TEST_OBJECT_CAPACITY);
	struct CSTRK_OBJECT *object = &preview_objects[preview_track.object_count++];
	object->legacy_id = id;
	object->x = x;
	object->y = y;
	object->width = width;
	object->height = height;
	object->occupied_mask = mask;
	for (legacy_u8 row = 0; row < height; row++) {
		for (legacy_u8 column = 0; column < width; column++) {
			if ((mask & (1U << (row * width + column))) != 0) {
				legacy_u32 index = (legacy_u32)(y + row) * preview_track.width + x + column;
				assert(preview_owners[index] == NULL);
				preview_owners[index] = object;
			}
		}
	}
}

static struct SHAPE3D_HIRES_VECTOR render_3d(legacy_u32 ticks)
{
	preview_ticks = ticks;
	preview_hires = supersight_enabled = 1;
	reset_capture();
	draw_track_preview();
	assert(tile_count == (legacy_u32)preview_track.width * preview_track.height);
	assert(origin_count == tile_count);
	assert(flush_count == 1 && reset_count != 0 && origin_active == 0);
	assert(fill_count == 0 && outline_count == 0);
	assert(projection_count != 0);
	assert(projection_origin_x == 0 && projection_origin_y == 0);
	assert(current_clip.left == 0 && current_clip.right == PREVIEW_TEST_SCREEN_WIDTH);
	assert(current_clip.top == 0 && current_clip.bottom == PREVIEW_TEST_SCREEN_HEIGHT);
	assert(background_count == 1 && background_heading == current_yaw);
	assert(current_pitch != 0);
	return captured_camera;
}

static struct SHAPE3D_HIRES_POINT project_world_point(legacy_f64 x, legacy_f64 y, legacy_f64 z)
{
	legacy_f64 dx = x - captured_camera.x;
	legacy_f64 dy = y - captured_camera.y;
	legacy_f64 dz = z - captured_camera.z;
	legacy_f64 view_x = (captured_rotation.m._11 * dx + captured_rotation.m._12 * dy +
						 captured_rotation.m._13 * dz) /
						TRIG_FIXED_ONE;
	legacy_f64 view_y = (captured_rotation.m._21 * dx + captured_rotation.m._22 * dy +
						 captured_rotation.m._23 * dz) /
						TRIG_FIXED_ONE;
	legacy_f64 view_z = (captured_rotation.m._31 * dx + captured_rotation.m._32 * dy +
						 captured_rotation.m._33 * dz) /
						TRIG_FIXED_ONE;
	assert(view_z > 0);
	struct SHAPE3D_HIRES_POINT point = {captured_center_x + captured_focal_x * view_x / view_z,
										captured_center_y - captured_focal_y * view_y / view_z,
										1 / view_z};
	return point;
}

static void assert_bounds_fit(const struct PREVIEW_TEST_BOUNDS *bounds)
{
	if (bounds == NULL) {
		return;
	}
	/* Check all eight world-space corners through the actual view matrix,
	 * independently of the camera's distance-selection calculation. */
	for (legacy_u8 corner = 0; corner < 8U; corner++) {
		struct SHAPE3D_HIRES_POINT point =
			project_world_point(corner & 1U ? bounds->max_x : bounds->min_x,
								corner & 2U ? bounds->max_y : bounds->min_y,
								corner & 4U ? bounds->max_z : bounds->min_z);
		assert(point.x >=
			   background_clip.left + PREVIEW_TEST_FIT_MARGIN - PREVIEW_TEST_FIT_TOLERANCE);
		assert(point.x <=
			   background_clip.right - PREVIEW_TEST_FIT_MARGIN + PREVIEW_TEST_FIT_TOLERANCE);
		assert(point.y >=
			   PREVIEW_TEST_HEADER_BOTTOM + PREVIEW_TEST_FIT_MARGIN - PREVIEW_TEST_FIT_TOLERANCE);
		assert(point.y <=
			   background_clip.bottom - PREVIEW_TEST_FIT_MARGIN + PREVIEW_TEST_FIT_TOLERANCE);
	}
}

static void assert_orbit_center(legacy_f64 x, legacy_f64 y, legacy_f64 z,
								const struct PREVIEW_TEST_BOUNDS *bounds)
{
	struct SHAPE3D_HIRES_VECTOR first = render_3d(0);
	legacy_s16 first_yaw = current_yaw;
	legacy_f64 height = first.y - y;
	assert(height >= PREVIEW_TEST_MINIMUM_ORBIT_HEIGHT);
	assert_close(first.x, x);
	assert_close(z - first.z, height);
	assert(current_pitch == -ANGLE_EIGHTH_TURN);
	assert_bounds_fit(bounds);
	/* The camera circles the tile-weighted target at a constant altitude.
	 * Its horizontal radius equals its height for the fixed 45-degree view. */
	for (legacy_u16 eighth = 1; eighth <= 8U; eighth++) {
		struct SHAPE3D_HIRES_VECTOR turned =
			render_3d(eighth * PREVIEW_TEST_QUARTER_SPIN_TICKS / 2U);
		assert(((current_yaw - first_yaw) & ANGLE_MASK) ==
			   ((eighth * ANGLE_EIGHTH_TURN) & ANGLE_MASK));
		assert(current_pitch == -ANGLE_EIGHTH_TURN);
		assert_close(turned.y, first.y);
		assert_close(turned.x - x, height * sin_fast(current_yaw) / TRIG_FIXED_ONE);
		assert_close(z - turned.z, height * cos_fast(current_yaw) / TRIG_FIXED_ONE);
		struct SHAPE3D_HIRES_POINT target = project_world_point(x, y, z);
		assert(target.x > captured_center_x - PREVIEW_TEST_FIT_TOLERANCE);
		assert(target.x < captured_center_x + PREVIEW_TEST_FIT_TOLERANCE);
		assert(target.y > captured_center_y - PREVIEW_TEST_FIT_TOLERANCE);
		assert(target.y < captured_center_y + PREVIEW_TEST_FIT_TOLERANCE);
		assert_bounds_fit(bounds);
	}
	struct SHAPE3D_HIRES_VECTOR intermediate = render_3d(PREVIEW_TEST_QUARTER_SPIN_TICKS / 2U);
	struct SHAPE3D_HIRES_VECTOR repeated = render_3d(PREVIEW_TEST_QUARTER_SPIN_TICKS / 2U);
	assert_close(repeated.x, intermediate.x);
	assert_close(repeated.y, intermediate.y);
	assert_close(repeated.z, intermediate.z);
}

static void mark_lap_tile(legacy_u16 column, legacy_u16 row)
{
	const struct CSTRK_OBJECT *owner = preview_owners[row * preview_track.width + column];
	assert(owner != NULL && trkObjectList[owner->legacy_id].ss_trkObjInfoPtr != NULL);
	lap_tiles[row * preview_track.width + column] = 1;
}

static void assert_same_camera(const struct SHAPE3D_HIRES_VECTOR *first,
							   const struct SHAPE3D_HIRES_VECTOR *second)
{
	assert_close(first->x, second->x);
	assert_close(first->y, second->y);
	assert_close(first->z, second->z);
}

static void test_equal_lap_tile_weight(void)
{
	make_track(90, 70);
	add_object(4, 1, 1, 1, 1, 1);
	add_object(37, 8, 4, 1, 1, 1);
	add_object(60, 12, 8, 2, 2, 6);
	mark_lap_tile(1, 1);
	mark_lap_tile(8, 4);
	mark_lap_tile(13, 8);
	mark_lap_tile(12, 9);
	add_object(151, 80, 60, 1, 1, 1);
	add_object(151, 12, 8, 1, 1, 1);
	/* Four equally weighted lap tile centers: (1,1), (8,4), (13,8), (12,9).
	 * Free stunt quadrants, scenery and roads outside a finishable lap have no vote. */
	struct PREVIEW_TEST_BOUNDS bounds = {
		CSTRK_TILE_WORLD_SIZE,		14 * CSTRK_TILE_WORLD_SIZE, 0, 0,
		60 * CSTRK_TILE_WORLD_SIZE, 69 * CSTRK_TILE_WORLD_SIZE};
	assert_orbit_center(9 * CSTRK_TILE_WORLD_SIZE, 0, 64 * CSTRK_TILE_WORLD_SIZE, &bounds);
	struct SHAPE3D_HIRES_VECTOR before_decoration = render_3d(0);
	add_object(4, 88, 68, 1, 1, 1);	 /* Detached road. */
	add_object(37, 87, 60, 1, 1, 1); /* A connected branch that cannot finish. */
	struct SHAPE3D_HIRES_VECTOR after_decoration = render_3d(0);
	assert_same_camera(&before_decoration, &after_decoration);
	preview_elevations[1U * preview_track.width + 1U] = CSTRK_LEVEL_WORLD_HEIGHT;
	preview_elevations[4U * preview_track.width + 8U] = 2 * CSTRK_LEVEL_WORLD_HEIGHT;
	preview_elevations[9U * preview_track.width + 12U] = CSTRK_LEVEL_WORLD_HEIGHT;
	preview_elevations[60U * preview_track.width + 80U] = 4 * CSTRK_LEVEL_WORLD_HEIGHT;
	bounds.max_y = 2 * CSTRK_LEVEL_WORLD_HEIGHT;
	assert_orbit_center(9 * CSTRK_TILE_WORLD_SIZE, CSTRK_LEVEL_WORLD_HEIGHT,
						64 * CSTRK_TILE_WORLD_SIZE, &bounds);
	preview_hires = supersight_enabled = 0;
	reset_capture();
	draw_track_preview();
	assert(fill_count == 90U * 70U + 8U);
	assert(outline_count == 1 && clear_count == 1);
	assert(tile_count == 0 && flush_count == 0 && origin_count == 0);
	assert(background_count == 0);
	assert_orbit_center(9 * CSTRK_TILE_WORLD_SIZE, CSTRK_LEVEL_WORLD_HEIGHT,
						64 * CSTRK_TILE_WORLD_SIZE, &bounds);
}

static void test_no_completed_lap(void)
{
	make_track(90, 30);
	assert_orbit_center(45 * CSTRK_TILE_WORLD_SIZE, 0, 15 * CSTRK_TILE_WORLD_SIZE, NULL);
	struct SHAPE3D_HIRES_VECTOR empty = render_3d(0);
	add_object(151, 89, 29, 1, 1, 1);
	add_object(4, 88, 28, 1, 1, 1);
	assert_orbit_center(45 * CSTRK_TILE_WORLD_SIZE, 0, 15 * CSTRK_TILE_WORLD_SIZE, NULL);
	struct SHAPE3D_HIRES_VECTOR incomplete = render_3d(0);
	assert_same_camera(&empty, &incomplete);
}

static void test_far_edge_lap(void)
{
	make_track(CSTRK_MAX_DIMENSION, CSTRK_MAX_DIMENSION);
	add_object(4, 89, 89, 1, 1, 1);
	add_object(37, 89, 88, 1, 1, 1);
	mark_lap_tile(89, 89);
	mark_lap_tile(89, 88);
	struct PREVIEW_TEST_BOUNDS bounds = {
		89 * CSTRK_TILE_WORLD_SIZE, 90 * CSTRK_TILE_WORLD_SIZE, 0, 0, 0, 2 * CSTRK_TILE_WORLD_SIZE};
	assert_orbit_center(89.5 * CSTRK_TILE_WORLD_SIZE, 0, CSTRK_TILE_WORLD_SIZE, &bounds);
}

static void test_large_lap_bounds(void)
{
	make_track(CSTRK_MAX_DIMENSION, CSTRK_MAX_DIMENSION);
	add_object(4, 0, 0, 1, 1, 1);
	add_object(4, 89, 0, 1, 1, 1);
	add_object(4, 0, 89, 1, 1, 1);
	add_object(4, 89, 89, 1, 1, 1);
	mark_lap_tile(0, 0);
	mark_lap_tile(89, 0);
	mark_lap_tile(0, 89);
	mark_lap_tile(89, 89);
	struct PREVIEW_TEST_BOUNDS bounds = {0, 90 * CSTRK_TILE_WORLD_SIZE, 0, 0,
										 0, 90 * CSTRK_TILE_WORLD_SIZE};
	assert_orbit_center(45 * CSTRK_TILE_WORLD_SIZE, 0, 45 * CSTRK_TILE_WORLD_SIZE, &bounds);
	assert(captured_camera.y > PREVIEW_TEST_MINIMUM_ORBIT_HEIGHT);
}

static void test_unused_map_does_not_change_fit(void)
{
	make_track(90, 70);
	add_object(4, 4, 5, 1, 1, 1);
	mark_lap_tile(4, 5);
	struct SHAPE3D_HIRES_VECTOR wide_map = render_3d(0);
	make_track(30, 70);
	add_object(4, 4, 5, 1, 1, 1);
	mark_lap_tile(4, 5);
	struct SHAPE3D_HIRES_VECTOR narrow_map = render_3d(0);
	assert_same_camera(&wide_map, &narrow_map);
}

legacy_int main(void)
{
	test_equal_lap_tile_weight();
	test_no_completed_lap();
	test_far_edge_lap();
	test_large_lap_bounds();
	test_unused_map_does_not_change_fit();
	return 0;
}
