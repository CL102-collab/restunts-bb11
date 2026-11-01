#include <assert.h>
#include <string.h>

#include "../c/externs.h"
#include "../c/shape2d/shape2d.h"
#include "../c/shape2d/shape2d_internal.h"
#include "../c/shape3d/shape3d.h"
#include "../c/shape3d/shape3d_internal.h"
#include "../c/projection.h"

/* Exercise the stopped-wheel handoff without exporting its private geometry helpers. */
#include "../c/stateply/stateply.c"
#include "../c/stateply/stateply_motion.c"
#include "../c/stateply/stateply_contact.c"

#undef memcpy

static legacy_u32 solid_calls;
static legacy_u16 expected_polygon_vertices = 3;
static legacy_u32 ghost_calls;
static legacy_u32 checking_ghost;
static legacy_s16 colors[4] = {7, 8, 9, 10};
static legacy_s16 patterns[3];
static legacy_s16 secondary_patterns[3];
static legacy_s16 secondary_colors[3] = {19, 20, 21};
static legacy_u8 polygon_record[22] = {0, 0, 0, 3, RENDER_PRIMITIVE_POLYGON, 0};

void preRender_default(legacy_u16 color, legacy_u16 count, const struct POINT2D *points)
{
	assert(count == expected_polygon_vertices && points != 0);
	if (count == 15) {
		assert(points[14].px == 123 && points[14].py == 45);
	}
	if (checking_ghost != 0U) {
		assert(color == PRERENDER_GHOST_COLOR);
		ghost_calls++;
	} else {
		assert(color == 7);
		solid_calls++;
	}
}

void preRender_patterned(legacy_u16 pattern, legacy_u16 color, legacy_u16 count,
						 const struct POINT2D *points)
{
	(void)pattern;
	(void)color;
	(void)count;
	(void)points;
}

void preRender_two_color(legacy_u16 pattern, legacy_u16 color, legacy_u16 alternate_color,
						 legacy_u16 count, const struct POINT2D *points)
{
	(void)pattern;
	(void)color;
	(void)alternate_color;
	(void)count;
	(void)points;
}

void preRender_line(legacy_u16 x1, legacy_u16 y1, legacy_u16 x2, legacy_u16 y2, legacy_u16 color)
{
	if (checking_ghost != 0U) {
		assert(color == PRERENDER_GHOST_COLOR);
		ghost_calls++;
	}
	(void)x1;
	(void)y1;
	(void)x2;
	(void)y2;
	(void)color;
}

void preRender_sphere(legacy_s16 x, legacy_s16 y, legacy_u16 size, legacy_u16 color)
{
	if (checking_ghost != 0U) {
		assert(color == PRERENDER_GHOST_COLOR);
		ghost_calls++;
	}
	(void)x;
	(void)y;
	(void)size;
	(void)color;
}

void preRender_wheel(const struct POINT2D *points, legacy_u16 scale, legacy_u16 outer_color,
					 legacy_u16 inner_color, legacy_u16 hub_color)
{
	if (checking_ghost != 0U) {
		assert(outer_color == PRERENDER_GHOST_COLOR && inner_color == PRERENDER_GHOST_COLOR &&
			   hub_color == PRERENDER_GHOST_COLOR);
		ghost_calls++;
	}
	(void)points;
	(void)scale;
	(void)outer_color;
	(void)inner_color;
	(void)hub_color;
}

void sprite_putpixel_clipped(legacy_s16 x, legacy_s16 y, legacy_s16 color)
{
	(void)x;
	(void)y;
	(void)color;
}

static void queue_polygon(legacy_s16 pattern)
{
	material_clrlist_ptr_cpy = colors;
	material_patlist_ptr_cpy = patterns;
	material_patlist2_ptr_cpy = secondary_patterns;
	material_clrlist2_ptr_cpy = secondary_colors;
	patterns[0] = pattern;
	polygon_record[4] = RENDER_PRIMITIVE_POLYGON;
	polyinfo_reset();
	polyinfonumpolys = 1;
	polygon_next_index[400] = 0;
	polyinfoptr = polygon_record;
	polygon_record_offsets[0] = 0;
}

