#include "../c/renderer/gpu_scene.h"
#include "../c/renderer/vulkan_scene.h"
#include "../c/renderer/gpu_overlay.h"
#include "../c/shape2d/shape2d.h"
#include "../c/fatal.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_CAPTURE_VERTICES (HIRES_BACKEND_MAX_VERTICES * 12U)
#define TEST_TRIANGLE_VERTICES 3U
#define TEST_QUAD_TRIANGLE_VERTICES (TEST_TRIANGLE_VERTICES * 2U)
#define TEST_RECTANGLE_VERTICES 4U
#define TEST_CONCAVE_VERTICES 8U
#define TEST_CONCAVE_VARIANTS 4
#define TEST_WIDTH 16
#define TEST_HEIGHT 16
#define TEST_OPAQUE_COLOR 7U
#define TEST_DETAIL_COLOR 11U
#define TEST_PATTERN_COLOR 13U
#define TEST_PATTERN 0xAAAAU
#define TEST_DEPTH 1.0
#define TEST_EPSILON 0.000001
#define TEST_AREA 40.0
#define TEST_HUGE_X 41450.0
#define TEST_HUGE_Y 1123.0
#define TEST_TINY_DEPTH 3.9e-10
#define TEST_PLANE_X_STEP 2e-15
#define TEST_PLANE_Y_STEP 5e-15
#define TEST_DEPTH_ROUNDING_ULPS 2
#define TEST_DISCONNECTED_AREA 64.0
#define TEST_OUTSIDE_SHIFT 20
#define TEST_POINT_OVERHANG 0.25
#define TEST_CLASSIC_WIDTH (HIRES_WIDTH / HIRES_SCALE)
#define TEST_CLASSIC_HEIGHT (HIRES_HEIGHT / HIRES_SCALE)
#define TEST_COCKPIT_LEFT 64
#define TEST_COCKPIT_RIGHT 265
#define TEST_COCKPIT_TOP 10
#define TEST_COCKPIT_BOTTOM 130

static struct VULKAN_SCENE_VERTEX captured[TEST_CAPTURE_VERTICES];
static size_t captured_count;
static legacy_u32 submit_count;
static legacy_s32 backend_active;
static legacy_s32 target_available = 1, target_eligible = 1, target_height_eligible = 1;
static legacy_s32 target_cockpit;
static legacy_s32 submitted_width, submitted_height, submitted_left, submitted_right;
static legacy_s32 submitted_top, submitted_bottom, submitted_preserve, submitted_classic;

legacy_s32 hires_raster_prepare(struct HIRES_RASTER_TARGET *result)
{
	memset(result, 0, sizeof(*result));
	result->width = TEST_WIDTH;
	result->height = TEST_HEIGHT;
	result->right = target_eligible ? TEST_WIDTH : TEST_WIDTH / 2 - 1;
	result->bottom = target_height_eligible ? TEST_HEIGHT : TEST_HEIGHT / 3 - 1;
	if (target_cockpit) {
		result->width = TEST_CLASSIC_WIDTH;
		result->height = TEST_CLASSIC_HEIGHT;
		result->left = TEST_COCKPIT_LEFT;
		result->right = TEST_COCKPIT_RIGHT;
		result->top = TEST_COCKPIT_TOP;
		result->bottom = TEST_COCKPIT_BOTTOM;
	}
	return target_available;
}

void gpu_overlay_begin(const struct HIRES_RASTER_TARGET *raster)
{
	assert(raster != NULL);
}

void gpu_overlay_begin_sprite(const struct SPRITE *sprite, legacy_s32 width, legacy_s32 height)
{
	assert(sprite != NULL && width > 0 && height > 0);
}

legacy_s32 vulkan_scene_active(void)
{
	return backend_active;
}

