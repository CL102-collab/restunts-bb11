#include <assert.h>
#include <string.h>

#include "../c/externs.h"
#include "../c/shape3d.h"
#include "../c/shape3d_internal.h"
#include "../c/projection.h"

#undef memcpy

/* Identity rotations avoid the angle lookup; these are math.c's link dependencies. */
legacy_u8 atantable[257];
legacy_u8 vector_saved_z_low;
legacy_u8 vector_saved_z_high;

extern legacy_u16 polyinfoptrnext;
extern legacy_u16 polygon_buffer_full;
extern legacy_s8 backlights_paint_override;

static legacy_u8 vertices[8U * SHAPE3D_VERTEX_SIZE];
static legacy_u8 primitives[128];
static legacy_u8 masks[128];
static legacy_u8 polyinfo[POLYINFO_SUPERSIGHT_DATA_SIZE + 32U];
static struct SHAPE3D shape;
static struct TRANSFORMEDSHAPE3D instance;
static struct RECTANGLE bounds;

static void reset_scene(void)
{
	memset(&shape, 0, sizeof(shape));
	memset(&instance, 0, sizeof(instance));
	memset(primitives, 0, sizeof(primitives));
	memset(polyinfo, 0, sizeof(polyinfo));
	memset(masks, 255, sizeof(masks));
	shape.shape3d_numverts = 8;
	shape.shape3d_vertex_bytes = vertices;
	shape.shape3d_numpaints = 1;
	shape.shape3d_primitives = primitives;
	shape.shape3d_visibility_masks = masks;
	shape.shape3d_front_facing_masks = masks;
	instance.shapeptr = &shape;
	instance.rectptr = &bounds;
	instance.ts_flags = 10; /* Pretransformed translation and bounding rectangle. */
	instance.culling_distance = 1024;
	bounds.left = 32767;
	bounds.top = 32767;
	bounds.right = -32768;
	bounds.bottom = -32768;
	static const struct VECTOR points[] = {{-20, -20, 100}, {20, -20, 100},	 {0, 20, 100},
										   {0, 0, 0},		{-20, -20, 150}, {20, -20, 150},
										   {0, 20, 150},	{0, 0, 100}};
	for (legacy_u32 i = 0; i < 8; i++) {
		shape3d_vertex_write(&shape, i, &points[i]);
	}
	projection_focal_length_x = 256;
	projection_focal_length_y = 256;
	select_rect_rc.left = 0;
	select_rect_rc.right = 319;
	select_rect_rc.top = 0;
	select_rect_rc.bottom = 199;
	mat_temp = *mat_rot_zxy(0, 0, 0, MATRIX_ROTATION_ORDER_ZXY);
	polyinfoptr = polyinfo;
	polyinfo_reset();
}

static void check_point(const legacy_u8 *record, legacy_u32 index, legacy_s16 x, legacy_s16 y)
{
	assert(LEGACY_READ_S16_LE(record + 6U + index * 4U) == x);
	assert(LEGACY_READ_S16_LE(record + 8U + index * 4U) == y);
}

