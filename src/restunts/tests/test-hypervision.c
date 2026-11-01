#include "../c/video/video_framebuffer.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL_stdinc.h>
#include "../c/hires/hypervision.h"
#include "../c/platform.h"
#include "../c/hires/render_workers.h"
#include "../c/shape2d/shape2d.h"
#include "../c/shape2d/shape2d_internal.h"

#define TEST_WIDTH (HIRES_WIDTH / HIRES_SCALE)
#define TEST_HEIGHT (HIRES_HEIGHT / HIRES_SCALE)
#define TEST_SCREEN_SEGMENT 0xA000U
#define TEST_BACKGROUND 3U
#define TEST_FIRST_COLOR 7U
#define TEST_SECOND_COLOR 8U
#define TEST_DECAL_COLOR 9U
#define TEST_FIRST_FAMILY 1U
#define TEST_SECOND_FAMILY 2U
#define TEST_NEAR_DEPTH 3.0
#define TEST_MIDDLE_DEPTH 2.0
#define TEST_FAR_DEPTH 1.0
#define TEST_BORDER 12
#define TEST_RECTANGLE_VERTICES 4U
#define TEST_CONCAVE_VERTICES 8U
#define TEST_GROWTH_COMMANDS 2049U
#define TEST_PATTERN_EVEN_COLUMNS 0xAAAAU
#define TEST_WORKER_COUNT "2"
#define TEST_WORKER_THREADS 2
#define TEST_NO_WORKERS "0"
#define TEST_SCENE_POLYGONS 19U
#define TEST_RANDOM_MULTIPLIER 1664525U
#define TEST_RANDOM_INCREMENT 1013904223U
#define TEST_RANDOM_SEED 38129U
#define TEST_PLANE_DEPTH 0.002
#define TEST_PLANE_STEP 0.0000001
#define TEST_PLANE_SEPARATION 0.000009147
#define TEST_SAMPLE_JITTER 0.125
#define TEST_DEPTH_TOLERANCE (8 * FLT_EPSILON)

static struct SPRITE sprite;
static struct HIRES_RASTER_TARGET target;
static legacy_u8 rows[TEST_HEIGHT * LEGACY_WORD_BYTES];
static legacy_u8 *screen;
static legacy_u8 reference[HIRES_WIDTH * HIRES_HEIGHT];

static void begin_frame(legacy_s32 scale)
{
	hires_shutdown();
	hires_set_enabled(1);
	hires_set_render_scale(scale);
	memset(screen, TEST_BACKGROUND, TEST_WIDTH * TEST_HEIGHT);
	assert(hires_begin(&sprite));
	hires_depth_begin(0, hires_render_width(), 0, hires_render_height());
	assert(hires_raster_prepare(&target));
	hires_backend_begin(NULL);
}

static const legacy_u8 *end_frame(void)
{
	(void)hires_backend_end(&target, 0);
	hires_end();
	legacy_s32 width, height;
	const legacy_u8 *pixels = video_framebuffer(screen, &width, &height);
	assert(pixels != NULL && width == target.width && height == target.height);
	return pixels;
}

static struct HIRES_BACKEND_MATERIAL material(legacy_u16 color, legacy_u32 family)
{
	struct HIRES_BACKEND_MATERIAL result = {family, color, 0, 0, HIRES_PAINT_SOLID, 0};
	return result;
}

static void rectangle(legacy_f64 left, legacy_f64 right, legacy_f64 top, legacy_f64 bottom,
					  legacy_f64 left_depth, legacy_f64 right_depth,
					  const struct HIRES_BACKEND_MATERIAL *paint)
{
	const struct HIRES_BACKEND_VERTEX vertices[TEST_RECTANGLE_VERTICES] = {
		{left, top, left_depth},
		{right, top, right_depth},
		{right, bottom, right_depth},
		{left, bottom, left_depth}};
	hires_backend_polygon(vertices, TEST_RECTANGLE_VERTICES, paint);
}

static void assert_same_pixels(const legacy_u8 *pixels)
{
	assert(memcmp(reference, pixels, (legacy_u32)target.width * target.height) == 0);
}