legacy_s32 vulkan_scene_submit(const struct VULKAN_SCENE_VERTEX *vertices, size_t count,
							   legacy_s32 width, legacy_s32 height, legacy_s32 left,
							   legacy_s32 right, legacy_s32 top, legacy_s32 bottom,
							   legacy_s32 preserve, legacy_s32 classic, legacy_char *error,
							   size_t error_capacity)
{
	(void)error;
	(void)error_capacity;
	assert(count <= TEST_CAPTURE_VERTICES);
	if (count != 0) {
		memcpy(captured, vertices, count * sizeof(*captured));
	}
	captured_count = count;
	submit_count++;
	submitted_width = width;
	submitted_height = height;
	submitted_left = left;
	submitted_right = right;
	submitted_top = top;
	submitted_bottom = bottom;
	submitted_preserve = preserve;
	submitted_classic = classic;
	return 1;
}

void fatal_error(const legacy_s8 *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	vfprintf(stderr, (const legacy_char *)format, arguments);
	va_end(arguments);
	abort();
}

static void reset_capture(void)
{
	captured_count = 0;
	submit_count = 0;
}

static struct HIRES_RASTER_TARGET target(void)
{
	struct HIRES_RASTER_TARGET result = {0};
	result.width = TEST_WIDTH;
	result.height = TEST_HEIGHT;
	result.right = TEST_WIDTH;
	result.bottom = TEST_HEIGHT;
	return result;
}

static struct HIRES_BACKEND_MATERIAL material(legacy_u16 color)
{
	struct HIRES_BACKEND_MATERIAL result = {0};
	result.family = 1;
	result.color = color;
	return result;
}

static legacy_f64 triangle_cross(const struct VULKAN_SCENE_VERTEX *first,
								 const struct VULKAN_SCENE_VERTEX *second,
								 const struct VULKAN_SCENE_VERTEX *third)
{
	return ((legacy_f64)second->x - first->x) * ((legacy_f64)third->y - first->y) -
		   ((legacy_f64)second->y - first->y) * ((legacy_f64)third->x - first->x);
}

static legacy_s32 triangle_contains(const struct VULKAN_SCENE_VERTEX *vertices, legacy_f64 x,
									legacy_f64 y)
{
	struct VULKAN_SCENE_VERTEX point = {0};
	point.x = (legacy_f32)x;
	point.y = (legacy_f32)y;
	legacy_f64 a = triangle_cross(&vertices[0], &vertices[1], &point);
	legacy_f64 b = triangle_cross(&vertices[1], &vertices[2], &point);
	legacy_f64 c = triangle_cross(&vertices[2], &vertices[0], &point);
	return (a >= -TEST_EPSILON && b >= -TEST_EPSILON && c >= -TEST_EPSILON) ||
		   (a <= TEST_EPSILON && b <= TEST_EPSILON && c <= TEST_EPSILON);
}

static legacy_f64 captured_area(void)
{
	assert(captured_count % TEST_TRIANGLE_VERTICES == 0);
	legacy_f64 result = 0;
	for (size_t index = 0; index < captured_count; index += TEST_TRIANGLE_VERTICES) {
		legacy_f64 area =
			triangle_cross(captured + index, captured + index + 1U, captured + index + 2U);
		assert(fabs(area) > TEST_EPSILON);
		result += fabs(area) * 0.5;
	}
	return result;
}