static void assert_headings(const legacy_s16 *headings, legacy_s16 a, legacy_s16 b, legacy_s16 c,
							legacy_s16 d)
{
	assert(headings[0] == a && headings[1] == b && headings[2] == c && headings[3] == d);
}

static legacy_u8 opponent_polyinfo[64];

static void queue_opponent_primitive(legacy_u8 primitive, legacy_s16 pattern)
{
	queue_polygon(pattern);
	memcpy(opponent_polyinfo + 24, polygon_record, sizeof(polygon_record));
	opponent_polyinfo[24 + 4] = primitive;
	polyinfoptr = opponent_polyinfo;
	polygon_record_offsets[0] = 24;
}

static void assert_stopped_opponent_travel(legacy_s16 expected_x, legacy_s16 expected_z)
{
	struct PLAYER_WHEEL_MOTION motion;

	memset(&motion, 0, sizeof(motion));
	restore_stopped_wheel_headings(&state.opponentstate, &motion, OPPONENT_CAR_INDEX);
	car_to_world_rotation = *mat_rot_zxy(0, 0, 0, MATRIX_ROTATION_ORDER_ZXY);
	planindex = -1;
	prepare_wheel_plane_travel(&motion, 2, 64);
	assert(wheel_world_travel.x == expected_x);
	assert(wheel_world_travel.y == 0);
	assert(wheel_world_travel.z == expected_z);
	motion.travel = 1;
	motion.headings[2] = 75;
	restore_stopped_wheel_headings(&state.opponentstate, &motion, OPPONENT_CAR_INDEX);
	assert(motion.headings[2] == 75);
}

static void test_opponent_render_handoff(void)
{
	legacy_s16 *headings = legacy_execution_residue.wheel_angle_stack_words;

	headings[0] = 11;
	headings[1] = 22;
	headings[2] = 33;
	headings[3] = 44;
	struct SHAPE3D_LEGACY_OPPONENT_RENDER_CONTEXT context;
	context.wheel_headings = headings;
	context.polyinfo_offset = 65500U;
	context.polyinfo_segment = 256U;
	context.material_color_offset = 20628U;
	shape3d_set_legacy_render_stack(0, 0, 0, &context);
	/* The setter copies the context while retaining the caller's word buffer. */
	context.polyinfo_segment = 512U;
	polyinfo_reset();
	shape3d_render_queued_primitives();
	assert_headings(headings, 11, 22, 33, 44);

	/* Even an invisible polygon copies its vertices, advances the far offset,
	 * and leaves the segment unchanged when that offset wraps. */
	queue_opponent_primitive(RENDER_PRIMITIVE_POLYGON, 3);
	shape3d_render_queued_primitives();
	assert_headings(headings, 11, 6, 256, 44);
	assert_stopped_opponent_travel(-64, 0);

	queue_opponent_primitive(RENDER_PRIMITIVE_POLYGON, 1);
	secondary_patterns[0] = 0;
	shape3d_render_queued_primitives();
	assert_headings(headings, 0, 6, 256, 44);
	queue_opponent_primitive(RENDER_PRIMITIVE_POLYGON, 1);
	secondary_patterns[0] = 7;
	shape3d_render_queued_primitives();
	assert_headings(headings, 7, 6, 256, 44);
	queue_opponent_primitive(RENDER_PRIMITIVE_POLYGON, 2);
	opponent_polyinfo[24 + 2] = 1;
	patterns[1] = 2;
	shape3d_render_queued_primitives();
	assert_headings(headings, 2, 6, 256, 44);
	queue_opponent_primitive(RENDER_PRIMITIVE_WHEEL, 0);
	opponent_polyinfo[24 + 2] = 1;
	shape3d_render_queued_primitives();
	assert_headings(headings, 20630, 6, 256, 44);
	queue_opponent_primitive(RENDER_PRIMITIVE_LINE, 0);
	shape3d_render_queued_primitives();
	queue_opponent_primitive(RENDER_PRIMITIVE_SPHERE, 0);
	shape3d_render_queued_primitives();
	queue_opponent_primitive(RENDER_PRIMITIVE_POINT, 0);
	shape3d_render_queued_primitives();
	assert_headings(headings, 20630, 6, 256, 44);

	/* A different logical allocation changes the next stopped wheel's travel,
	 * without relying on a host pointer or a replay-specific segment value. */
	shape3d_set_legacy_render_stack(0, 0, 0, &context);
	queue_opponent_primitive(RENDER_PRIMITIVE_POLYGON, 3);
	shape3d_render_queued_primitives();
	assert_headings(headings, 20630, 6, 512, 44);
	assert_stopped_opponent_travel(0, -64);
	shape3d_set_legacy_render_stack(0, 0, 0, 0);
	queue_opponent_primitive(RENDER_PRIMITIVE_POLYGON, 1);
	shape3d_render_queued_primitives();
	assert_headings(headings, 20630, 6, 512, 44);
}