static void test_crossing_surfaces(legacy_s32 scale)
{
	const struct HIRES_BACKEND_MATERIAL first = material(TEST_FIRST_COLOR, TEST_FIRST_FAMILY);
	const struct HIRES_BACKEND_MATERIAL second = material(TEST_SECOND_COLOR, TEST_SECOND_FAMILY);
	for (legacy_s32 reverse = 0; reverse <= 1; reverse++) {
		begin_frame(scale);
		legacy_s32 border = TEST_BORDER * scale;
		legacy_s32 left = border, right = target.width - border;
		legacy_s32 top = border, bottom = target.height - border;
		for (legacy_s32 command = 0; command <= 1; command++) {
			if ((command ^ reverse) == 0) {
				rectangle(left, right, top, bottom, TEST_FAR_DEPTH, TEST_NEAR_DEPTH, &first);
			} else {
				rectangle(left, right, top, bottom, TEST_NEAR_DEPTH, TEST_FAR_DEPTH, &second);
			}
		}
		const legacy_u8 *pixels = end_frame();
		for (legacy_s32 y = 0; y < target.height; y++) {
			for (legacy_s32 x = 0; x < target.width; x++) {
				legacy_u8 expected = TEST_BACKGROUND;
				if (x >= left && x < right && y >= top && y < bottom) {
					expected = x < target.width / 2 ? TEST_SECOND_COLOR : TEST_FIRST_COLOR;
				}
				assert(pixels[y * target.width + x] == expected);
			}
		}
		if (reverse == 0) {
			memcpy(reference, pixels, (legacy_u32)target.width * target.height);
		} else {
			assert_same_pixels(pixels);
		}
	}
}

static void test_concave_coverage(legacy_s32 scale)
{
	const struct HIRES_BACKEND_MATERIAL paint = material(TEST_FIRST_COLOR, TEST_FIRST_FAMILY);
	for (legacy_s32 reverse = 0; reverse <= 1; reverse++) {
		begin_frame(scale);
		legacy_s32 left = target.width / 8, right = target.width * 7 / 8;
		legacy_s32 top = target.height / 8, bottom = target.height * 7 / 8;
		legacy_s32 notch_left = target.width * 3 / 8, notch_right = target.width * 5 / 8;
		legacy_s32 notch_top = target.height / 2;
		const struct HIRES_BACKEND_VERTEX outline[TEST_CONCAVE_VERTICES] = {
			{left, top, TEST_FAR_DEPTH},
			{right, top, TEST_FAR_DEPTH},
			{right, bottom, TEST_FAR_DEPTH},
			{notch_right, bottom, TEST_FAR_DEPTH},
			{notch_right, notch_top, TEST_FAR_DEPTH},
			{notch_left, notch_top, TEST_FAR_DEPTH},
			{notch_left, bottom, TEST_FAR_DEPTH},
			{left, bottom, TEST_FAR_DEPTH}};
		struct HIRES_BACKEND_VERTEX vertices[TEST_CONCAVE_VERTICES];
		for (legacy_u32 index = 0; index < TEST_CONCAVE_VERTICES; index++) {
			vertices[index] = outline[reverse ? TEST_CONCAVE_VERTICES - index - 1U : index];
		}
		hires_backend_polygon(vertices, TEST_CONCAVE_VERTICES, &paint);
		const legacy_u8 *pixels = end_frame();
		for (legacy_s32 y = 0; y < target.height; y++) {
			for (legacy_s32 x = 0; x < target.width; x++) {
				legacy_s32 covered = x >= left && x < right && y >= top && y < bottom &&
									 (x < notch_left || x >= notch_right || y < notch_top);
				assert(pixels[y * target.width + x] ==
					   (covered ? TEST_FIRST_COLOR : TEST_BACKGROUND));
			}
		}
	}
}

