#include <assert.h>
#include <string.h>

#include "../c/hires/shape3d_car_ground.c"

legacy_u16 projection_focal_length_x = 16;
legacy_u16 projection_focal_length_y = 13;

struct SHAPE3D *hires_shape3d_car_mesh(const struct SHAPE3D *shape)
{
	return (struct SHAPE3D *)shape;
}

static const struct SHAPE3D_HIRES_VECTOR *test_precise_vertices;

const struct SHAPE3D_HIRES_VECTOR *hires_shape3d_car_vertices(const struct SHAPE3D *shape)
{
	(void)shape;
	return test_precise_vertices;
}

#define TEST_VERTEX_COUNT 18U

static legacy_u8 vertex_bytes[TEST_VERTEX_COUNT * SHAPE3D_VERTEX_SIZE];
static legacy_u8 primitive_bytes[] = {3, 0, 1, 17, 17, 17, 12, 0,  1,  1,  2,  3,
									  4, 5, 6, 12, 0,  1,  10, 11, 12, 13, 14, 15};

void shape3d_vertex_read(const struct SHAPE3D *shape, legacy_u16 index, struct VECTOR *destination)
{
	assert(index < shape->shape3d_numverts);
	const legacy_u8 *vertex = shape->shape3d_vertex_bytes + index * SHAPE3D_VERTEX_SIZE;
	destination->x = LEGACY_S16_FROM_BITS(LEGACY_READ_U16_LE(vertex));
	destination->y = LEGACY_S16_FROM_BITS(LEGACY_READ_U16_LE(vertex + 2));
	destination->z = LEGACY_S16_FROM_BITS(LEGACY_READ_U16_LE(vertex + 4));
}

static void set_height(legacy_u16 index, legacy_s16 height)
{
	LEGACY_WRITE_U16_LE(vertex_bytes + index * SHAPE3D_VERTEX_SIZE + SHAPE3D_VERTEX_Y_OFFSET,
						(legacy_u16)height);
}

static void set_vertex(legacy_u16 index, legacy_s16 x, legacy_s16 y, legacy_s16 z)
{
	legacy_u8 *vertex = vertex_bytes + index * SHAPE3D_VERTEX_SIZE;
	LEGACY_WRITE_U16_LE(vertex, (legacy_u16)x);
	LEGACY_WRITE_U16_LE(vertex + SHAPE3D_VERTEX_Y_OFFSET, (legacy_u16)y);
	LEGACY_WRITE_U16_LE(vertex + 4, (legacy_u16)z);
}

static void set_rim(legacy_u16 first, legacy_s16 center, legacy_s16 first_axis,
					legacy_s16 second_axis)
{
	set_vertex(first, 0, center, 0);
	set_vertex(first + 1, 1, first_axis, 0);
	set_vertex(first + 2, 0, second_axis, 1);
}

static void set_wheel(legacy_u16 first, legacy_s16 center, legacy_s16 radius)
{
	set_rim(first, center, center + radius, center);
	set_rim(first + 3, center, center + radius, center);
}

static struct SHAPE3D reset_shape(void)
{
	test_precise_vertices = NULL;
	memset(vertex_bytes, 0, sizeof(vertex_bytes));
	struct SHAPE3D shape = {0};
	shape.shape3d_numverts = TEST_VERTEX_COUNT;
	shape.shape3d_vertex_bytes = vertex_bytes;
	shape.shape3d_numprimitives = 3;
	shape.shape3d_numpaints = 1;
	shape.shape3d_primitives = primitive_bytes;
	set_wheel(1, 4, 5);
	set_wheel(10, 4, 5);
	/* Unused bounding vertices and collapsed polygons are not visible geometry. */
	set_height(17, -30000);
	return shape;
}

static legacy_s16 calculate_offset(const struct SHAPE3D *shape)
{
	struct CAR_GROUND_BOUNDS bounds;
	shape3d_hires_car_ground_bounds(shape, &bounds);
	return shape3d_hires_calculate_car_ground_level(
		&bounds, projection_focal_length_x, projection_focal_length_y, CAR_PHYSICS_TIRE_BOTTOM);
}