static void concave_polygon(legacy_s32 reverse, legacy_s32 variant)
{
	/* A U shape has no fan from its first vertex: triangles crossing the open
	 * middle would increase the area or cover one of the independent probes. */
	const struct HIRES_BACKEND_VERTEX shape[TEST_CONCAVE_VERTICES] = {
		{0, 0, TEST_DEPTH}, {8, 0, TEST_DEPTH}, {8, 8, TEST_DEPTH}, {6, 8, TEST_DEPTH},
		{6, 2, TEST_DEPTH}, {2, 2, TEST_DEPTH}, {2, 8, TEST_DEPTH}, {0, 8, TEST_DEPTH}};
	struct HIRES_BACKEND_VERTEX vertices[TEST_CONCAVE_VERTICES + 2U];
	legacy_u32 count = 0;
	for (legacy_u32 index = 0; index < TEST_CONCAVE_VERTICES; index++) {
		legacy_u32 selected = reverse ? TEST_CONCAVE_VERTICES - 1U - index : index;
		vertices[count++] = shape[selected];
		if (variant == 2 && index == 2U) {
			vertices[count++] = shape[selected];
		}
		if (variant == 3 && index == 0) {
			legacy_u32 next = reverse ? selected - 1U : selected + 1U;
			vertices[count].x = (shape[selected].x + shape[next].x) * 0.5;
			vertices[count].y = (shape[selected].y + shape[next].y) * 0.5;
			vertices[count++].inverse_z = TEST_DEPTH;
		}
	}
	if (variant == 1) {
		vertices[count++] = vertices[0];
	}
	struct HIRES_BACKEND_MATERIAL paint = material(TEST_OPAQUE_COLOR);
	struct HIRES_RASTER_TARGET raster = target();
	reset_capture();
	gpu_scene_begin(0);
	gpu_scene_polygon(vertices, count, &paint);
	(void)gpu_scene_flush(&raster, 0);
	assert(submit_count == 1 && captured_count != 0);
	assert(fabs(captured_area() - TEST_AREA) < TEST_EPSILON);
	for (legacy_s32 y = 0; y < 8; y++) {
		for (legacy_s32 x = 0; x < 8; x++) {
			legacy_s32 expected = y < 2 || x < 2 || x >= 6;
			legacy_s32 covered = 0;
			for (size_t index = 0; index < captured_count; index += TEST_TRIANGLE_VERTICES) {
				covered |= triangle_contains(captured + index, x + 0.5, y + 0.5);
			}
			assert(covered == expected);
		}
	}
}

static void test_material_order(legacy_s32 classic)
{
	const struct HIRES_BACKEND_VERTEX rectangle[TEST_RECTANGLE_VERTICES] = {
		{1, 1, TEST_DEPTH}, {5, 1, TEST_DEPTH}, {5, 5, TEST_DEPTH}, {1, 5, TEST_DEPTH}};
	struct HIRES_BACKEND_MATERIAL paints[TEST_TRIANGLE_VERTICES];
	paints[0] = material(TEST_PATTERN_COLOR);
	paints[0].mode = HIRES_PAINT_PATTERN;
	paints[0].pattern = TEST_PATTERN;
	paints[1] = material(TEST_DETAIL_COLOR);
	paints[1].flags = HIRES_BACKEND_DECAL;
	paints[2] = material(TEST_OPAQUE_COLOR);
	struct HIRES_RASTER_TARGET raster = target();
	reset_capture();
	gpu_scene_begin(classic);
	for (legacy_u32 index = 0; index < TEST_TRIANGLE_VERTICES; index++) {
		gpu_scene_polygon(rectangle, TEST_RECTANGLE_VERTICES, paints + index);
	}
	(void)gpu_scene_flush(&raster, 1);
	assert(submit_count == 1 &&
		   captured_count == TEST_TRIANGLE_VERTICES * TEST_QUAD_TRIANGLE_VERTICES);
	assert(submitted_classic == classic && submitted_preserve == 1);
	legacy_u16 expected[TEST_TRIANGLE_VERTICES];
	expected[0] = classic ? TEST_PATTERN_COLOR : TEST_OPAQUE_COLOR;
	expected[1] = TEST_DETAIL_COLOR;
	expected[2] = classic ? TEST_OPAQUE_COLOR : TEST_PATTERN_COLOR;
	for (size_t index = 0; index < captured_count; index++) {
		assert(captured[index].color == expected[index / TEST_QUAD_TRIANGLE_VERTICES]);
	}
}