static void test_cutout_and_occluder(legacy_s32 scale)
{
	const struct HIRES_BACKEND_MATERIAL backing = material(TEST_FIRST_COLOR, TEST_FIRST_FAMILY);
	struct HIRES_BACKEND_MATERIAL cutout = material(TEST_SECOND_COLOR, TEST_SECOND_FAMILY);
	cutout.mode = HIRES_PAINT_PATTERN;
	cutout.pattern = TEST_PATTERN_EVEN_COLUMNS;
	const struct HIRES_BACKEND_MATERIAL occluder = material(TEST_DECAL_COLOR, TEST_SECOND_FAMILY);
	for (legacy_s32 reverse = 0; reverse <= 1; reverse++) {
		begin_frame(scale);
		for (legacy_s32 command = 0; command < 3; command++) {
			legacy_s32 selected = reverse ? 2 - command : command;
			if (selected == 0) {
				rectangle(0, target.width, 0, target.height, TEST_FAR_DEPTH, TEST_FAR_DEPTH,
						  &backing);
			} else if (selected == 1) {
				rectangle(0, target.width, 0, target.height, TEST_MIDDLE_DEPTH, TEST_MIDDLE_DEPTH,
						  &cutout);
			} else {
				rectangle(target.width / 2, target.width, 0, target.height, TEST_NEAR_DEPTH,
						  TEST_NEAR_DEPTH, &occluder);
			}
		}
		const legacy_u8 *pixels = end_frame();
		for (legacy_s32 y = 0; y < target.height; y++) {
			for (legacy_s32 x = 0; x < target.width; x++) {
				legacy_u8 expected = x >= target.width / 2
										 ? TEST_DECAL_COLOR
										 : ((x & 1) == 0 ? TEST_SECOND_COLOR : TEST_FIRST_COLOR);
				assert(pixels[y * target.width + x] == expected);
			}
		}
	}
}

static void test_coplanar_decal(legacy_s32 scale)
{
	const struct HIRES_BACKEND_MATERIAL backing = material(TEST_FIRST_COLOR, TEST_FIRST_FAMILY);
	struct HIRES_BACKEND_MATERIAL decal = material(TEST_DECAL_COLOR, TEST_FIRST_FAMILY);
	decal.flags = HIRES_BACKEND_DECAL;
	for (legacy_s32 reverse = 0; reverse <= 1; reverse++) {
		begin_frame(scale);
		for (legacy_s32 command = 0; command <= 1; command++) {
			if ((command ^ reverse) == 0) {
				rectangle(0, target.width, 0, target.height, TEST_FAR_DEPTH, TEST_NEAR_DEPTH,
						  &backing);
			} else {
				rectangle(0, target.width, 0, target.height, TEST_FAR_DEPTH, TEST_NEAR_DEPTH,
						  &decal);
			}
		}
		const legacy_u8 *pixels = end_frame();
		for (legacy_u32 index = 0; index < (legacy_u32)target.width * target.height; index++) {
			assert(pixels[index] == TEST_DECAL_COLOR);
		}
	}
}

struct TEST_POLYGON {
	struct HIRES_BACKEND_VERTEX vertices[TEST_RECTANGLE_VERTICES];
	legacy_f64 x_step, y_step, origin;
	struct HIRES_BACKEND_MATERIAL paint;
};

static legacy_u32 random_coordinate(legacy_u32 *state)
{
	*state = *state * TEST_RANDOM_MULTIPLIER + TEST_RANDOM_INCREMENT;
	return *state;
}

static legacy_s32 contains_sample(const struct TEST_POLYGON *polygon, legacy_f64 x, legacy_f64 y)
{
	legacy_s32 inside = 0;
	const struct HIRES_BACKEND_VERTEX *previous = &polygon->vertices[TEST_RECTANGLE_VERTICES - 1U];
	for (legacy_u32 index = 0; index < TEST_RECTANGLE_VERTICES; index++) {
		const struct HIRES_BACKEND_VERTEX *current = &polygon->vertices[index];
		if ((previous->y > y) != (current->y > y) &&
			x < previous->x +
					(current->x - previous->x) * (y - previous->y) / (current->y - previous->y)) {
			inside = !inside;
		}
		previous = current;
	}
	return inside;
}