static void check_wheel_clearance(void)
{
	struct SHAPE3D shape = reset_shape();
	assert(calculate_offset(&shape) == 5);
	set_wheel(1, 6, 9);
	set_wheel(10, 6, 9);
	assert(calculate_offset(&shape) == 3);
	set_wheel(1, 19, 20);
	set_wheel(10, 19, 20);
	assert(calculate_offset(&shape) == 5);

	/* Unequal wheels use the lowest tire, so lowering cannot bury that tire. */
	set_wheel(10, 6, 10);
	assert(calculate_offset(&shape) == 2);
	set_wheel(10, 1, 10);
	assert(calculate_offset(&shape) == -3);

	shape = reset_shape();
	set_rim(4, 1, 6, 1);
	assert(calculate_offset(&shape) == 2);

	/* The visible tread joins both rim centers; an asymmetric face can be
	 * wider than the other face even when its own center is higher. */
	shape = reset_shape();
	set_rim(1, 4, 9, 4);
	set_rim(4, 10, 22, 10);
	assert(calculate_offset(&shape) == -2);
	shape = reset_shape();
	for (legacy_u16 vertex = 4; vertex < 7; vertex++) {
		set_vertex(vertex, 0, -3, 0);
	}
	assert(calculate_offset(&shape) == -2);
	shape = reset_shape();
	for (legacy_u16 vertex = 1; vertex < 4; vertex++) {
		set_vertex(vertex, 0, -3, 0);
	}
	assert(calculate_offset(&shape) == -2);

	/* Both authored radial axes contribute to tilted and custom wheel rims. */
	shape = reset_shape();
	set_rim(1, 4, 7, 8);
	assert(calculate_offset(&shape) == 5);
	set_rim(1, 4, 7, 7);
	set_rim(4, 4, 7, 7);
	set_rim(10, 4, 7, 7);
	set_rim(13, 4, 7, 7);
	assert(calculate_offset(&shape) == 5);
	assert(shape3d_hires_car_extent(3, 4, 0) == 5);
	assert(shape3d_hires_car_extent(3, 3, 0) == 5);
	assert(shape3d_hires_car_extent(-65535, 65535, 0) == 92681);

	/* Coordinate subtraction and squared radii must not overflow signed words. */
	set_rim(1, 32767, -32768, -32768);
	assert(calculate_offset(&shape) == -32767);
	shape = reset_shape();
	set_wheel(1, 32767, 0);
	set_wheel(10, 32767, 0);
	assert(calculate_offset(&shape) == 32767);
}

static void check_geometry_without_wheels(void)
{
	static legacy_u8 polygon[] = {3, 0, 1, 1, 2, 3};
	static legacy_u8 line_and_point[] = {2, 0, 1, 1, 2, 1, 0, 1, 3};
	static legacy_u8 dummy_wheel_and_polygon[] = {12, 0, 1, 4, 5, 6, 7, 8, 9, 3, 0, 1, 1, 2, 3};
	struct SHAPE3D shape = reset_shape();
	shape.shape3d_numprimitives = 1;
	shape.shape3d_primitives = polygon;
	set_height(0, -30000);
	set_height(1, 2);
	set_height(2, 5);
	set_height(3, 9);
	/* Used geometry determines the bottom, never unused resource vertices. */
	assert(calculate_offset(&shape) == 8);
	set_height(1, -9);
	assert(calculate_offset(&shape) == -3);

	shape.shape3d_numprimitives = 2;
	shape.shape3d_primitives = line_and_point;
	set_height(1, 4);
	set_height(2, 7);
	set_height(3, 1);
	assert(calculate_offset(&shape) == 7);
	shape.shape3d_numprimitives = 1;
	assert(calculate_offset(&shape) == 10);

	shape.shape3d_numprimitives = 2;
	shape.shape3d_primitives = dummy_wheel_and_polygon;
	for (legacy_u16 vertex = 4; vertex < 10; vertex++) {
		set_vertex(vertex, 0, -20, 0);
	}
	/* Some custom cars reserve wheel records but make them zero-area. Their
	 * invisible control points cannot suppress the real body's correction. */
	assert(calculate_offset(&shape) == 7);
	set_vertex(5, 1, -18, 3);
	set_vertex(6, 2, -16, 6);
	set_vertex(8, 1, -18, 3);
	set_vertex(9, 2, -16, 6);
	assert(calculate_offset(&shape) == 7);
}