static void test_invalid_geometry(void)
{
	struct HIRES_BACKEND_VERTEX vertices[HIRES_BACKEND_MAX_VERTICES + 1U] = {
		{1, 1, TEST_DEPTH}, {5, 1, TEST_DEPTH}, {5, 5, TEST_DEPTH}, {1, 5, TEST_DEPTH}};
	struct HIRES_BACKEND_MATERIAL paint = material(TEST_OPAQUE_COLOR);
	struct HIRES_RASTER_TARGET raster = target();
	for (legacy_u32 invalid = 0; invalid < TEST_RECTANGLE_VERTICES; invalid++) {
		reset_capture();
		gpu_scene_begin(0);
		struct HIRES_BACKEND_VERTEX saved = vertices[0];
		if (invalid == 0) {
			vertices[0].x = NAN;
		} else if (invalid == 1) {
			vertices[0].y = INFINITY;
		} else if (invalid == 2) {
			vertices[0].x = DBL_MAX;
		} else {
			vertices[0].inverse_z = 0;
		}
		gpu_scene_polygon(vertices, TEST_RECTANGLE_VERTICES, &paint);
		vertices[0] = saved;
		(void)gpu_scene_flush(&raster, 0);
		assert(captured_count == 0);
	}
	reset_capture();
	gpu_scene_begin(0);
	gpu_scene_polygon(vertices, HIRES_BACKEND_MAX_VERTICES + 1U, &paint);
	gpu_scene_polygon(vertices, TEST_TRIANGLE_VERTICES - 1U, &paint);
	(void)gpu_scene_flush(&raster, 0);
	assert(captured_count == 0);
}

static void test_lines(void)
{
	struct HIRES_BACKEND_MATERIAL paint = material(TEST_OPAQUE_COLOR);
	struct HIRES_RASTER_TARGET raster = target();
	reset_capture();
	gpu_scene_begin(1);
	gpu_scene_line(1, 2, 7, 2, &paint);
	(void)gpu_scene_flush(&raster, 0);
	assert(captured_count >= TEST_TRIANGLE_VERTICES * 2U);
	assert(captured_area() > 0);
	legacy_s32 covered = 0;
	for (size_t index = 0; index < captured_count; index += TEST_TRIANGLE_VERTICES) {
		covered |= triangle_contains(captured + index, 4, 2);
	}
	assert(covered);
	reset_capture();
	gpu_scene_begin(1);
	gpu_scene_line(3, 5, 3, 5, &paint);
	(void)gpu_scene_flush(&raster, 0);
	assert(captured_count >= TEST_TRIANGLE_VERTICES * 2U && captured_area() > 0);
}