static void test_offscreen_depth_oracle(legacy_s32 scale)
{
	struct TEST_POLYGON polygons[TEST_SCENE_POLYGONS];
	legacy_u32 random = TEST_RANDOM_SEED;
	for (legacy_u32 index = 0; index < TEST_SCENE_POLYGONS; index++) {
		struct TEST_POLYGON *polygon = &polygons[index];
		legacy_f64 center_x = ((legacy_s32)(random_coordinate(&random) % (TEST_WIDTH * 2)) -
							   TEST_WIDTH / 2 + TEST_SAMPLE_JITTER) *
							  scale;
		legacy_f64 center_y = ((legacy_s32)(random_coordinate(&random) % (TEST_HEIGHT * 2)) -
							   TEST_HEIGHT / 2 + TEST_SAMPLE_JITTER) *
							  scale;
		legacy_f64 along_x = (random_coordinate(&random) % TEST_WIDTH + TEST_BORDER) * scale;
		legacy_f64 along_y =
			((legacy_s32)(random_coordinate(&random) % TEST_HEIGHT) - TEST_HEIGHT / 2) * scale;
		legacy_f64 across_x = -along_y / 2;
		legacy_f64 across_y = along_x / 2;
		polygon->origin = TEST_PLANE_DEPTH + index * TEST_PLANE_SEPARATION;
		polygon->x_step = ((legacy_s32)(index % 7U) - 3) * TEST_PLANE_STEP / scale;
		polygon->y_step = ((legacy_s32)(index % 5U) - 2) * TEST_PLANE_STEP / scale;
		polygon->paint =
			material((legacy_u16)(TEST_FIRST_COLOR + index), TEST_FIRST_FAMILY + index);
		for (legacy_u32 vertex = 0; vertex < TEST_RECTANGLE_VERTICES; vertex++) {
			legacy_f64 along = vertex == 0 || vertex == 3 ? -1 : 1;
			legacy_f64 across = vertex < 2 ? -1 : 1;
			struct HIRES_BACKEND_VERTEX *point = &polygon->vertices[vertex];
			point->x = center_x + along * along_x + across * across_x;
			point->y = center_y + along * along_y + across * across_y;
			point->inverse_z =
				polygon->origin + point->x * polygon->x_step + point->y * polygon->y_step;
			assert(point->inverse_z > 0);
		}
	}
	for (legacy_s32 reverse = 0; reverse <= 1; reverse++) {
		begin_frame(scale);
		for (legacy_u32 command = 0; command < TEST_SCENE_POLYGONS; command++) {
			const struct TEST_POLYGON *polygon =
				&polygons[reverse ? TEST_SCENE_POLYGONS - command - 1U : command];
			hires_backend_polygon(polygon->vertices, TEST_RECTANGLE_VERTICES, &polygon->paint);
		}
		const legacy_u8 *pixels = end_frame();
		for (legacy_s32 y = 0; y < target.height; y++) {
			for (legacy_s32 x = 0; x < target.width; x++) {
				legacy_f64 sample_x = x + HIRES_SAMPLE_CENTER_OFFSET;
				legacy_f64 sample_y = y + HIRES_SAMPLE_CENTER_OFFSET;
				legacy_f64 nearest = 0;
				legacy_u8 expected = TEST_BACKGROUND;
				for (legacy_u32 index = 0; index < TEST_SCENE_POLYGONS; index++) {
					const struct TEST_POLYGON *polygon = &polygons[index];
					legacy_f64 depth =
						polygon->origin + sample_x * polygon->x_step + sample_y * polygon->y_step;
					if (depth > nearest && contains_sample(polygon, sample_x, sample_y)) {
						nearest = depth;
						expected = (legacy_u8)polygon->paint.color;
					}
				}
				assert(pixels[y * target.width + x] == expected);
			}
		}
		if (reverse == 0) {
			memcpy(reference, pixels, (legacy_u32)target.width * target.height);
		} else {
			assert_same_pixels(pixels);
		}
	}
}