static void check_visible_body_caps_wheel_correction(void)
{
	static legacy_u8 primitives[] = {12, 0, 1, 4, 5, 6, 7, 8, 9, 3, 0, 1, 1, 2, 3};
	struct SHAPE3D shape = reset_shape();
	shape.shape3d_primitives = primitives;
	shape.shape3d_numprimitives = 2;
	set_wheel(4, 4, 1);
	set_vertex(1, 0, 2, 0);
	set_vertex(2, 4, 5, 0);
	set_vertex(3, 0, 7, 4);
	/* A real underside below the wheel records must not be buried. */
	assert(calculate_offset(&shape) == 8);
	legacy_u8 visibility[8] = {0};
	shape.shape3d_visibility_masks = visibility;
	assert(calculate_offset(&shape) == 0);
	LEGACY_WRITE_U32_LE(visibility, 0xFFFFFFFFUL);
	assert(calculate_offset(&shape) == 9);
	LEGACY_WRITE_U32_LE(visibility + 4, 0xFFFFFFFFUL);
	assert(calculate_offset(&shape) == 8);
	shape.shape3d_visibility_masks = 0;
	set_height(1, 4);
	assert(calculate_offset(&shape) == 9);
	/* A collapsed polygon is not visible, even far below the usable wheels. */
	set_vertex(1, 0, -30000, 0);
	set_vertex(2, 0, -30000, 0);
	set_vertex(3, 0, -30000, 0);
	assert(calculate_offset(&shape) == 9);

	static legacy_u8 duplicate_corner_polygon[] = {4, 0, 1, 1, 2, 1, 3};
	shape.shape3d_primitives = duplicate_corner_polygon;
	shape.shape3d_numprimitives = 1;
	set_vertex(1, 0, 2, 0);
	set_vertex(2, 4, 5, 0);
	set_vertex(3, 0, 7, 4);
	assert(calculate_offset(&shape) == 8);
}

static void check_sphere_geometry(void)
{
	static legacy_u8 sphere[] = {11, 0, 1, 10, 11};
	static legacy_u8 wheel_and_sphere[] = {12, 0, 1, 4, 5, 6, 7, 8, 9, 11, 0, 1, 10, 11};
	struct SHAPE3D shape = reset_shape();
	shape.shape3d_primitives = sphere;
	shape.shape3d_numprimitives = 1;
	set_vertex(10, 0, 10, 0);
	set_vertex(11, 0, -10, 0);
	/* The control-point separation specifies diameter; its endpoint is not
	 * rendered geometry. At this projection the vertical radius is half. */
	assert(calculate_offset(&shape) == 6);
	set_vertex(10, 0, 2, 0);
	set_vertex(11, 0, -18, 0);
	assert(calculate_offset(&shape) == -2);
	set_vertex(10, 0, 4, 0);
	set_vertex(11, 0, 4, 10);
	assert(calculate_offset(&shape) == 5);

	shape.shape3d_primitives = wheel_and_sphere;
	shape.shape3d_numprimitives = 2;
	set_vertex(10, 0, 4, 0);
	set_vertex(11, 0, 12, 0);
	for (legacy_u16 vertex = 4; vertex < 10; vertex++) {
		set_vertex(vertex, 0, -30000, 0);
	}
	assert(calculate_offset(&shape) == 6);
	/* Sphere tires can extend below tiny, otherwise valid wheel primitives. */
	set_wheel(4, 4, 1);
	assert(calculate_offset(&shape) == 6);
	/* A zero-size sphere does not draw pixels and cannot pull the model up. */
	set_vertex(10, 0, -30000, 0);
	set_vertex(11, 0, -30000, 0);
	assert(calculate_offset(&shape) == 9);
	shape.shape3d_primitives = sphere;
	shape.shape3d_numprimitives = 1;
	assert(calculate_offset(&shape) == 0);
	shape.shape3d_primitives = wheel_and_sphere;
	shape.shape3d_numprimitives = 2;
	set_vertex(10, 0, 4, 0);
	set_vertex(11, 0, 12, 0);

	shape3d_hires_reset_car_ground_offsets();
	shape3d_hires_cache_car_ground_offset(0, &shape);
	assert(shape3d_hires_car_ground_offset(&shape) == 6);
	/* Projection changes recompute sphere extent from neutral cached controls,
	 * rather than rereading animated geometry every rendered frame. */
	set_vertex(11, 0, 100, 0);
	assert(shape3d_hires_car_ground_offset(&shape) == 6);
	projection_focal_length_x = 230;
	projection_focal_length_y = 155;
	assert(shape3d_hires_car_ground_offset(&shape) == 5);
	projection_focal_length_x = 16;
	projection_focal_length_y = 13;
	assert(shape3d_hires_car_ground_offset(&shape) == 6);
	shape3d_hires_reset_car_ground_offsets();

	shape.shape3d_primitives = sphere;
	shape.shape3d_numprimitives = 1;
	set_vertex(10, -32768, 32767, -32768);
	set_vertex(11, 32767, -32768, 32767);
	projection_focal_length_x = 65535;
	projection_focal_length_y = 1;
	assert(calculate_offset(&shape) == -32767);
	set_vertex(10, 0, 4, 0);
	set_vertex(11, 0, 12, 0);
	projection_focal_length_x = 0;
	assert(calculate_offset(&shape) == 6);
	projection_focal_length_x = 16;
	projection_focal_length_y = 0;
	assert(calculate_offset(&shape) == 6);
	projection_focal_length_y = 13;
}