static void test_primitive_records(void)
{
	static const struct {
		legacy_u8 primitive[9];
		legacy_u16 depth;
		legacy_u8 type;
		legacy_u8 count;
		struct POINT2D points[4];
		legacy_s16 left, right, top, bottom;
	} cases[] = {
		{{1, 1, 7, 0}, 100, RENDER_PRIMITIVE_POINT, 1, {{109, 151}}, 109, 110, 151, 152},
		{{2, 1, 7, 0, 3},
		 50,
		 RENDER_PRIMITIVE_LINE,
		 2,
		 {{109, 151}, {118, 142}},
		 109,
		 119,
		 142,
		 152},
		/* Both near-plane intersections precede the associated visible edge.
		 * The legacy depth average divides by the clipped output count. */
		{{3, 1, 7, 0, 1, 3},
		 50,
		 RENDER_PRIMITIVE_POLYGON,
		 4,
		 {{118, 142}, {109, 151}, {211, 151}, {202, 142}},
		 109,
		 212,
		 142,
		 152},
		{{11, 1, 7, 7, 1}, 100, RENDER_PRIMITIVE_SPHERE, 2, {{160, 100}, {71, 0}}, 89, 172, 29, 30},
		{{12, 1, 7, 0, 2, 1, 4, 6, 5},
		 100,
		 RENDER_PRIMITIVE_WHEEL,
		 4,
		 {{109, 151}, {160, 49}, {211, 151}, {126, 134}},
		 6,
		 230,
		 31,
		 255},
		/* Reversing the first face selects the wheel's opposite face and depth. */
		{{12, 1, 7, 0, 1, 2, 4, 5, 6},
		 150,
		 RENDER_PRIMITIVE_WHEEL,
		 4,
		 {{126, 134}, {194, 134}, {160, 66}, {109, 151}},
		 40,
		 196,
		 65,
		 221}};

	for (legacy_u32 i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		reset_scene();
		memcpy(primitives, cases[i].primitive, sizeof(cases[i].primitive));
		assert(shape3d_transform_and_queue(&instance) == 0);
		assert(polyinfonumpolys == 1);
		assert(polyinfoptrnext == 6U + 4U * cases[i].count);
		assert(LEGACY_READ_U16_LE(polyinfo) == cases[i].depth);
		assert(polyinfo[2] == 7);
		assert(polyinfo[3] == cases[i].count);
		assert(polyinfo[4] == cases[i].type);
		for (legacy_u32 point = 0; point < cases[i].count; point++) {
			check_point(polyinfo, point, cases[i].points[point].px, cases[i].points[point].py);
		}
		assert(bounds.left == cases[i].left);
		assert(bounds.right == cases[i].right);
		assert(bounds.top == cases[i].top);
		assert(bounds.bottom == cases[i].bottom);
	}
}

static void test_sphere_bounds_preserve_original_coordinate_write(void)
{
	reset_scene();
	static const struct VECTOR center = {0, 0, 100};
	shape3d_vertex_write(&shape, 0, &center);
	static const struct VECTOR endpoint = {1, 0, 100};
	shape3d_vertex_write(&shape, 1, &endpoint);
	static const legacy_u8 sphere[] = {11, 1, 7, 0, 1, 0, 0};
	memcpy(primitives, sphere, sizeof(sphere));
	/* Projected center (160,100), radius2. Original sphere bounds visit
	 * (158,98) then (102,98): the second X comes from center Y + radius.
	 * Preserve this expansion of existing bounds, used to scale explosions. */
	bounds.left = 140;
	bounds.right = 180;
	bounds.top = 90;
	bounds.bottom = 130;
	assert(shape3d_transform_and_queue(&instance) == 0);
	assert(polyinfonumpolys == 1);
	check_point(polyinfo, 0, 160, 100);
	assert(LEGACY_READ_U16_LE(polyinfo + 10) == 2);
	assert(bounds.left == 102 && bounds.right == 180);
	assert(bounds.top == 90 && bounds.bottom == 130);
	/* Disabling bounds leaves geometry queued without touching the rectangle. */
	polyinfo_reset();
	instance.ts_flags = 2;
	bounds.left = 140;
	assert(shape3d_transform_and_queue(&instance) == 0);
	assert(polyinfonumpolys == 1 && bounds.left == 140);
}

static void test_shared_clipped_vertices(void)
{
	static const legacy_u8 records[] = {
		2, 1, 7, 0, 3,	  /* Clipping a line projects its shared behind-plane vertex. */
		3, 1, 8, 7, 1, 3, /* Polygon clipping must still use its original clip flag. */
		0, 0};

	reset_scene();
	memcpy(primitives, records, sizeof(records));
	assert(shape3d_transform_and_queue(&instance) == 0);
	assert(polyinfonumpolys == 2);
	const legacy_u8 *polygon = polyinfoptr + polygon_record_offsets[1];
	/* The intersection at the center equals vertex 7 and must not be duplicated. */
	assert(polygon[3] == 3);
	assert(LEGACY_READ_U16_LE(polygon) == 66);
	check_point(polygon, 0, 160, 100);
	check_point(polygon, 1, 211, 151);
	check_point(polygon, 2, 202, 142);
}