static void test_coplanar_edge_interpolation(legacy_s32 scale)
{
	/* A wide ground surface and a road strip sample the same plane through
	 * different edges. Roundoff must not alternate their paint precedence. */
	const struct HIRES_BACKEND_MATERIAL ground = material(TEST_FIRST_COLOR, TEST_FIRST_FAMILY);
	const struct HIRES_BACKEND_MATERIAL road = material(TEST_SECOND_COLOR, TEST_SECOND_FAMILY);
	static const legacy_f64 coordinates[][TEST_RECTANGLE_VERTICES][2] = {
		{{-400, -200}, {600, -200}, {600, 400}, {-400, 400}},
		{{-100, 175.125}, {375, 50.125}, {375, 100.125}, {-100, 225.125}}};
	struct TEST_POLYGON polygons[2];
	begin_frame(scale);
	for (legacy_u32 shape = 0; shape < 2; shape++) {
		polygons[shape].paint = shape == 0 ? ground : road;
		for (legacy_u32 vertex = 0; vertex < TEST_RECTANGLE_VERTICES; vertex++) {
			struct HIRES_BACKEND_VERTEX *point = &polygons[shape].vertices[vertex];
			point->x = coordinates[shape][vertex][0] * scale;
			point->y = coordinates[shape][vertex][1] * scale;
			point->inverse_z = TEST_PLANE_DEPTH + TEST_PLANE_STEP * point->y / scale;
		}
		hires_backend_polygon(polygons[shape].vertices, TEST_RECTANGLE_VERTICES,
							  &polygons[shape].paint);
	}
	const legacy_u8 *pixels = end_frame();
	for (legacy_s32 y = 0; y < target.height; y++) {
		for (legacy_s32 x = 0; x < target.width; x++) {
			legacy_u8 expected = contains_sample(&polygons[1], x + HIRES_SAMPLE_CENTER_OFFSET,
												 y + HIRES_SAMPLE_CENTER_OFFSET)
									 ? TEST_SECOND_COLOR
									 : TEST_FIRST_COLOR;
			assert(pixels[y * target.width + x] == expected);
		}
	}
}

static void test_clipping_at_pixel_centers(legacy_s32 scale)
{
	const struct HIRES_BACKEND_MATERIAL paint = material(TEST_FIRST_COLOR, TEST_FIRST_FAMILY);
	begin_frame(scale);
	legacy_s32 last_column = target.width - 1;
	legacy_s32 last_row = target.height - 1;
	/* A left/top edge through the final sample center includes that sample. */
	rectangle(last_column + HIRES_SAMPLE_CENTER_OFFSET, target.width,
			  last_row + HIRES_SAMPLE_CENTER_OFFSET, target.height, TEST_FAR_DEPTH, TEST_FAR_DEPTH,
			  &paint);
	const legacy_u8 *pixels = end_frame();
	for (legacy_s32 y = 0; y < target.height; y++) {
		for (legacy_s32 x = 0; x < target.width; x++) {
			legacy_u8 expected =
				x == last_column && y == last_row ? TEST_FIRST_COLOR : TEST_BACKGROUND;
			assert(pixels[y * target.width + x] == expected);
		}
	}
	begin_frame(scale);
	/* A right/bottom edge through a sample center excludes that sample. */
	rectangle(0, last_column + HIRES_SAMPLE_CENTER_OFFSET, 0, last_row + HIRES_SAMPLE_CENTER_OFFSET,
			  TEST_FAR_DEPTH, TEST_FAR_DEPTH, &paint);
	pixels = end_frame();
	for (legacy_s32 y = 0; y < target.height; y++) {
		for (legacy_s32 x = 0; x < target.width; x++) {
			legacy_u8 expected =
				x < last_column && y < last_row ? TEST_FIRST_COLOR : TEST_BACKGROUND;
			assert(pixels[y * target.width + x] == expected);
		}
	}
}