static void check_missing_wheels(void)
{
	struct SHAPE3D shape = reset_shape();
	assert(calculate_offset(0) == 0);
	shape.shape3d_numprimitives = 1;
	assert(calculate_offset(&shape) == 0);
	shape.shape3d_numprimitives = 3;
	shape.shape3d_vertex_bytes = 0;
	assert(calculate_offset(&shape) == 0);
	shape.shape3d_vertex_bytes = vertex_bytes;
	shape.shape3d_primitives = 0;
	assert(calculate_offset(&shape) == 0);
	shape.shape3d_primitives = primitive_bytes;
	primitive_bytes[0] = 16;
	assert(calculate_offset(&shape) == 0);
	primitive_bytes[0] = 3;
	primitive_bytes[9] = TEST_VERTEX_COUNT;
	assert(calculate_offset(&shape) == 0);
	primitive_bytes[9] = 1;
}

static void check_cached_clearance(void)
{
	struct SHAPE3D first = reset_shape();
	struct SHAPE3D second = first;
	struct SHAPE3D unknown = first;
	shape3d_hires_reset_car_ground_offsets();
	assert(shape3d_hires_car_ground_offset(&first) == 0);
	assert(shape3d_hires_car_ground_offset(0) == 0);
	shape3d_hires_cache_car_ground_offset(0, &first);
	assert(shape3d_hires_car_ground_offset(&first) == 5);
	assert(shape3d_hires_car_ground_offset(&unknown) == 0);
	set_wheel(1, 6, 10);
	shape3d_hires_cache_car_ground_offset(1, &second);
	assert(shape3d_hires_car_ground_offset(&second) == 2);
	/* Suspension changes the mutable resource but not the cached neutral offset. */
	assert(shape3d_hires_car_ground_offset(&first) == 5);
	shape3d_hires_reset_car_ground_offsets();
	assert(shape3d_hires_car_ground_offset(&first) == 0);
	assert(shape3d_hires_car_ground_offset(&second) == 0);
	shape3d_hires_cache_car_ground_offset(0, &first);
	assert(shape3d_hires_car_ground_offset(&first) == 2);
}

