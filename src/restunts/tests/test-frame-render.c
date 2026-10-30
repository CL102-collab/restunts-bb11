#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define plane_signed_distance frame_test_plane_distance
#define subst_hillroad_track frame_test_subst_hillroad_track
#define transform_wheel_travel_to_world frame_test_transform_wheel_travel
#include "../c/frame.c"
#include "extended-replay-test-stubs.h"

#undef printf
#undef memcpy

struct TRACKOBJECT trkObjectList[215];
legacy_s16 camera_track_height_offset;
legacy_u8 supersight_enabled;

static legacy_u64 trace_hash = UINT64_C(1469598103934665603);
static legacy_s16 fixture_plane_distance;
static legacy_s16 transform_stop_at;
static legacy_s16 transform_count;
static legacy_s16 transform_capacity;
static legacy_s16 queue_resets;
static legacy_s16 check_retry_brake_paint;
static legacy_s16 rejected_shape;
static struct VECTOR flag_vertices[4];
static legacy_u8 terrain_map[900];
static legacy_u8 element_map[900];
static legacy_u8 sign_map[900];

enum {
	HUD_TEST_SCREEN_HEIGHT = 200,
	HUD_TEST_ROOF_HEIGHT = 10,
	HUD_TEST_DASHBOARD_TOP = 130,
	HUD_TEST_DASHBOARD_BOTTOM = 165,
	HUD_TEST_REPLAY_TOP = 151,
	HUD_TEST_TEXT_LEFT = 100,
	HUD_TEST_TEXT_RIGHT = 220,
	HUD_TEST_TEXT_HEIGHT = 8,
	HUD_TEST_TIMER_WIDTH = 40,
	HUD_TEST_FONT_SIZE = FRAME_FONT_HEIGHT_OFFSET + 2,
	HUD_TEST_SCENE_COLOR = 1,
	HUD_TEST_CARBON_COLOR = 4,
	HUD_TEST_TIMER_COLOR = 12,
	HUD_TEST_MESSAGE_COLOR = 15,
	HUD_TEST_RENDER_MODES = 128,
	HUD_TEST_ENHANCED_BIT = 1,
	HUD_TEST_REPLAY_BIT = 2,
	HUD_TEST_DIRTY_BIT = 4,
	HUD_TEST_PAGE_BIT = 8,
	HUD_TEST_HIDDEN_BIT = 16,
	HUD_TEST_MISSING_TOP_BIT = 32,
	HUD_TEST_SECOND_BUFFER_BIT = 64,
	HUD_TEST_SHAPE_OFFSET = SHAPE2D_HEADER_SIZE,
	HUD_TEST_SHAPE_SEGMENT = 1
};

static struct {
	legacy_u8 active;
	legacy_u16 masks, overlays, clipped_masks, timers, messages;
	legacy_u8 pixels[HUD_TEST_SCREEN_HEIGHT][FRAME_SCREEN_WIDTH];
	struct RECTANGLE clip;
	struct RECTANGLE timer_rect;
	struct RECTANGLE message_rect;
	struct SHAPE2D mask, overlay;
	legacy_u8 font[HUD_TEST_FONT_SIZE];
} hud_fixture;

static void hud_test_fill(const struct RECTANGLE *rect, legacy_u8 color)
{
	for (legacy_s16 y = rect->top; y < rect->bottom; y++) {
		for (legacy_s16 x = rect->left; x < rect->right; x++) {
			if (x >= hud_fixture.clip.left && x < hud_fixture.clip.right &&
				y >= hud_fixture.clip.top && y < hud_fixture.clip.bottom) {
				hud_fixture.pixels[y][x] = color;
			}
		}
	}
}

static void hud_test_reset(void)
{
	hud_fixture.masks = hud_fixture.overlays = hud_fixture.clipped_masks = 0;
	hud_fixture.timers = hud_fixture.messages = 0;
	memset(hud_fixture.pixels, HUD_TEST_SCENE_COLOR, sizeof(hud_fixture.pixels));
	hud_fixture.clip = (struct RECTANGLE){0, FRAME_SCREEN_WIDTH, 0, HUD_TEST_SCREEN_HEIGHT};
	hud_fixture.timer_rect = (struct RECTANGLE){
		FRAME_ELAPSED_TIME_X, FRAME_ELAPSED_TIME_X + HUD_TEST_TIMER_WIDTH,
		FRAME_ELAPSED_TIME_Y_OFFSET, FRAME_ELAPSED_TIME_Y_OFFSET + HUD_TEST_TEXT_HEIGHT};
	hud_fixture.message_rect =
		(struct RECTANGLE){HUD_TEST_TEXT_LEFT, HUD_TEST_TEXT_RIGHT, HUD_TEST_DASHBOARD_TOP,
						   HUD_TEST_DASHBOARD_TOP + HUD_TEST_TEXT_HEIGHT};
}

static void hud_test_assert_pixels(const struct RECTANGLE *rect, legacy_u8 color)
{
	for (legacy_s16 y = rect->top; y < rect->bottom; y++) {
		for (legacy_s16 x = rect->left; x < rect->right; x++) {
			assert(hud_fixture.pixels[y][x] == color);
		}
	}
}

static void hud_test_paint_top(legacy_u8 clipped, legacy_u8 overlay)
{
	assert(hud_fixture.active != 0);
	assert(hud_fixture.timers == 0 && hud_fixture.messages == 0);
	struct RECTANGLE saved_clip = hud_fixture.clip;
	if (clipped == 0) {
		hud_fixture.clip = (struct RECTANGLE){0, FRAME_SCREEN_WIDTH, 0, HUD_TEST_SCREEN_HEIGHT};
	} else {
		assert(hud_fixture.clip.bottom == height_above_replaybar);
		hud_fixture.clipped_masks++;
	}
	struct RECTANGLE roof = {0, FRAME_SCREEN_WIDTH, 0, HUD_TEST_ROOF_HEIGHT};
	struct RECTANGLE dashboard = {0, FRAME_SCREEN_WIDTH, HUD_TEST_DASHBOARD_TOP,
								  HUD_TEST_DASHBOARD_BOTTOM};
	legacy_u8 color = overlay != 0 ? HUD_TEST_CARBON_COLOR : 0;
	hud_test_fill(&roof, color);
	hud_test_fill(&dashboard, color);
	hud_fixture.clip = saved_clip;
	if (overlay != 0) {
		assert(hud_fixture.masks == 1);
		hud_fixture.overlays++;
	} else {
		assert(hud_fixture.overlays == 0);
		hud_fixture.masks++;
	}
}

void shape2d_render_bmp_as_mask(struct SHAPE2D far *shape)
{
	assert(shape == &hud_fixture.mask);
	hud_test_paint_top(0, 0);
}

void shape2d_rle_or_far_pointer(legacy_u16 offset, legacy_u16 segment)
{
	assert(offset == HUD_TEST_SHAPE_OFFSET && segment == HUD_TEST_SHAPE_SEGMENT);
	hud_test_paint_top(0, 1);
}

void shape2d_rle_mask_position_clipped(struct SHAPE2D far *shape)
{
	assert(shape == &hud_fixture.mask);
	hud_test_paint_top(1, 0);
}

void shape2d_rle_or_position_clipped(struct SHAPE2D far *shape)
{
	assert(shape == &hud_fixture.overlay);
	hud_test_paint_top(1, 1);
}

void far *dos_memory_make_pointer(legacy_u16 segment, legacy_u16 offset)
{
	assert(segment == HUD_TEST_SHAPE_SEGMENT && offset == HUD_TEST_SHAPE_OFFSET);
	return &hud_fixture.overlay;
}

void set_fontdefseg(void far *data)
{
	assert(hud_fixture.active != 0 && data == hud_fixture.font);
}

struct RECTANGLE *draw_ingame_text(void)
{
	if (hud_fixture.active != 0) {
		hud_test_fill(&hud_fixture.message_rect, HUD_TEST_MESSAGE_COLOR);
		hud_fixture.messages++;
		return &hud_fixture.message_rect;
	}
	return &empty_rect;
}

void format_frame_as_string(legacy_s8 *destination, legacy_s16 frames, legacy_s16 hundredths)
{
	assert(hud_fixture.active != 0 && destination == &resID_byte1);
	assert(frames == elapsed_time1 + elapsed_time2 && hundredths == 0);
}

struct RECTANGLE *intro_draw_text(legacy_s8 *text, legacy_s16 x, legacy_s16 y, legacy_s16 color,
								  legacy_s16 shadow_color)
{
	assert(hud_fixture.active != 0 && text == &resID_byte1);
	assert(x == hud_fixture.timer_rect.left && y == hud_fixture.timer_rect.top);
	assert(color == dialog_fnt_colour && shadow_color == 0);
	hud_test_fill(&hud_fixture.timer_rect, HUD_TEST_TIMER_COLOR);
	hud_fixture.timers++;
	return &hud_fixture.timer_rect;
}

static struct CARSTATE ghost_fixture;
static struct SIMD ghost_simd_fixture;
static struct GHOST_CAMERA_STATE ghost_camera_fixture;
static legacy_s16 cockpit_effect_frame;
static legacy_s16 cockpit_effect_kind;
static legacy_s16 ghost_fixture_active;
static legacy_s16 ghost_wheel_updates;
#if !defined(RESTUNTS_SDL3)
static legacy_s16 fixed_queue_capacity;
static legacy_s16 fixed_queue_cost;
#endif

#if defined(RESTUNTS_SDL3)
legacy_u16 frame_callback_count;
legacy_s8 *lookahead_tiles_tables[8];

legacy_int cstrk_terrain_profile(const struct CSTRK_TRACK *track, legacy_u16 x, legacy_u16 y,
								 struct CSTRK_TERRAIN_PROFILE *profile)
{
	(void)track;
	(void)x;
	(void)y;
	(void)profile;
	assert(0 && "Classic frame fixtures must not query extended terrain");
	return 0;
}

enum { ADAPTIVE_TEST_CAPTURE_COUNT = 16 };
static legacy_s16 adaptive_capture_enabled;
static legacy_s16 adaptive_captured_count;
static struct TRANSFORMEDSHAPE3D adaptive_captured[ADAPTIVE_TEST_CAPTURE_COUNT];
struct FRAME_SHADOW_FIXTURE {
	struct VECTOR position;
	legacy_s16 heading, half_width, half_length;
	const struct SHAPE3D *model;
};
static struct FRAME_SHADOW_FIXTURE shadow_cars[2];
static struct VECTOR shadow_camera;
static legacy_s16 shadow_count, shadow_begin_count, shadow_projection_count;
static legacy_s16 shadow_frame_active;
static legacy_s16 car_ground_offsets[2];
#endif

#define RACING_LINE_TEST_SEGMENTS 125U
#define RACING_LINE_TEST_BATCHES 8U
static struct GHOST_RACING_LINE_SEGMENT racing_line_segments[RACING_LINE_TEST_SEGMENTS];
static legacy_s16 racing_line_east[RACING_LINE_TEST_SEGMENTS];
static legacy_s16 racing_line_south[RACING_LINE_TEST_SEGMENTS];
static legacy_u32 racing_line_segment_count, racing_line_bucket_reads;
static legacy_u8 racing_line_capture_enabled;
static legacy_s16 racing_line_transform_result;
static legacy_u32 racing_line_batch_count;
static struct {
	struct TRANSFORMEDSHAPE3D instance;
	legacy_u16 count;
	legacy_s16 order;
	struct VECTOR vertices[248];
	legacy_u8 paints[FRAME_RACING_LINE_BATCH_SIZE];
} racing_line_batches[RACING_LINE_TEST_BATCHES];

const struct GHOST_RACING_LINE_SEGMENT *ghost_racing_line_first(legacy_s16 east, legacy_s16 south)
{
	racing_line_bucket_reads++;
	for (legacy_u32 index = 0; index < racing_line_segment_count; index++) {
		if (racing_line_east[index] == east && racing_line_south[index] == south) {
			return &racing_line_segments[index];
		}
	}
	return 0;
}

const struct GHOST_RACING_LINE_SEGMENT *
ghost_racing_line_next(const struct GHOST_RACING_LINE_SEGMENT *segment)
{
	return segment->next < racing_line_segment_count ? &racing_line_segments[segment->next] : 0;
}

legacy_u8 ghost_racing_line_color(legacy_u16 current_speed, legacy_u16 recorded_speed)
{
	legacy_s32 difference = (legacy_s32)current_speed - recorded_speed;
	return difference > 768	   ? GHOST_RACING_LINE_RED
		   : difference < -768 ? GHOST_RACING_LINE_GREEN
							   : GHOST_RACING_LINE_YELLOW;
}

#if defined(RESTUNTS_SDL3)
legacy_s16 shape3d_car_ground_offset(const struct SHAPE3D *shape)
{
	assert(shape == &game3dshapes[PLAYER_CAR_WHEEL_SHAPE] ||
		   shape == &game3dshapes[OPPONENT_CAR_WHEEL_SHAPE]);
	return car_ground_offsets[shape == &game3dshapes[OPPONENT_CAR_WHEEL_SHAPE]];
}

void shape3d_hires_ground_begin(const struct VECTOR *camera_position)
{
	assert(shadow_projection_count == 1);
	(void)camera_position;
}

void shape3d_hires_shadows_begin(const struct VECTOR *camera_position)
{
	assert(shadow_projection_count == 1);
	shadow_camera = *camera_position;
	shadow_count = 0;
	shadow_begin_count++;
}

void shape3d_hires_shadow_car(const struct VECTOR *position, legacy_s16 heading,
							  legacy_s16 half_width, legacy_s16 half_length)
{
	assert(shadow_begin_count == 1 && shadow_count < 2);
	shadow_cars[shadow_count++] =
		(struct FRAME_SHADOW_FIXTURE){*position, heading, half_width, half_length, NULL};
}

void shape3d_hires_shadow_model(const struct SHAPE3D *shape)
{
	assert(shadow_count > 0 && shadow_count <= 2);
	assert(shadow_cars[shadow_count - 1].model == NULL);
	shadow_cars[shadow_count - 1].model = shape;
}

legacy_u16 select_cliprect_rotate(legacy_s16 roll, legacy_s16 pitch, legacy_s16 yaw,
								  struct RECTANGLE *cliprect, legacy_s16 half_scale)
{
	(void)cliprect;
	(void)half_scale;
	mat_temp = *mat_rot_zxy(roll, pitch, yaw, MATRIX_ROTATION_ORDER_YXZ);
	shadow_projection_count++;
	return 0;
}

void polyinfo_set_supersight(legacy_u8 enabled)
{
	(void)enabled;
}

void shape3d_render_queued_primitives(void)
{
	assert(shadow_frame_active != 0);
}

legacy_s16 skybox_render(legacy_s16 view_index, struct RECTANGLE *clip, legacy_s16 direction,
						 struct MATRIX *rotation, legacy_s16 roll, legacy_s16 angle,
						 legacy_s16 camera_y)
{
	(void)view_index;
	(void)clip;
	(void)direction;
	(void)rotation;
	(void)roll;
	(void)angle;
	(void)camera_y;
	return 0;
}

void shape2d_draw_scaled_transparent_clipped(legacy_s16 scale, struct SHAPE2D far *shape,
											 legacy_s16 x, legacy_s16 y)
{
	(void)scale;
	(void)shape;
	(void)x;
	(void)y;
	assert(0);
}
#endif

struct CARSTATE *ghost_car_state(void)
{
	return ghost_fixture_active != 0 ? &ghost_fixture : 0;
}

legacy_s16 ghost_is_active(void)
{
	return ghost_fixture_active != 0 && gameconfig.game_opponenttype == 0;
}

const struct GHOST_CAMERA_STATE *ghost_camera_state(void)
{
	return ghost_fixture_active != 0 ? &ghost_camera_fixture : 0;
}

void fatal_error(const legacy_s8 *format, ...)
{
	(void)format;
	assert(0);
}

void sprite_set_target_clip_bounds(legacy_u16 left, legacy_u16 right, legacy_u16 top,
								   legacy_u16 bottom)
{
	assert(left == 0 && right == FRAME_SCREEN_WIDTH);
	if (hud_fixture.active != 0) {
		assert(top <= bottom && bottom <= HUD_TEST_SCREEN_HEIGHT);
		hud_fixture.clip = (struct RECTANGLE){left, right, top, bottom};
	} else {
		assert(top == 0 && bottom == HUD_TEST_SCREEN_HEIGHT);
	}
}

struct RECTANGLE *init_crak(legacy_s16 frame, legacy_s16 top, legacy_s16 height)
{
	static struct RECTANGLE rect;
	assert(top == 0 && height == 200);
	cockpit_effect_frame = frame;
	cockpit_effect_kind = CRASH_EVENT_COLLISION;
	return &rect;
}

struct RECTANGLE *do_sinking(legacy_s16 frame, legacy_s16 top, legacy_s16 height)
{
	struct RECTANGLE *rect = init_crak(frame, top, height);
	cockpit_effect_kind = CRASH_EVENT_WATER;
	return rect;
}

const struct SIMD *ghost_car_simd(void)
{
	return &ghost_simd_fixture;
}

legacy_u8 ghost_car_material(void)
{
	return 2;
}

void shape3d_update_car_wheel_vertices(struct SHAPE3D *shape, legacy_u16 first_vertex,
									   legacy_s16 steering_angle,
									   const legacy_s16 *suspension_offsets,
									   legacy_s16 *cached_wheel_state, struct VECTOR *base_vertices,
									   struct VECTOR *front_wheel_centers)
{
#if defined(RESTUNTS_SDL3)
	if (shadow_frame_active != 0) {
		assert(shape == &game3dshapes[PLAYER_CAR_WHEEL_SHAPE] ||
			   shape == &game3dshapes[OPPONENT_CAR_WHEEL_SHAPE]);
		return;
	}
#endif
	assert(shape == &game3dshapes[127]);
	assert(first_vertex == FRAME_STEERED_WHEEL_FIRST_VERTEX);
	assert(steering_angle == ghost_fixture.car_steeringAngle);
	assert(suspension_offsets == ghost_fixture.car_suspension_deflection);
	assert(cached_wheel_state == opponent_wheel_vertex_state);
	assert(base_vertices == opponent_base_wheel_vertices);
	assert(front_wheel_centers == opponent_front_wheel_centers);
	ghost_wheel_updates++;
}