static void test_hidden_primitive_children(void)
{
	static const legacy_u8 records[] = {
		1, 1, 7, 3, /* A point behind the near plane. */
		1, 2, 8, 7, /* Its attached child must also be skipped. */
		1, 1, 9, 1, /* The next independent primitive remains visible. */
		0, 0};

	reset_scene();
	memcpy(primitives, records, sizeof(records));
	assert(shape3d_transform_and_queue(&instance) == 0);
	assert(polyinfonumpolys == 1);
	assert(polyinfo[2] == 9);
	check_point(polyinfo, 0, 211, 151);
}

#define TEST_HIDDEN_TERMINATOR_STREAM_BYTES 16U

static void test_hidden_primitive_stops_at_terminator(void)
{
	static const struct {
		legacy_u8 records[TEST_HIDDEN_TERMINATOR_STREAM_BYTES];
		legacy_u8 primitive_count;
	} cases[] = {{{1, 1, 7, 3, /* Last independent point is behind the near plane. */
				   0,		   /* A primitive stream ends with one zero type byte. */
				   SHAPE3D_PRIMITIVE_SKIP_DEPTH_SORT_FLAG, 0, 1, 1, 9, 1, 0},
				  1},
				 {{1, 1, 7, 3,										/* Hidden independent point. */
				   1, SHAPE3D_PRIMITIVE_SKIP_DEPTH_SORT_FLAG, 8, 7, /* Hidden attached child. */
				   0, SHAPE3D_PRIMITIVE_SKIP_DEPTH_SORT_FLAG, 0, 1, 1, 9, 1, 0},
				  2}};

	for (legacy_u16 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		reset_scene();
		shape.shape3d_numprimitives = cases[index].primitive_count;
		/* Bytes after the single-byte terminator are unrelated storage. Their
		 * apparent attached flag must not advance to the poisoned visible point. */
		memcpy(primitives, cases[index].records, sizeof(cases[index].records));
		assert(shape3d_transform_and_queue(&instance) == LEGACY_U16_MAX);
		assert(polyinfonumpolys == 0);
		assert(polyinfoptrnext == 0);
	}
}

static void test_depth_order_and_attached_primitive(void)
{
	static const legacy_u8 records[] = {
		1, 1, 7, 0, /* Depth 100. */
		1, 1, 8, 4, /* Depth 150 sorts before the first primitive. */
		1, 2, 9, 0, /* Attached child stays immediately after its parent. */
		0, 0};

	reset_scene();
	/* Record offsets remain relative when the allocation starts at a nonzero offset. */
	memset(polyinfo, 255, 17);
	polyinfoptr = polyinfo + 17;
	memcpy(primitives, records, sizeof(records));
	assert(shape3d_transform_and_queue(&instance) == 0);
	assert(polyinfonumpolys == 3);
	assert(polygon_record_offsets[0] == 0);
	assert(polygon_record_offsets[1] == 10);
	assert(polygon_record_offsets[2] == 20);
	for (legacy_u32 index = 0; index < 17; index++) {
		assert(polyinfo[index] == 255);
	}
	assert(polygon_next_index[400] == 1);
	assert(polygon_next_index[1] == 2);
	assert(polygon_next_index[2] == 0);
	assert(polygon_next_index[0] == -1);
}