static void check_model_ground_height(void)
{
	enum {
		FIRST_WHEEL_VERTEX = 1,
		SECOND_WHEEL_VERTEX = 10,
		RAISED_WHEEL_CENTER = 10,
		RAISED_WHEEL_RADIUS = 4,
		LOWERED_WHEEL_CENTER = 1,
		LOWERED_WHEEL_RADIUS = 10,
		SPHERE_CENTER_VERTEX = 10,
		SPHERE_CONTROL_VERTEX = 11,
		SPHERE_CENTER_HEIGHT = 4,
		SPHERE_DIAMETER = 8,
		WIDE_PROJECTION_FOCAL_X = 230,
		WIDE_PROJECTION_FOCAL_Y = 155,
		PRIMITIVE_PAINT = 1
	};
	struct SHAPE3D shape = reset_shape();
	/* The showroom floor uses the visible underside without a physics offset. */
	assert(shape3d_hires_car_ground_height(&shape) == -1);
	assert(calculate_offset(&shape) == 5);
	set_wheel(FIRST_WHEEL_VERTEX, RAISED_WHEEL_CENTER, RAISED_WHEEL_RADIUS);
	set_wheel(SECOND_WHEEL_VERTEX, RAISED_WHEEL_CENTER, RAISED_WHEEL_RADIUS);
	assert(shape3d_hires_car_ground_height(&shape) == 6);
	set_wheel(FIRST_WHEEL_VERTEX, LOWERED_WHEEL_CENTER, LOWERED_WHEEL_RADIUS);
	assert(shape3d_hires_car_ground_height(&shape) == -9);
	assert(shape3d_hires_car_ground_height(0) == 0);
	shape.shape3d_numprimitives = 0;
	assert(shape3d_hires_car_ground_height(&shape) == 0);

	static legacy_u8 point[] = {SHAPE3D_PRIMITIVE_POINT, 0, PRIMITIVE_PAINT, FIRST_WHEEL_VERTEX};
	shape.shape3d_numprimitives = 1;
	shape.shape3d_primitives = point;
	set_height(FIRST_WHEEL_VERTEX, LEGACY_S16_FROM_BITS(LEGACY_U16_SIGN_BIT));
	/* Apply the physics reference before clamping, preserving extreme models. */
	assert(shape3d_hires_car_ground_height(&shape) == -(legacy_s32)LEGACY_S16_MAX);
	assert(calculate_offset(&shape) == -(legacy_s32)LEGACY_S16_MAX - 1 - CAR_PHYSICS_TIRE_BOTTOM);

	static legacy_u8 sphere[] = {SHAPE3D_PRIMITIVE_SPHERE, 0, PRIMITIVE_PAINT, SPHERE_CENTER_VERTEX,
								 SPHERE_CONTROL_VERTEX};
	shape.shape3d_primitives = sphere;
	set_vertex(SPHERE_CENTER_VERTEX, 0, SPHERE_CENTER_HEIGHT, 0);
	set_vertex(SPHERE_CONTROL_VERTEX, 0, SPHERE_CENTER_HEIGHT + SPHERE_DIAMETER, 0);
	assert(shape3d_hires_car_ground_height(&shape) == 0);
	legacy_u16 original_focal_x = projection_focal_length_x;
	legacy_u16 original_focal_y = projection_focal_length_y;
	projection_focal_length_x = WIDE_PROJECTION_FOCAL_X;
	projection_focal_length_y = WIDE_PROJECTION_FOCAL_Y;
	assert(shape3d_hires_car_ground_height(&shape) == -1);
	projection_focal_length_x = original_focal_x;
	projection_focal_length_y = original_focal_y;
}