static void trace_word(legacy_u16 value)
{
	trace_hash = (trace_hash ^ (value & 255U)) * UINT64_C(1099511628211);
	trace_hash = (trace_hash ^ (value >> 8)) * UINT64_C(1099511628211);
}

static void trace_vector(const struct VECTOR *vector)
{
	trace_word(vector->x);
	trace_word(vector->y);
	trace_word(vector->z);
}

void build_track_object(struct VECTOR *first, struct VECTOR *second)
{
	assert(first == second);
	trace_word(1);
	trace_vector(first);
}

legacy_s16 plane_signed_distance(legacy_s16 plane, legacy_s16 x, legacy_s16 y, legacy_s16 z)
{
	trace_word(2);
	trace_word(plane);
	trace_word(x);
	trace_word(y);
	trace_word(z);
	return fixture_plane_distance;
}

void transform_wheel_travel_to_world(void)
{
	trace_word(3);
	trace_vector(&wheel_forward_travel);
	trace_word(planindex_copy);
	trace_word(wheel_heading_offset);
	trace_word(car_initial_pitch);
	trace_word(car_initial_roll);
	trace_word(car_initial_yaw);
	wheel_world_travel.x = -5;
	wheel_world_travel.y = wheel_forward_travel.y;
	wheel_world_travel.z = 7;
}

static void trace_shape(const struct TRANSFORMEDSHAPE3D *shape)
{
	trace_word(shape->shapeptr == 0 ? 65535U : (legacy_u16)(shape->shapeptr - game3dshapes));
	trace_vector(&shape->pos);
	trace_vector(&shape->rotvec);
	trace_word(shape->culling_distance);
	trace_word(shape->ts_flags);
	trace_word(shape->material);
	trace_word(shape->rectptr == &frame_unsorted_shapes_rect);
	trace_word(shape->rectptr == &frame_sorted_shapes_rect);
}

legacy_u16 shape3d_transform_and_queue(struct TRANSFORMEDSHAPE3D *shape)
{
#if !defined(RESTUNTS_SDL3)
	if (fixed_queue_capacity != 0 && polygon_buffer_full != 0) {
		return 1;
	}
#endif
	if (racing_line_capture_enabled != 0 &&
		((uintptr_t)shape->shapeptr < (uintptr_t)game3dshapes ||
		 (uintptr_t)shape->shapeptr >= (uintptr_t)(game3dshapes + 130))) {
		assert(racing_line_batch_count < RACING_LINE_TEST_BATCHES);
		legacy_u32 batch = racing_line_batch_count++;
		racing_line_batches[batch].instance = *shape;
		racing_line_batches[batch].count = shape->shapeptr->shape3d_numprimitives;
		racing_line_batches[batch].order = ++transform_count;
		assert(shape->shapeptr->shape3d_numverts <= 248);
		assert(shape->shapeptr->shape3d_numverts == 8 + 4 * racing_line_batches[batch].count);
		assert(racing_line_batches[batch].count <= FRAME_RACING_LINE_BATCH_SIZE);
		for (legacy_u16 vertex = 0; vertex < shape->shapeptr->shape3d_numverts; vertex++) {
			const legacy_u8 *source = shape->shapeptr->shape3d_vertex_bytes + vertex * 6;
			racing_line_batches[batch].vertices[vertex] =
				(struct VECTOR){LEGACY_READ_S16_LE(source), LEGACY_READ_S16_LE(source + 2),
								LEGACY_READ_S16_LE(source + 4)};
		}
		for (legacy_u16 polygon = 0; polygon < racing_line_batches[batch].count; polygon++) {
			const legacy_u8 *primitive = shape->shapeptr->shape3d_primitives + polygon * 7;
			assert(primitive[0] == 4 && primitive[1] == 1);
			racing_line_batches[batch].paints[polygon] = primitive[2];
			for (legacy_u16 vertex = 0; vertex < 4; vertex++) {
				assert(primitive[3 + vertex] == 8 + polygon * 4 + vertex);
			}
		}
		assert(shape->shapeptr->shape3d_primitives[racing_line_batches[batch].count * 7] == 0);
		/* Check both bytes after full and partial batches: an invisible final
		 * polygon makes the classic renderer read these flags before the type. */
		assert(shape->shapeptr->shape3d_primitives[racing_line_batches[batch].count * 7 + 1] == 0);
#if !defined(RESTUNTS_SDL3)
		if (fixed_queue_capacity != 0) {
			fixed_queue_cost += racing_line_batches[batch].count;
			if (fixed_queue_cost >= fixed_queue_capacity) {
				polygon_buffer_full = 1;
				return 1;
			}
		}
#endif
		return (legacy_u16)racing_line_transform_result;
	}
	trace_word(4);
	trace_shape(shape);
	trace_word(backlights_paint_override);
#if defined(RESTUNTS_SDL3)
	if (adaptive_capture_enabled != 0) {
		assert(adaptive_captured_count < ADAPTIVE_TEST_CAPTURE_COUNT);
		adaptive_captured[adaptive_captured_count++] = *shape;
	}
#endif
	transform_count++;
#if !defined(RESTUNTS_SDL3)
	if (fixed_queue_capacity != 0 && ++fixed_queue_cost >= fixed_queue_capacity) {
		polygon_buffer_full = 1;
		return 1;
	}
#endif
	if (check_retry_brake_paint != 0 && transform_count == 1) {
		assert(backlights_paint_override == BACKLIGHT_PAINT_DEFAULT);
	}
	if (transform_count == transform_stop_at ||
		(transform_capacity != 0 && transform_count > transform_capacity)) {
		if (check_retry_brake_paint != 0) {
			backlights_paint_override = BACKLIGHT_PAINT_BRAKING;
		}
		return 1;
	}
	return transform_count == rejected_shape ? 65535U : 0;
}

void shape3d_vertex_read(const struct SHAPE3D *shape, legacy_u16 index, struct VECTOR *destination)
{
	assert(shape == &game3dshapes[111]);
	trace_word(5);
	trace_word(index);
	*destination = flag_vertices[index - FRAME_START_FLAG_FIRST_VERTEX];
}

void shape3d_vertex_write(struct SHAPE3D *shape, legacy_u16 index, const struct VECTOR *source)
{
	if (shape != &game3dshapes[111]) {
		assert(racing_line_capture_enabled != 0);
		legacy_u8 *destination = shape->shape3d_vertex_bytes + index * SHAPE3D_VERTEX_SIZE;
		LEGACY_WRITE_U16_LE(destination, (legacy_u16)source->x);
		LEGACY_WRITE_U16_LE(destination + 2, (legacy_u16)source->y);
		LEGACY_WRITE_U16_LE(destination + 4, (legacy_u16)source->z);
		return;
	}
	assert(shape == &game3dshapes[111]);
	trace_word(6);
	trace_word(index);
	trace_vector(source);
	flag_vertices[index - FRAME_START_FLAG_FIRST_VERTEX] = *source;
}

legacy_u8 subst_hillroad_track(legacy_u8 terrain, legacy_u8 element)
{
	trace_word(7);
	trace_word(terrain);
	trace_word(element);
	return element;
}

void polyinfo_reset(void)
{
	transform_count = 0;
#if !defined(RESTUNTS_SDL3)
	polygon_buffer_full = 0;
	fixed_queue_cost = 0;
#endif
	queue_resets++;
}

static void test_dashboard_top_layers(void)
{
	hud_fixture.active = 1;
	fontdefptr = fontledresptr = hud_fixture.font;
	video_x_alignment = 1;
	frame_state = &state;
	state.game_inputmode = GAME_INPUT_MODE_ACTIVE;
	roofbmpheight = 0;
	dashbmp_y_copy = HUD_TEST_DASHBOARD_BOTTOM;
	dasmshapeptr = &hud_fixture.mask;
	dastbmp_y2 = HUD_TEST_SHAPE_OFFSET;
	dastseg = HUD_TEST_SHAPE_SEGMENT;
	for (legacy_u16 mode = 0; mode < HUD_TEST_RENDER_MODES; mode++) {
		hud_test_reset();
		supersight_enabled = (mode & HUD_TEST_ENHANCED_BIT) != 0;
		replaybar_enabled = (mode & HUD_TEST_REPLAY_BIT) != 0;
		slow_video_mgmt_copy = (mode & HUD_TEST_DIRTY_BIT) != 0;
		video_uses_page_flipping = (mode & HUD_TEST_PAGE_BIT) != 0;
		dashboard_visible = (mode & HUD_TEST_HIDDEN_BIT) == 0;
		dastbmp_y = (mode & HUD_TEST_MISSING_TOP_BIT) != 0 ? 0 : HUD_TEST_DASHBOARD_TOP;
		height_above_replaybar =
			replaybar_enabled != 0 ? HUD_TEST_REPLAY_TOP : HUD_TEST_SCREEN_HEIGHT;
		game_replay_mode = replaybar_enabled != 0 ? REPLAY_MODE_PLAYBACK : REPLAY_MODE_LIVE;
		legacy_s8 buffer = (mode & HUD_TEST_SECOND_BUFFER_BIT) != 0;
		struct RECTANGLE clip = {0, FRAME_SCREEN_WIDTH, 0, height_above_replaybar};
		frame_begin(buffer);
		/* An existing scene bound must survive the dashboard union and final copy. */
		struct RECTANGLE scene = {HUD_TEST_TEXT_LEFT, HUD_TEST_TEXT_RIGHT, HUD_TEST_ROOF_HEIGHT,
								  HUD_TEST_DASHBOARD_TOP};
		frame_layer_rects[0] = scene;
		hud_fixture.clip = clip;
		frame_draw_dashboard_top(&clip);
		assert(memcmp(&hud_fixture.clip, &clip, sizeof(clip)) == 0);
		frame_draw_elapsed_time();
		frame_finish(buffer, &clip, 0, 0);
		legacy_u16 draws = dashboard_visible != 0 && dastbmp_y != 0;
		assert(hud_fixture.masks == draws && hud_fixture.overlays == draws);
		assert(hud_fixture.clipped_masks ==
			   (supersight_enabled != 0 && replaybar_enabled != 0 ? draws * 2 : 0));
		assert(hud_fixture.timers == (replaybar_enabled == 0));
		assert(hud_fixture.messages == 1);
		hud_test_assert_pixels(&hud_fixture.message_rect, HUD_TEST_MESSAGE_COLOR);
		if (replaybar_enabled == 0) {
			hud_test_assert_pixels(&hud_fixture.timer_rect, HUD_TEST_TIMER_COLOR);
		}
		if (draws != 0 && supersight_enabled != 0 && replaybar_enabled != 0) {
			struct RECTANGLE toolbar = {0, FRAME_SCREEN_WIDTH, HUD_TEST_REPLAY_TOP,
										HUD_TEST_SCREEN_HEIGHT};
			hud_test_assert_pixels(&toolbar, HUD_TEST_SCENE_COLOR);
		}
		if (slow_video_mgmt_copy != 0) {
			assert(active_frame_rects == (video_uses_page_flipping != 0 && buffer != 0
											  ? frame_rects_page1
											  : frame_rects_page0));
			assert(active_frame_rects[0].left == (draws != 0 ? 0 : scene.left));
			assert(active_frame_rects[0].right == (draws != 0 ? FRAME_SCREEN_WIDTH : scene.right));
			assert(active_frame_rects[0].top == scene.top);
			assert(active_frame_rects[0].bottom ==
				   (draws != 0 ? HUD_TEST_DASHBOARD_BOTTOM : hud_fixture.message_rect.bottom));
			assert(memcmp(active_frame_rects, frame_layer_rects,
						  sizeof(*active_frame_rects) * FRAME_DIRTY_RECT_COUNT) == 0);
		}
	}
	hud_fixture.active = 0;
	fontdefptr = fontledresptr = NULL;
	dashboard_visible = dastbmp_y = replaybar_enabled = slow_video_mgmt_copy = 0;
	video_uses_page_flipping = supersight_enabled = 0;
}

static void test_camera_modes(void)
{
	gameconfig.game_opponenttype = 1;
	memset(&state, 0, sizeof(state));
	state.playerstate.car_position.lx = -80000;
	state.playerstate.car_position.ly = 1280;
	state.playerstate.car_position.lz = 96000;
	state.opponentstate.car_position.lx = 120000;
	state.opponentstate.car_position.ly = -640;
	state.opponentstate.car_position.lz = -40000;
	static struct VECTOR track_cameras[] = {{200, -20, 700}, {-400, 40, -300}};
	state.game_follow_camera_position[0] = track_cameras[1];
	state.game_follow_camera_position[1] = track_cameras[0];
	state.game_trackside_camera_index[0] = 0;
	state.game_trackside_camera_index[1] = 1;
	trackside_camera_positions = track_cameras;
	simd_player.car_height = 60;
	simd_opponent.car_height = 90;
	camera_track_height_offset = 25;
	planindex = 2;
	struct FRAME_CAMERA camera;
	for (legacy_u32 scenario = 0; scenario < 64U; scenario++) {
		memset(&camera, 0, sizeof(camera));
		followOpponentFlag = (scenario / 4U) % 2U;
		cameramode = scenario % 4U;
		state.playerstate.car_rotate.x = (legacy_s16)(scenario * 73U);
		state.playerstate.car_rotate.y = (legacy_s16)(scenario * 7U);
		state.playerstate.car_rotate.z = (legacy_s16)(scenario % 4U);
		state.opponentstate.car_rotate.x = (legacy_s16)(-300 + scenario * 37U);
		state.opponentstate.car_rotate.y = -20;
		state.opponentstate.car_rotate.z = (legacy_s16)(1023 - scenario % 4U);
		custom_camera.distance = 900 + scenario;
		custom_camera.elevation_angle = 50 + scenario;
		custom_camera.azimuth_angle = -20 + scenario;
		terrainHeight = scenario % 2U == 0 ? -100 : 600;
		track_wall_collision_enabled = (scenario / 8U) % 2U;
		fixture_plane_distance = (scenario / 16U) * 10;
		frame_setup_camera(&camera);
		trace_vector(&camera.position);
		trace_word(camera.pitch);
		trace_word(camera.yaw);
		trace_word(camera.roll);
	}
}

static void test_covered_tiles(void)
{
	struct FRAME_TILE tile;
	memset(&tile, 0, sizeof(tile));
	static const legacy_s8 offsets[] = {-128, -1, 0, 126, 127};
	struct FRAME_LOOKAHEAD_TILE lookahead[24];
	struct FRAME_TILE_SELECTION tiles;
	for (legacy_u32 flag = 0; flag < 5U; flag++) {
		trkObjectList[1].ss_multiTileFlag = flag;
		for (legacy_u32 east = 0; east < 5U; east++) {
			for (legacy_u32 south = 0; south < 5U; south++) {
				memset(&tiles, 0, sizeof(tiles));
				tiles.lookahead = lookahead;
				tile.element = 1;
				tile.east = offsets[east];
				tile.south = offsets[south];
				for (legacy_u32 index = 0; index < 24U; index++) {
					lookahead[index].east = LEGACY_S8_WRAP_ADD(offsets[east], (index % 3U) - 1U);
					lookahead[index].south =
						LEGACY_S8_WRAP_ADD(offsets[south], (index / 3U) % 3U - 1U);
					tiles.markers[index] = index % 3U;
				}
				frame_mark_covered_tiles(&tiles, &tile, 22);
				for (legacy_u32 index = 0; index < 24U; index++) {
					trace_word(tiles.markers[index]);
				}
			}
		}
	}
}

static void reset_shapes(void)
{
	memset(currenttransshape, 0, sizeof(currenttransshape));
	curtransshape_ptr = currenttransshape;
	transformedshape_counter = 0;
	transform_count = 0;
	transform_stop_at = 0;
	rejected_shape = 0;
	backlights_paint_override = BACKLIGHT_PAINT_DEFAULT;
	memset(&mat_temp, 0, sizeof(mat_temp));
	mat_temp.m._11 = mat_temp.m._22 = mat_temp.m._33 = 16384;
}

static void configure_track(void)
{
	memset(terrain_map, 0, sizeof(terrain_map));
	memset(element_map, 0, sizeof(element_map));
	memset(sign_map, 255, sizeof(sign_map));
	track_terrain_map = terrain_map;
	track_element_map = element_map;
	roadside_sign_indices_by_tile = sign_map;
	for (legacy_u32 index = 0; index < 30U; index++) {
		terrainrows[index] = trackrows[index] = index * 30U;
		track_column_centers[index] = index * 1024U + 512U;
		track_row_centers[index] = 30000 - index * 1024U;
		track_column_positions[index] = index * 1024U;
		track_row_positions[index] = 30512 - index * 1024U;
	}
}

static void test_tile_selection(void)
{
	struct FRAME_LOOKAHEAD_TILE lookahead[24];
	struct FRAME_CAMERA camera;
	struct FRAME_TILE_SELECTION tiles;
	for (legacy_u32 scenario = 0; scenario < 30U; scenario++) {
		memset(&camera, 0, sizeof(camera));
		memset(&tiles, 0x35, sizeof(tiles));
		configure_track();
		detail_level = scenario % 5U;
		camera.position.x = scenario % 2U == 0 ? 10240 : -512;
		camera.position.z = 19456;
		state.playerstate.car_position.lx = 10L * 65536L;
		state.playerstate.car_position.lz = 19L * 65536L;
		tiles.lookahead = lookahead;
		for (legacy_u32 index = 0; index < 24U; index++) {
			lookahead[index].east = (legacy_s8)(index % 5U - 2U);
			lookahead[index].south = (legacy_s8)(index / 5U - 2U);
			lookahead[index].detail = index % 3U;
		}
		trkObjectList[1].ss_multiTileFlag = scenario % 4U;
		trkObjectList[1].ss_physicalModel = scenario % 2U == 0 ? 64 : 63;
		element_map[10 + 10 * 30] = 1;
		element_map[11 + 10 * 30] = TRACK_TILE_CONTINUATION_EAST;
		element_map[10 + 11 * 30] = TRACK_TILE_CONTINUATION_SOUTH;
		element_map[11 + 11 * 30] = TRACK_TILE_CONTINUATION_SOUTHEAST;
		terrain_map[10 + 10 * 30] = scenario % 3U == 0 ? 7 : 0;
		frame_select_tiles(&tiles, &camera);
		for (legacy_u32 index = 0; index < 24U; index++) {
			trace_word(tiles.markers[index]);
			trace_word(tiles.east[index]);
			trace_word(tiles.south[index]);
			trace_word(tiles.elements[index]);
			trace_word(tiles.terrain[index]);
			trace_word(tiles.detail[index]);
		}
	}
}