static void test_clipped_depth_signedness(void)
{
	struct VECTOR vertex;
	static const struct {
		legacy_u8 source_count;
		legacy_u8 output_count;
		legacy_s16 depths[4];
		legacy_s16 average;
		legacy_u16 first_index;
	} cases[] = {{3, 3, {-100, -100, 100, 0}, 21812, 0},
				 {3, 4, {100, 100, -401, 0}, -51, 1},
				 {4, 5, {40, 50, 60, -600}, 13017, 0}};
	static const struct VECTOR points[] = {{-20, -20, 0}, {20, -20, 0}, {20, 20, 0}, {-20, 20, 0}};
	for (legacy_u32 i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		reset_scene();
		primitives[0] = cases[i].source_count;
		primitives[1] = 1;
		primitives[2] = 7;
		for (legacy_u32 j = 0; j < cases[i].source_count; j++) {
			vertex = points[j];
			vertex.z = cases[i].depths[j];
			shape3d_vertex_write(&shape, j, &vertex);
			primitives[3U + j] = (legacy_u8)j;
		}
		legacy_u32 next = 3U + cases[i].source_count;
		primitives[next] = 1;
		primitives[next + 1U] = 1;
		primitives[next + 2U] = 8;
		primitives[next + 3U] = 4;

		assert(shape3d_transform_and_queue(&instance) == 0);
		assert(polyinfonumpolys == 2);
		assert(polyinfoptr[polygon_record_offsets[0] + 3U] == cases[i].output_count);
		/* Preserve the original's mixed signedness: negative depth sums use
		 * unsigned division for counts 3 and 5, but signed shifts for count 4.
		 * The resulting low word determines the signed painter-order key,
		 * even when this puts a near-clipped polygon behind a farther one. */
		assert(LEGACY_READ_S16_LE(polyinfoptr + polygon_record_offsets[0]) == cases[i].average);
		assert(LEGACY_READ_S16_LE(polyinfoptr + polygon_record_offsets[1]) == 150);
		assert(polygon_next_index[400] == cases[i].first_index);
		assert(polygon_next_index[cases[i].first_index] == (legacy_s16)(1U - cases[i].first_index));
		assert(polygon_next_index[1U - cases[i].first_index] == -1);
	}
}

static void test_backface_and_material_override(void)
{
	reset_scene();
	static const legacy_u8 triangle[] = {3, 0, 7, 0, 1, 2, 0, 0};
	memcpy(primitives, triangle, sizeof(triangle));
	assert(shape3d_transform_and_queue(&instance) == LEGACY_U16_MAX);
	assert(polyinfonumpolys == 0);
	primitives[1] = 1; /* Always-visible material side. */
	primitives[2] = BACKLIGHT_PAINT_DEFAULT;
	backlights_paint_override = 17;
	instance.material = 255; /* Invalid paint selection falls back to paint zero. */
	assert(shape3d_transform_and_queue(&instance) == 0);
	assert(polyinfonumpolys == 1);
	assert(polyinfo[2] == 17);
}

static void test_queue_limits(void)
{
	reset_scene();
	static const legacy_u8 point[] = {1, 1, 7, 0, 0, 0};
	memcpy(primitives, point, sizeof(point));
	for (legacy_u32 i = 0; i < 399; i++) {
		assert(shape3d_transform_and_queue(&instance) == 0);
	}
	assert(shape3d_transform_and_queue(&instance) == 1);
	assert(polyinfonumpolys == 400);
	assert(polygon_buffer_full == 1);
	assert(shape3d_transform_and_queue(0) == 1);
	reset_scene();
	static const legacy_u8 polygon[] = {10, 1, 7, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 0, 0};
	memcpy(primitives, polygon, sizeof(polygon));
	for (legacy_u32 i = 0; i < 225; i++) {
		assert(shape3d_transform_and_queue(&instance) == 0);
	}
	assert(shape3d_transform_and_queue(&instance) == 1);
	assert(polyinfonumpolys == 226);
	assert(polyinfoptrnext == 10396);
	reset_scene();
	shape.shape3d_numverts = 256;
	assert(shape3d_transform_and_queue(&instance) == 1);
	assert(polyinfonumpolys == 0);
}

static void check_queue_chain(legacy_u16 capacity, legacy_u16 count)
{
	legacy_s16 index = polygon_next_index[capacity];
	for (legacy_u16 expected = 0; expected < count; expected++) {
		assert(index == (legacy_s16)expected);
		assert(polygon_record_offsets[index] < POLYINFO_SUPERSIGHT_DATA_SIZE);
		assert(polygon_record_offsets[index] < polyinfoptrnext);
		index = polygon_next_index[index];
	}
	assert(index == -1);
}