static void test_rendered_player_crash_transition(void)
{
	struct PLAYER_WHEEL_MOTION motion;
	legacy_s16 *headings = legacy_execution_residue.wheel_plane_angles;

	memset(&state, 0, sizeof(state));
	memset(&simd_opponent, 0, sizeof(simd_opponent));
	memset(&motion, 0, sizeof(motion));
	drawing_sprite.sprite_raster_left = 0;
	drawing_sprite.sprite_raster_right = 320;
	gameconfig.game_opponenttype = 1;
	state.playerstate.car_lastspeed = 200;
	state.playerstate.car_crashBmpFlag = CRASH_EVENT_COLLISION;
	shape3d_set_legacy_render_stack(headings, 256, 512, 0);
	queue_polygon(0);
	shape3d_render_queued_primitives();
	assert_headings(headings, 319, 0, 256, 4895);
	restore_stopped_wheel_headings(&state.playerstate, &motion, PLAYER_CAR_INDEX);
	assert_headings(motion.headings, 319, 0, 256, 4895);
	car_to_world_rotation = *mat_rot_zxy(0, 0, 0, MATRIX_ROTATION_ORDER_ZXY);
	planindex = -1;
	prepare_wheel_plane_travel(&motion, 2, 64);
	assert(wheel_world_travel.x == -64 && wheel_world_travel.z == 0);

	/* Disabling capture restores the physics-only caller's existing contract. */
	shape3d_set_legacy_render_stack(0, 0, 0, 0);
	restore_stopped_wheel_headings(&state.playerstate, &motion, PLAYER_CAR_INDEX);
	assert_headings(motion.headings, 0, 0, 0, -384);
	gameconfig.game_opponenttype = 0;
}