static void test_terrain_exhaustion(void)
{
	struct FRAME_CAMERA camera;
	memset(&camera, 0, sizeof(camera));
	camera.position.x = -100;
	camera.position.y = 250;
	camera.position.z = 400;
	struct FRAME_TILE tile;
	for (legacy_u32 scenario = 0; scenario < 24U; scenario++) {
		configure_track();
		reset_shapes();
		memset(&tile, 0, sizeof(tile));
		tile.east = 4;
		tile.south = 5;
		tile.element = scenario % 3U == 0 ? 105 : 0;
		tile.terrain = scenario % 3U == 1 ? TERRAIN_RAISED_TILE : 1;
		terrain_map[4 + 5 * 30] = 1;
		terrain_map[5 + 5 * 30] = 2;
		terrain_map[4 + 6 * 30] = 0;
		terrain_map[5 + 6 * 30] = 3;
		transform_stop_at = scenario / 3U;
		trace_word(frame_draw_terrain(&tile, &camera, 8));
		trace_word(tile.terrain);
		trace_word(tile.height);
		trace_word(tile.last_east);
		trace_word(tile.last_south);
	}
}

static void test_sorted_shapes(void)
{
	struct FRAME_CAR_RENDER cars[2];
	for (legacy_u32 scenario = 0; scenario < 36U; scenario++) {
		reset_shapes();
		memset(cars, 0, sizeof(cars));
		state.playerstate.car_is_braking = scenario % 2U;
		state.opponentstate.car_is_braking = (scenario / 2U) % 2U;
		state.playerstate.car_crashBmpFlag = CRASH_EVENT_COLLISION;
		state.opponentstate.car_crashBmpFlag = scenario % 2U == 0 ? CRASH_EVENT_COLLISION : 0;
		transformedshape_counter = scenario % 5U;
		for (legacy_u32 index = 0; index < 5U; index++) {
			currenttransshape[index].shapeptr = &game3dshapes[index];
			transformed_shape_sort_types[index] = index % 4U;
			transformedshape_indices[index] = index;
			transformedshape_zarray[index] = (legacy_s16)((index * 7U + scenario) % 4U);
		}
		transform_stop_at = (scenario / 5U) % 4U;
		rejected_shape = scenario % 4U;
		frame_draw_sorted_shapes(cars, 0, 0, 0);
		trace_word(cars[0].explosion_visible);
		trace_word(cars[1].explosion_visible);
	}
}

static void test_track_elements_and_flags(void)
{
	struct FRAME_CAMERA camera;
	memset(&camera, 0, sizeof(camera));
	camera.position.x = 100;
	camera.position.y = 200;
	camera.position.z = -100;
	struct FRAME_TILE tile;
	struct FRAME_CAR_RENDER cars[2];
	legacy_s8 overlay;
	for (legacy_u32 scenario = 0; scenario < 32U; scenario++) {
		configure_track();
		reset_shapes();
		memset(&tile, 0, sizeof(tile));
		memset(cars, 0, sizeof(cars));
		tile.east = 4;
		tile.south = 5;
		tile.element = scenario % 8U == 0 ? 0 : 1;
		tile.detail = scenario % 2U;
		tile.height = scenario % 3U == 0 ? 450 : 0;
		trkObjectList[1].ss_shapePtr = &game3dshapes[1];
		trkObjectList[1].ss_loShapePtr = &game3dshapes[2];
		trkObjectList[1].ss_multiTileFlag = scenario % 4U;
		trkObjectList[1].ss_ignoreZBias = (scenario / 4U) % 2U;
		trkObjectList[1].ss_surfaceType = scenario % 2U == 0 ? -1 : 2;
		trkObjectList[1].ss_ssOvelay = 0;
		trkObjectList[1].ss_rotY = scenario * 31U;
		start_finish_column = 4;
		start_finish_row = scenario % 2U == 0 ? 5 : 6;
		overlay = scenario % 2U;
		cars[0].depth_adjustment = cars[1].depth_adjustment = 2048;
		transform_stop_at = (scenario / 8U) % 3U;
		trace_word(frame_add_track_element(&tile, &camera, cars, 8, 3, &overlay));
		trace_vector(&tile.position);
		trace_word(tile.last_east);
		trace_word(tile.last_south);
		trace_word(tile.depth_mask);
		trace_word(overlay);
		trace_word(cars[0].depth_adjustment);
		trace_word(cars[1].depth_adjustment);
		state.game_inputmode = GAME_INPUT_MODE_WAITING;
		start_flag_animation = scenario * 33U;
		track_angle = scenario * 17U;
		hillFlag = scenario % 2U;
		for (legacy_u32 index = 0; index < 4U; index++) {
			flag_vertices[index].x = index;
			flag_vertices[index].y = index * 30U;
			flag_vertices[index].z = -10;
		}
		frame_add_start_flag(&tile, &camera, 8);
		trace_word(transformedshape_counter);
		for (legacy_u32 index = 0; index < (legacy_u32)transformedshape_counter; index++) {
			trace_shape(&currenttransshape[index]);
			trace_word(transformedshape_zarray[index]);
		}
	}
}

static void test_invalid_multitile_flags(void)
{
	struct FRAME_TILE tile = {0};
	struct FRAME_TILE_SELECTION tiles = {0};
	struct FRAME_CAMERA camera = {0};
	configure_track();
	tile.element = 1;
	tile.height = 450;
	/* Unknown footprint codes must never select an indeterminate offset table. */
	for (legacy_s16 flag = -128; flag <= 127; flag++) {
		if (flag >= FRAME_MULTITILE_NONE && flag <= FRAME_MULTITILE_BOTH) {
			continue;
		}
		reset_shapes();
		trkObjectList[1].ss_multiTileFlag = (legacy_s8)flag;
		assert(frame_draw_fences(&tile, &tiles, &camera, 0) == 0);
		assert(frame_draw_hill_fill(&tile, &trkObjectList[1], 0) == 0);
		assert(transform_count == 0);
	}
}

static void test_ghost_uses_independent_visual_state(void)
{
	struct FRAME_LOOKAHEAD_TILE lookahead[FRAME_LOOKAHEAD_TILE_COUNT] = {{0}};
	struct FRAME_TILE_SELECTION tiles = {0};
	struct FRAME_CAR_RENDER cars[2] = {{0}};
	struct FRAME_CAMERA camera = {0};
	struct FRAME_TILE tile = {0};
	struct GAMESTATE original_state = state;
	gameconfig.game_opponenttype = 0;
	ghost_fixture_active = 1;
	ghost_fixture.car_position.lx = 10L * 65536L;
	ghost_fixture.car_position.lz = 20L * 65536L;
	ghost_fixture.car_steeringAngle = 29;
	ghost_fixture.car_is_braking = 1;
	ghost_fixture.car_crashBmpFlag = CRASH_EVENT_COLLISION;
	cameramode = CAMERA_MODE_COCKPIT;
	followOpponentFlag = 0;
	detail_level = 0;
	slow_video_mgmt_copy = 0;
	lookahead[0].east = 10;
	lookahead[0].south = 9;
	tiles.lookahead = lookahead;
	tiles.count = FRAME_LOOKAHEAD_TILE_COUNT;
	for (legacy_u32 index = 1; index < FRAME_LOOKAHEAD_TILE_COUNT; index++) {
		tiles.markers[index] = FRAME_TILE_UNAVAILABLE_MARKER;
	}
	reset_shapes();
	frame_place_cars(&tiles, cars);
	assert(cars[PLAYER_CAR_INDEX].east == -1);
	assert(cars[OPPONENT_CAR_INDEX].east == 10);
	assert(cars[OPPONENT_CAR_INDEX].south == 9);
	tile.east = tile.last_east = 10;
	tile.south = tile.last_south = 9;
	trkObjectList[FRAME_OPPONENT_SORT_ID].ss_shapePtr = &game3dshapes[OPPONENT_CAR_HIGH_SHAPE];
	trkObjectList[FRAME_OPPONENT_SORT_ID].ss_loShapePtr = &game3dshapes[OPPONENT_CAR_LOW_SHAPE];
	frame_add_tile_cars(&tile, &camera, cars, 0);
	assert(transformedshape_counter == 1);
	assert(ghost_wheel_updates == 1);
	assert(currenttransshape[0].shapeptr == &game3dshapes[OPPONENT_CAR_HIGH_SHAPE]);
	assert((currenttransshape[0].ts_flags & SHAPE3D_GHOST_FLAG) != 0U);
	assert(currenttransshape[0].pos.x == position_to_word(ghost_fixture.car_position.lx));
	frame_draw_sorted_shapes(cars, 0, 0, 0);
	assert(cars[OPPONENT_CAR_INDEX].explosion_visible == 0);
	assert(memcmp(&state, &original_state, sizeof(state)) == 0);
	assert(gameconfig.game_opponenttype == 0);
	ghost_fixture_active = 0;
	frame_place_cars(&tiles, cars);
	assert(cars[OPPONENT_CAR_INDEX].east == -1);
}

static void test_ghost_camera_modes(void)
{
	struct GAMESTATE live_state = {0};
	struct VECTOR track_cameras[] = {{500, 10, -800}, {2500, 60, 3400}};
	trackside_camera_positions = track_cameras;
	live_state.playerstate.car_position.lx = -100000;
	live_state.playerstate.car_position.lz = -60000;
	live_state.opponentstate.car_position.lx = 900000;
	live_state.opponentstate.car_position.lz = 800000;
	live_state.game_follow_camera_position[0] = track_cameras[0];
	live_state.game_follow_camera_position[1] = track_cameras[0];
	live_state.game_frame = 2000;
	ghost_fixture_active = 1;
	ghost_fixture.car_position.lx = 320000;
	ghost_fixture.car_position.ly = 6400;
	ghost_fixture.car_position.lz = 480000;
	ghost_fixture.car_rotate.x = 190;
	ghost_fixture.car_rotate.y = 17;
	ghost_fixture.car_rotate.z = 9;
	ghost_camera_fixture.follow_position = track_cameras[1];
	ghost_camera_fixture.trackside_index = 1;
	ghost_camera_fixture.frame = 120;
	ghost_camera_fixture.crash_frame = 100;
	ghost_simd_fixture.car_height = 82;
	custom_camera.distance = 1100;
	custom_camera.azimuth_angle = 30;
	custom_camera.elevation_angle = 70;
	terrainHeight = -1000;
	track_wall_collision_enabled = 0;
	camera_track_height_offset = 20;
	for (cameramode = CAMERA_MODE_COCKPIT; cameramode < CAMERA_MODE_COUNT; cameramode++) {
		struct FRAME_CAMERA expected = {0};
		struct FRAME_CAMERA actual = {0};
		/* The ghost view must match an opponent at the recorded pose and camera,
		 * regardless of the live player's or unused opponent's positions. */
		state = live_state;
		state.opponentstate = ghost_fixture;
		state.game_follow_camera_position[1] = ghost_camera_fixture.follow_position;
		state.game_trackside_camera_index[1] = ghost_camera_fixture.trackside_index;
		gameconfig.game_opponenttype = 1;
		followOpponentFlag = 1;
		simd_player.car_height = ghost_simd_fixture.car_height;
		frame_setup_camera(&expected);
		state = live_state;
		gameconfig.game_opponenttype = 0;
		simd_player.car_height = 40;
		frame_setup_camera(&actual);
		assert(memcmp(&actual, &expected, sizeof(actual)) == 0);
		assert(memcmp(&state, &live_state, sizeof(state)) == 0);
		/* T back to the player uses the live camera, and a missing ghost
		 * also falls back to that camera instead of the unused AI state. */
		followOpponentFlag = 0;
		frame_setup_camera(&expected);
		ghost_fixture_active = 0;
		followOpponentFlag = 1;
		frame_setup_camera(&actual);
		assert(memcmp(&actual, &expected, sizeof(actual)) == 0);
		ghost_fixture_active = 1;
	}
	cameramode = CAMERA_MODE_COCKPIT;
	slow_video_mgmt_copy = 0;
	struct RECTANGLE cliprect = {0, 320, 0, 200};
	ghost_fixture.car_crashBmpFlag = CRASH_EVENT_COLLISION;
	frame_draw_cockpit_effects(&cliprect);
	assert(cockpit_effect_kind == CRASH_EVENT_COLLISION && cockpit_effect_frame == 20);
	ghost_fixture.car_crashBmpFlag = CRASH_EVENT_WATER;
	frame_draw_cockpit_effects(&cliprect);
	assert(cockpit_effect_kind == CRASH_EVENT_WATER && cockpit_effect_frame == 20);
	assert(memcmp(&state, &live_state, sizeof(state)) == 0);
	ghost_fixture_active = 0;
	followOpponentFlag = 0;
}

/* Snapshot collision or water flags must not start irreversible effects.
 * Confirmed events must still be visible on every intervening presentation. */
static void test_snapshot_uses_authoritative_events(void)
{
	struct FRAME_LOOKAHEAD_TILE lookahead = {11, 9, 0};
	struct FRAME_TILE_SELECTION tiles = {0};
	struct FRAME_CAMERA camera = {0};
	struct FRAME_TILE tile = {0};
	struct RECTANGLE cliprect = {0, 320, 0, 200};
	tiles.lookahead = &lookahead;
	tiles.count = 1;
	tile.east = tile.last_east = 11;
	tile.south = tile.last_south = 9;
	tile.detail = 1;
	memset(&simd_player, 0, sizeof(simd_player));
	memset(&simd_opponent, 0, sizeof(simd_opponent));
	memset(&ghost_simd_fixture, 0, sizeof(ghost_simd_fixture));
	trkObjectList[FRAME_PLAYER_SORT_ID].ss_loShapePtr = &game3dshapes[PLAYER_CAR_LOW_SHAPE];
	trkObjectList[FRAME_OPPONENT_SORT_ID].ss_loShapePtr = &game3dshapes[OPPONENT_CAR_LOW_SHAPE];
	slow_video_mgmt_copy = 0;
	for (legacy_s16 viewed = 0; viewed < 3; viewed++) {
		for (legacy_s16 real_event = CRASH_EVENT_NONE; real_event <= CRASH_EVENT_WATER;
			 real_event++) {
			for (legacy_s16 snapshot_event = CRASH_EVENT_NONE; snapshot_event <= CRASH_EVENT_WATER;
				 snapshot_event++) {
				memset(&state, 0, sizeof(state));
				memset(&ghost_fixture, 0, sizeof(ghost_fixture));
				state.game_frame = 2000;
				state.game_pEndFrame = 1980;
				state.game_oEndFrame = 1960;
				ghost_camera_fixture.frame = 120;
				ghost_camera_fixture.crash_frame = 100;
				gameconfig.game_opponenttype = viewed == 1;
				ghost_fixture_active = viewed == 2;
				followOpponentFlag = viewed != 0;
				legacy_s16 car_index = viewed == 0 ? PLAYER_CAR_INDEX : OPPONENT_CAR_INDEX;
				struct CARSTATE *real_car = viewed == 0	  ? &state.playerstate
											: viewed == 1 ? &state.opponentstate
														  : &ghost_fixture;
				real_car->car_crashBmpFlag = real_event;
				real_car->car_position.lx = 10L * 65536L;
				real_car->car_position.lz = 20L * 65536L;
				struct GAMESTATE real_state = state;
				struct CARSTATE real_ghost = ghost_fixture;
				struct GHOST_CAMERA_STATE real_ghost_camera = ghost_camera_fixture;
				struct GAMESTATE snapshot_state = state;
				struct CARSTATE snapshot_ghost = ghost_fixture;
				struct GHOST_CAMERA_STATE snapshot_ghost_camera = ghost_camera_fixture;
				snapshot_state.game_pEndFrame = snapshot_state.game_oEndFrame = 1999;
				snapshot_ghost_camera.frame = 999;
				snapshot_ghost_camera.crash_frame = 119;
				struct CARSTATE *snapshot_car = viewed == 0	  ? &snapshot_state.playerstate
												: viewed == 1 ? &snapshot_state.opponentstate
															  : &snapshot_ghost;
				snapshot_car->car_crashBmpFlag = snapshot_event;
				snapshot_car->car_position.lx = 11L * 65536L;
				struct GAMESTATE presentation = snapshot_state;
				struct CARSTATE presentation_ghost = snapshot_ghost;
				struct GHOST_CAMERA_STATE presentation_ghost_camera = snapshot_ghost_camera;
				frame_preserve_authoritative_events(&presentation, &presentation_ghost,
													&presentation_ghost_camera);
				frame_state = &presentation;
				frame_ghost = ghost_fixture_active != 0 ? &presentation_ghost : 0;
				frame_ghost_camera = ghost_fixture_active != 0 ? &presentation_ghost_camera : 0;
				frame_uses_snapshot = 1;

				cameramode = CAMERA_MODE_COCKPIT;
				cockpit_effect_kind = cockpit_effect_frame = -1;
				frame_draw_cockpit_effects(&cliprect);
				if (real_event == CRASH_EVENT_NONE) {
					assert(cockpit_effect_kind == -1 && cockpit_effect_frame == -1);
				} else {
					assert(cockpit_effect_kind == real_event);
					assert(cockpit_effect_frame == (viewed == 1 ? 40 : 20));
				}

				cameramode = CAMERA_MODE_FOLLOW;
				struct FRAME_CAR_RENDER cars[2] = {{0}};
				reset_shapes();
				frame_place_cars(&tiles, cars);
				if (real_event == CRASH_EVENT_WATER) {
					assert(cars[car_index].east == -1);
				} else {
					assert(cars[car_index].east == 11 && cars[car_index].south == 9);
					cars[1 - car_index].east = -1;
					frame_add_tile_cars(&tile, &camera, cars, 0);
					assert(transformedshape_counter == 1);
					assert(currenttransshape[0].pos.x ==
						   position_to_word(snapshot_car->car_position.lx));
					legacy_s8 expected_flags = real_event == CRASH_EVENT_COLLISION
												   ? FRAME_TRANSFORM_FLAGS_CLIPPED
												   : FRAME_TRANSFORM_FLAGS_DEFAULT;
					if (viewed == 2) {
						expected_flags |= SHAPE3D_GHOST_FLAG;
					}
					assert(currenttransshape[0].ts_flags == expected_flags);
					frame_draw_sorted_shapes(cars, 0, 0, 0);
					assert(cars[car_index].explosion_visible ==
						   (real_event == CRASH_EVENT_COLLISION && viewed != 2));
				}
				assert(memcmp(&state, &real_state, sizeof(state)) == 0);
				assert(memcmp(&ghost_fixture, &real_ghost, sizeof(ghost_fixture)) == 0);
				assert(memcmp(&ghost_camera_fixture, &real_ghost_camera,
							  sizeof(ghost_camera_fixture)) == 0);
				assert(snapshot_car->car_crashBmpFlag == snapshot_event);
				assert(snapshot_state.game_pEndFrame == 1999 &&
					   snapshot_state.game_oEndFrame == 1999);
				assert(snapshot_ghost_camera.frame == 999 &&
					   snapshot_ghost_camera.crash_frame == 119);
				frame_uses_snapshot = 0;
				frame_state = &state;
				frame_ghost = 0;
				frame_ghost_camera = 0;
			}
		}
	}
	ghost_fixture_active = 0;
	followOpponentFlag = 0;
}