static void test_attached_support_depth(legacy_s32 scale)
{
	const struct HIRES_BACKEND_MATERIAL support = material(TEST_FIRST_COLOR, TEST_FIRST_FAMILY);
	const struct HIRES_BACKEND_MATERIAL occluder = material(TEST_SECOND_COLOR, TEST_SECOND_FAMILY);
	struct HIRES_BACKEND_MATERIAL decal = material(TEST_DECAL_COLOR, TEST_FIRST_FAMILY);
	decal.flags = HIRES_BACKEND_DECAL;
	for (legacy_s32 reverse = 0; reverse <= 1; reverse++) {
		begin_frame(scale);
		legacy_s32 support_right = target.width * 3 / 4;
		legacy_s32 occluder_left = target.width / 2;
		for (legacy_s32 command = 0; command < 3; command++) {
			legacy_s32 selected = reverse ? 2 - command : command;
			if (selected == 0) {
				rectangle(0, support_right, 0, target.height, TEST_MIDDLE_DEPTH, TEST_MIDDLE_DEPTH,
						  &support);
			} else if (selected == 1) {
				rectangle(occluder_left, support_right, 0, target.height, TEST_NEAR_DEPTH,
						  TEST_NEAR_DEPTH, &occluder);
			} else {
				rectangle(0, target.width, 0, target.height, TEST_FAR_DEPTH, TEST_FAR_DEPTH,
						  &decal);
			}
		}
		const legacy_u8 *pixels = end_frame();
		for (legacy_s32 y = 0; y < target.height; y++) {
			for (legacy_s32 x = 0; x < target.width; x++) {
				legacy_s32 occluded = x >= occluder_left && x < support_right;
				legacy_u32 offset = (legacy_u32)y * target.width + x;
				legacy_f32 depth = (legacy_f32)(occluded ? TEST_NEAR_DEPTH
														 : (x < support_right ? TEST_MIDDLE_DEPTH
																			  : TEST_FAR_DEPTH));
				assert(pixels[offset] == (occluded ? TEST_SECOND_COLOR : TEST_DECAL_COLOR));
				/* Authored overlay vertices do not move the physical surface
				 * read by other shapes, patterned ghosts and car shadows. */
				assert(target.inverse_depth[offset] == depth);
				assert(target.depth_family[offset] ==
					   (occluded ? TEST_SECOND_FAMILY : TEST_FIRST_FAMILY));
			}
		}
	}
}

static void test_ground_uses_surface_ownership(legacy_s32 scale)
{
	struct HIRES_BACKEND_MATERIAL support = material(TEST_FIRST_COLOR, TEST_FIRST_FAMILY);
	struct HIRES_BACKEND_MATERIAL detail = material(TEST_DECAL_COLOR, TEST_SECOND_FAMILY);
	detail.flags = HIRES_BACKEND_GROUND_CLIP;
	for (legacy_s32 patterned = 0; patterned <= 1; patterned++) {
		begin_frame(scale);
		struct HIRES_DEPTH_PLANE ground = {(TEST_NEAR_DEPTH - TEST_FAR_DEPTH) / target.width, 0,
										   TEST_FAR_DEPTH};
		hires_backend_begin(&ground);
		legacy_s32 support_left = target.width * 3 / 4;
		rectangle(support_left, target.width, 0, target.height, TEST_FAR_DEPTH, TEST_FAR_DEPTH,
				  &support);
		detail.mode = patterned ? HIRES_PAINT_PATTERN : HIRES_PAINT_SOLID;
		detail.pattern = TEST_PATTERN_EVEN_COLUMNS;
		rectangle(0, target.width, 0, target.height, TEST_MIDDLE_DEPTH, TEST_MIDDLE_DEPTH, &detail);
		const legacy_u8 *pixels = end_frame();
		/* Derive the crossing from fixture depths without rounding the per-pixel slope. */
		legacy_s32 ground_boundary =
			(legacy_s32)(target.width * (TEST_MIDDLE_DEPTH - TEST_FAR_DEPTH) /
						 (TEST_NEAR_DEPTH - TEST_FAR_DEPTH));
		for (legacy_s32 y = 0; y < target.height; y++) {
			for (legacy_s32 x = 0; x < target.width; x++) {
				legacy_s32 supported = x >= support_left;
				legacy_s32 covered =
					(supported || x <= ground_boundary) && (!patterned || (x & 1) == 0);
				legacy_u8 expected =
					covered ? TEST_DECAL_COLOR : (supported ? TEST_FIRST_COLOR : TEST_BACKGROUND);
				assert(pixels[y * target.width + x] == expected);
			}
		}
	}
}