static void test_view_rotation_stopped_wheel_handoff(void)
{
	struct SHAPE3D_LEGACY_OPPONENT_RENDER_CONTEXT context;
	struct RECTANGLE clip = {0, 320, 17, 200};
	struct PLAYER_WHEEL_MOTION motion;
	legacy_s16 *headings = legacy_execution_residue.wheel_angle_stack_words;
	legacy_s16 rotation_bp = LEGACY_S16_FROM_BITS(51678U);

	memset(&context, 0, sizeof(context));
	context.wheel_headings = headings;
	/* A relocated return CS supplies a quarter-turn heading in this fixture. */
	shape3d_set_legacy_render_stack(0, 51720U, 256U, &context);
	headings[3] = 44;
	select_cliprect_rotate(0, 7, 19, &clip, 0);
	assert_headings(headings, 3, rotation_bp, 0x156a, 256);
	/* The incremental sky path leaves its local rectangle untouched. Polygon
	 * output also leaves this word intact, before stopped physics consumes it. */
	queue_opponent_primitive(RENDER_PRIMITIVE_POLYGON, 0);
	shape3d_render_queued_primitives();
	memset(&motion, 0, sizeof(motion));
	restore_stopped_wheel_headings(&state.opponentstate, &motion, OPPONENT_CAR_INDEX);
	car_to_world_rotation = *mat_rot_zxy(0, 0, 0, MATRIX_ROTATION_ORDER_ZXY);
	planindex = -1;
	prepare_wheel_plane_travel(&motion, 3, 64);
	assert(wheel_world_travel.x == -64 && wheel_world_travel.z == 0);

	shape3d_set_legacy_render_stack(0, 51720U, 512U, &context);
	select_cliprect_rotate(7, 19, 0, &clip, 0);
	assert_headings(headings, 6, rotation_bp, 0x156a, 512);
	select_cliprect_rotate(7, 0, 19, &clip, 0);
	assert_headings(headings, 5, rotation_bp, 0x156a, 512);
	select_cliprect_rotate(7, 11, 19, &clip, 0);
	assert_headings(headings, 7, rotation_bp, 0x15fa, 512);

	select_cliprect_rotate(7, 0, 0, &clip, 0);
	assert_headings(headings, sin_fast(-7), cos_fast(-7), rotation_bp, 0x14ba);
	select_cliprect_rotate(0, -7, 0, &clip, 0);
	assert_headings(headings, sin_fast(7), cos_fast(7), rotation_bp, 0x14d3);
	select_cliprect_rotate(0, 0, 19, &clip, 0);
	assert_headings(headings, sin_fast(-19), cos_fast(-19), rotation_bp, 0x1513);
	select_cliprect_rotate(0, 0, -19, &clip, 0);
	assert_headings(headings, sin_fast(19), cos_fast(19), rotation_bp, 0x1513);
	select_cliprect_rotate(1024, -1024, 256, &clip, 0);
	assert_headings(headings, sin_fast(19), cos_fast(19), rotation_bp, 0x1513);
	select_cliprect_rotate(0, 0, 0, &clip, 0);
	assert_headings(headings, sin_fast(19), cos_fast(19), rotation_bp, 0x1513);

	/* A final solid polygon can also leave the constructor's sine intact.
	 * Stopped physics must consume that first heading on the following tick. */
	select_cliprect_rotate(256, 0, 0, &clip, 0);
	assert_headings(headings, -16384, 0, rotation_bp, 0x14ba);
	queue_opponent_primitive(RENDER_PRIMITIVE_POLYGON, 0);
	shape3d_render_queued_primitives();
	assert(headings[0] == -16384);
	restore_stopped_wheel_headings(&state.opponentstate, &motion, OPPONENT_CAR_INDEX);
	prepare_wheel_plane_travel(&motion, 0, 64);
	assert(wheel_world_travel.x == 0 && wheel_world_travel.z == 64);
	select_cliprect_rotate(0, -256, 0, &clip, 0);
	assert_headings(headings, 16384, 0, rotation_bp, 0x14d3);

	/* Full sky redraws still replace the camera residue with their rectangle. */
	shape3d_retain_legacy_skybox_rect(&clip);
	assert(headings[3] == 17);
	shape3d_set_legacy_render_stack(0, 0, 0, 0);
	select_cliprect_rotate(7, 11, 19, &clip, 0);
	assert(headings[3] == 17);
}

static void test_ghost_material_and_physics_isolation(void)
{
	legacy_s16 player[4] = {11, 22, 33, 44};
	legacy_s16 opponent[4] = {55, 66, 77, 88};
	struct SHAPE3D_LEGACY_OPPONENT_RENDER_CONTEXT context = {opponent, 100, 200, 300};
	shape3d_set_legacy_render_stack(player, 400, 500, &context);
	checking_ghost = 1;
	static const legacy_u8 types[] = {RENDER_PRIMITIVE_POLYGON, RENDER_PRIMITIVE_LINE,
									  RENDER_PRIMITIVE_SPHERE, RENDER_PRIMITIVE_WHEEL,
									  RENDER_PRIMITIVE_POINT};
	for (legacy_u32 index = 0; index < sizeof(types); index++) {
		queue_polygon(0);
		polygon_record[4] = types[index] | RENDER_PRIMITIVE_GHOST_FLAG;
		shape3d_render_queued_primitives();
		assert(ghost_calls == index + 1);
		assert_headings(player, 11, 22, 33, 44);
		assert_headings(opponent, 55, 66, 77, 88);
	}
	queue_polygon(1);
	secondary_patterns[0] = 0;
	polygon_record[4] |= RENDER_PRIMITIVE_GHOST_FLAG;
	shape3d_render_queued_primitives();
	assert(ghost_calls == 5);
	checking_ghost = 0;
	shape3d_set_legacy_render_stack(0, 0, 0, 0);
}