#if !defined(RESTUNTS_SDL3)
static void test_supersight_selection(void)
{
	/* The eight original headings select four cardinal rotations and their
	 * mirrored painter order. Every one must cover the same 110 unique cells. */
	static const struct FRAME_LOOKAHEAD_TILE headings[] = {{2, -4, 2}, {-2, -4, 2}, {4, -2, 2},
														   {4, 2, 2},  {-2, 4, 2},	{2, 4, 2},
														   {-4, 2, 2}, {-4, -2, 2}};
	struct FRAME_CAMERA camera = {0};
	camera.position.x = 15 * 1024;
	camera.position.z = 14 * 1024;
	supersight_enabled = 1;
	for (legacy_u32 heading = 0; heading < 8; heading++) {
		for (detail_level = 0; detail_level < 5; detail_level++) {
			struct FRAME_TILE_SELECTION tiles = {0};
			configure_track();
			tiles.lookahead = &headings[heading];
			frame_select_tiles(&tiles, &camera);
			assert(tiles.count == FRAME_SUPERSIGHT_TILE_COUNT);
			for (legacy_u32 i = 0; i < FRAME_SUPERSIGHT_TILE_COUNT; i++) {
				assert(tiles.markers[i] == FRAME_TILE_DRAW_MARKER);
				assert(tiles.lookahead[i].detail == supersight_tiles[i].priority);
				for (legacy_u32 j = 0; j < i; j++) {
					assert(tiles.east[i] != tiles.east[j] || tiles.south[i] != tiles.south[j]);
				}
			}
			assert(tiles.east[109] == 15 && tiles.south[109] == 15);
		}
	}
	/* Expanded view still respects the scenery option and map boundaries. */
	for (legacy_u32 corner = 0; corner < 4; corner++) {
		struct FRAME_TILE_SELECTION tiles = {0};
		camera.position.x = corner & 1 ? 29 * 1024 : 0;
		camera.position.z = corner & 2 ? 29 * 1024 : 0;
		tiles.lookahead = &headings[corner * 2];
		frame_select_tiles(&tiles, &camera);
		for (legacy_u32 i = 0; i < FRAME_SUPERSIGHT_TILE_COUNT; i++) {
			if (tiles.markers[i] == FRAME_TILE_DRAW_MARKER) {
				assert(tiles.east[i] >= 0 && tiles.east[i] <= 29);
				assert(tiles.south[i] >= 0 && tiles.south[i] <= 29);
			}
		}
	}
	camera.position.x = 15 * 1024;
	camera.position.z = 14 * 1024;
	for (detail_level = 0; detail_level < 5; detail_level++) {
		struct FRAME_TILE_SELECTION tiles = {0};
		configure_track();
		element_map[15 + 14 * 30] = 1;
		trkObjectList[1].ss_multiTileFlag = 0;
		trkObjectList[1].ss_physicalModel = FRAME_SCENERY_PHYSICAL_MODEL_FIRST;
		state.playerstate.car_position.lx = 15L * 65536L;
		state.playerstate.car_position.lz = 14L * 65536L;
		tiles.lookahead = &headings[0];
		frame_select_tiles(&tiles, &camera);
		assert(tiles.elements[107] == (detail_level == 0 ? 1 : 0));
	}
	supersight_enabled = 0;
}

static void test_supersight_capacity_retries(void)
{
	struct FRAME_CAMERA camera = {0};
	struct FRAME_TILE_SELECTION tiles = {0};
	struct FRAME_CAR_RENDER cars[2] = {{0}};
	static const struct FRAME_LOOKAHEAD_TILE north = {2, -4, 2};
	configure_track();
	reset_shapes();
	memset(terrain_map, 1, sizeof(terrain_map));
	memset(&state, 0, sizeof(state));
	camera.position.x = 15 * 1024;
	camera.position.z = 14 * 1024;
	tiles.lookahead = &north;
	detail_level = 1;
	cameramode = CAMERA_MODE_COCKPIT;
	followOpponentFlag = 0;
	gameconfig.game_opponenttype = 0;
	supersight_enabled = 1;
	frame_select_tiles(&tiles, &camera);
	check_retry_brake_paint = 1;
	frame_supersight_reset();
	transform_capacity = 35;
	queue_resets = 0;
	frame_draw_supersight(&tiles, &camera, cars, 0, 0, 0);
	assert(queue_resets == 5);
	assert(tiles.first == 80);
	assert(transform_count == 30);
	/* Same-view frames skip failed levels, including paused frame zero. A full
	 * probe recovers quality within sixteen presented frames without relying
	 * on advancing simulation time. */
	transform_capacity = 200;
	for (legacy_u32 frame = 1; frame <= FRAME_SUPERSIGHT_PROBE_INTERVAL; frame++) {
		polyinfo_reset();
		legacy_s16 resets = queue_resets;
		tiles.lookahead = &north;
		frame_select_tiles(&tiles, &camera);
		frame_draw_supersight(&tiles, &camera, cars, 0, 0, 0);
		assert(queue_resets == resets);
		assert(tiles.first == (frame < FRAME_SUPERSIGHT_PROBE_INTERVAL ? 80 : 0));
	}
	assert(transform_count == 110);
	/* Camera changes and explicit replay seeks recover on their very next frame. */
	supersight_attempt_hint = 5;
	camera.yaw = 128;
	assert(frame_supersight_first_attempt(&tiles, &camera) == 0);
	supersight_attempt_hint = 5;
	select_rect_rc.right++;
	assert(frame_supersight_first_attempt(&tiles, &camera) == 0);
	supersight_attempt_hint = 5;
	frame_supersight_reset();
	assert(frame_supersight_first_attempt(&tiles, &camera) == 0);
	transform_capacity = 1;
	polyinfo_reset();
	tiles.lookahead = &north;
	frame_select_tiles(&tiles, &camera);
	frame_draw_supersight(&tiles, &camera, cars, 0, 0, 0);
	assert(tiles.first == 106);
	assert(transform_count == 2);
	supersight_enabled = 0;
	transform_capacity = 0;
	check_retry_brake_paint = 0;
}

#else
#define TEST_LOOKUP_CAMERA_POSES 2U
#define TEST_LOOKUP_MARKER_KINDS 3U
#define TEST_LOOKUP_WORLD_TILE_SIZE 65536L
#define TEST_LOOKUP_WHEEL_TILE_STEP 1024

static void test_complete_tile_lookup_matches_scans(void)
{
	const legacy_s8 camera_tiles[] = {-128, -100, -99, -98, -1, 0, 15, 29, 127};
	const legacy_s8 coordinates[] = {-1, 0, 14, 29, 30};
	const legacy_s16 indices[] = {0, 1, TRACK_GRID_SIZE * TRACK_GRID_SIZE / 2,
								  TRACK_GRID_SIZE * TRACK_GRID_SIZE};
	struct FRAME_CAMERA camera = {0};
	struct FRAME_TILE_SELECTION tiles = {0};
	struct FRAME_TILE_SELECTION reference;
	configure_track();
	for (legacy_u32 pose = 0; pose < TEST_LOOKUP_CAMERA_POSES; pose++) {
		for (legacy_u32 camera_index = 0;
			 camera_index < sizeof(camera_tiles) / sizeof(camera_tiles[0]); camera_index++) {
			tiles.camera_east = camera_tiles[camera_index];
			tiles.camera_south =
				camera_tiles[sizeof(camera_tiles) / sizeof(camera_tiles[0]) - 1U - camera_index];
			mat_temp =
				*mat_rot_zxy(pose * 43, camera_index * 73, pose * 127, MATRIX_ROTATION_ORDER_ZXY);
			frame_extend_lookahead(&tiles, &camera);
			legacy_s32 reconstructed = 1;
			for (legacy_s16 index = 0; index < tiles.count; index++) {
				legacy_s16 east = tiles.lookahead[index].east + tiles.camera_east;
				legacy_s16 south = tiles.lookahead[index].south + tiles.camera_south;
				if (east < 0 || east > TRACK_GRID_LAST_COORDINATE || south < 0 ||
					south > TRACK_GRID_LAST_COORDINATE) {
					reconstructed = 0;
				} else if (frame_has_complete_tile_lookup(&tiles)) {
					assert(frame_lookup_world_tile(&tiles, east, south) == index);
				}
			}
			assert(frame_has_complete_tile_lookup(&tiles) == reconstructed);
			/* A copied selection retains the same offsets but explicitly uses the
			 * original scan, including duplicate and signed-byte wrap semantics. */
			reference = tiles;
			reference.complete_tile_lookup = 0;
			for (legacy_s16 flag = FRAME_MULTITILE_NONE - 1; flag <= FRAME_MULTITILE_BOTH + 1;
				 flag++) {
				trkObjectList[1].ss_multiTileFlag = flag;
				for (legacy_u32 location = 0;
					 location < sizeof(coordinates) / sizeof(coordinates[0]); location++) {
					struct FRAME_TILE tile = {0};
					tile.element = 1;
					tile.east = coordinates[location];
					tile.south = coordinates[(location + pose) %
											 (sizeof(coordinates) / sizeof(coordinates[0]))];
					for (legacy_u32 limit = 0; limit < sizeof(indices) / sizeof(indices[0]);
						 limit++) {
						for (legacy_s16 index = 0; index < tiles.count; index++) {
							tiles.markers[index] = index % TEST_LOOKUP_MARKER_KINDS;
						}
						memcpy(reference.markers, tiles.markers, sizeof(tiles.markers));
						frame_mark_covered_tiles(&tiles, &tile, indices[limit]);
						frame_mark_covered_tiles(&reference, &tile, indices[limit]);
						assert(memcmp(reference.markers, tiles.markers, sizeof(tiles.markers)) ==
							   0);
					}
				}
			}
			for (legacy_u32 location = 0; location < sizeof(coordinates) / sizeof(coordinates[0]);
				 location++) {
				for (legacy_s16 spread = 0; spread < TEST_LOOKUP_CAMERA_POSES; spread++) {
					struct CARSTATE car = {0};
					struct SIMD simd = {0};
					car.car_position.lx = coordinates[location] * TEST_LOOKUP_WORLD_TILE_SIZE;
					car.car_position.lz = (TRACK_GRID_LAST_COORDINATE - coordinates[location]) *
										  TEST_LOOKUP_WORLD_TILE_SIZE;
					car.car_rotate.y = pose * ANGLE_QUARTER_TURN;
					for (legacy_s16 wheel = 0; wheel < FRAME_CAR_WHEEL_COUNT; wheel++) {
						simd.wheel_coords[wheel].x =
							spread * (wheel & 1 ? TEST_LOOKUP_WHEEL_TILE_STEP
												: -TEST_LOOKUP_WHEEL_TILE_STEP);
						simd.wheel_coords[wheel].z =
							spread * (wheel & 2 ? TEST_LOOKUP_WHEEL_TILE_STEP
												: -TEST_LOOKUP_WHEEL_TILE_STEP);
					}
					legacy_s8 east = -1, south = -1, reference_east = -1, reference_south = -1;
					legacy_s16 adjustment =
						frame_find_car_wheel(&car, &simd, &tiles, &east, &south);
					legacy_s16 reference_adjustment = frame_find_car_wheel(
						&car, &simd, &reference, &reference_east, &reference_south);
					assert(adjustment == reference_adjustment);
					assert(east == reference_east && south == reference_south);
				}
			}
		}
	}
}

static void test_supersight_selection(void)
{
	struct FRAME_CAMERA camera = {0};
	supersight_enabled = 1;
	for (legacy_s16 pose = 0; pose < 24; pose++) {
		configure_track();
		camera.position.x = pose & 1 ? 0 : 29 * 1024;
		camera.position.z = pose & 2 ? 0 : 29 * 1024;
		camera.position.y = 2000;
		mat_temp = *mat_rot_zxy(pose * 43, pose * 73, pose * 127, MATRIX_ROTATION_ORDER_ZXY);
		for (detail_level = 0; detail_level < 5; detail_level++) {
			struct FRAME_TILE_SELECTION tiles = {0};
			legacy_u8 seen[900] = {0};
			frame_select_tiles(&tiles, &camera);
			assert(tiles.count == 900 && tiles.first == 0);
			for (legacy_s16 i = 0; i < tiles.count; i++) {
				assert(tiles.markers[i] == FRAME_TILE_DRAW_MARKER);
				assert(tiles.detail[i] == FRAME_TILE_DETAIL_FULL);
				assert(tiles.east[i] >= 0 && tiles.east[i] < 30);
				assert(tiles.south[i] >= 0 && tiles.south[i] < 30);
				assert(seen[tiles.south[i] * 30 + tiles.east[i]]++ == 0);
				/* Scene submission is stable across camera rotation; visibility is
				 * resolved by HyperVision rather than painter ordering. */
				assert(tiles.east[i] == i % TRACK_GRID_SIZE);
				assert(tiles.south[i] == i / TRACK_GRID_SIZE);
			}
		}
	}
	/* A distant multi-tile object is resolved once, even when a continuation
	 * is closer than its owner. Scenery remains an explicit graphics option. */
	for (detail_level = 0; detail_level < 5; detail_level++) {
		configure_track();
		struct FRAME_TILE_SELECTION tiles = {0};
		element_map[0] = 1;
		element_map[1] = TRACK_TILE_CONTINUATION_EAST;
		element_map[30] = TRACK_TILE_CONTINUATION_SOUTH;
		element_map[31] = TRACK_TILE_CONTINUATION_SOUTHEAST;
		trkObjectList[1].ss_multiTileFlag = FRAME_MULTITILE_BOTH;
		trkObjectList[1].ss_physicalModel = 0;
		element_map[29] = 2;
		trkObjectList[2].ss_multiTileFlag = 0;
		trkObjectList[2].ss_physicalModel = FRAME_SCENERY_PHYSICAL_MODEL_FIRST;
		state.playerstate.car_position.lx = 15L * 65536;
		state.playerstate.car_position.lz = 15L * 65536;
		frame_select_tiles(&tiles, &camera);
		legacy_s16 objects = 0, covered = 0, scenery = 0;
		for (legacy_s16 i = 0; i < tiles.count; i++) {
			if (tiles.markers[i] == FRAME_TILE_MULTITILE_COVERED_MARKER) {
				covered++;
			} else if (tiles.markers[i] == FRAME_TILE_DRAW_MARKER) {
				objects += tiles.elements[i] == 1;
				scenery += tiles.elements[i] == 2;
			}
		}
		assert(objects == 1 && covered == 3);
		assert(scenery == (detail_level == 0));
	}
	supersight_enabled = 0;
}

static void test_supersight_capacity_retries(void)
{
	struct FRAME_CAMERA camera = {0};
	struct FRAME_TILE_SELECTION tiles = {0};
	struct FRAME_CAR_RENDER cars[2] = {{0}};
	configure_track();
	reset_shapes();
	memset(terrain_map, 1, sizeof(terrain_map));
	memset(&state, 0, sizeof(state));
	state.playerstate.car_position.lx = 15L * 65536;
	state.playerstate.car_position.lz = 15L * 65536;
	detail_level = 1;
	cameramode = CAMERA_MODE_COCKPIT;
	followOpponentFlag = 0;
	gameconfig.game_opponenttype = 0;
	start_finish_column = -1;
	supersight_enabled = 1;
	frame_select_tiles(&tiles, &camera);
	transform_capacity = 0;
	queue_resets = 0;
	frame_draw_supersight(&tiles, &camera, cars, 0, 0, 0);
	assert(queue_resets == 0 && tiles.first == 0);
	assert(transform_count == 900);
	/* Camera depths and legacy painter biases must not reorder enhanced submissions. */
	reset_shapes();
	mat_temp = *mat_rot_zxy(0, 0, 128, MATRIX_ROTATION_ORDER_ZXY);
	curtransshape_ptr = currenttransshape;
	for (legacy_s16 i = 0; i < 3; i++) {
		curtransshape_ptr->pos.x = -29000 + i * 1000;
		curtransshape_ptr->pos.y = 0;
		curtransshape_ptr->pos.z = 29000 - i * 1000;
		curtransshape_ptr->shapeptr = &game3dshapes[0];
		transformed_shape_add_for_sort(i == 0 ? -2048 : 0, 0);
	}
	frame_draw_sorted_shapes(cars, 0, 0, 0);
	for (legacy_s16 i = 0; i < transformedshape_counter; i++) {
		assert(transformedshape_indices[i] == i);
	}
	supersight_enabled = 0;
}
#endif

#if defined(RESTUNTS_SDL3)
static void assert_grounding_offset(const struct CARSTATE *car, legacy_s16 x, legacy_s16 y,
									legacy_s16 z)
{
	struct CARSTATE original = *car;
	struct VECTOR offset = frame_car_render_offset(car, &game3dshapes[PLAYER_CAR_WHEEL_SHAPE]);
	assert(offset.x == x && offset.y == y && offset.z == z);
	assert(memcmp(car, &original, sizeof(original)) == 0);
}