/* Custom road models can contain warped quads. Their edge-interpolated depth
 * must not be replaced by the plane through only three of their vertices. */
static void test_warped_quad_depth(legacy_s32 scale)
{
	const struct HIRES_BACKEND_MATERIAL warped = material(TEST_FIRST_COLOR, TEST_FIRST_FAMILY);
	const struct HIRES_BACKEND_MATERIAL flat = material(TEST_SECOND_COLOR, TEST_SECOND_FAMILY);
	for (legacy_s32 reverse = 0; reverse <= 1; reverse++) {
		begin_frame(scale);
		const struct HIRES_BACKEND_VERTEX vertices[TEST_RECTANGLE_VERTICES] = {
			{0, 0, TEST_FAR_DEPTH},
			{target.width, 0, TEST_FAR_DEPTH},
			{target.width, target.height, TEST_NEAR_DEPTH},
			{0, target.height, TEST_FAR_DEPTH}};
		for (legacy_s32 order = 0; order <= 1; order++) {
			if ((order ^ reverse) == 0) {
				hires_backend_polygon(vertices, TEST_RECTANGLE_VERTICES, &warped);
			} else {
				rectangle(0, target.width, 0, target.height, TEST_MIDDLE_DEPTH, TEST_MIDDLE_DEPTH,
						  &flat);
			}
		}
		const legacy_u8 *pixels = end_frame();
		for (legacy_s32 y = 0; y < target.height; y++) {
			for (legacy_s32 x = 0; x < target.width; x++) {
				legacy_f64 u = (x + HIRES_SAMPLE_CENTER_OFFSET) / target.width;
				legacy_f64 v = (y + HIRES_SAMPLE_CENTER_OFFSET) / target.height;
				legacy_f64 depth = TEST_FAR_DEPTH + (TEST_NEAR_DEPTH - TEST_FAR_DEPTH) * u * v;
				legacy_s32 visible = depth >= TEST_MIDDLE_DEPTH;
				legacy_u32 offset = (legacy_u32)y * target.width + x;
				/* Coincident depths can choose either surface within the
				 * renderer's floating-point tolerance. Away from that
				 * narrow crossing, require the independent bilinear result. */
				legacy_f64 separation = depth - TEST_MIDDLE_DEPTH;
				if (separation < -TEST_MIDDLE_DEPTH * TEST_DEPTH_TOLERANCE ||
					separation > TEST_MIDDLE_DEPTH * TEST_DEPTH_TOLERANCE) {
					assert(pixels[offset] == (visible ? TEST_FIRST_COLOR : TEST_SECOND_COLOR));
				} else {
					assert(pixels[offset] == TEST_FIRST_COLOR ||
						   pixels[offset] == TEST_SECOND_COLOR);
				}
				legacy_f64 expected = visible ? depth : TEST_MIDDLE_DEPTH;
				legacy_f64 error = target.inverse_depth[offset] - expected;
				assert(error >= -expected * TEST_DEPTH_TOLERANCE &&
					   error <= expected * TEST_DEPTH_TOLERANCE);
			}
		}
	}
}