static void test_scope_and_viewport(void)
{
	const struct HIRES_BACKEND_VERTEX rectangle[TEST_RECTANGLE_VERTICES] = {
		{1, 1, TEST_DEPTH}, {5, 1, TEST_DEPTH}, {5, 5, TEST_DEPTH}, {1, 5, TEST_DEPTH}};
	struct HIRES_BACKEND_MATERIAL paint = material(TEST_OPAQUE_COLOR);
	struct HIRES_RASTER_TARGET raster = target();
	raster.left = 2;
	raster.right = TEST_WIDTH - 3;
	raster.top = 3;
	raster.bottom = TEST_HEIGHT - 2;
	reset_capture();
	gpu_scene_begin(0);
	gpu_scene_polygon(rectangle, TEST_RECTANGLE_VERTICES, &paint);
	(void)gpu_scene_flush(&raster, 0);
	assert(submitted_width == TEST_WIDTH && submitted_height == TEST_HEIGHT);
	assert(submitted_left == raster.left && submitted_right == raster.right &&
		   submitted_top == raster.top && submitted_bottom == raster.bottom);
	reset_capture();
	(void)gpu_scene_flush(&raster, 1);
	assert(captured_count == 0);
	backend_active = 0;
	reset_capture();
	gpu_scene_begin(0);
	assert(!gpu_scene_active());
	assert(!gpu_scene_software_preview());
	gpu_scene_polygon(rectangle, TEST_RECTANGLE_VERTICES, &paint);
	(void)gpu_scene_flush(&raster, 0);
	assert(submit_count == 0);
	gpu_scene_shutdown();
	backend_active = 1;
	target_available = 0;
	gpu_scene_begin(0);
	assert(!gpu_scene_active());
	assert(!gpu_scene_software_preview());
	target_available = 1;
	target_eligible = 0;
	gpu_scene_begin(0);
	assert(!gpu_scene_active());
	assert(gpu_scene_software_preview());
	target_eligible = 1;
	target_height_eligible = 0;
	gpu_scene_begin(0);
	assert(!gpu_scene_active());
	assert(gpu_scene_software_preview());
	target_height_eligible = 1;
	struct SPRITE sprite = {0};
	sprite.sprite_raster_right = TEST_WIDTH;
	sprite.sprite_bottom = TEST_HEIGHT;
	gpu_scene_classic_begin(&sprite, TEST_WIDTH, TEST_HEIGHT);
	assert(!gpu_scene_active());
	assert(gpu_scene_software_preview());
	sprite.sprite_buffer_width = TEST_CLASSIC_WIDTH;
	sprite.sprite_raster_right = TEST_CLASSIC_WIDTH;
	sprite.sprite_bottom = TEST_CLASSIC_HEIGHT;
	gpu_scene_classic_begin(&sprite, TEST_CLASSIC_WIDTH, TEST_CLASSIC_HEIGHT);
	assert(gpu_scene_active());
	assert(!gpu_scene_software_preview());
	gpu_scene_polygon(rectangle, TEST_RECTANGLE_VERTICES, &paint);
	assert(gpu_scene_classic_flush(&sprite, TEST_CLASSIC_WIDTH, TEST_CLASSIC_HEIGHT));
	assert(submitted_width == TEST_CLASSIC_WIDTH && submitted_height == TEST_CLASSIC_HEIGHT);
	sprite.sprite_raster_left = 1;
	gpu_scene_classic_begin(&sprite, TEST_CLASSIC_WIDTH, TEST_CLASSIC_HEIGHT);
	assert(gpu_scene_active());
	sprite.sprite_raster_right = sprite.sprite_raster_left + TEST_CLASSIC_WIDTH / 2 - 1;
	gpu_scene_classic_begin(&sprite, TEST_CLASSIC_WIDTH, TEST_CLASSIC_HEIGHT);
	assert(!gpu_scene_active());
	assert(gpu_scene_software_preview());
	sprite.sprite_raster_left = 0;
	sprite.sprite_raster_right = TEST_CLASSIC_WIDTH;
	sprite.sprite_bottom = TEST_CLASSIC_HEIGHT / 3 - 1;
	gpu_scene_classic_begin(&sprite, TEST_CLASSIC_WIDTH, TEST_CLASSIC_HEIGHT / 3 - 1);
	assert(!gpu_scene_active());
	assert(gpu_scene_software_preview());
	backend_active = 0;
	gpu_scene_classic_begin(&sprite, TEST_CLASSIC_WIDTH, TEST_CLASSIC_HEIGHT / 3 - 1);
	assert(!gpu_scene_active());
	assert(!gpu_scene_software_preview());
	backend_active = 1;
	gpu_scene_begin(0);
	assert(gpu_scene_active());
	assert(!gpu_scene_software_preview());
	gpu_scene_shutdown();
	assert(!gpu_scene_software_preview());
}

static legacy_f64 plane_depth(legacy_f64 x, legacy_f64 y)
{
	return TEST_TINY_DEPTH + x * TEST_PLANE_X_STEP + y * TEST_PLANE_Y_STEP;
}

static void assert_capture_in_viewport(legacy_s32 planar)
{
	for (size_t index = 0; index < captured_count; index++) {
		const struct VULKAN_SCENE_VERTEX *point = captured + index;
		assert(isfinite(point->x) && isfinite(point->y) && isfinite(point->inverse_depth));
		assert(point->x >= 0 && point->x <= TEST_WIDTH);
		assert(point->y >= 0 && point->y <= TEST_HEIGHT);
		if (planar) {
			legacy_f32 expected = (legacy_f32)plane_depth(point->x, point->y);
			assert(fabs((legacy_f64)point->inverse_depth - expected) <=
				   TEST_DEPTH_ROUNDING_ULPS * FLT_EPSILON * fabs(expected));
		}
	}
}

