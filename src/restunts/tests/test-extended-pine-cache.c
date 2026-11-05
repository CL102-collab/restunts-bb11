#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "../c/extended/extended_frame_pine_cache.h"
#include "../c/extended/extended_geometry.h"
#include "../c/extended/extended_track.h"
#include "../c/extended/pine_trees.h"
#include "../c/hires/render_workers.h"
#include "../c/shape3d/shape3d_internal.h"

#if defined(__SANITIZE_ADDRESS__)
#define PINE_CACHE_TEST_ASAN
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define PINE_CACHE_TEST_ASAN
#endif
#endif
#ifdef PINE_CACHE_TEST_ASAN
#include <sanitizer/asan_interface.h>
#endif

extern legacy_u8 primidxcounttab[SHAPE3D_PRIMITIVE_TYPE_COUNT];

enum {
	TEST_VERTEX_COUNT = 4,
	TEST_PRIMITIVE_COUNT = 3,
	TEST_PAINT_COUNT = 2,
	TEST_ROTATIONS = 4,
	TEST_TRACK_SIDE = 12,
	TEST_ERROR_BYTES = 192,
	TEST_FOOTER_BYTES = 2,
	TEST_PARALLEL_JOBS = 32,
	TEST_PARALLEL_REUSE = 4,
	TEST_PARALLEL_REPEATS = 2
};

struct MESH_FIXTURE {
	struct SHAPE3D full, low;
	legacy_u8 vertices[TEST_VERTEX_COUNT * SHAPE3D_VERTEX_SIZE];
	legacy_u8 primitives[18];
	legacy_u8 visibility[TEST_PRIMITIVE_COUNT * SHAPE3D_VISIBILITY_MASK_SIZE];
	legacy_u8 facing[TEST_PRIMITIVE_COUNT * SHAPE3D_VISIBILITY_MASK_SIZE];
};

static void prepare_mesh(struct MESH_FIXTURE *fixture)
{
	const struct VECTOR vertices[] = {{-37, 0, -59}, {41, 91, 23}, {-11, 127, 61}, {59, -7, -31}};
	/* Deliberately omit a source footer and retain two authored paint columns. */
	const legacy_u8 primitives[] = {1, 0x81, 19, 20,   0,  2,  0x02, 21, 22,
									0, 1,	 3,	 0x03, 23, 24, 0,	 2,	 3};
	memset(fixture, 0, sizeof(*fixture));
	memcpy(fixture->primitives, primitives, sizeof(primitives));
	memset(fixture->visibility, 0x36, sizeof(fixture->visibility));
	memset(fixture->facing, 0xA7, sizeof(fixture->facing));
	fixture->full = (struct SHAPE3D){TEST_VERTEX_COUNT, fixture->vertices,	 TEST_PRIMITIVE_COUNT,
									 TEST_PAINT_COUNT,	fixture->primitives, fixture->visibility,
									 fixture->facing};
	for (legacy_u16 vertex = 0; vertex < TEST_VERTEX_COUNT; vertex++) {
		shape3d_vertex_write(&fixture->full, vertex, &vertices[vertex]);
	}
	fixture->low = fixture->full;
	fixture->low.shape3d_numverts = TEST_VERTEX_COUNT / 2;
	fixture->low.shape3d_numprimitives = 1;
}