static void test_supersight_grounding_surfaces(void)
{
	struct CARSTATE car = {0};
	car_ground_offsets[0] = 8;
	for (legacy_s16 wheel = 0; wheel < FRAME_CAR_WHEEL_COUNT; wheel++) {
		car.car_surfaceWhl[wheel] = CAR_SURFACE_PAVED;
	}
	assert_grounding_offset(&car, 0, -10, 0);
	/* The authored tire correction follows the local up axis. The collision
	 * surface allowance remains vertical, including on walls and loops. */
	car.car_rotate.z = ANGLE_QUARTER_TURN;
	assert_grounding_offset(&car, -8, -2, 0);
	car.car_rotate.z = 0;
	car.car_rotate.y = ANGLE_QUARTER_TURN;
	assert_grounding_offset(&car, 0, -2, 8);
	car.car_rotate.y = ANGLE_HALF_TURN;
	assert_grounding_offset(&car, 0, 6, 0);
	car.car_rotate.y = 0;
	car.car_rotate.x = ANGLE_QUARTER_TURN;
	assert_grounding_offset(&car, 0, -10, 0);
	car.car_rotate.x = 0;

	for (legacy_s16 wheel = 0; wheel < FRAME_CAR_WHEEL_COUNT; wheel++) {
		car.car_surfaceWhl[wheel] = CAR_SURFACE_GRASS;
		car.car_wheel_contact_positions[wheel] = (struct VECTOR){256, 0, 0};
	}
	assert_grounding_offset(&car, 0, -9, 0);
	car.car_wheel_contact_positions[2].z = 256;
	assert_grounding_offset(&car, 0, -8, 0);
	/* At a road edge the lowest supported wheel determines the correction,
	 * so the grass-side tires cannot be buried by the road's larger bias. */
	car.car_surfaceWhl[0] = car.car_surfaceWhl[1] = CAR_SURFACE_PAVED;
	assert_grounding_offset(&car, 0, -8, 0);
	car.car_wheel_contact_positions[2].z = 0;
	assert_grounding_offset(&car, 0, -9, 0);
	car_ground_offsets[0] = 0;
}

static void test_supersight_grounding_poses(void)
{
	struct FRAME_CAMERA camera = {0};
	struct FRAME_TILE tile = {0};
	camera.position = (struct VECTOR){100, 50, 700};
	tile.east = tile.last_east = 10;
	tile.south = tile.last_south = 12;
	tile.detail = FRAME_TILE_DETAIL_FULL;
	shadow_frame_active = 1;
	car_ground_offsets[0] = 5;
	car_ground_offsets[1] = 3;
	frame_state = &state;
	frame_uses_snapshot = 0;
	slow_video_mgmt_copy = 0;
	detail_level = 0;
	for (legacy_s16 enabled = 0; enabled < 2; enabled++) {
		for (legacy_s16 kind = 0; kind < 3; kind++) {
			for (legacy_s16 airborne = 0; airborne < 2; airborne++) {
				memset(&state, 0, sizeof(state));
				memset(&ghost_fixture, 0, sizeof(ghost_fixture));
				struct FRAME_CAR_RENDER cars[2] = {{0}};
				legacy_s16 owner = kind == 0 ? PLAYER_CAR_INDEX : OPPONENT_CAR_INDEX;
				struct CARSTATE *car = kind == 0   ? &state.playerstate
									   : kind == 1 ? &state.opponentstate
												   : &ghost_fixture;
				car->car_position =
					(struct VECTORLONG){64000, (200 + 600 * airborne) * 64L, 192000};
				for (legacy_s16 wheel = 0; wheel < FRAME_CAR_WHEEL_COUNT; wheel++) {
					car->car_surfaceWhl[wheel] =
						airborne != 0 ? CAR_WHEEL_CONTACT_NONE : CAR_SURFACE_PAVED;
				}
				cars[0].east = cars[1].east = -1;
				cars[owner].east = tile.east;
				cars[owner].south = tile.south;
				gameconfig.game_opponenttype = kind == 1;
				ghost_fixture_active = kind == 2;
				state.game_particles_active = 1;
				state.game_particle_forward_speed[0] = 1;
				state.game_particle_owner[0] = owner;
				state.game_particle_shape_index[0] = 0;
				state.game_particle_x[0] = 2 * 64;
				state.game_particle_y[0] = 3 * 64;
				state.game_particle_z[0] = 4 * 64;
				particle_scene_objects[0].ss_shapePtr = &game3dshapes[116];
				trkObjectList[FRAME_PLAYER_SORT_ID].ss_shapePtr =
					&game3dshapes[PLAYER_CAR_WHEEL_SHAPE];
				trkObjectList[FRAME_OPPONENT_SORT_ID].ss_shapePtr =
					&game3dshapes[OPPONENT_CAR_WHEEL_SHAPE];
				struct GAMESTATE original = state;
				struct CARSTATE original_ghost = ghost_fixture;
				reset_shapes();
				supersight_enabled = enabled;
				frame_add_tile_cars(&tile, &camera, cars, 0);
				assert(transformedshape_counter == (kind == 2 ? 1 : 2));
				legacy_s16 body = transformedshape_counter - 1;
				struct VECTOR expected = {900, 150 + 600 * airborne, 2300};
				if (enabled != 0) {
					expected.y -= car_ground_offsets[owner] + 2;
				}
				assert(memcmp(&currenttransshape[body].pos, &expected, sizeof(expected)) == 0);
				assert(((currenttransshape[body].ts_flags & SHAPE3D_GHOST_FLAG) != 0) ==
					   (kind == 2));
				if (kind != 2) {
					expected.x += 2;
					expected.y += 3;
					expected.z += 4;
					assert(memcmp(&currenttransshape[0].pos, &expected, sizeof(expected)) == 0);
				}
				assert(memcmp(&state, &original, sizeof(state)) == 0);
				assert(memcmp(&ghost_fixture, &original_ghost, sizeof(ghost_fixture)) == 0);
			}
		}
	}
	car_ground_offsets[0] = car_ground_offsets[1] = 0;
	shadow_frame_active = supersight_enabled = ghost_fixture_active = 0;
	state.game_particles_active = 0;
}

static void test_supersight_car_shadows(void)
{
	struct RECTANGLE cliprect = {0, 320, 0, 200};
	static struct FRAME_LOOKAHEAD_TILE lookahead[24];
	lookahead_tiles_tables[0] = (legacy_s8 *)lookahead;
	configure_track();
	memset(&state, 0, sizeof(state));
	memset(&simd_player, 0, sizeof(simd_player));
	memset(&simd_opponent, 0, sizeof(simd_opponent));
	state.playerstate.car_position = (struct VECTORLONG){640000, 6400, 700000};
	state.opponentstate.car_position = (struct VECTORLONG){660000, 9600, 720000};
	state.playerstate.car_rotate.x = 179;
	state.opponentstate.car_rotate.x = -321;
	state.game_follow_camera_position[0] = (struct VECTOR){9500, 500, 10000};
	state.game_follow_camera_position[1] = (struct VECTOR){9600, 600, 10100};
	simd_player.car_height = simd_opponent.car_height = 40;
	simd_player.collide_points[0].px = 47;
	simd_player.collide_points[1].px = 96;
	simd_opponent.collide_points[0].px = 39;
	simd_opponent.collide_points[1].px = 81;
	ghost_fixture = state.opponentstate;
	ghost_fixture.car_position.lx += 10000;
	ghost_camera_fixture.follow_position = (struct VECTOR){9000, 450, 10300};
	ghost_simd_fixture.car_height = 40;
	trkObjectList[FRAME_PLAYER_SORT_ID].ss_shapePtr = &game3dshapes[PLAYER_CAR_WHEEL_SHAPE];
	trkObjectList[FRAME_OPPONENT_SORT_ID].ss_shapePtr = &game3dshapes[OPPONENT_CAR_WHEEL_SHAPE];
	trkObjectList[FRAME_PLAYER_SORT_ID].ss_loShapePtr = &game3dshapes[PLAYER_CAR_LOW_SHAPE];
	trkObjectList[FRAME_OPPONENT_SORT_ID].ss_loShapePtr = &game3dshapes[OPPONENT_CAR_LOW_SHAPE];
	start_finish_column = -1;
	detail_level = 1;
	slow_video_mgmt_copy = 0;
	game_replay_mode = REPLAY_MODE_LIVE;
	state.game_inputmode = GAME_INPUT_MODE_ACTIVE;
	hud_fixture.active = 1;
	fontdefptr = fontledresptr = hud_fixture.font;
	dashboard_visible = 1;
	dastbmp_y = HUD_TEST_DASHBOARD_TOP;
	dashbmp_y_copy = HUD_TEST_DASHBOARD_BOTTOM;
	dasmshapeptr = &hud_fixture.mask;
	dastbmp_y2 = HUD_TEST_SHAPE_OFFSET;
	dastseg = HUD_TEST_SHAPE_SEGMENT;
	roofbmpheight = replaybar_enabled = 0;
	terrainHeight = 0;
	track_wall_collision_enabled = 0;
	shadow_frame_active = 1;
	/* Exercise the actual frame entry point, including T into an AI or ghost
	 * cockpit, with both a live pose and a separate interpolated snapshot. */
	for (legacy_s16 snapshot = 0; snapshot < 2; snapshot++) {
		struct GAMESTATE presentation = state;
		presentation.playerstate.car_position.lx += 640;
		frame_state = snapshot != 0 ? &presentation : &state;
		frame_uses_snapshot = snapshot;
		frame_ghost = &ghost_fixture;
		frame_ghost_camera = &ghost_camera_fixture;
		for (legacy_s16 scenario = 0; scenario < 48; scenario++) {
			supersight_enabled = scenario & 1;
			followOpponentFlag = (scenario >> 1) & 1;
			cameramode = (scenario & 4) != 0 ? CAMERA_MODE_COCKPIT : CAMERA_MODE_FOLLOW;
			gameconfig.game_opponenttype = (scenario >> 3) & 1;
			ghost_fixture_active = gameconfig.game_opponenttype == 0;
			state.playerstate.car_crashBmpFlag = presentation.playerstate.car_crashBmpFlag =
				scenario >= 32 ? CRASH_EVENT_WATER : CRASH_EVENT_NONE;
			state.opponentstate.car_crashBmpFlag = presentation.opponentstate.car_crashBmpFlag =
				scenario >= 16 && scenario < 32 ? CRASH_EVENT_WATER : CRASH_EVENT_NONE;
			reset_shapes();
			legacy_u16 adaptive_quality = (scenario & 16) != 0 ? FRAME_ADAPTIVE_MAX_QUALITY : 0;
			frame_adaptive.quality = adaptive_quality;
			shadow_count = shadow_begin_count = shadow_projection_count = 0;
			hud_test_reset();
			update_frame(0, &cliprect);
			assert(hud_fixture.masks == 1 && hud_fixture.overlays == 1);
			assert(hud_fixture.timers == 1 && hud_fixture.messages == 1);
			hud_test_assert_pixels(&hud_fixture.timer_rect, HUD_TEST_TIMER_COLOR);
			hud_test_assert_pixels(&hud_fixture.message_rect, HUD_TEST_MESSAGE_COLOR);
			assert(frame_adaptive_active ==
				   (supersight_enabled != 0 && snapshot != 0 && adaptive_quality != 0));
			assert(shadow_begin_count == supersight_enabled);
			legacy_s16 expected_count = 0;
			for (legacy_s16 car_index = 0; car_index < 2; car_index++) {
				const struct CARSTATE *car =
					car_index == 0 ? &frame_state->playerstate : &frame_state->opponentstate;
				const struct SIMD *simd = car_index == 0 ? &simd_player : &simd_opponent;
				if (supersight_enabled == 0 ||
					(car_index == 1 && gameconfig.game_opponenttype == 0) ||
					car->car_crashBmpFlag == CRASH_EVENT_WATER ||
					(cameramode == CAMERA_MODE_COCKPIT && car_index == followOpponentFlag) ||
					frame_adaptive_car_flags(car, simd) == FRAME_ADAPTIVE_HIDE) {
					continue;
				}
				assert(expected_count < shadow_count);
				const struct FRAME_SHADOW_FIXTURE *shadow = &shadow_cars[expected_count++];
				assert(shadow->position.x ==
					   position_to_word(car->car_position.lx) - shadow_camera.x);
				assert(shadow->position.y ==
					   position_to_word(car->car_position.ly) - shadow_camera.y);
				assert(shadow->position.z ==
					   position_to_word(car->car_position.lz) - shadow_camera.z);
				assert(shadow->heading == -car->car_rotate.x);
				assert(shadow->half_width == simd->collide_points[0].px);
				assert(shadow->half_length == simd->collide_points[1].px);
				assert(shadow->model == &game3dshapes[car_index == 0 ? PLAYER_CAR_WHEEL_SHAPE
																	 : OPPONENT_CAR_WHEEL_SHAPE]);
			}
			assert(shadow_count == expected_count);
		}
	}
	hud_fixture.active = 0;
	fontdefptr = fontledresptr = NULL;
	dashboard_visible = dastbmp_y = 0;
	frame_uses_snapshot = 0;
	frame_adaptive_reset(&frame_adaptive);
	frame_adaptive_active = 0;
	frame_state = &state;
	frame_ghost = 0;
	frame_ghost_camera = 0;
	ghost_fixture_active = 0;
	/* Body, wheel model, ghost and attached debris all reject receiving;
	 * ordinary rendering retains exactly its original flag bits. */
	for (legacy_s16 enabled = 0; enabled < 2; enabled++) {
		for (legacy_s16 ghost = 0; ghost < 2; ghost++) {
			reset_shapes();
			supersight_enabled = enabled;
			state.playerstate.car_crashBmpFlag = CRASH_EVENT_NONE;
			state.game_particles_active = 1;
			state.game_particle_forward_speed[0] = 1;
			state.game_particle_owner[0] = PLAYER_CAR_INDEX;
			state.game_particle_shape_index[0] = 0;
			particle_scene_objects[0].ss_shapePtr = &game3dshapes[116];
			struct VECTOR camera_position = {0};
			struct RECTANGLE crash_rect = {0};
			frame_add_car(&state.playerstate, PLAYER_CAR_INDEX, FRAME_PLAYER_SORT_ID,
						  &game3dshapes[PLAYER_CAR_WHEEL_SHAPE], player_wheel_vertex_state,
						  player_base_wheel_vertices, player_front_wheel_centers,
						  &frame_player_car_rect, &crash_rect, &camera_position, 0,
						  ghost != 0 ? SHAPE3D_GHOST_FLAG : 0, 0, 0);
			assert(transformedshape_counter == (ghost != 0 ? 1 : 2));
			for (legacy_s16 shape = 0; shape < transformedshape_counter; shape++) {
				assert(((currenttransshape[shape].ts_flags & SHAPE3D_NO_SHADOW_RECEIVE_FLAG) !=
						0) == enabled);
			}
			legacy_s16 body = transformedshape_counter - 1;
			assert(currenttransshape[body].shapeptr == &game3dshapes[PLAYER_CAR_WHEEL_SHAPE]);
			assert((currenttransshape[body].ts_flags & ~SHAPE3D_NO_SHADOW_RECEIVE_FLAG) ==
				   (FRAME_TRANSFORM_FLAGS_DEFAULT | (ghost != 0 ? SHAPE3D_GHOST_FLAG : 0)));
		}
	}
	shadow_frame_active = 0;
	supersight_enabled = 0;
	state.game_particles_active = 0;
	followOpponentFlag = 0;
}
#endif

#if defined(RESTUNTS_SDL3)
enum {
	ADAPTIVE_TEST_CAMERA_TILE = TRACK_GRID_SIZE / 2,
	ADAPTIVE_TEST_OBJECT = 1,
	ADAPTIVE_TEST_HIGH_SHAPE = 10,
	ADAPTIVE_TEST_LOW_SHAPE = 11,
	ADAPTIVE_TEST_OVERLAY_OBJECT = 4,
	ADAPTIVE_TEST_OVERLAY_SHAPE = 12,
	ADAPTIVE_TEST_OVERLAY_LOW_SHAPE = 13,
	ADAPTIVE_TEST_REDUCED_DETAIL = FRAME_TILE_DETAIL_FULL + 1,
	ADAPTIVE_TEST_WORLD_TILE_SIZE = 1U << FRAME_CAMERA_TILE_SHIFT,
	ADAPTIVE_TEST_LARGE_COUNT = 70,
	ADAPTIVE_TEST_SMALL_COUNT = 23,
	ADAPTIVE_TEST_LARGE_FORWARD = 9,
	ADAPTIVE_TEST_H_FORWARD = 6,
	ADAPTIVE_TEST_L_COLUMN = -3,
	ADAPTIVE_TEST_POSITION_SCALE = TEST_LOOKUP_WORLD_TILE_SIZE / ADAPTIVE_TEST_WORLD_TILE_SIZE,
	ADAPTIVE_TEST_WHEEL_CROSSING_OFFSET = 2 * ADAPTIVE_TEST_POSITION_SCALE,
	ADAPTIVE_TEST_CAR_HALF_WIDTH = 17 * ADAPTIVE_TEST_POSITION_SCALE,
	ADAPTIVE_TEST_CAR_HALF_LENGTH = 29 * ADAPTIVE_TEST_POSITION_SCALE,
	/* This fixture exercises the unchanged legacy 30x30 world boundary. */
	ADAPTIVE_TEST_EDGE_CAR_EAST = 29 - 1,
	ADAPTIVE_TEST_EDGE_CAR_SOUTH = TRACK_GRID_SIZE * 2 / 3 - 1,
	ADAPTIVE_TEST_EDGE_CAR_X = ADAPTIVE_TEST_EDGE_CAR_EAST * ADAPTIVE_TEST_WORLD_TILE_SIZE +
							   ADAPTIVE_TEST_WORLD_TILE_SIZE * 2 / 3,
	ADAPTIVE_TEST_EDGE_CAR_Z = (29 - ADAPTIVE_TEST_EDGE_CAR_SOUTH) * ADAPTIVE_TEST_WORLD_TILE_SIZE +
							   2 * ADAPTIVE_TEST_CAR_HALF_LENGTH / ADAPTIVE_TEST_POSITION_SCALE,
	ADAPTIVE_TEST_EDGE_CAR_HEADING = ANGLE_HALF_TURN + ANGLE_EIGHTH_TURN / 2,
	ADAPTIVE_TEST_EDGE_VIEW_HEADING = ANGLE_QUARTER_TURN - ANGLE_EIGHTH_TURN / 3,
	ADAPTIVE_TEST_EDGE_CAMERA_X_OFFSET = ADAPTIVE_TEST_WORLD_TILE_SIZE * 7 / 16,
	ADAPTIVE_TEST_EDGE_CAMERA_HEIGHT = ADAPTIVE_TEST_WORLD_TILE_SIZE / 4,
	ADAPTIVE_TEST_EDGE_CAMERA_Z_OFFSET = ADAPTIVE_TEST_WORLD_TILE_SIZE / 8,
	ADAPTIVE_TEST_PARTICLE_OWNER_DISTANCE = 8,
	ADAPTIVE_TEST_PARTICLE_HEIGHT = 100,
	ADAPTIVE_TEST_PARTICLE_GROUND_OFFSET = 7,
	ADAPTIVE_TEST_PARTICLE_PLAYER_MATERIAL = 3,
	ADAPTIVE_TEST_PARTICLE_OPPONENT_MATERIAL = 5,
	ADAPTIVE_TEST_PARTICLE_SIGN = 0
};