static void test_large_planar_clipping(void)
{
	const struct HIRES_BACKEND_VERTEX corners[TEST_RECTANGLE_VERTICES] = {
		{-TEST_HUGE_X, -TEST_HUGE_Y, TEST_DEPTH},
		{TEST_HUGE_X, -TEST_HUGE_Y, TEST_DEPTH},
		{TEST_HUGE_X, TEST_HUGE_Y, TEST_DEPTH},
		{-TEST_HUGE_X, TEST_HUGE_Y, TEST_DEPTH}};
	struct HIRES_BACKEND_MATERIAL paint = material(TEST_OPAQUE_COLOR);
	struct HIRES_RASTER_TARGET raster = target();
	for (legacy_s32 reverse = 0; reverse <= 1; reverse++) {
		struct HIRES_BACKEND_VERTEX vertices[TEST_RECTANGLE_VERTICES];
		for (legacy_u32 index = 0; index < TEST_RECTANGLE_VERTICES; index++) {
			vertices[index] = corners[reverse ? TEST_RECTANGLE_VERTICES - 1U - index : index];
			vertices[index].inverse_z = plane_depth(vertices[index].x, vertices[index].y);
		}
		reset_capture();
		gpu_scene_begin(0);
		gpu_scene_polygon(vertices, TEST_RECTANGLE_VERTICES, &paint);
		(void)gpu_scene_flush(&raster, 0);
		assert(submit_count == 1 && captured_count != 0);
		assert_capture_in_viewport(1);
		assert(fabs(captured_area() - TEST_WIDTH * TEST_HEIGHT) < TEST_EPSILON);
	}
	/* One triangle extends past all four viewport planes. Its lower edge is
	 * below the viewport even after perspective clipping, so coverage is full. */
	struct HIRES_BACKEND_VERTEX triangle[TEST_TRIANGLE_VERTICES] = {
		{-TEST_HUGE_X, -TEST_HUGE_Y, TEST_DEPTH},
		{TEST_HUGE_X, -TEST_HUGE_Y, TEST_DEPTH},
		{0, TEST_HUGE_Y, TEST_DEPTH}};
	for (legacy_u32 index = 0; index < TEST_TRIANGLE_VERTICES; index++) {
		triangle[index].inverse_z = plane_depth(triangle[index].x, triangle[index].y);
	}
	reset_capture();
	gpu_scene_begin(0);
	gpu_scene_polygon(triangle, TEST_TRIANGLE_VERTICES, &paint);
	(void)gpu_scene_flush(&raster, 0);
	assert(submit_count == 1 && captured_count != 0);
	assert_capture_in_viewport(1);
	assert(fabs(captured_area() - TEST_WIDTH * TEST_HEIGHT) < TEST_EPSILON);
}

static void test_disconnected_clipping(void)
{
	/* The U's connecting bar lies above the viewport. The exact intersection
	 * contains two disjoint arms; clipping it as one fan would bridge the gap. */
	const struct HIRES_BACKEND_VERTEX shape[TEST_CONCAVE_VERTICES] = {
		{-4, -8, TEST_DEPTH}, {20, -8, TEST_DEPTH}, {20, 16, TEST_DEPTH}, {14, 16, TEST_DEPTH},
		{14, -2, TEST_DEPTH}, {2, -2, TEST_DEPTH},	{2, 16, TEST_DEPTH},  {-4, 16, TEST_DEPTH}};
	struct HIRES_BACKEND_MATERIAL paint = material(TEST_OPAQUE_COLOR);
	struct HIRES_RASTER_TARGET raster = target();
	for (legacy_s32 reverse = 0; reverse <= 1; reverse++) {
		struct HIRES_BACKEND_VERTEX vertices[TEST_CONCAVE_VERTICES];
		for (legacy_u32 index = 0; index < TEST_CONCAVE_VERTICES; index++) {
			vertices[index] = shape[reverse ? TEST_CONCAVE_VERTICES - 1U - index : index];
		}
		reset_capture();
		gpu_scene_begin(0);
		gpu_scene_polygon(vertices, TEST_CONCAVE_VERTICES, &paint);
		(void)gpu_scene_flush(&raster, 0);
		assert(submit_count == 1 && captured_count != 0);
		assert_capture_in_viewport(0);
		assert(fabs(captured_area() - TEST_DISCONNECTED_AREA) < TEST_EPSILON);
		for (legacy_s32 y = 0; y < TEST_HEIGHT; y++) {
			for (legacy_s32 x = 0; x < TEST_WIDTH; x++) {
				legacy_s32 expected = x < 2 || x >= TEST_WIDTH - 2;
				legacy_s32 covered = 0;
				for (size_t index = 0; index < captured_count; index += TEST_TRIANGLE_VERTICES) {
					covered |= triangle_contains(captured + index, x + 0.5, y + 0.5);
				}
				assert(covered == expected);
			}
		}
	}
}