static void test_supersight_queue_limits(void)
{
	static const legacy_u8 point[] = {1, 1, 7, 0, 0, 0};
	static const legacy_u8 polygon[] = {10, 1, 7, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 0, 0};
	static const struct {
		legacy_u8 enabled;
		legacy_u16 capacity;
		legacy_u16 polygon_count;
		legacy_u16 data_end;
	} cases[] = {{1, 592, 288, 13248}, {0, 400, 226, 10396}, {1, 592, 288, 13248}};
	for (legacy_u32 test = 0; test < sizeof(cases) / sizeof(cases[0]); test++) {
		reset_scene();
		polyinfo_set_supersight(cases[test].enabled);
		assert(polygon_next_index[cases[test].capacity] == -1);
		memcpy(primitives, point, sizeof(point));
		for (legacy_u16 i = 1; i < cases[test].capacity; i++) {
			assert(shape3d_transform_and_queue(&instance) == 0);
		}
		assert(shape3d_transform_and_queue(&instance) == 1);
		assert(polyinfonumpolys == cases[test].capacity);
		assert(polygon_buffer_full == 1);
		check_queue_chain(cases[test].capacity, cases[test].capacity);
		assert(shape3d_transform_and_queue(0) == 1);
		reset_scene();
		memcpy(primitives, polygon, sizeof(polygon));
		for (legacy_u16 i = 1; i < cases[test].polygon_count; i++) {
			assert(shape3d_transform_and_queue(&instance) == 0);
		}
		assert(shape3d_transform_and_queue(&instance) == 1);
		assert(polyinfonumpolys == cases[test].polygon_count);
		assert(polyinfoptrnext == cases[test].data_end);
		check_queue_chain(cases[test].capacity, cases[test].polygon_count);
	}
	polyinfo_set_supersight(0);
	assert(polyinfonumpolys == 0);
	assert(polyinfoptrnext == 0);
	assert(polygon_buffer_full == 0);
	assert(polygon_next_index[400] == -1);
}

static void test_supersight_clipped_record_boundary(void)
{
	reset_scene();
	polyinfo_set_supersight(1);
	memset(polyinfo + POLYINFO_SUPERSIGHT_DATA_SIZE, 255, 32);
	static const legacy_u8 polygon[] = {10, 1, 7, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 0, 0};
	memcpy(primitives, polygon, sizeof(polygon));
	for (legacy_u32 i = 0; i < 287; i++) {
		assert(shape3d_transform_and_queue(&instance) == 0);
	}
	static const legacy_u8 line[] = {2, 1, 7, 0, 1, 0, 0};
	memcpy(primitives, line, sizeof(line));
	assert(shape3d_transform_and_queue(&instance) == 0);
	static const legacy_u8 point[] = {1, 1, 7, 0, 0, 0};
	memcpy(primitives, point, sizeof(point));
	for (legacy_u32 i = 0; i < 3; i++) {
		assert(shape3d_transform_and_queue(&instance) == 0);
	}
	assert(polyinfoptrnext == 13246);
	/* Five front vertices separated by five behind the near plane emit all
	 * ten intersections, producing the largest possible clipped record. */
	static const legacy_u8 clipped[] = {10, 1, 7, 0, 3, 1, 3, 2, 3, 4, 3, 5, 3, 0, 0};
	memcpy(primitives, clipped, sizeof(clipped));
	assert(shape3d_transform_and_queue(&instance) == 1);
	assert(polygon_record_offsets[291] == 13246);
	assert(polyinfoptr[polygon_record_offsets[291] + 3U] == 15);
	assert(polyinfoptrnext == POLYINFO_SUPERSIGHT_DATA_SIZE);
	assert(polygon_buffer_full == 1);
	assert(shape3d_transform_and_queue(0) == 1);
	for (legacy_u32 i = 0; i < 32; i++) {
		assert(polyinfo[POLYINFO_SUPERSIGHT_DATA_SIZE + i] == 255);
	}
	polyinfo_set_supersight(0);
}