static void check_wide_model_ground_height(void)
{
	enum {
		WIDE_VERTEX_COUNT = LEGACY_U8_MAX + 5U,
		WIDE_FIRST_VERTEX = LEGACY_U8_MAX + 1U,
		WIDE_PRIMITIVE_COUNT = LEGACY_U8_MAX + 2U,
		WIDE_SPHERE_BYTES = SHAPE3D_PRIMITIVE_HEADER_SIZE + 1U + 2U * sizeof(legacy_u16),
		WIDE_FLOOR_HEIGHT = -8,
		WIDE_SPHERE_HEIGHT = -12,
		WIDE_SPHERE_DIAMETER = 8,
		WIDE_SPHERE_BOTTOM = WIDE_SPHERE_HEIGHT - WIDE_SPHERE_DIAMETER / 2
	};
	legacy_u8 vertices[WIDE_VERTEX_COUNT * SHAPE3D_VERTEX_SIZE] = {0};
	legacy_u8 floor[] = {SHAPE3D_PRIMITIVE_POLYGON_FIRST + 1U, 0, 1, 0, 1, 1, 1, 2, 1, 3, 1};
	const struct VECTOR floor_points[] = {{-2, WIDE_FLOOR_HEIGHT, -2},
										  {-2, WIDE_FLOOR_HEIGHT, 2},
										  {2, WIDE_FLOOR_HEIGHT, 2},
										  {2, WIDE_FLOOR_HEIGHT, -2}};
	struct SHAPE3D shape = {0};
	shape.shape3d_numverts = WIDE_VERTEX_COUNT;
	shape.shape3d_vertex_bytes = vertices;
	shape.shape3d_numprimitives = 1;
	shape.shape3d_numpaints = 1;
	shape.shape3d_primitives = floor;
	for (legacy_u16 index = 0; index < sizeof(floor_points) / sizeof(floor_points[0]); index++) {
		legacy_u8 *vertex = vertices + (WIDE_FIRST_VERTEX + index) * SHAPE3D_VERTEX_SIZE;
		LEGACY_WRITE_U16_LE(vertex, floor_points[index].x);
		LEGACY_WRITE_U16_LE(vertex + SHAPE3D_VERTEX_Y_OFFSET, floor_points[index].y);
		LEGACY_WRITE_U16_LE(vertex + SHAPE3D_VERTEX_Z_OFFSET, floor_points[index].z);
	}
	/* The visible floor uses vertices beyond the byte-index boundary. */
	assert(shape3d_hires_car_ground_height(&shape) == WIDE_FLOOR_HEIGHT);

	const struct VECTOR sphere_points[] = {{0, 20, 0},
										   {2, 20, 0},
										   {0, WIDE_SPHERE_HEIGHT, 0},
										   {WIDE_SPHERE_DIAMETER, WIDE_SPHERE_HEIGHT, 0}};
	for (legacy_u16 index = 0; index < sizeof(sphere_points) / sizeof(sphere_points[0]); index++) {
		legacy_u8 *vertex = vertices + (WIDE_FIRST_VERTEX + index) * SHAPE3D_VERTEX_SIZE;
		LEGACY_WRITE_U16_LE(vertex, sphere_points[index].x);
		LEGACY_WRITE_U16_LE(vertex + SHAPE3D_VERTEX_Y_OFFSET, sphere_points[index].y);
		LEGACY_WRITE_U16_LE(vertex + SHAPE3D_VERTEX_Z_OFFSET, sphere_points[index].z);
	}
	legacy_u8 spheres[WIDE_PRIMITIVE_COUNT * WIDE_SPHERE_BYTES];
	for (legacy_u16 index = 0; index < WIDE_PRIMITIVE_COUNT; index++) {
		legacy_u8 *primitive = spheres + index * WIDE_SPHERE_BYTES;
		primitive[0] = SHAPE3D_PRIMITIVE_SPHERE;
		primitive[1] = 0;
		primitive[SHAPE3D_PRIMITIVE_HEADER_SIZE] = 1;
		legacy_u16 first = WIDE_FIRST_VERTEX + (index + 1U == WIDE_PRIMITIVE_COUNT ? 2U : 0U);
		LEGACY_WRITE_U16_LE(primitive + SHAPE3D_PRIMITIVE_HEADER_SIZE + 1U, first);
		LEGACY_WRITE_U16_LE(primitive + SHAPE3D_PRIMITIVE_HEADER_SIZE + 1U + sizeof(legacy_u16),
							first + 1U);
	}
	shape.shape3d_primitives = spheres;
	shape.shape3d_numprimitives = WIDE_PRIMITIVE_COUNT;
	/* The lowest sphere follows more than 255 preceding primitives. */
	assert(shape3d_hires_car_ground_height(&shape) == WIDE_SPHERE_BOTTOM);
	shape3d_hires_cache_car_ground_offset(0, &shape);
	assert(shape3d_hires_car_ground_offset(&shape) == WIDE_SPHERE_BOTTOM - CAR_PHYSICS_TIRE_BOTTOM);
	shape3d_hires_reset_car_ground_offsets();
	assert(shape3d_hires_car_ground_offset(&shape) == 0);
}

static void check_fractional_body_ground_height(void)
{
	struct SHAPE3D shape = reset_shape();
	legacy_u8 triangle[] = {SHAPE3D_PRIMITIVE_POLYGON_FIRST, 0, 1, 0, 1, 2};
	const struct SHAPE3D_HIRES_VECTOR precise[] = {
		{0, -6.0625, 0}, {0.5, -6.0625, 0}, {0, -6.0625, 0.5}};
	shape.shape3d_numverts = sizeof(precise) / sizeof(precise[0]);
	shape.shape3d_numprimitives = 1;
	shape.shape3d_primitives = triangle;
	/* Fixed-point payload bytes must not become the visible body's height. */
	set_vertex(0, 0, -97, 0);
	set_vertex(1, 16, -97, 0);
	set_vertex(2, 0, -97, 16);
	test_precise_vertices = precise;
	assert(shape3d_hires_car_ground_height(&shape) == -7);
	assert(calculate_offset(&shape) == -1);
	test_precise_vertices = NULL;
}

legacy_int main(void)
{
	check_wheel_clearance();
	check_missing_wheels();
	check_geometry_without_wheels();
	check_visible_body_caps_wheel_correction();
	check_sphere_geometry();
	check_cached_clearance();
	check_model_ground_height();
	check_wide_model_ground_height();
	check_fractional_body_ground_height();
	return 0;
}