static void test_outside_rejection(void)
{
	const struct HIRES_BACKEND_VERTEX triangle[TEST_TRIANGLE_VERTICES] = {
		{1, 1, TEST_DEPTH}, {4, 1, TEST_DEPTH}, {1, 4, TEST_DEPTH}};
	struct HIRES_BACKEND_MATERIAL paint = material(TEST_OPAQUE_COLOR);
	struct HIRES_RASTER_TARGET raster = target();
	for (legacy_u32 plane = 0; plane < TEST_RECTANGLE_VERTICES; plane++) {
		struct HIRES_BACKEND_VERTEX vertices[TEST_TRIANGLE_VERTICES];
		for (legacy_u32 index = 0; index < TEST_TRIANGLE_VERTICES; index++) {
			vertices[index] = triangle[index];
			if (plane < 2U) {
				vertices[index].x += plane == 0 ? -TEST_OUTSIDE_SHIFT : TEST_OUTSIDE_SHIFT;
			} else {
				vertices[index].y += plane == 2U ? -TEST_OUTSIDE_SHIFT : TEST_OUTSIDE_SHIFT;
			}
		}
		reset_capture();
		gpu_scene_begin(0);
		gpu_scene_polygon(vertices, TEST_TRIANGLE_VERTICES, &paint);
		(void)gpu_scene_flush(&raster, 0);
		assert(submit_count == 0 && captured_count == 0);
	}
}

static void test_boundary_line_clipping(void)
{
	const legacy_f32 lines[TEST_RECTANGLE_VERTICES][TEST_RECTANGLE_VERTICES] = {
		{-4, 0, 20, 0}, {-4, 15, 20, 15}, {0, -4, 0, 20}, {15, -4, 15, 20}};
	struct HIRES_BACKEND_MATERIAL paint = material(TEST_OPAQUE_COLOR);
	struct HIRES_RASTER_TARGET raster = target();
	for (legacy_u32 line = 0; line < TEST_RECTANGLE_VERTICES; line++) {
		reset_capture();
		gpu_scene_begin(0);
		gpu_scene_line(lines[line][0], lines[line][1], lines[line][2], lines[line][3], &paint);
		(void)gpu_scene_flush(&raster, 0);
		assert(submit_count == 1 && captured_count != 0);
		assert_capture_in_viewport(0);
		assert(fabs(captured_area() - TEST_WIDTH) < TEST_EPSILON);
	}
	reset_capture();
	gpu_scene_begin(0);
	gpu_scene_line(-TEST_POINT_OVERHANG, 5, -TEST_POINT_OVERHANG, 5, &paint);
	(void)gpu_scene_flush(&raster, 0);
	assert(submit_count == 1 && captured_count != 0);
	assert_capture_in_viewport(0);
	assert(fabs(captured_area() - (1 - TEST_POINT_OVERHANG)) < TEST_EPSILON);
}