static void test_ghost_preserves_normal_point_indices(void)
{
	legacy_u8 vertex_bytes[8U * SHAPE3D_VERTEX_SIZE] = {0};
	legacy_u8 primitive[] = {1, 1, 0, 0, 0, 0};
	legacy_u8 masks[4] = {255, 255, 255, 255};
	legacy_u8 queue[128] = {0};
	struct SHAPE3D shape = {0};
	shape.shape3d_numverts = 8;
	shape.shape3d_numprimitives = 1;
	shape.shape3d_numpaints = 1;
	shape.shape3d_vertex_bytes = vertex_bytes;
	shape.shape3d_primitives = primitive;
	shape.shape3d_visibility_masks = masks;
	shape.shape3d_front_facing_masks = masks;
	struct VECTOR position = {0, 0, 100};
	for (legacy_u32 index = 0; index < 8; index++) {
		shape3d_vertex_write(&shape, index, &position);
	}
	struct TRANSFORMEDSHAPE3D instance = {0};
	instance.shapeptr = &shape;
	instance.culling_distance = 1024;
	projection_focal_length_x = projection_focal_length_y = 256;
	select_rect_rc.left = select_rect_rc.top = 0;
	select_rect_rc.right = 320;
	select_rect_rc.bottom = 200;
	mat_temp = *mat_rot_zxy(0, 0, 0, MATRIX_ROTATION_ORDER_ZXY);
	legacy_s16 player[4] = {11, 22, 33, 44};
	shape3d_set_legacy_render_stack(player, 400, 500, 0);
	checking_ghost = 1;
	for (legacy_u32 first_is_ghost = 0; first_is_ghost < 2; first_is_ghost++) {
		polyinfo_reset();
		polyinfoptr = queue;
		instance.ts_flags = 2U | (first_is_ghost ? SHAPE3D_GHOST_FLAG : 0U);
		assert(shape3d_transform_and_queue(&instance) == 0);
		instance.ts_flags ^= SHAPE3D_GHOST_FLAG;
		assert(shape3d_transform_and_queue(&instance) == 0);
		shape3d_render_queued_primitives();
		assert_headings(player, 0, 0, 400, 5261);
	}
	checking_ghost = 0;
	shape3d_set_legacy_render_stack(0, 0, 0, 0);
}

static void test_supersight_render_capacity(void)
{
	shape3d_set_legacy_render_stack(0, 0, 0, 0);
	static legacy_u8 queue[32U + POLYINFO_SUPERSIGHT_DATA_SIZE];
	static legacy_u8 clipped_record[66] = {0, 0, 0, 15, RENDER_PRIMITIVE_POLYGON, 0};
	LEGACY_WRITE_U16_LE(clipped_record + 62, 123);
	LEGACY_WRITE_U16_LE(clipped_record + 64, 45);
	static const legacy_u8 modes[] = {1, 0};
	for (legacy_u32 test = 0; test < sizeof(modes) / sizeof(modes[0]); test++) {
		polyinfo_set_supersight(modes[test]);
		queue_polygon(0);
		legacy_u16 capacity = modes[test] != 0U ? 592U : 400U;
		legacy_u8 *record = modes[test] != 0U ? clipped_record : polygon_record;
		expected_polygon_vertices = record[3];
		polyinfoptr = queue + 32;
		legacy_u16 offset = modes[test] != 0U ? POLYINFO_SUPERSIGHT_DATA_SIZE - 66U : 0U;
		memcpy(polyinfoptr, record, modes[test] != 0U ? 66U : sizeof(polygon_record));
		memcpy(polyinfoptr + offset, record, modes[test] != 0U ? 66U : sizeof(polygon_record));
		polyinfonumpolys = capacity;
		polygon_next_index[capacity] = capacity - 1U;
		for (legacy_u16 index = 0; index < capacity; index++) {
			polygon_next_index[index] = (legacy_s16)index - 1;
			polygon_record_offsets[index] = index != 0U ? offset : 0U;
		}
		legacy_u32 calls_before = solid_calls;
		shape3d_render_queued_primitives();
		assert(solid_calls == calls_before + capacity);
		assert(polyinfonumpolys == 0);
		assert(polygon_next_index[capacity] == -1);
	}
}