static void assert_mesh_equal(const struct SHAPE3D *actual, const struct SHAPE3D *expected)
{
	assert(actual->shape3d_numverts == expected->shape3d_numverts);
	assert(actual->shape3d_numprimitives == expected->shape3d_numprimitives);
	assert(actual->shape3d_numpaints == expected->shape3d_numpaints);
	assert(memcmp(actual->shape3d_vertex_bytes, expected->shape3d_vertex_bytes,
				  (size_t)expected->shape3d_numverts * SHAPE3D_VERTEX_SIZE) == 0);
	size_t bytes = 0;
	for (legacy_u16 primitive = 0; primitive < expected->shape3d_numprimitives; primitive++) {
		legacy_u8 type = expected->shape3d_primitives[bytes];
		assert(type < SHAPE3D_PRIMITIVE_TYPE_COUNT);
		bytes +=
			SHAPE3D_PRIMITIVE_HEADER_SIZE + expected->shape3d_numpaints + primidxcounttab[type];
	}
	assert(memcmp(actual->shape3d_primitives, expected->shape3d_primitives,
				  bytes + TEST_FOOTER_BYTES) == 0);
	bytes = (size_t)expected->shape3d_numprimitives * SHAPE3D_VISIBILITY_MASK_SIZE;
	assert(memcmp(actual->shape3d_visibility_masks, expected->shape3d_visibility_masks, bytes) ==
		   0);
	assert(memcmp(actual->shape3d_front_facing_masks, expected->shape3d_front_facing_masks,
				  bytes) == 0);
}

static void assert_grove_equal(const struct SHAPE3D *cached, const struct SHAPE3D *source,
							   const struct CSTRK_OBJECT *object)
{
	for (legacy_u8 tree = 0; tree < object->pine_count; tree++) {
		struct SHAPE3D expected;
		assert(pine_trees_create_mesh(source, &expected, object, tree));
		if (cached != NULL) {
			assert_mesh_equal(&cached[tree], &expected);
		}
		extended_geometry_free_shape(&expected);
	}
}

static void prepare_track(struct CSTRK_TRACK *track)
{
	legacy_char error[TEST_ERROR_BYTES];
	const legacy_u8 counts[] = {CSTRK_PINE_GROUP_SMALL_COUNT, CSTRK_PINE_GROUP_LARGE_COUNT};
	cstrk_init(track);
	assert(cstrk_create(track, TEST_TRACK_SIDE, TEST_TRACK_SIDE, error, sizeof(error)));
	for (legacy_u8 group = 0; group < SDL_arraysize(counts); group++) {
		for (legacy_u8 rotation = 0; rotation < TEST_ROTATIONS; rotation++) {
			struct CSTRK_OBJECT object = {0};
			object.legacy_id = CSTRK_PINE_TREE_ID;
			object.x = 1 + group * TEST_ROTATIONS + rotation;
			object.y = 1;
			object.rotation = rotation;
			object.width = object.height = object.occupied_mask = 1;
			object.pine_count = counts[group];
			assert(cstrk_add_object(track, &object, error, sizeof(error)));
		}
	}
	assert(extended_track_activate(track, error, sizeof(error)));
}

static void test_mesh_parity(struct MESH_FIXTURE *fixture)
{
	const struct CSTRK_TRACK *track = extended_track_model();
	struct MESH_FIXTURE original = *fixture;
	hires_frame_extended_pine_cache_set_budget(HIRES_FRAME_EXTENDED_PINE_CACHE_MAX_BYTES);
	for (legacy_u32 object = 0; object < track->object_count; object++) {
		const struct SHAPE3D *sources[] = {&fixture->full, &fixture->low};
		for (legacy_u8 detail = 0; detail < SDL_arraysize(sources); detail++) {
			const struct SHAPE3D *cached = hires_frame_extended_pine_cache_lookup(
				track, &track->objects[object], sources[detail]);
			assert(cached != NULL);
			assert_grove_equal(cached, sources[detail], &track->objects[object]);
			assert(hires_frame_extended_pine_cache_lookup(track, &track->objects[object],
														  sources[detail]) == cached);
		}
	}
	assert(hires_frame_extended_pine_cache_bytes() > 0);
	assert(hires_frame_extended_pine_cache_bytes() < HIRES_FRAME_EXTENDED_PINE_CACHE_MAX_BYTES);
	assert(memcmp(fixture, &original, sizeof(*fixture)) == 0);
}