static void test_ghost_override_is_per_instance(void)
{
	reset_scene();
	static const legacy_u8 triangle[] = {3, 1, 119, 0, 1, 2, 0, 0};
	memcpy(primitives, triangle, sizeof(triangle));
	instance.ts_flags |= SHAPE3D_GHOST_FLAG;
	assert(shape3d_transform_and_queue(&instance) == 0);
	assert(polyinfonumpolys == 1);
	assert(polyinfo[2] == 119);
	assert(polyinfo[4] == (RENDER_PRIMITIVE_POLYGON | RENDER_PRIMITIVE_GHOST_FLAG));
	assert(memcmp(primitives, triangle, sizeof(triangle)) == 0);
	instance.ts_flags &= ~SHAPE3D_GHOST_FLAG;
	assert(shape3d_transform_and_queue(&instance) == 0);
	assert(polyinfoptr[polygon_record_offsets[1] + 2U] == 119);
	assert(polyinfoptr[polygon_record_offsets[1] + 4U] == RENDER_PRIMITIVE_POLYGON);
}

#define TEST_YELLOW_GRILLE_MATERIAL 24U
#define TEST_ASPHALT_MATERIAL 19U
#define TEST_QUEUED_MATERIAL_OFFSET 2U
#define TEST_QUEUED_TYPE_OFFSET 4U

static void test_black_grille_override_selects_only_instance_grille_polygons(void)
{
	reset_scene();
	static const legacy_u8 source[] = {
		3, 1, SHAPE3D_STOCK_GRILLE_MATERIAL, 0, 1, 2, 3, 1, TEST_YELLOW_GRILLE_MATERIAL,   0, 1, 2,
		3, 1, TEST_ASPHALT_MATERIAL,		 0, 1, 2, 2, 1, SHAPE3D_STOCK_GRILLE_MATERIAL, 0, 1, 0,
		0};
	static const legacy_u8 materials[] = {SHAPE3D_STOCK_GRILLE_MATERIAL,
										  TEST_YELLOW_GRILLE_MATERIAL, TEST_ASPHALT_MATERIAL,
										  SHAPE3D_STOCK_GRILLE_MATERIAL};
	static const legacy_u8 types[] = {RENDER_PRIMITIVE_POLYGON, RENDER_PRIMITIVE_POLYGON,
									  RENDER_PRIMITIVE_POLYGON, RENDER_PRIMITIVE_LINE};
	memcpy(primitives, source, sizeof(source));
	instance.ts_flags |= SHAPE3D_BLACK_GRILLE_FLAG;
	assert(shape3d_transform_and_queue(&instance) == 0);
	assert(polyinfonumpolys == sizeof(materials));
	for (legacy_u16 index = 0; index < sizeof(materials); index++) {
		const legacy_u8 *record = polyinfoptr + polygon_record_offsets[index];
		assert(record[TEST_QUEUED_MATERIAL_OFFSET] == materials[index]);
		assert(record[TEST_QUEUED_TYPE_OFFSET] ==
			   (types[index] | (index == 0 ? RENDER_PRIMITIVE_BLACK_GRILLE_FLAG : 0)));
	}
	/* The next ordinary instance of the same source retains its original
	 * stock grille, yellow marking and asphalt material selections. */
	instance.ts_flags &= ~SHAPE3D_BLACK_GRILLE_FLAG;
	assert(shape3d_transform_and_queue(&instance) == 0);
	assert(polyinfonumpolys == 2U * sizeof(materials));
	for (legacy_u16 index = 0; index < sizeof(materials); index++) {
		const legacy_u8 *record = polyinfoptr + polygon_record_offsets[sizeof(materials) + index];
		assert(record[TEST_QUEUED_MATERIAL_OFFSET] == materials[index]);
		assert(record[TEST_QUEUED_TYPE_OFFSET] == types[index]);
	}
	assert(memcmp(primitives, source, sizeof(source)) == 0);
}

#define TEST_VISIBILITY_DEPTH 100
#define TEST_VISIBILITY_FORWARD_SCALE 4096
#define TEST_VISIBILITY_OFFSCREEN_X 30000
#define TEST_VISIBILITY_BOUNDING_RECT_FLAG 8U
#define TEST_VISIBILITY_PRETRANSFORMED_FLAG 2U