int main(void)
{
	drawing_sprite.sprite_raster_left = 13;
	drawing_sprite.sprite_raster_right = 247;
	queue_polygon(0);
	shape3d_render_queued_primitives();
	assert(solid_calls == 1);
	legacy_s16 headings[4] = {11, 22, 33, 44};
	assert_headings(headings, 11, 22, 33, 44);

	shape3d_set_legacy_render_stack(headings, 51720U, 6004U, 0);
	polyinfo_reset();
	shape3d_render_queued_primitives();
	assert_headings(headings, 11, 22, 33, 44);
	queue_polygon(3); /* An invisible material does not enter the rasterizer. */
	shape3d_render_queued_primitives();
	assert(solid_calls == 1);
	assert_headings(headings, 11, 22, 33, 44);

	queue_polygon(0);
	shape3d_render_queued_primitives();
	assert(solid_calls == 2);
	assert_headings(headings, 246, 13, -13816, 4895);

	shape3d_set_legacy_render_stack(headings, 32767U, 6005U, 0);
	drawing_sprite.sprite_raster_left = 0;
	drawing_sprite.sprite_raster_right = 320;
	queue_polygon(0);
	shape3d_render_queued_primitives();
	assert_headings(headings, 319, 0, 32767, 4895);

	queue_polygon(1);
	secondary_patterns[0] = 0;
	shape3d_render_queued_primitives();
	assert_headings(headings, 319, 0, 32767, 4895);
	queue_polygon(1);
	secondary_patterns[0] = 3;
	shape3d_render_queued_primitives();
	assert_headings(headings, 0, 32767, 4936, 6005);
	queue_polygon(2);
	shape3d_render_queued_primitives();
	assert_headings(headings, 32767, 5089, 6005, 19);

	queue_polygon(0);
	polygon_record[4] = RENDER_PRIMITIVE_LINE;
	LEGACY_WRITE_U16_LE(polygon_record + 6, 33000U);
	shape3d_render_queued_primitives();
	assert_headings(headings, 32767, 5119, 6005, -32536);

	queue_polygon(0);
	polygon_record[4] = RENDER_PRIMITIVE_WHEEL;
	shape3d_render_queued_primitives();
	assert_headings(headings, 32767, 5213, 6005, 32717);

	queue_polygon(0);
	polygon_record[4] = RENDER_PRIMITIVE_POINT;
	polyinfonumpolys = 2;
	polygon_next_index[400] = 7;
	polygon_next_index[7] = 3;
	polygon_record_offsets[7] = 0;
	polygon_record_offsets[3] = 0;
	shape3d_render_queued_primitives();
	assert_headings(headings, 3, 1, 32767, 5261);

	queue_polygon(0);
	polygon_record[4] = RENDER_PRIMITIVE_SPHERE;
	LEGACY_WRITE_U16_LE(polygon_record + 10, 0);
	shape3d_render_queued_primitives();
	assert_headings(headings, 3, 32767, 5239, 6005);
	queue_polygon(0);
	polygon_record[4] = RENDER_PRIMITIVE_SPHERE;
	LEGACY_WRITE_U16_LE(polygon_record + 10, 1);
	shape3d_render_queued_primitives();
	assert_headings(headings, 3, 32767, 5239, 6005);
	queue_polygon(0);
	polygon_record[4] = RENDER_PRIMITIVE_SPHERE;
	LEGACY_WRITE_U16_LE(polygon_record + 10, 65535U);
	shape3d_render_queued_primitives();
	assert_headings(headings, 3, 32767, 5239, 6005);
	queue_polygon(0);
	polygon_record[4] = RENDER_PRIMITIVE_SPHERE;
	LEGACY_WRITE_U16_LE(polygon_record + 10, 2);
	shape3d_render_queued_primitives();
	assert_headings(headings, 319, 32767, 5239, 6005);

	shape3d_set_legacy_render_stack(0, 0, 0, 0);
	drawing_sprite.sprite_raster_right = 80;
	queue_polygon(0);
	shape3d_render_queued_primitives();
	assert(solid_calls == 4);
	assert_headings(headings, 319, 32767, 5239, 6005);
	test_opponent_render_handoff();
	test_view_rotation_stopped_wheel_handoff();
	test_rendered_player_crash_transition();
	test_ghost_material_and_physics_isolation();
	test_ghost_preserves_normal_point_indices();
	test_supersight_render_capacity();
	return 0;
}