static void adaptive_reset_capture(void)
{
	reset_shapes();
	transform_capacity = 0;
	check_retry_brake_paint = 0;
	adaptive_captured_count = 0;
	adaptive_capture_enabled = 1;
}

enum {
	WATER_TEST_TILE = TRACK_GRID_SIZE / 2,
	WATER_TEST_OBJECT = 1,
	WATER_TEST_SHAPE_BASE = 16,
	WATER_TEST_CAMERA_HEIGHT = 250,
	WATER_TEST_NORTHWEST = 1U << FRAME_CORNER_NORTHWEST,
	WATER_TEST_NORTHEAST = 1U << FRAME_CORNER_NORTHEAST,
	WATER_TEST_SOUTHWEST = 1U << FRAME_CORNER_SOUTHWEST,
	WATER_TEST_SOUTHEAST = 1U << FRAME_CORNER_SOUTHEAST,
	WATER_TEST_ALL_CELLS = (1U << FRAME_ELEVATED_CORNER_COUNT) - 1U,
	WATER_TEST_MASK_COUNT = WATER_TEST_ALL_CELLS + 1U
};

static void water_reset_scene(struct FRAME_TILE *tile, legacy_s16 footprint, legacy_u16 wet_cells,
							  legacy_u8 terrain, legacy_u8 raised)
{
	configure_track();
	adaptive_reset_capture();
	frame_adaptive_active = 0;
	memset(tile, 0, sizeof(*tile));
	tile->east = tile->south = WATER_TEST_TILE;
	tile->element = WATER_TEST_OBJECT;
	tile->detail = FRAME_TILE_DETAIL_FULL;
	trkObjectList[WATER_TEST_OBJECT].ss_multiTileFlag = footprint;
	for (legacy_u16 corner = 0; corner < FRAME_ELEVATED_CORNER_COUNT; corner++) {
		legacy_s16 east = tile->east + (corner & 1U);
		legacy_s16 south = tile->south + (corner >> 1U);
		terrain_map[terrainrows[south] + east] = (wet_cells & (1U << corner)) != 0 ? terrain : 0;
	}
	if (raised != 0) {
		terrain_map[terrainrows[tile->south] + tile->east] = TERRAIN_RAISED_TILE;
	}
	tile->terrain = terrain_map[terrainrows[tile->south] + tile->east];
	for (legacy_u8 water = FRAME_WATER_TERRAIN_FIRST; water <= FRAME_WATER_TERRAIN_LAST; water++) {
		terrain_scene_objects[water].ss_shapePtr = &game3dshapes[WATER_TEST_SHAPE_BASE + water];
		terrain_scene_objects[water].ss_loShapePtr = terrain_scene_objects[water].ss_shapePtr;
		terrain_scene_objects[water].ss_rotY = water * ANGLE_QUARTER_TURN;
	}
}

static void water_assert_shapes(const struct FRAME_TILE *tile, const struct FRAME_CAMERA *camera,
								legacy_u16 expected_cells)
{
	legacy_u16 seen = 0;
	for (legacy_s16 index = 0; index < adaptive_captured_count; index++) {
		const struct TRANSFORMEDSHAPE3D *shape = &adaptive_captured[index];
		legacy_u16 cell = 0;
		for (legacy_u16 corner = 0; corner < FRAME_ELEVATED_CORNER_COUNT; corner++) {
			legacy_s16 east = tile->east + (corner & 1U);
			legacy_s16 south = tile->south + (corner >> 1U);
			if (east >= TRACK_GRID_SIZE || south >= TRACK_GRID_SIZE ||
				shape->pos.x !=
					LEGACY_S16_WRAP_SUB(track_column_centers[east], camera->position.x) ||
				shape->pos.z != LEGACY_S16_WRAP_SUB(track_row_centers[south], camera->position.z)) {
				continue;
			}
			legacy_u8 terrain = terrain_map[terrainrows[south] + east];
			assert(shape->shapeptr == &game3dshapes[WATER_TEST_SHAPE_BASE + terrain]);
			assert(shape->rotvec.z == terrain_scene_objects[terrain].ss_rotY);
			cell = 1U << corner;
		}
		assert(cell != 0 && (expected_cells & cell) != 0 && (seen & cell) == 0);
		seen |= cell;
		assert(shape->pos.y == -camera->position.y);
		assert(shape->rotvec.x == 0 && shape->rotvec.y == 0);
		assert(shape->ts_flags ==
			   (FRAME_TRANSFORM_FLAGS_CLIPPED | FRAME_TRANSFORM_FLAGS_NO_DEPTH_SORT));
		assert(shape->material == 0);
	}
	assert(seen == expected_cells);
}

static void test_supersight_covered_water(void)
{
	static const struct {
		legacy_s16 footprint;
		legacy_u16 cells;
	} footprints[] = {{FRAME_MULTITILE_NONE, WATER_TEST_NORTHWEST},
					  {FRAME_MULTITILE_ROW, WATER_TEST_NORTHWEST | WATER_TEST_SOUTHWEST},
					  {FRAME_MULTITILE_COLUMN, WATER_TEST_NORTHWEST | WATER_TEST_NORTHEAST},
					  {FRAME_MULTITILE_BOTH, WATER_TEST_ALL_CELLS}};
	struct FRAME_CAMERA camera = {0};
	camera.position.y = WATER_TEST_CAMERA_HEIGHT;
	struct FRAME_TILE tile;
	/* Every wet/dry arrangement checks independent terrain under the footprint,
	 * including a dry or raised owner. The classic path keeps only its owner. */
	for (legacy_u16 footprint = 0; footprint < sizeof(footprints) / sizeof(footprints[0]);
		 footprint++) {
		for (legacy_u8 water = FRAME_WATER_TERRAIN_FIRST; water <= FRAME_WATER_TERRAIN_LAST;
			 water++) {
			for (legacy_u16 wet_cells = 0; wet_cells < WATER_TEST_MASK_COUNT; wet_cells++) {
				for (legacy_u8 raised = 0; raised <= 1; raised++) {
					for (supersight_enabled = 0; supersight_enabled <= 1; supersight_enabled++) {
						water_reset_scene(&tile, footprints[footprint].footprint, wet_cells, water,
										  raised);
						legacy_u16 expected =
							wet_cells & (supersight_enabled != 0 ? footprints[footprint].cells
																 : WATER_TEST_NORTHWEST);
						if (raised != 0) {
							expected &= ~WATER_TEST_NORTHWEST;
						}
						assert(frame_draw_terrain(&tile, &camera, FRAME_TRANSFORM_FLAGS_CLIPPED) ==
							   0);
						water_assert_shapes(&tile, &camera, expected);
						assert(tile.height ==
							   (raised != 0 ? hillHeightConsts[TERRAIN_RAISED_HEIGHT_INDEX] : 0));
					}
				}
			}
		}
	}
	supersight_enabled = 1;
	/* Shoreline models and rotations must come from each cell, not the owner. */
	water_reset_scene(&tile, FRAME_MULTITILE_BOTH, WATER_TEST_ALL_CELLS, FRAME_WATER_TERRAIN_FIRST,
					  0);
	for (legacy_u16 corner = 0; corner < FRAME_ELEVATED_CORNER_COUNT; corner++) {
		terrain_map[terrainrows[tile.south + (corner >> 1U)] + tile.east + (corner & 1U)] =
			FRAME_WATER_TERRAIN_FIRST + corner;
	}
	assert(frame_draw_terrain(&tile, &camera, FRAME_TRANSFORM_FLAGS_CLIPPED) == 0);
	water_assert_shapes(&tile, &camera, WATER_TEST_ALL_CELLS);
	/* A visible object keeps water across its footprint even across an adaptive
	 * mask boundary. Elevated corners already own their complete terrain pass. */
	for (legacy_u8 special = 0; special <= 1; special++) {
		water_reset_scene(&tile, FRAME_MULTITILE_BOTH, WATER_TEST_ALL_CELLS,
						  FRAME_WATER_TERRAIN_FIRST, 0);
		if (special != 0) {
			tile.element = FRAME_ELEVATED_CORNER_FIRST;
			trkObjectList[tile.element].ss_multiTileFlag = FRAME_MULTITILE_BOTH;
		} else {
			frame_adaptive_active = 1;
			memset(frame_adaptive.tile_flags, FRAME_ADAPTIVE_HIDE,
				   sizeof(frame_adaptive.tile_flags));
		}
		assert(frame_draw_terrain(&tile, &camera, FRAME_TRANSFORM_FLAGS_CLIPPED) == 0);
		water_assert_shapes(&tile, &camera, WATER_TEST_ALL_CELLS);
	}
	/* Raised land and hillside terrain are not water. */
	for (legacy_u8 terrain = TERRAIN_RAISED_TILE; terrain < FRAME_HILL_ROAD_TERRAIN_END;
		 terrain++) {
		water_reset_scene(&tile, FRAME_MULTITILE_BOTH, WATER_TEST_ALL_CELLS,
						  FRAME_WATER_TERRAIN_FIRST, 0);
		for (legacy_u16 corner = FRAME_CORNER_NORTHEAST; corner < FRAME_ELEVATED_CORNER_COUNT;
			 corner++) {
			terrain_map[terrainrows[tile.south + (corner >> 1U)] + tile.east + (corner & 1U)] =
				terrain;
		}
		assert(frame_draw_terrain(&tile, &camera, FRAME_TRANSFORM_FLAGS_CLIPPED) == 0);
		water_assert_shapes(&tile, &camera, WATER_TEST_NORTHWEST);
	}
	/* Track-edge continuations cannot read beyond the terrain map. */
	water_reset_scene(&tile, FRAME_MULTITILE_BOTH, WATER_TEST_ALL_CELLS, FRAME_WATER_TERRAIN_FIRST,
					  0);
	tile.east = tile.south = TRACK_GRID_LAST_COORDINATE;
	terrain_map[terrainrows[tile.south] + tile.east] = tile.terrain;
	assert(frame_draw_terrain(&tile, &camera, FRAME_TRANSFORM_FLAGS_CLIPPED) == 0);
	water_assert_shapes(&tile, &camera, WATER_TEST_NORTHWEST);
	/* All terrain submissions propagate queue exhaustion; a culled shape does
	 * not prevent the remaining covered water from being queued. */
	for (legacy_s16 stop = 1; stop <= FRAME_ELEVATED_CORNER_COUNT; stop++) {
		water_reset_scene(&tile, FRAME_MULTITILE_BOTH, WATER_TEST_ALL_CELLS,
						  FRAME_WATER_TERRAIN_FIRST, 0);
		transform_stop_at = stop;
		assert(frame_draw_terrain(&tile, &camera, FRAME_TRANSFORM_FLAGS_CLIPPED) == 1);
		assert(transform_count == stop);
	}
	water_reset_scene(&tile, FRAME_MULTITILE_BOTH, WATER_TEST_ALL_CELLS, FRAME_WATER_TERRAIN_FIRST,
					  0);
	rejected_shape = 1;
	assert(frame_draw_terrain(&tile, &camera, FRAME_TRANSFORM_FLAGS_CLIPPED) == 0);
	water_assert_shapes(&tile, &camera, WATER_TEST_ALL_CELLS);
	adaptive_capture_enabled = frame_adaptive_active = supersight_enabled = 0;
}

static void adaptive_reset_scene(void)
{
	configure_track();
	memset(&state, 0, sizeof(state));
	memset(trkObjectList, 0, sizeof(trkObjectList));
	frame_state = &state;
	frame_uses_snapshot = 0;
	frame_adaptive_active = 1;
	frame_adaptive_reset(&frame_adaptive);
	frame_adaptive.quality = FRAME_ADAPTIVE_LARGE_VIEW;
	assert(frame_adaptive_prepare(&frame_adaptive, ADAPTIVE_TEST_CAMERA_TILE,
								  ADAPTIVE_TEST_CAMERA_TILE, TRIG_FIXED_ONE, 0));
	supersight_enabled = 1;
	detail_level = FRAME_DETAIL_FULL;
	slow_video_mgmt_copy = 0;
	gameconfig.game_opponenttype = 0;
	ghost_fixture_active = 0;
	start_finish_column = -1;
	shadow_frame_active = 1;
	car_ground_offsets[0] = car_ground_offsets[1] = 0;
	trkObjectList[ADAPTIVE_TEST_OBJECT].ss_shapePtr = &game3dshapes[ADAPTIVE_TEST_HIGH_SHAPE];
	trkObjectList[ADAPTIVE_TEST_OBJECT].ss_loShapePtr = &game3dshapes[ADAPTIVE_TEST_LOW_SHAPE];
	trkObjectList[ADAPTIVE_TEST_OBJECT].ss_ignoreZBias = FRAME_NO_DEPTH_SORT_FLAG;
	adaptive_reset_capture();
}

static struct FRAME_CAMERA adaptive_camera(void)
{
	struct FRAME_CAMERA camera = {0};
	camera.position.x = ADAPTIVE_TEST_CAMERA_TILE * ADAPTIVE_TEST_WORLD_TILE_SIZE;
	camera.position.z =
		(TRACK_GRID_LAST_COORDINATE - ADAPTIVE_TEST_CAMERA_TILE) * ADAPTIVE_TEST_WORLD_TILE_SIZE;
	return camera;
}

static void test_adaptive_selection_before_submission(void)
{
	adaptive_reset_scene();
	adaptive_capture_enabled = 0;
	struct FRAME_CAMERA camera = adaptive_camera();
	struct FRAME_TILE_SELECTION tiles = {0};
	for (legacy_u16 quality = FRAME_ADAPTIVE_LARGE_VIEW; quality <= FRAME_ADAPTIVE_SMALL_VIEW;
		 quality++) {
		frame_adaptive.quality = quality;
		for (legacy_s16 heading = 0; heading < ANGLE_FULL_TURN; heading += ANGLE_QUARTER_TURN) {
			camera.view_heading = heading;
			/* Deliberately disagree with the camera's effective heading. */
			camera.yaw = state.playerstate.car_rotate.x = heading + ANGLE_HALF_TURN;
			mat_temp = *mat_rot_zxy(0, 0, heading, MATRIX_ROTATION_ORDER_YXZ);
			struct GAMESTATE before = state;
			frame_select_tiles(&tiles, &camera);
			assert(memcmp(&before, &state, sizeof(state)) == 0);
			assert(tiles.count == (quality == FRAME_ADAPTIVE_SMALL_VIEW
									   ? ADAPTIVE_TEST_SMALL_COUNT
									   : ADAPTIVE_TEST_LARGE_COUNT));
			assert(frame_has_complete_tile_lookup(&tiles));
			assert(frame_adaptive.cached_cos == cos_fast(camera.view_heading));
			assert(frame_adaptive.cached_sin == sin_fast(camera.view_heading));
			legacy_s16 previous_tile = -1;
			for (legacy_s16 index = 0; index < tiles.count; index++) {
				legacy_s16 east = tiles.lookahead[index].east + tiles.camera_east;
				legacy_s16 south = tiles.lookahead[index].south + tiles.camera_south;
				assert(frame_adaptive_flags(&frame_adaptive, east, south) != FRAME_ADAPTIVE_HIDE);
				assert(frame_lookup_world_tile(&tiles, east, south) == index);
				legacy_s16 tile = south * TRACK_GRID_SIZE + east;
				assert(tile > previous_tile);
				previous_tile = tile;
			}
			for (legacy_s16 south = 0; south < TRACK_GRID_SIZE; south++) {
				for (legacy_s16 east = 0; east < TRACK_GRID_SIZE; east++) {
					assert((frame_lookup_world_tile(&tiles, east, south) < 0) ==
						   (frame_adaptive_flags(&frame_adaptive, east, south) ==
							FRAME_ADAPTIVE_HIDE));
				}
			}
		}
	}
	/* Returning to full view restores every world tile, including stale sparse entries. */
	frame_adaptive.quality = FRAME_ADAPTIVE_FULL_VIEW;
	frame_select_tiles(&tiles, &camera);
	assert(tiles.count == TRACK_GRID_SIZE * TRACK_GRID_SIZE);
	for (legacy_s16 south = 0; south < TRACK_GRID_SIZE; south++) {
		for (legacy_s16 east = 0; east < TRACK_GRID_SIZE; east++) {
			assert(frame_lookup_world_tile(&tiles, east, south) >= 0);
		}
	}
}