static void test_visibility_masks_after_bounds(void)
{
	const struct VECTOR rotations[] = {{0, 0, 0},
									   {0, ANGLE_QUARTER_TURN, 0},
									   {ANGLE_QUARTER_TURN / 2, 0, 0},
									   {-ANGLE_QUARTER_TURN / 2, 0, 0},
									   {0, ANGLE_HALF_TURN, 0}};
	const legacy_s16 heights[] = {-10, 0, 10};
	const legacy_s16 distances[] = {TEST_VISIBILITY_DEPTH / 2 - 1, TEST_VISIBILITY_DEPTH / 2,
									TEST_VISIBILITY_DEPTH / 2 + 1, -1, LEGACY_S16_MAX};
	const struct VECTOR points[] = {{-2, -2, 0}, {2, -2, 0}, {2, 2, 0}, {-2, 2, 0}};
	const legacy_u8 point_primitives[] = {1, 1, 7, 0, 1, 1, 8, 1, 0, 0};
	for (legacy_u32 rotation = 0; rotation < sizeof(rotations) / sizeof(rotations[0]); rotation++) {
		for (legacy_u32 height = 0; height < sizeof(heights) / sizeof(heights[0]); height++) {
			for (legacy_u32 distance = 0; distance < sizeof(distances) / sizeof(distances[0]);
				 distance++) {
				for (legacy_s16 pretransformed = 0; pretransformed < 2; pretransformed++) {
					reset_scene();
					shape.shape3d_numverts = sizeof(points) / sizeof(points[0]);
					for (legacy_u32 point = 0; point < sizeof(points) / sizeof(points[0]);
						 point++) {
						shape3d_vertex_write(&shape, point, &points[point]);
					}
					memcpy(primitives, point_primitives, sizeof(point_primitives));
					instance.rotvec = rotations[rotation];
					instance.pos = (struct VECTOR){0, heights[height], TEST_VISIBILITY_DEPTH};
					instance.culling_distance = distances[distance];
					instance.ts_flags = TEST_VISIBILITY_BOUNDING_RECT_FLAG |
										(pretransformed ? TEST_VISIBILITY_PRETRANSFORMED_FLAG : 0);
					struct MATRIX object_rotation =
						*mat_rot_zxy(instance.rotvec.x, instance.rotvec.y, instance.rotvec.z,
									 MATRIX_ROTATION_ORDER_ZXY);
					struct MATRIX inverse_rotation;
					mat_invert(&object_rotation, &inverse_rotation);
					struct VECTOR forward = {0, 0, TEST_VISIBILITY_FORWARD_SCALE};
					struct VECTOR direction;
					mat_mul_vector(&forward, &inverse_rotation, &direction);
					legacy_u32 selected_mask =
						LEGACY_U32_SIGN_BIT >> vector_direction_sector(&direction);
					LEGACY_WRITE_U32_LE(masks, selected_mask);
					LEGACY_WRITE_U32_LE(masks + sizeof(selected_mask), ~selected_mask);
					legacy_s32 use_sector =
						!pretransformed && (direction.y <= 0 || instance.pos.y >= 0) &&
						LEGACY_S16_SHL(instance.culling_distance, 1U) <= TEST_VISIBILITY_DEPTH;
					/* Reject an earlier instance before testing visibility of this one. */
					instance.pos.x = TEST_VISIBILITY_OFFSCREEN_X;
					assert(shape3d_transform_and_queue(&instance) == LEGACY_U16_MAX);
					assert(polyinfonumpolys == 0);
					instance.pos.x = 0;
					assert(shape3d_transform_and_queue(&instance) == 0);
					assert(polyinfonumpolys == (use_sector ? 1 : 2));
					assert(polyinfo[2] == 7);
					if (!use_sector) {
						assert(polyinfo[polygon_record_offsets[1] + 2U] == 8);
					}
				}
			}
		}
	}
}

int main(void)
{
	test_primitive_records();
	test_sphere_bounds_preserve_original_coordinate_write();
	test_shared_clipped_vertices();
	test_hidden_primitive_children();
	test_hidden_primitive_stops_at_terminator();
	test_depth_order_and_attached_primitive();
	test_clipped_depth_signedness();
	test_backface_and_material_override();
	test_queue_limits();
	test_supersight_queue_limits();
	test_supersight_clipped_record_boundary();
	test_ghost_override_is_per_instance();
	test_black_grille_override_selects_only_instance_grille_polygons();
	test_visibility_masks_after_bounds();
	return 0;
}