static void test_budget_and_dedup(struct MESH_FIXTURE *fixture)
{
	const struct CSTRK_TRACK *track = extended_track_model();
	const struct CSTRK_OBJECT *object = &track->objects[0];
	const legacy_u32 budgets[] = {0, 1};
	for (legacy_u8 index = 0; index < SDL_arraysize(budgets); index++) {
		hires_frame_extended_pine_cache_set_budget(budgets[index]);
		assert(hires_frame_extended_pine_cache_lookup(track, object, &fixture->full) == NULL);
		assert(hires_frame_extended_pine_cache_bytes() == 0);
		assert_grove_equal(NULL, &fixture->full, object);
	}
	hires_frame_extended_pine_cache_set_budget(HIRES_FRAME_EXTENDED_PINE_CACHE_MAX_BYTES);
	const struct SHAPE3D *full =
		hires_frame_extended_pine_cache_lookup(track, object, &fixture->full);
	assert(full != NULL);
	legacy_u32 bytes = hires_frame_extended_pine_cache_bytes();
	struct SHAPE3D alias = fixture->full;
	assert(hires_frame_extended_pine_cache_lookup(track, object, &alias) == full);
	assert(hires_frame_extended_pine_cache_bytes() == bytes);
	const struct SHAPE3D *low =
		hires_frame_extended_pine_cache_lookup(track, object, &fixture->low);
	assert(low != NULL && low != full);
	assert_grove_equal(low, &fixture->low, object);
	legacy_u32 both = hires_frame_extended_pine_cache_bytes();
	assert(both > bytes);
	alias = fixture->low;
	assert(hires_frame_extended_pine_cache_lookup(track, object, &alias) == low);
	assert(hires_frame_extended_pine_cache_bytes() == both);
	hires_frame_extended_pine_cache_set_budget(bytes - 1);
	assert(hires_frame_extended_pine_cache_lookup(track, object, &fixture->full) == NULL);
	assert(hires_frame_extended_pine_cache_bytes() == 0);
	hires_frame_extended_pine_cache_set_budget(bytes);
	assert(hires_frame_extended_pine_cache_lookup(track, object, &fixture->full) != NULL);
	assert(hires_frame_extended_pine_cache_bytes() == bytes);
	assert(hires_frame_extended_pine_cache_lookup(track, &track->objects[1], &fixture->full) ==
		   NULL);
	assert(hires_frame_extended_pine_cache_bytes() == bytes);
	hires_frame_extended_pine_cache_reset();
	assert(hires_frame_extended_pine_cache_bytes() == 0);
	assert(hires_frame_extended_pine_cache_lookup(track, &track->objects[1], &fixture->full) !=
		   NULL);
	hires_frame_extended_pine_cache_set_budget(HIRES_FRAME_EXTENDED_PINE_CACHE_MAX_BYTES);
}