static void test_cockpit_scope(void)
{
	const struct HIRES_BACKEND_VERTEX windshield[TEST_RECTANGLE_VERTICES] = {
		{80, 20, TEST_DEPTH}, {240, 20, TEST_DEPTH}, {240, 120, TEST_DEPTH}, {80, 120, TEST_DEPTH}};
	struct HIRES_BACKEND_MATERIAL paint = material(TEST_OPAQUE_COLOR);
	struct HIRES_RASTER_TARGET raster;
	target_cockpit = 1;
	assert(hires_raster_prepare(&raster));
	reset_capture();
	gpu_scene_begin(0);
	assert(gpu_scene_active());
	assert(!gpu_scene_software_preview());
	gpu_scene_polygon(windshield, TEST_RECTANGLE_VERTICES, &paint);
	assert(gpu_scene_flush(&raster, 0));
	assert(submitted_width == TEST_CLASSIC_WIDTH && submitted_height == TEST_CLASSIC_HEIGHT);
	assert(submitted_left == TEST_COCKPIT_LEFT && submitted_right == TEST_COCKPIT_RIGHT &&
		   submitted_top == TEST_COCKPIT_TOP && submitted_bottom == TEST_COCKPIT_BOTTOM);
	assert(captured_count == TEST_QUAD_TRIANGLE_VERTICES);
	target_cockpit = 0;
	struct SPRITE sprite = {0};
	sprite.sprite_buffer_width = TEST_CLASSIC_WIDTH;
	sprite.sprite_raster_left = TEST_COCKPIT_LEFT;
	sprite.sprite_raster_right = TEST_COCKPIT_RIGHT;
	sprite.sprite_top = TEST_COCKPIT_TOP;
	sprite.sprite_bottom = TEST_COCKPIT_BOTTOM;
	assert(gpu_scene_target_eligible(&sprite, TEST_CLASSIC_WIDTH, TEST_CLASSIC_HEIGHT));
	reset_capture();
	gpu_scene_classic_begin(&sprite, TEST_CLASSIC_WIDTH, TEST_COCKPIT_BOTTOM);
	assert(gpu_scene_active());
	assert(!gpu_scene_software_preview());
	gpu_scene_polygon(windshield, TEST_RECTANGLE_VERTICES, &paint);
	assert(gpu_scene_classic_flush(&sprite, TEST_CLASSIC_WIDTH, TEST_COCKPIT_BOTTOM));
	assert(submitted_classic && submitted_width == TEST_CLASSIC_WIDTH &&
		   submitted_height == TEST_CLASSIC_HEIGHT && submitted_left == TEST_COCKPIT_LEFT &&
		   submitted_right == TEST_COCKPIT_RIGHT && submitted_top == TEST_COCKPIT_TOP &&
		   submitted_bottom == TEST_COCKPIT_BOTTOM);
	sprite.sprite_buffer_width = TEST_WIDTH;
	assert(!gpu_scene_target_eligible(&sprite, TEST_CLASSIC_WIDTH, TEST_CLASSIC_HEIGHT));
	gpu_scene_shutdown();
}

legacy_int main(void)
{
	backend_active = 1;
	legacy_s32 scene_enabled = 0;
#ifdef RESTUNTS_VULKAN_SCENE
	scene_enabled = 1;
#endif
	if (!scene_enabled) {
		struct SPRITE sprite = {0};
		target_eligible = 0;
		gpu_scene_begin(0);
		assert(!gpu_scene_active() && !gpu_scene_software_preview());
		gpu_scene_classic_begin(&sprite, TEST_WIDTH, TEST_HEIGHT);
		assert(!gpu_scene_active() && !gpu_scene_software_preview());
		gpu_scene_shutdown();
		assert(!gpu_scene_software_preview());
		puts("Disabled GPU scene preserves the compute fallback.");
		return 0;
	}
	for (legacy_s32 reverse = 0; reverse <= 1; reverse++) {
		for (legacy_s32 variant = 0; variant < TEST_CONCAVE_VARIANTS; variant++) {
			concave_polygon(reverse, variant);
		}
	}
	test_material_order(0);
	test_material_order(1);
	test_invalid_geometry();
	test_lines();
	test_scope_and_viewport();
	test_cockpit_scope();
	test_large_planar_clipping();
	test_disconnected_clipping();
	test_outside_rejection();
	test_boundary_line_clipping();
	puts("GPU scene geometry, viewport scope and software preview policy passed.");
	return 0;
}