static void test_adaptive_multitile_boundary(void)
{
	adaptive_reset_scene();
	struct FRAME_CAMERA camera = adaptive_camera();
	struct FRAME_CAR_RENDER cars[2] = {{0}};
	cars[PLAYER_CAR_INDEX].east = cars[OPPONENT_CAR_INDEX].east = -1;
	const struct {
		legacy_s16 east_offset, south_offset, footprint;
		legacy_u8 expected;
	} cases[] = {
		{ADAPTIVE_TEST_L_COLUMN, -ADAPTIVE_TEST_H_FORWARD, FRAME_MULTITILE_COLUMN,
		 FRAME_ADAPTIVE_FULL},
		{ADAPTIVE_TEST_L_COLUMN, -ADAPTIVE_TEST_H_FORWARD, FRAME_MULTITILE_BOTH,
		 FRAME_ADAPTIVE_FULL},
		{0, -ADAPTIVE_TEST_LARGE_FORWARD - 1, FRAME_MULTITILE_ROW, FRAME_ADAPTIVE_LOW_GEOMETRY},
		{0, -ADAPTIVE_TEST_LARGE_FORWARD - 1, FRAME_MULTITILE_BOTH, FRAME_ADAPTIVE_LOW_GEOMETRY},
		{0, -ADAPTIVE_TEST_LARGE_FORWARD - 2, FRAME_MULTITILE_BOTH, FRAME_ADAPTIVE_HIDE}};
	for (legacy_u32 which = 0; which < sizeof(cases) / sizeof(cases[0]); which++) {
		memset(element_map, 0, sizeof(element_map));
		struct FRAME_TILE tile = {0};
		tile.element = ADAPTIVE_TEST_OBJECT;
		tile.east = ADAPTIVE_TEST_CAMERA_TILE + cases[which].east_offset;
		tile.south = ADAPTIVE_TEST_CAMERA_TILE + cases[which].south_offset;
		trkObjectList[tile.element].ss_multiTileFlag = cases[which].footprint;
		element_map[trackrows[tile.south] + tile.east] = tile.element;
		if ((cases[which].footprint & FRAME_MULTITILE_COLUMN) != 0) {
			element_map[trackrows[tile.south] + tile.east + 1] = TRACK_TILE_CONTINUATION_EAST;
		}
		if ((cases[which].footprint & FRAME_MULTITILE_ROW) != 0) {
			element_map[trackrows[tile.south + 1] + tile.east] = TRACK_TILE_CONTINUATION_SOUTH;
		}
		if (cases[which].footprint == FRAME_MULTITILE_BOTH) {
			element_map[trackrows[tile.south + 1] + tile.east + 1] =
				TRACK_TILE_CONTINUATION_SOUTHEAST;
		}
		assert(frame_adaptive_tile_flags(&tile) == cases[which].expected);
		struct FRAME_TILE_SELECTION tiles = {0};
		tiles.detail_threshold = FRAME_TILE_DETAIL_FULL + 1;
		mat_temp = *mat_rot_zxy(0, 0, 0, MATRIX_ROTATION_ORDER_YXZ);
		frame_select_tiles(&tiles, &camera);
		legacy_s16 selected = 0;
		for (legacy_s16 index = 0; index < tiles.count; index++) {
			if (tiles.markers[index] == FRAME_TILE_DRAW_MARKER &&
				tiles.elements[index] == tile.element) {
				assert(tiles.east[index] == tile.east && tiles.south[index] == tile.south);
				selected++;
			}
		}
		assert(selected == (cases[which].expected != FRAME_ADAPTIVE_HIDE));
		adaptive_reset_capture();
		struct GAMESTATE before = state;
		assert(frame_draw_tiles(&tiles, &camera, cars, 0, 0) == 0);
		assert(memcmp(&before, &state, sizeof(state)) == 0);
		assert(adaptive_captured_count == selected);
		if (selected != 0) {
			assert(adaptive_captured[0].shapeptr ==
				   &game3dshapes[cases[which].expected == FRAME_ADAPTIVE_FULL
									 ? ADAPTIVE_TEST_HIGH_SHAPE
									 : ADAPTIVE_TEST_LOW_SHAPE]);
		}
	}
}

static void test_adaptive_model_fallbacks(void)
{
	adaptive_reset_scene();
	struct FRAME_CAMERA camera = adaptive_camera();
	struct FRAME_CAR_RENDER cars[2] = {{0}};
	struct FRAME_TILE_SELECTION tiles = {0};
	struct FRAME_TILE tile = {0};
	tile.east = ADAPTIVE_TEST_CAMERA_TILE;
	tile.south = ADAPTIVE_TEST_CAMERA_TILE - ADAPTIVE_TEST_LARGE_FORWARD;
	tile.element = ADAPTIVE_TEST_OBJECT;
	tile.detail = ADAPTIVE_TEST_REDUCED_DETAIL;
	tile.adaptive_flags = FRAME_ADAPTIVE_LOW_GEOMETRY;
	trkObjectList[ADAPTIVE_TEST_OBJECT].ss_ssOvelay = ADAPTIVE_TEST_OVERLAY_OBJECT;
	struct TRACKOBJECT *overlay_object = &trkObjectList[ADAPTIVE_TEST_OVERLAY_OBJECT];
	overlay_object->ss_shapePtr = &game3dshapes[ADAPTIVE_TEST_OVERLAY_SHAPE];
	overlay_object->ss_ignoreZBias = FRAME_NO_DEPTH_SORT_FLAG;
	for (legacy_s16 missing = 0; missing <= 1; missing++) {
		adaptive_reset_capture();
		trkObjectList[ADAPTIVE_TEST_OBJECT].ss_loShapePtr =
			missing ? NULL : &game3dshapes[ADAPTIVE_TEST_LOW_SHAPE];
		overlay_object->ss_loShapePtr =
			missing ? NULL : &game3dshapes[ADAPTIVE_TEST_OVERLAY_LOW_SHAPE];
		legacy_s8 overlay = 0;
		assert(frame_add_track_element(&tile, &camera, cars, 0, 0, &overlay) == 0);
		assert(adaptive_captured_count == 2);
		assert(
			adaptive_captured[0].shapeptr ==
			&game3dshapes[missing ? ADAPTIVE_TEST_OVERLAY_SHAPE : ADAPTIVE_TEST_OVERLAY_LOW_SHAPE]);
		assert(adaptive_captured[1].shapeptr ==
			   &game3dshapes[missing ? ADAPTIVE_TEST_HIGH_SHAPE : ADAPTIVE_TEST_LOW_SHAPE]);
	}
	tile.element = 0;
	tile.east = 0;
	tile.south = ADAPTIVE_TEST_CAMERA_TILE;
	frame_adaptive.tile_flags[tile.south * TRACK_GRID_SIZE + tile.east] =
		FRAME_ADAPTIVE_LOW_GEOMETRY;
	legacy_s16 fence = fence_by_edge[FRAME_FENCE_EDGE_LOW][FRAME_FENCE_EDGE_INTERIOR];
	struct TRACKOBJECT *fence_object =
		frame_track_object_from_legacy_index(fence_TrkObjCodes[fence]);
	fence_object->ss_shapePtr = &game3dshapes[ADAPTIVE_TEST_HIGH_SHAPE];
	for (legacy_s16 missing = 0; missing <= 1; missing++) {
		adaptive_reset_capture();
		fence_object->ss_loShapePtr = missing ? NULL : &game3dshapes[ADAPTIVE_TEST_LOW_SHAPE];
		assert(frame_draw_fences(&tile, &tiles, &camera, 0) == 0);
		assert(adaptive_captured_count == 1);
		assert(adaptive_captured[0].shapeptr ==
			   &game3dshapes[missing ? ADAPTIVE_TEST_HIGH_SHAPE : ADAPTIVE_TEST_LOW_SHAPE]);
	}
	adaptive_capture_enabled = 0;
	frame_adaptive_reset(&frame_adaptive);
	frame_adaptive_active = 0;
	shadow_frame_active = 0;
	supersight_enabled = 0;
}

static void test_adaptive_world_tile_boundaries(void)
{
	const legacy_s32 minimum_position = -(legacy_s32)LEGACY_S32_MAX - 1;
	const struct {
		legacy_s32 position;
		legacy_s16 offset;
		legacy_s32 expected;
	} cases[] = {{TEST_LOOKUP_WORLD_TILE_SIZE - 1, 1, 1},
				 {TEST_LOOKUP_WORLD_TILE_SIZE, -1, 0},
				 {-1, 1, 0},
				 {0, -1, -1},
				 {-TEST_LOOKUP_WORLD_TILE_SIZE, -1, -2},
				 {-TEST_LOOKUP_WORLD_TILE_SIZE - 1, 1, -1},
				 {TRACK_GRID_SIZE * TEST_LOOKUP_WORLD_TILE_SIZE, -1, TRACK_GRID_LAST_COORDINATE},
				 {TRACK_GRID_SIZE * TEST_LOOKUP_WORLD_TILE_SIZE - 1, 1, TRACK_GRID_SIZE},
				 {LEGACY_S32_MAX, 1, LEGACY_S32_MAX / TEST_LOOKUP_WORLD_TILE_SIZE + 1},
				 {minimum_position, -1, minimum_position / TEST_LOOKUP_WORLD_TILE_SIZE - 1}};
	for (legacy_u16 which = 0; which < sizeof(cases) / sizeof(cases[0]); which++) {
		assert(frame_adaptive_world_tile(cases[which].position, cases[which].offset) ==
			   cases[which].expected);
	}
}

static void test_adaptive_car_submission_near_track_edge(void)
{
	/* A rotated car sits just in front of the helicopter camera near the east
	 * edge. Its real wheel footprint occupies one tile; treating fixed-point
	 * offsets as whole units moves every wheel outside the selected tiles. */
	for (legacy_u16 quality = FRAME_ADAPTIVE_FULL_VIEW; quality <= FRAME_ADAPTIVE_SMALL_VIEW;
		 quality++) {
		adaptive_reset_scene();
		memset(&simd_player, 0, sizeof(simd_player));
		frame_adaptive.quality = quality;
		frame_adaptive_active = quality != FRAME_ADAPTIVE_FULL_VIEW;
		/* Omit unrelated border fences from this empty-track fixture. */
		detail_level = FRAME_DETAIL_FASTEST;
		cameramode = CAMERA_MODE_FOLLOW;
		followOpponentFlag = 0;
		state.playerstate.car_position.lx =
			(legacy_s32)ADAPTIVE_TEST_EDGE_CAR_X * ADAPTIVE_TEST_POSITION_SCALE;
		state.playerstate.car_position.lz =
			(legacy_s32)ADAPTIVE_TEST_EDGE_CAR_Z * ADAPTIVE_TEST_POSITION_SCALE;
		state.playerstate.car_rotate.x = ADAPTIVE_TEST_EDGE_CAR_HEADING;
		for (legacy_s16 wheel = 0; wheel < FRAME_CAR_WHEEL_COUNT; wheel++) {
			simd_player.wheel_coords[wheel].x =
				wheel & 1 ? ADAPTIVE_TEST_CAR_HALF_WIDTH : -ADAPTIVE_TEST_CAR_HALF_WIDTH;
			simd_player.wheel_coords[wheel].z =
				wheel & 2 ? ADAPTIVE_TEST_CAR_HALF_LENGTH : -ADAPTIVE_TEST_CAR_HALF_LENGTH;
		}
		trkObjectList[FRAME_PLAYER_SORT_ID].ss_shapePtr = &game3dshapes[PLAYER_CAR_WHEEL_SHAPE];
		trkObjectList[FRAME_PLAYER_SORT_ID].ss_loShapePtr = &game3dshapes[PLAYER_CAR_LOW_SHAPE];
		struct FRAME_CAMERA camera = {0};
		camera.position.x = ADAPTIVE_TEST_EDGE_CAR_X - ADAPTIVE_TEST_EDGE_CAMERA_X_OFFSET;
		camera.position.y = ADAPTIVE_TEST_EDGE_CAMERA_HEIGHT;
		camera.position.z = ADAPTIVE_TEST_EDGE_CAR_Z - ADAPTIVE_TEST_EDGE_CAMERA_Z_OFFSET;
		camera.view_heading = ADAPTIVE_TEST_EDGE_VIEW_HEADING;
		mat_temp = *mat_rot_zxy(0, 0, camera.view_heading, MATRIX_ROTATION_ORDER_YXZ);
		struct FRAME_TILE_SELECTION tiles = {0};
		tiles.detail_threshold = FRAME_TILE_DETAIL_FULL + 1;
		struct FRAME_CAR_RENDER cars[GAME_CAR_COUNT] = {{0}};
		struct GAMESTATE before = state;
		frame_select_tiles(&tiles, &camera);
		assert(frame_lookup_world_tile(&tiles, ADAPTIVE_TEST_EDGE_CAR_EAST,
									   ADAPTIVE_TEST_EDGE_CAR_SOUTH) >= 0);
		assert(frame_adaptive_car_flags(&state.playerstate, &simd_player) == FRAME_ADAPTIVE_FULL);
		frame_place_cars(&tiles, cars);
		assert(cars[PLAYER_CAR_INDEX].east == ADAPTIVE_TEST_EDGE_CAR_EAST);
		assert(cars[PLAYER_CAR_INDEX].south == ADAPTIVE_TEST_EDGE_CAR_SOUTH);
		assert(frame_draw_tiles(&tiles, &camera, cars, 0, 0) == 0);
		assert(memcmp(&before, &state, sizeof(state)) == 0);
		assert(adaptive_captured_count == 1);
		assert(adaptive_captured[0].shapeptr == &game3dshapes[PLAYER_CAR_WHEEL_SHAPE]);
	}
	adaptive_capture_enabled = 0;
	frame_adaptive_reset(&frame_adaptive);
	frame_adaptive_active = shadow_frame_active = supersight_enabled = 0;
}

static void test_adaptive_car_footprint_and_fallback(void)
{
	adaptive_reset_scene();
	memset(&simd_player, 0, sizeof(simd_player));
	state.playerstate.car_position.lx = ADAPTIVE_TEST_CAMERA_TILE * TEST_LOOKUP_WORLD_TILE_SIZE;
	state.playerstate.car_position.lz =
		(TRACK_GRID_LAST_COORDINATE - ADAPTIVE_TEST_CAMERA_TILE) * TEST_LOOKUP_WORLD_TILE_SIZE;
	struct TRACKOBJECT *object = &trkObjectList[FRAME_PLAYER_SORT_ID];
	object->ss_shapePtr = &game3dshapes[PLAYER_CAR_WHEEL_SHAPE];
	struct FRAME_CAMERA camera = adaptive_camera();
	struct RECTANGLE crash_rect = {0};
	const legacy_u8 flags[] = {FRAME_ADAPTIVE_HIDE, FRAME_ADAPTIVE_LOW_GEOMETRY,
							   FRAME_ADAPTIVE_FULL};
	for (legacy_u32 which = 0; which < sizeof(flags) / sizeof(flags[0]); which++) {
		memset(frame_adaptive.tile_flags, FRAME_ADAPTIVE_HIDE, sizeof(frame_adaptive.tile_flags));
		frame_adaptive
			.tile_flags[ADAPTIVE_TEST_CAMERA_TILE * TRACK_GRID_SIZE + ADAPTIVE_TEST_CAMERA_TILE] =
			flags[which];
		for (legacy_s16 missing = 0; missing <= 1; missing++) {
			adaptive_reset_capture();
			object->ss_loShapePtr = missing ? NULL : &game3dshapes[PLAYER_CAR_LOW_SHAPE];
			struct GAMESTATE before = state;
			frame_add_car(&state.playerstate, PLAYER_CAR_INDEX, FRAME_PLAYER_SORT_ID,
						  &game3dshapes[PLAYER_CAR_WHEEL_SHAPE], player_wheel_vertex_state,
						  player_base_wheel_vertices, player_front_wheel_centers,
						  &frame_player_car_rect, &crash_rect, &camera.position,
						  ADAPTIVE_TEST_REDUCED_DETAIL, 0, 0, 0);
			assert(memcmp(&before, &state, sizeof(state)) == 0);
			assert(transformedshape_counter == (flags[which] != FRAME_ADAPTIVE_HIDE));
			if (transformedshape_counter != 0) {
				assert(currenttransshape[0].shapeptr ==
					   &game3dshapes[flags[which] == FRAME_ADAPTIVE_LOW_GEOMETRY && !missing
										 ? PLAYER_CAR_LOW_SHAPE
										 : PLAYER_CAR_WHEEL_SHAPE]);
			}
		}
	}
	/* The center is one world unit outside, but a two-unit wheel offset
	 * crosses into H. Positions and wheel vectors both have six fractional bits. */
	state.playerstate.car_position.lx += ADAPTIVE_TEST_POSITION_SCALE;
	memset(frame_adaptive.tile_flags, FRAME_ADAPTIVE_HIDE, sizeof(frame_adaptive.tile_flags));
	frame_adaptive
		.tile_flags[ADAPTIVE_TEST_CAMERA_TILE * TRACK_GRID_SIZE + ADAPTIVE_TEST_CAMERA_TILE - 1] =
		FRAME_ADAPTIVE_FULL;
	simd_player.wheel_coords[0].x = -ADAPTIVE_TEST_POSITION_SCALE / 2;
	assert(frame_adaptive_car_flags(&state.playerstate, &simd_player) == FRAME_ADAPTIVE_HIDE);
	simd_player.wheel_coords[0].x = -ADAPTIVE_TEST_WHEEL_CROSSING_OFFSET;
	assert(frame_adaptive_car_flags(&state.playerstate, &simd_player) == FRAME_ADAPTIVE_FULL);
	frame_adaptive
		.tile_flags[ADAPTIVE_TEST_CAMERA_TILE * TRACK_GRID_SIZE + ADAPTIVE_TEST_CAMERA_TILE - 1] =
		FRAME_ADAPTIVE_LOW_GEOMETRY;
	assert(frame_adaptive_car_flags(&state.playerstate, &simd_player) ==
		   FRAME_ADAPTIVE_LOW_GEOMETRY);
	/* Placement must use the same real wheel coordinates as the mask policy. */
	cameramode = CAMERA_MODE_FOLLOW;
	mat_temp = *mat_rot_zxy(0, 0, 0, MATRIX_ROTATION_ORDER_YXZ);
	struct FRAME_TILE_SELECTION tiles = {0};
	frame_select_tiles(&tiles, &camera);
	struct FRAME_CAR_RENDER cars[GAME_CAR_COUNT] = {{0}};
	struct GAMESTATE before = state;
	frame_place_cars(&tiles, cars);
	assert(cars[PLAYER_CAR_INDEX].east == ADAPTIVE_TEST_CAMERA_TILE - 1);
	assert(cars[PLAYER_CAR_INDEX].south == ADAPTIVE_TEST_CAMERA_TILE);
	assert(memcmp(&before, &state, sizeof(state)) == 0);
	/* Adaptive and classic placement agree on the physical wheel footprint. */
	frame_adaptive_active = 0;
	frame_place_cars(&tiles, cars);
	assert(cars[PLAYER_CAR_INDEX].east == ADAPTIVE_TEST_CAMERA_TILE - 1);
	assert(cars[PLAYER_CAR_INDEX].south == ADAPTIVE_TEST_CAMERA_TILE);
	adaptive_capture_enabled = 0;
	frame_adaptive_reset(&frame_adaptive);
	frame_adaptive_active = shadow_frame_active = supersight_enabled = 0;
}