static void test_changed_inputs(struct MESH_FIXTURE *fixture)
{
	const struct CSTRK_TRACK *track = extended_track_model();
	struct CSTRK_OBJECT *object = &track->objects[0];
	const struct CSTRK_OBJECT original = *object;
	const struct SHAPE3D *cached =
		hires_frame_extended_pine_cache_lookup(track, object, &fixture->full);
	assert(cached != NULL);
	struct SHAPE3D expected;
	assert(pine_trees_create_mesh(&fixture->full, &expected, object, 0));
	fixture->vertices[0] ^= 1;
	assert(hires_frame_extended_pine_cache_lookup(track, object, &fixture->full) == NULL);
	assert_mesh_equal(cached, &expected);
	fixture->vertices[0] ^= 1;
	fixture->primitives[2] ^= 1;
	assert(hires_frame_extended_pine_cache_lookup(track, object, &fixture->full) == NULL);
	assert_mesh_equal(cached, &expected);
	fixture->primitives[2] ^= 1;
	fixture->full.shape3d_numprimitives--;
	assert(hires_frame_extended_pine_cache_lookup(track, object, &fixture->full) == NULL);
	assert_mesh_equal(cached, &expected);
	fixture->full.shape3d_numprimitives++;
	fixture->visibility[0] ^= 1;
	fixture->facing[0] ^= 1;
	assert(hires_frame_extended_pine_cache_lookup(track, object, &fixture->full) == cached);
	fixture->visibility[0] ^= 1;
	fixture->facing[0] ^= 1;
	struct CSTRK_OBJECT variants[] = {original, original, original, original};
	variants[0].x++;
	variants[1].rotation = (variants[1].rotation + 1) % TEST_ROTATIONS;
	variants[2].pine_count = CSTRK_PINE_GROUP_LARGE_COUNT;
	variants[3].slope_x = 1;
	for (legacy_u8 variant = 0; variant < SDL_arraysize(variants); variant++) {
		*object = variants[variant];
		assert(hires_frame_extended_pine_cache_lookup(track, object, &fixture->full) == NULL);
		assert_mesh_equal(cached, &expected);
		*object = original;
		assert(hires_frame_extended_pine_cache_lookup(track, object, &fixture->full) == cached);
	}
	assert(hires_frame_extended_pine_cache_lookup(track, &original, &fixture->full) == NULL);
	struct CSTRK_TRACK foreign = *track;
	assert(hires_frame_extended_pine_cache_lookup(&foreign, object, &fixture->full) == NULL);
	extended_geometry_free_shape(&expected);
	hires_frame_extended_pine_cache_reset();
}

static void test_shorter_primitive(struct MESH_FIXTURE *fixture)
{
	const struct CSTRK_TRACK *track = extended_track_model();
	const struct CSTRK_OBJECT *object = &track->objects[0];
	const legacy_u8 triangle[] = {3, 1, 19, 20, 0, 1, 2};
	legacy_u8 *records = malloc(sizeof(triangle));
	assert(records != NULL);
	memcpy(records, triangle, sizeof(triangle));
	struct SHAPE3D source = fixture->full;
	source.shape3d_numprimitives = 1;
	source.shape3d_primitives = records;
	const struct SHAPE3D *cached = hires_frame_extended_pine_cache_lookup(track, object, &source);
	assert(cached != NULL);
	struct SHAPE3D expected;
	assert(pine_trees_create_mesh(&source, &expected, object, 0));
	records[0] = SHAPE3D_PRIMITIVE_POINT;
	size_t shorter = SHAPE3D_PRIMITIVE_HEADER_SIZE + source.shape3d_numpaints + 1;
#ifdef PINE_CACHE_TEST_ASAN
	__asan_poison_memory_region(records + shorter, sizeof(triangle) - shorter);
#endif
	assert(hires_frame_extended_pine_cache_lookup(track, object, &source) == NULL);
	assert_mesh_equal(cached, &expected);
#ifdef PINE_CACHE_TEST_ASAN
	__asan_unpoison_memory_region(records + shorter, sizeof(triangle) - shorter);
#else
	(void)shorter;
#endif
	records[0] = SHAPE3D_PRIMITIVE_POLYGON_FIRST;
	assert(hires_frame_extended_pine_cache_lookup(track, object, &source) == cached);
	extended_geometry_free_shape(&expected);
	hires_frame_extended_pine_cache_reset();
	free(records);
}

struct PARALLEL_CONTEXT {
	const struct CSTRK_TRACK *track;
	const struct SHAPE3D *sources[2];
	legacy_u8 hits[TEST_PARALLEL_JOBS];
};

static void lookup_parallel(void *argument, legacy_s32 index)
{
	struct PARALLEL_CONTEXT *context = argument;
	const struct CSTRK_OBJECT *object =
		&context->track->objects[(index / TEST_PARALLEL_REUSE) % context->track->object_count];
	const struct SHAPE3D *source = context->sources[index % SDL_arraysize(context->sources)];
	for (legacy_u8 repeat = 0; repeat < TEST_PARALLEL_REPEATS; repeat++) {
		const struct SHAPE3D *cached =
			hires_frame_extended_pine_cache_lookup(context->track, object, source);
		context->hits[index] += cached != NULL;
		assert_grove_equal(cached, source, object);
	}
}