static void test_growth_and_reset(void)
{
	begin_frame(HIRES_SCALE);
	legacy_s32 row = target.height / 2;
	for (legacy_u32 command = 0; command < TEST_GROWTH_COMMANDS; command++) {
		legacy_s32 column = command % target.width;
		struct HIRES_BACKEND_MATERIAL paint =
			material((column & 1) == 0 ? TEST_FIRST_COLOR : TEST_SECOND_COLOR, TEST_FIRST_FAMILY);
		rectangle(column, column + 1, row, row + 1, command + TEST_FAR_DEPTH,
				  command + TEST_FAR_DEPTH, &paint);
	}
	const legacy_u8 *pixels = end_frame();
	for (legacy_s32 y = 0; y < target.height; y++) {
		for (legacy_s32 x = 0; x < target.width; x++) {
			legacy_u8 expected =
				y == row ? ((x & 1) == 0 ? TEST_FIRST_COLOR : TEST_SECOND_COLOR) : TEST_BACKGROUND;
			assert(pixels[y * target.width + x] == expected);
		}
	}
	/* Beginning a new empty frame must discard every edge, bin and visibility span. */
	begin_frame(HIRES_MINIMUM_SCALE);
	pixels = end_frame();
	for (legacy_u32 index = 0; index < (legacy_u32)target.width * target.height; index++) {
		assert(pixels[index] == TEST_BACKGROUND);
	}
}

static void test_workers_match(void)
{
	for (legacy_s32 workers = 0; workers <= 1; workers++) {
		hires_render_workers_shutdown();
		assert(SDL_setenv_unsafe("RESTUNTS_RENDER_WORKERS",
								 workers ? TEST_WORKER_COUNT : TEST_NO_WORKERS, true) == 0);
#if !defined(__DJGPP__) && !defined(__EMSCRIPTEN__)
		assert(hires_render_workers_count() == (workers ? TEST_WORKER_THREADS : 0));
#endif
		begin_frame(HIRES_SCALE);
		const struct HIRES_BACKEND_MATERIAL first = material(TEST_FIRST_COLOR, TEST_FIRST_FAMILY);
		struct HIRES_BACKEND_MATERIAL second = material(TEST_SECOND_COLOR, TEST_SECOND_FAMILY);
		rectangle(0, target.width, 0, target.height, TEST_FAR_DEPTH, TEST_NEAR_DEPTH, &first);
		rectangle(0, target.width, 0, target.height, TEST_NEAR_DEPTH, TEST_FAR_DEPTH, &second);
		second.mode = HIRES_PAINT_PATTERN;
		second.pattern = TEST_PATTERN_EVEN_COLUMNS;
		second.color = TEST_DECAL_COLOR;
		rectangle(0, target.width, 0, target.height, TEST_NEAR_DEPTH, TEST_NEAR_DEPTH, &second);
		const legacy_u8 *pixels = end_frame();
		if (workers == 0) {
			memcpy(reference, pixels, (legacy_u32)target.width * target.height);
		} else {
			assert_same_pixels(pixels);
		}
	}
	hires_render_workers_shutdown();
}

legacy_int main(void)
{
	screen = dos_memory_make_pointer(TEST_SCREEN_SEGMENT, 0);
	sprite.sprite_bitmapptr = (struct SHAPE2D *)screen;
	sprite.sprite_lineofs = rows;
	sprite.sprite_right = sprite.sprite_pitch = sprite.sprite_buffer_width = TEST_WIDTH;
	sprite.sprite_raster_right = TEST_WIDTH;
	sprite.sprite_bottom = TEST_HEIGHT;
	for (legacy_u32 row = 0; row < TEST_HEIGHT; row++) {
		LEGACY_WRITE_U16_LE(rows + row * LEGACY_WORD_BYTES, row * TEST_WIDTH);
	}
	for (legacy_s32 scale = HIRES_MINIMUM_SCALE; scale <= HIRES_SCALE;
		 scale *= HIRES_MEDIUM_SCALE) {
		test_crossing_surfaces(scale);
		test_concave_coverage(scale);
		test_cutout_and_occluder(scale);
		test_coplanar_decal(scale);
		test_clipping_at_pixel_centers(scale);
		test_offscreen_depth_oracle(scale);
		test_coplanar_edge_interpolation(scale);
		test_attached_support_depth(scale);
		test_ground_uses_surface_ownership(scale);
		test_warped_quad_depth(scale);
	}
	test_growth_and_reset();
	test_workers_match();
	hires_backend_shutdown();
	hires_shutdown();
	puts("HyperVision visibility, cutouts, decals, queue lifetime and worker parity passed.");
	return 0;
}