static void test_adaptive_detached_particles(void)
{
	struct VECTOR sign_positions[GAMESTATE_BREAKABLE_OBJECT_COUNT] = {{0}};
	struct VECTOR *saved_sign_positions = roadside_sign_positions;
	roadside_sign_positions = sign_positions;
	static const legacy_s16 owners[] = {PLAYER_CAR_INDEX, OPPONENT_CAR_INDEX,
										FRAME_CHECKPOINT_OWNER_OFFSET +
											ADAPTIVE_TEST_PARTICLE_SIGN};
	static const legacy_s16 east_offsets[] = {0, 0, ADAPTIVE_TEST_PARTICLE_OWNER_DISTANCE};
	static const legacy_s16 forward_offsets[] = {0, ADAPTIVE_TEST_LARGE_FORWARD, 0};
	static const legacy_u8 expected_flags[] = {FRAME_ADAPTIVE_FULL, FRAME_ADAPTIVE_LOW_GEOMETRY,
											   FRAME_ADAPTIVE_HIDE};
	for (legacy_u16 kind = 0; kind < sizeof(owners) / sizeof(owners[0]); kind++) {
		for (legacy_u16 view = 0; view < sizeof(expected_flags) / sizeof(expected_flags[0]);
			 view++) {
			for (legacy_s16 missing = 0; missing <= 1; missing++) {
				adaptive_reset_scene();
				struct FRAME_CAMERA camera = adaptive_camera();
				struct FRAME_CAR_RENDER cars[GAME_CAR_COUNT] = {{0}};
				memset(&simd_player, 0, sizeof(simd_player));
				memset(&simd_opponent, 0, sizeof(simd_opponent));
				gameconfig.game_opponenttype = 1;
				gameconfig.game_playermaterial = ADAPTIVE_TEST_PARTICLE_PLAYER_MATERIAL;
				gameconfig.game_opponentmaterial = ADAPTIVE_TEST_PARTICLE_OPPONENT_MATERIAL;
				legacy_s16 owner = owners[kind];
				legacy_s16 owner_x = camera.position.x + ADAPTIVE_TEST_PARTICLE_OWNER_DISTANCE *
															 ADAPTIVE_TEST_WORLD_TILE_SIZE;
				legacy_s16 target_x = camera.position.x +
									  east_offsets[view] * ADAPTIVE_TEST_WORLD_TILE_SIZE +
									  ADAPTIVE_TEST_WORLD_TILE_SIZE / 2;
				legacy_s16 target_z = camera.position.z +
									  forward_offsets[view] * ADAPTIVE_TEST_WORLD_TILE_SIZE +
									  ADAPTIVE_TEST_WORLD_TILE_SIZE / 2;
				state.game_particles_active = 1;
				state.game_particle_forward_speed[0] = 1;
				state.game_particle_owner[0] = owner;
				state.game_particle_shape_index[0] = 0;
				state.game_particle_x[0] = (target_x - owner_x) * ADAPTIVE_TEST_POSITION_SCALE;
				state.game_particle_y[0] =
					ADAPTIVE_TEST_PARTICLE_HEIGHT * ADAPTIVE_TEST_POSITION_SCALE;
				state.game_particle_z[0] =
					(target_z - camera.position.z) * ADAPTIVE_TEST_POSITION_SCALE;
				struct VECTOR expected_position = {target_x - camera.position.x,
												   ADAPTIVE_TEST_PARTICLE_HEIGHT,
												   target_z - camera.position.z};
				legacy_s16 material = 0;
				if (owner < FRAME_CHECKPOINT_OWNER_OFFSET) {
					struct CARSTATE *car =
						owner == PLAYER_CAR_INDEX ? &state.playerstate : &state.opponentstate;
					car->car_position.lx = (legacy_s32)owner_x * ADAPTIVE_TEST_POSITION_SCALE;
					car->car_position.lz =
						(legacy_s32)camera.position.z * ADAPTIVE_TEST_POSITION_SCALE;
					for (legacy_u16 wheel = 0; wheel < FRAME_CAR_WHEEL_COUNT; wheel++) {
						car->car_surfaceWhl[wheel] = CAR_SURFACE_PAVED;
					}
					car_ground_offsets[owner] = ADAPTIVE_TEST_PARTICLE_GROUND_OFFSET;
					assert(frame_adaptive_car_flags(
							   car, owner == PLAYER_CAR_INDEX ? &simd_player : &simd_opponent) ==
						   FRAME_ADAPTIVE_HIDE);
					expected_position.y -=
						ADAPTIVE_TEST_PARTICLE_GROUND_OFFSET + FRAME_NON_GRASS_HEIGHT_OFFSET;
					material = owner == PLAYER_CAR_INDEX ? ADAPTIVE_TEST_PARTICLE_PLAYER_MATERIAL
														 : ADAPTIVE_TEST_PARTICLE_OPPONENT_MATERIAL;
				} else {
					state.game_object_destroyed[ADAPTIVE_TEST_PARTICLE_SIGN] = 1;
					roadside_sign_positions[ADAPTIVE_TEST_PARTICLE_SIGN] =
						(struct VECTOR){owner_x, 0, camera.position.z};
					assert(frame_adaptive_flags(&frame_adaptive,
												ADAPTIVE_TEST_CAMERA_TILE +
													ADAPTIVE_TEST_PARTICLE_OWNER_DISTANCE,
												ADAPTIVE_TEST_CAMERA_TILE) == FRAME_ADAPTIVE_HIDE);
				}
				particle_scene_objects[0].ss_shapePtr = &game3dshapes[ADAPTIVE_TEST_HIGH_SHAPE];
				particle_scene_objects[0].ss_loShapePtr =
					missing ? NULL : &game3dshapes[ADAPTIVE_TEST_LOW_SHAPE];
				mat_temp = *mat_rot_zxy(0, 0, 0, MATRIX_ROTATION_ORDER_YXZ);
				struct GAMESTATE before = state;
				assert(frame_draw_adaptive_particles(&camera, cars, 0) == 0);
				assert(memcmp(&before, &state, sizeof(state)) == 0);
				assert(adaptive_captured_count == (expected_flags[view] != FRAME_ADAPTIVE_HIDE));
				if (adaptive_captured_count != 0) {
					assert(adaptive_captured[0].shapeptr ==
						   &game3dshapes[expected_flags[view] == FRAME_ADAPTIVE_LOW_GEOMETRY &&
												 !missing
											 ? ADAPTIVE_TEST_LOW_SHAPE
											 : ADAPTIVE_TEST_HIGH_SHAPE]);
					assert(memcmp(&adaptive_captured[0].pos, &expected_position,
								  sizeof(expected_position)) == 0);
					assert(adaptive_captured[0].material == material);
					assert(((adaptive_captured[0].ts_flags & SHAPE3D_NO_SHADOW_RECEIVE_FLAG) !=
							0) == (owner < FRAME_CHECKPOINT_OWNER_OFFSET));
				}
				/* The independent pass does no work at full quality/classic. */
				frame_adaptive_active = 0;
				adaptive_reset_capture();
				assert(frame_draw_adaptive_particles(&camera, cars, 0) == 0);
				assert(adaptive_captured_count == 0);
			}
		}
	}
	roadside_sign_positions = saved_sign_positions;
	adaptive_capture_enabled = 0;
	frame_adaptive_reset(&frame_adaptive);
	frame_adaptive_active = shadow_frame_active = supersight_enabled = 0;
}

#endif

#if !defined(RESTUNTS_SDL3)
static void test_racing_line_queue_exhaustion(void)
{
	struct FRAME_LOOKAHEAD_TILE lookahead[FRAME_SUPERSIGHT_TILE_COUNT] = {{0}};
	struct FRAME_TILE_SELECTION tiles = {0};
	struct FRAME_CAMERA camera = {0};
	struct FRAME_CAR_RENDER cars[2] = {{0}};
	configure_track();
	memset(&state, 0, sizeof(state));
	memset(&ghost_fixture, 0, sizeof(ghost_fixture));
	frame_state = &state;
	gameconfig.game_opponenttype = 0;
	cameramode = CAMERA_MODE_COCKPIT;
	followOpponentFlag = 0;
	detail_level = 1;
	start_finish_column = -1;
	camera.position = (struct VECTOR){15 * 1024, 0, 14 * 1024};
	tiles.lookahead = lookahead;
	tiles.count = FRAME_SUPERSIGHT_TILE_COUNT;
	for (legacy_s16 index = 0; index < tiles.count; index++) {
		tiles.markers[index] = FRAME_TILE_UNAVAILABLE_MARKER;
	}
	for (legacy_s16 index = 0; index < 3; index++) {
		tiles.markers[index] = FRAME_TILE_DRAW_MARKER;
		tiles.east[index] = 15;
		tiles.south[index] = 15 + index;
		tiles.terrain[index] = 1;
	}
	ghost_fixture_active = racing_line_capture_enabled = 1;
	racing_line_transform_result = 0;
	fixed_queue_capacity = 4;
	for (legacy_s16 mode = 0; mode < 2; mode++) {
		supersight_enabled = mode;
		for (legacy_s16 exhaustion = 0; exhaustion < 3; exhaustion++) {
			/* Fill the queue in guidance, in a later world shape, or in the last
			 * line after tiles report success. Every case must restore all tiles. */
			racing_line_segment_count = exhaustion == 1 ? 5 : 1;
			for (legacy_u32 index = 0; index < racing_line_segment_count; index++) {
				racing_line_segments[index] = (struct GHOST_RACING_LINE_SEGMENT){0};
				racing_line_segments[index].next = index + 1;
				racing_line_east[index] = 15;
				racing_line_south[index] = exhaustion == 2 ? 17 : 15;
			}
			reset_shapes();
			polyinfo_reset();
			queue_resets = 0;
			racing_line_batch_count = 0;
			tiles.first = 0;
			struct GAMESTATE saved = state;
			if (mode != 0) {
				frame_supersight_reset();
				frame_draw_supersight(&tiles, &camera, cars, 0, 0, 0);
				assert(supersight_attempt_hint == 0);
			} else {
				frame_draw_classic(&tiles, &camera, cars, 0, 0, 0);
			}
			assert(queue_resets == 1 && racing_line_batch_count == 1);
			assert(transform_count == 3 && fixed_queue_cost == 3);
			assert(polygon_buffer_full == 0 && tiles.first == 0);
			assert(frame_racing_line_enabled != 0);
			assert(memcmp(&saved, &state, sizeof(saved)) == 0);
		}
	}
	fixed_queue_capacity = 0;
	ghost_fixture_active = racing_line_capture_enabled = supersight_enabled = 0;
	racing_line_segment_count = 0;
}
#endif

static void test_racing_line_batches_and_sorting(void)
{
	struct FRAME_CAR_RENDER cars[2] = {0};
	struct FRAME_CAMERA camera = {0};
	camera.position = (struct VECTOR){4000, 100, 7000};
	struct FRAME_TILE tile = {0};
	tile.east = tile.last_east = 4;
	tile.south = tile.last_south = 22;
	tile.element = 1;
	tile.position = (struct VECTOR){500, -100, 1000};
	struct GAMESTATE presentation = {0};
	presentation.playerstate.car_actual_speed = 60 * 256;
	frame_state = &presentation;
	gameconfig.game_opponenttype = 0;
	racing_line_capture_enabled = 1;
	racing_line_segment_count = RACING_LINE_TEST_SEGMENTS;
	for (legacy_u32 index = 0; index < racing_line_segment_count; index++) {
		struct GHOST_RACING_LINE_SEGMENT *segment = &racing_line_segments[index];
		segment->start_left = (struct VECTORLONG){4498 * 64, (legacy_s32)index * 64,
												  (8000 + (legacy_s32)index * 33) * 64};
		segment->start_right = segment->start_left;
		segment->start_right.lx += 4 * 64;
		segment->end_left = segment->start_left;
		segment->end_left.lz += 33 * 64;
		segment->end_right = segment->start_right;
		segment->end_right.lz += 33 * 64;
		segment->speed = (legacy_u16)((50 + index % 3 * 10) * 256);
		segment->next = index + 1;
		racing_line_east[index] = tile.east;
		racing_line_south[index] = tile.south;
	}
	reset_shapes();
	transform_capacity = 0;
	racing_line_bucket_reads = racing_line_batch_count = 0;
	ghost_fixture_active = 0;
	assert(frame_draw_sorted_shapes(cars, &tile, &camera, 8) == 0);
	assert(racing_line_bucket_reads == 0 && racing_line_batch_count == 0);
	ghost_fixture_active = 1;
	for (legacy_s16 mode = 0; mode < 2; mode++) {
		supersight_enabled = mode;
		racing_line_batch_count = 0;
		struct GAMESTATE saved = presentation;
		assert(frame_draw_sorted_shapes(cars, &tile, &camera, 8) == 0);
		assert(memcmp(&saved, &presentation, sizeof(saved)) == 0);
		assert(racing_line_batch_count == 3);
		legacy_u32 segments = 0;
		static const legacy_u8 expected_paints[] = {12, 14, 10};
		for (legacy_u32 batch = 0; batch < racing_line_batch_count; batch++) {
			assert(racing_line_batches[batch].count == (batch == 2 ? 5 : 60));
			assert(racing_line_batches[batch].instance.rectptr == &frame_sorted_shapes_rect);
			assert((racing_line_batches[batch].instance.ts_flags &
					(SHAPE3D_NO_SHADOW_RECEIVE_FLAG | 8)) == (SHAPE3D_NO_SHADOW_RECEIVE_FLAG | 8));
			for (legacy_u32 segment = 0; segment < racing_line_batches[batch].count; segment++) {
				assert(racing_line_batches[batch].paints[segment] == expected_paints[segments % 3]);
				const struct VECTOR *vertices = racing_line_batches[batch].vertices;
				assert(vertices[8 + segment * 4].x == -2);
				assert(vertices[9 + segment * 4].x == 2);
				assert(vertices[8 + segment * 4].y == (legacy_s16)segments);
				assert(vertices[8 + segment * 4].z == (legacy_s16)(segments * 33));
				for (legacy_u32 corner = 0; corner < 4; corner++) {
					const struct VECTOR *vertex = &vertices[8 + segment * 4 + corner];
					assert(vertex->x >= vertices[0].x && vertex->x <= vertices[7].x);
					assert(vertex->y >= vertices[0].y && vertex->y <= vertices[7].y);
					assert(vertex->z >= vertices[0].z && vertex->z <= vertices[7].z);
				}
				segments++;
			}
		}
		assert(segments == RACING_LINE_TEST_SEGMENTS);
	}

	/* A multitile piece must retain guidance in all of its covered cells. */
	racing_line_segment_count = 4;
	tile.last_east++;
	tile.last_south++;
	for (legacy_u32 index = 0; index < racing_line_segment_count; index++) {
		racing_line_east[index] = tile.east + (index & 1U);
		racing_line_south[index] = tile.south + (index >> 1);
		racing_line_segments[index].next = RACING_LINE_TEST_SEGMENTS;
	}
	racing_line_batch_count = 0;
	assert(frame_draw_sorted_shapes(cars, &tile, &camera, 0) == 0);
	assert(racing_line_batch_count == 4);

	/* Guidance merges after the road and before a nearer car without consuming
	 * any of the fixed shape slots. A failed optional batch preserves the car. */
	tile.last_east = tile.east;
	tile.last_south = tile.south;
	racing_line_segment_count = 1;
	for (legacy_s16 mode = 0; mode < 2; mode++) {
		supersight_enabled = mode;
		legacy_s16 expected_order = 2;
#if defined(RESTUNTS_SDL3)
		if (mode != 0) {
			expected_order = 1;
		}
#endif
		for (legacy_s16 result = -1; result <= 1; result++) {
			reset_shapes();
			racing_line_batch_count = 0;
			racing_line_transform_result = result;
			transformedshape_counter = 29;
			for (legacy_s16 index = 0; index < 29; index++) {
				currenttransshape[index].shapeptr = &game3dshapes[index];
				transformed_shape_sort_types[index] = 0;
				transformedshape_indices[index] = index;
				transformedshape_zarray[index] = index == 0 ? 1000 : 700 - index;
			}
			assert(frame_draw_sorted_shapes(cars, &tile, &camera, 0) == 0);
			assert(transformedshape_counter == 29 && transform_count == 30);
			assert(racing_line_batch_count == 1 && racing_line_batches[0].order == expected_order);
		}
	}
	supersight_enabled = 0;
	racing_line_capture_enabled = ghost_fixture_active = 0;
	racing_line_segment_count = 0;
	racing_line_transform_result = 0;
	frame_state = &state;
}

legacy_int main(void)
{
	test_camera_modes();
	test_covered_tiles();
	test_tile_selection();
	test_terrain_exhaustion();
	test_sorted_shapes();
	test_track_elements_and_flags();
	/* The model indices match the DOS resource table independently of native
	 * pointer width: camera modes, tile selection, queue exhaustion, sorted
	 * brake paint, component geometry and animated start-flag vertices. */
#ifdef FRAME_RECORD_BASELINE
	printf("Frame rendering fingerprint: %016" LEGACY_PRIx64 "\n", trace_hash);
#else
	assert(trace_hash == UINT64_C(0x1f0a24dd55dcfc37));
#endif
	test_dashboard_top_layers();
	test_invalid_multitile_flags();
	test_ghost_uses_independent_visual_state();
	test_ghost_camera_modes();
	test_snapshot_uses_authoritative_events();
	test_supersight_selection();
	test_supersight_capacity_retries();
#if defined(RESTUNTS_SDL3)
	test_complete_tile_lookup_matches_scans();
	test_supersight_car_shadows();
	test_supersight_grounding_surfaces();
	test_supersight_grounding_poses();
	test_supersight_covered_water();
	test_adaptive_selection_before_submission();
	test_adaptive_multitile_boundary();
	test_adaptive_model_fallbacks();
	test_adaptive_world_tile_boundaries();
	test_adaptive_car_submission_near_track_edge();
	test_adaptive_car_footprint_and_fallback();
	test_adaptive_detached_particles();
#endif
	test_racing_line_batches_and_sorting();
#if !defined(RESTUNTS_SDL3)
	test_racing_line_queue_exhaustion();
#endif
	puts("Frame rendering snapshots, ghost isolation and SuperSight passed.");
	return 0;
}