static void test_parallel_lookup(struct MESH_FIXTURE *fixture)
{
	const legacy_char *setting = SDL_getenv("RESTUNTS_RENDER_WORKERS");
	legacy_char *saved = setting != NULL ? SDL_strdup(setting) : NULL;
	assert(setting == NULL || saved != NULL);
	hires_render_workers_shutdown();
	assert(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "RESTUNTS_RENDER_WORKERS", "3", true));
	hires_render_workers_set_enabled(1);
	hires_frame_extended_pine_cache_reset();
	struct PARALLEL_CONTEXT context = {
		extended_track_model(), {&fixture->full, &fixture->low}, {0}};
	assert(hires_render_workers_run(TEST_PARALLEL_JOBS, lookup_parallel, &context) > 0);
	legacy_u32 hits = 0;
	for (legacy_u32 job = 0; job < TEST_PARALLEL_JOBS; job++) {
		hits += context.hits[job];
	}
	assert(hits > 0);
	assert(hires_frame_extended_pine_cache_bytes() > 0);
	assert(hires_frame_extended_pine_cache_bytes() < HIRES_FRAME_EXTENDED_PINE_CACHE_MAX_BYTES);
	hires_render_workers_shutdown();
	hires_render_workers_set_enabled(0);
	if (saved != NULL) {
		assert(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "RESTUNTS_RENDER_WORKERS", saved,
										  true));
		SDL_free(saved);
	} else {
		assert(SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "RESTUNTS_RENDER_WORKERS"));
	}
}

static void test_lifecycle(struct CSTRK_TRACK *authored, struct MESH_FIXTURE *fixture)
{
	const struct CSTRK_TRACK *track = extended_track_model();
	legacy_char error[TEST_ERROR_BYTES];
	const struct SHAPE3D *cached =
		hires_frame_extended_pine_cache_lookup(track, &track->objects[0], &fixture->full);
	assert(cached != NULL);
	legacy_u32 bytes = hires_frame_extended_pine_cache_bytes();
	const legacy_u8 invalid[] = {0};
	assert(!extended_track_load(invalid, sizeof(invalid), error, sizeof(error)));
	assert(hires_frame_extended_pine_cache_bytes() == bytes);
	assert(hires_frame_extended_pine_cache_lookup(track, &track->objects[0], &fixture->full) ==
		   cached);
	assert_grove_equal(cached, &fixture->full, &track->objects[0]);
	assert(extended_track_activate(authored, error, sizeof(error)));
	assert(hires_frame_extended_pine_cache_bytes() == 0);
	track = extended_track_model();
	assert(hires_frame_extended_pine_cache_lookup(track, &track->objects[0], &fixture->full) !=
		   NULL);
	shape3d_free_all();
	assert(hires_frame_extended_pine_cache_bytes() == 0);
	assert(hires_frame_extended_pine_cache_lookup(track, &track->objects[0], &fixture->full) !=
		   NULL);
	extended_track_unload();
	assert(hires_frame_extended_pine_cache_bytes() == 0);
	assert(extended_track_model() == NULL);
}

legacy_int main(void)
{
	assert(SDL_Init(0));
	struct MESH_FIXTURE fixture;
	prepare_mesh(&fixture);
	struct CSTRK_TRACK authored;
	prepare_track(&authored);
	test_mesh_parity(&fixture);
	test_budget_and_dedup(&fixture);
	test_changed_inputs(&fixture);
	test_shorter_primitive(&fixture);
	test_parallel_lookup(&fixture);
	test_lifecycle(&authored, &fixture);
	cstrk_free(&authored);
	hires_frame_extended_pine_cache_set_budget(HIRES_FRAME_EXTENDED_PINE_CACHE_MAX_BYTES);
	SDL_Quit();
	puts("Pine mesh cache byte parity, invalidation, budgets, ownership and worker lookup passed.");
	return 0;
}
