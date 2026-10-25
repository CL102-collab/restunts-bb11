#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../c/externs.h"
#include "../c/track_collision.h"
#include "../c/track_objects.h"
#include "../c/trackdata_layout.h"
#include "../c/physics_internal.h"

extern legacy_s8 corkFlag;

#ifdef TRACK_OBJECT_DIFFERENTIAL
void reference_build_track_object(struct VECTOR *, struct VECTOR *);
#endif

#define TEST_ROW 10
#define TEST_COLUMN 10
#define TEST_ORIGIN 10752
#define RESULT_COUNT 14

static legacy_u8 elements[TRACKDATA_MAP_SIZE];
static legacy_u8 terrain[TRACKDATA_MAP_SIZE];
static struct PLANE planes[TRACK_PLAN_RESOURCE_COUNT];
static struct TRACK_WALL walls[TRACK_WALL_RESOURCE_COUNT];

static void initialize_track(void)
{
	memset(elements, 0, sizeof(elements));
	memset(terrain, 0, sizeof(terrain));
	track_element_map = elements;
	track_terrain_map = terrain;
	planptr = planes;
	wallptr = walls;
	for (legacy_s32 index = 0; index < TRACK_GRID_SIZE; index++) {
		trackrows[index] = index * TRACK_GRID_SIZE;
		terrainrows[index] = (TRACK_GRID_LAST_INDEX - index) * TRACK_GRID_SIZE;
		track_column_centers[index] = index * 1024 + 512;
		terraincenterpos[index] = index * 1024 + 512;
		track_column_positions[index] = index * 1024;
		terrainpos[index] = index * 1024;
	}
	for (legacy_s32 index = 0; index < (legacy_s32)TRACK_WALL_RESOURCE_COUNT; index++) {
		walls[index].x = (legacy_s16)(index * 11 - 1024);
		walls[index].z = (legacy_s16)(index * 7 - 900);
		walls[index].orientation = (legacy_s16)(index * 19);
	}
	elements[terrainrows[TEST_ROW] + TEST_COLUMN] = 1;
}

static void reset_outputs(void)
{
	elem_xCenter = -123;
	elem_zCenter = 234;
	wallStartX = -345;
	wallStartZ = 456;
	wallOrientation = -567;
}

static void capture_result(legacy_s16 *result)
{
	result[0] = planindex;
	result[1] = (legacy_s16)(current_planptr - planptr);
	result[2] = wallindex;
	result[3] = wallHeight;
	result[4] = elRdWallRelated;
	result[5] = corkFlag;
	result[6] = current_surf_type;
	result[7] = track_wall_collision_enabled;
	result[8] = terrainHeight;
	result[9] = elem_xCenter;
	result[10] = elem_zCenter;
	result[11] = wallStartX;
	result[12] = wallStartZ;
	result[13] = wallOrientation;
}

static legacy_u32 probe(legacy_u32 hash, struct VECTOR *position, struct VECTOR *next_position)
{
#ifdef TRACK_OBJECT_DIFFERENTIAL
	reset_outputs();
	reference_build_track_object(position, next_position);
	legacy_s16 reference[RESULT_COUNT];
	capture_result(reference);
#endif
	reset_outputs();
	build_track_object(position, next_position);
	legacy_s16 result[RESULT_COUNT];
	capture_result(result);
#ifdef TRACK_OBJECT_DIFFERENTIAL
	if (memcmp(reference, result, sizeof(result)) != 0) {
		fprintf(stderr, "Track object mismatch: model %d, rotation %d, position %d/%d/%d\n",
				trkObjectList[1].ss_physicalModel, trkObjectList[1].ss_rotY, position->x,
				position->y, position->z);
		for (legacy_s32 index = 0; index < RESULT_COUNT; index++) {
			fprintf(stderr, "  output %" LEGACY_PRId32 ": %d != %d\n", index, reference[index],
					result[index]);
		}
		assert(0);
	}
#endif
	for (legacy_s32 index = 0; index < RESULT_COUNT; index++) {
		hash = (hash ^ ((legacy_u16)result[index] & 255U)) * 16777619UL;
		hash = (hash ^ ((legacy_u16)result[index] >> 8)) * 16777619UL;
	}
	return hash;
}

/* Exact boundary points and their neighbours exercise open/closed intervals,
 * mirrored pieces, all four rotations, both height layers and wall crossings.
 * Query-local centres let large elements use their full local coordinate range
 * without redirecting the query to another tile. */
static legacy_u32 probe_model(legacy_s32 model)
{
	trkObjectList[1].ss_physicalModel = (legacy_s8)model;
	trkObjectList[1].ss_multiTileFlag = 0;
	struct VECTOR position;
	position.x = TEST_ORIGIN;
	position.z = TEST_ORIGIN;
	legacy_u32 hash = 2166136261UL;
	static const legacy_s16 x_values[] = {
		-900, -693, -692, -633, -632, -512, -393, -392, -361, -360, -271, -270, -201,
		-200, -181, -180, -171, -170, -165, -164, -151, -150, -131, -130, -121, -120,
		-119, -115, -114, -109, -108, -103, -102, -98,	-97,  -85,	-84,  -83,	-32,
		-31,  -30,	-24,  -23,	-1,	  0,	1,	  23,	24,	  30,	31,	  32,	83,
		84,	  85,	97,	  98,	102,  103,	108,  109,	114,  115,	119,  120,	121,
		130,  131,	150,  151,	164,  165,	170,  171,	180,  181,	200,  201,	260,
		261,  270,	271,  360,	361,  392,	393,  512,	632,  633,	692,  693,	900};
	static const legacy_s16 z_values[] = {
		-900, -560, -559, -513, -512, -511, -477, -476, -466, -450, -449, -390, -389, -381,
		-380, -335, -334, -301, -300, -272, -271, -270, -242, -241, -240, -225, -224, -179,
		-178, -169, -168, -111, -110, -101, -100, -81,	-80,  -76,	-75,  -74,	-1,	  0,
		1,	  74,	75,	  76,	80,	  81,	100,  101,	110,  111,	168,  169,	178,  179,
		224,  225,	240,  241,	242,  270,	271,  272,	300,  301,	334,  335,	380,  381,
		389,  390,	449,  450,	466,  476,	477,  511,	512,  513,	559,  560,	900};
	static const legacy_s16 heights[] = {
		-32768, -1,	 0,	  87,  88,	89,	 99,  100, 101, 143, 144, 145, 150, 151, 152, 170,	171,
		172,	264, 265, 266, 349, 350, 351, 389, 390, 391, 450, 523, 524, 525, 975, 32767};
	struct VECTOR next_position;
	for (legacy_s32 rotation = 0; rotation < 4; rotation++) {
		trkObjectList[1].ss_rotY = (legacy_s16)(rotation * 256);
		for (legacy_u32 x_index = 0; x_index < sizeof(x_values) / sizeof(x_values[0]); x_index++) {
			for (legacy_u32 z_index = 0; z_index < sizeof(z_values) / sizeof(z_values[0]);
				 z_index++) {
				/* Cycle through every height at each x and z boundary while
				 * keeping the full regression fast enough for the host suite. */
				legacy_u32 height_index =
					(x_index + z_index + rotation) % (sizeof(heights) / sizeof(heights[0]));
				position.y = heights[height_index];
				legacy_s32 delta = (legacy_s32)((x_index + z_index) % 3) - 1;
				next_position.x = (legacy_s16)(position.x + delta * 130);
				next_position.z = (legacy_s16)(position.z - delta * 180);
				next_position.y = LEGACY_S16_WRAP_ADD(position.y, 1);
				track_column_centers[TEST_COLUMN] = (legacy_s16)(TEST_ORIGIN - x_values[x_index]);
				terraincenterpos[TEST_ROW] = (legacy_s16)(TEST_ORIGIN - z_values[z_index]);
				terrain[trackrows[TEST_ROW] + TEST_COLUMN] = (z_index & 1) ? 6 : 0;
				trkObjectList[1].ss_surfaceType = (legacy_s8)((x_index % 5) - 1);
				state.game_inputmode = (legacy_s8)(z_index % 3);
				hash = probe(hash, &position, &next_position);
			}
		}
	}
	return hash;
}

/* Exercise the real object metadata as well as the synthetic model sweep:
 * continuation tiles change the origin, and hills can substitute the object
 * or override the plane orientation after a wall has already been selected. */
static legacy_u32 probe_layouts(void)
{
	static const legacy_u8 continuations[] = {0, TRACK_TILE_CONTINUATION_SOUTHEAST,
											  TRACK_TILE_CONTINUATION_SOUTH,
											  TRACK_TILE_CONTINUATION_EAST};

	legacy_u32 hash = 2166136261UL;
	static const legacy_s16 offsets[] = {-512, -334, -120, 0, 120, 334, 511};
	static const legacy_s16 heights[] = {-32768, 0, 143, 151, 265, 390, 524};
	struct VECTOR position;
	struct VECTOR next_position;
	for (legacy_s32 tile = 0; tile < 215; tile++) {
		for (legacy_u32 continuation_index = 0; continuation_index < 4; continuation_index++) {
			initialize_track();
			legacy_s32 current_index = terrainrows[TEST_ROW] + TEST_COLUMN;
			elements[current_index] = (legacy_u8)tile;
			if (continuation_index != 0) {
				elements[current_index] = continuations[continuation_index];
				elements[terrainrows[TEST_ROW + 1] + TEST_COLUMN - 1] = (legacy_u8)tile;
				elements[terrainrows[TEST_ROW + 1] + TEST_COLUMN] = (legacy_u8)tile;
				elements[terrainrows[TEST_ROW] + TEST_COLUMN - 1] = (legacy_u8)tile;
			}
			for (legacy_s32 terrain_tile = 0; terrain_tile < 20; terrain_tile++) {
				terrain[trackrows[TEST_ROW] + TEST_COLUMN] = (legacy_u8)terrain_tile;
				for (legacy_u32 x_index = 0; x_index < 7; x_index++) {
					for (legacy_u32 z_index = 0; z_index < 7; z_index++) {
						position.x = TEST_ORIGIN + offsets[x_index];
						position.z = TEST_ORIGIN + offsets[z_index];
						position.y = heights[(x_index + z_index) % 7];
						next_position.x = LEGACY_S16_WRAP_ADD(position.x, offsets[z_index]);
						next_position.z = LEGACY_S16_WRAP_ADD(position.z, offsets[x_index]);
						next_position.y = LEGACY_S16_WRAP_ADD(position.y, 144);
						state.game_inputmode = (legacy_s8)(z_index % 3);
						hash = probe(hash, &position, &next_position);
					}
				}
			}
		}
	}
	return hash;
}

static void test_road_boundaries(void)
{
	struct VECTOR position = {TEST_ORIGIN, 0, TEST_ORIGIN};

	initialize_track();
	struct TRACKOBJECT saved_object = trkObjectList[1];
	trkObjectList[1].ss_physicalModel = PHYSICAL_MODEL_ROAD;
	trkObjectList[1].ss_rotY = 0;
	trkObjectList[1].ss_multiTileFlag = 0;
	trkObjectList[1].ss_surfaceType = 0;
	track_column_centers[TEST_COLUMN] = TEST_ORIGIN - 119;
	struct VECTOR next_position = position;
	probe(0, &position, &next_position);
	assert(current_surf_type == CAR_SURFACE_PAVED);
	assert(terrainHeight == 2);
	track_column_centers[TEST_COLUMN]--;
	probe(0, &position, &next_position);
	assert(current_surf_type == CAR_SURFACE_GRASS);
	assert(terrainHeight == 0);
	trkObjectList[1] = saved_object;
}

static void test_outside_track(void)
{
	struct VECTOR next_position = {0, 0, 0};
	static const legacy_s16 coordinates[] = {-32768, -1, 30720, 32767};
	struct VECTOR position = {0, 0, 0};
	for (legacy_u32 index = 0; index < sizeof(coordinates) / sizeof(coordinates[0]); index++) {
		position.x = coordinates[index];
		probe(0, &position, &next_position);
		assert(planindex == 0);
		assert(wallindex == -1);
		assert(current_surf_type == CAR_SURFACE_GRASS);
		assert(elem_xCenter == -123);
		assert(elem_zCenter == 234);
	}
}

#define TEST_CHICANE_GRASS_Z 500
#define TEST_CHICANE_ROAD_X (-350)
#define TEST_CHICANE_OUTER_EDGE_X (-488)
#define TEST_CHICANE_OUTER_EDGE_Z 534
#define TEST_COORDINATE_NEIGHBOR 1
#define TEST_WATER_TERRAIN_TILE 1U

static void probe_chicane_point(legacy_s16 model, legacy_s16 rotation, legacy_s16 x, legacy_s16 z,
								legacy_s16 material)
{
	initialize_track();
	trkObjectList[1].ss_physicalModel = (legacy_s8)model;
	trkObjectList[1].ss_rotY = rotation;
	trkObjectList[1].ss_multiTileFlag = 0;
	trkObjectList[1].ss_surfaceType = (legacy_s8)material;
	legacy_s16 world_x = x;
	legacy_s16 world_z = z;
	switch (rotation) {
		case ANGLE_QUARTER_TURN:
			world_x = z;
			world_z = -x;
			break;
		case ANGLE_HALF_TURN:
			world_x = -x;
			world_z = -z;
			break;
		case ANGLE_THREE_QUARTER_TURN:
			world_x = -z;
			world_z = x;
			break;
	}
	track_column_centers[TEST_COLUMN] = TEST_ORIGIN - world_x;
	terraincenterpos[TEST_ROW] = TEST_ORIGIN - world_z;
	struct VECTOR position = {TEST_ORIGIN, 0, TEST_ORIGIN};
	build_track_object(&position, &position);
}

static void test_chicane_fast_grass_options(void)
{
	static const struct {
		const legacy_s8 *option;
		legacy_s16 surface;
	} cases[] = {
		{NULL, CAR_SURFACE_PAVED},
		{(const legacy_s8 *)"--cfg:off", CAR_SURFACE_GRASS},
		{(const legacy_s8 *)"--CFG:OFF", CAR_SURFACE_GRASS},
		{(const legacy_s8 *)"--CfG:OfF", CAR_SURFACE_GRASS},
		{(const legacy_s8 *)"--cfg:on", CAR_SURFACE_PAVED},
		{(const legacy_s8 *)"--CFG:ON", CAR_SURFACE_PAVED},
		{(const legacy_s8 *)"--CfG:On", CAR_SURFACE_PAVED},
		{(const legacy_s8 *)"cfg:off", CAR_SURFACE_PAVED},
		{(const legacy_s8 *)"/cfg:off", CAR_SURFACE_PAVED},
		{(const legacy_s8 *)"-cfg:off", CAR_SURFACE_PAVED},
		{(const legacy_s8 *)"--cfg", CAR_SURFACE_PAVED},
		{(const legacy_s8 *)"--cfg:offx", CAR_SURFACE_PAVED},
		{(const legacy_s8 *)"--cfg:off ", CAR_SURFACE_PAVED},
		{(const legacy_s8 *)" --cfg:off", CAR_SURFACE_PAVED},
		{(const legacy_s8 *)"--cfg:off:on", CAR_SURFACE_PAVED},
		{(const legacy_s8 *)"--cfg=off", CAR_SURFACE_PAVED},
	};
	legacy_s8 *disabled[] = {(legacy_s8 *)"game", (legacy_s8 *)"--cfg:off"};
	for (legacy_u16 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		configure_chicane_fast_grass(sizeof(disabled) / sizeof(disabled[0]), disabled);
		legacy_s8 *arguments[] = {(legacy_s8 *)"game", (legacy_s8 *)cases[index].option};
		configure_chicane_fast_grass(cases[index].option == NULL ? 1 : 2, arguments);
		probe_chicane_point(PHYSICAL_MODEL_CHICANE_RIGHT_LEFT, 0, 0, TEST_CHICANE_GRASS_Z, 0);
		assert(current_surf_type == cases[index].surface);
	}
	legacy_s8 *repeated[] = {(legacy_s8 *)"game", (legacy_s8 *)"--cfg:off",
							 (legacy_s8 *)"--cfg:invalid", (legacy_s8 *)"--cfg:on",
							 (legacy_s8 *)"--cfg:off"};
	configure_chicane_fast_grass(sizeof(repeated) / sizeof(repeated[0]) - 2, repeated);
	probe_chicane_point(PHYSICAL_MODEL_CHICANE_RIGHT_LEFT, 0, 0, TEST_CHICANE_GRASS_Z, 0);
	assert(current_surf_type == CAR_SURFACE_GRASS);
	configure_chicane_fast_grass(sizeof(repeated) / sizeof(repeated[0]) - 1, repeated);
	probe_chicane_point(PHYSICAL_MODEL_CHICANE_RIGHT_LEFT, 0, 0, TEST_CHICANE_GRASS_Z, 0);
	assert(current_surf_type == CAR_SURFACE_PAVED);
	configure_chicane_fast_grass(sizeof(repeated) / sizeof(repeated[0]), repeated);
	probe_chicane_point(PHYSICAL_MODEL_CHICANE_RIGHT_LEFT, 0, 0, TEST_CHICANE_GRASS_Z, 0);
	assert(current_surf_type == CAR_SURFACE_GRASS);
	configure_chicane_fast_grass(0, NULL);
	probe_chicane_point(PHYSICAL_MODEL_CHICANE_RIGHT_LEFT, 0, 0, TEST_CHICANE_GRASS_Z, 0);
	assert(current_surf_type == CAR_SURFACE_PAVED);
	configure_chicane_fast_grass(1, disabled + 1);
	probe_chicane_point(PHYSICAL_MODEL_CHICANE_RIGHT_LEFT, 0, 0, TEST_CHICANE_GRASS_Z, 0);
	assert(current_surf_type == CAR_SURFACE_PAVED);
}

static void test_chicane_grass_surfaces(void)
{
	static const struct {
		legacy_s16 x;
		legacy_s16 z;
		legacy_s16 road;
	} points[] = {
		{0, TEST_CHICANE_GRASS_Z, 0},
		{0, -TEST_CHICANE_GRASS_Z, 0},
		{TEST_CHICANE_ROAD_X, TEST_CHICANE_GRASS_Z, 1},
		{-TEST_CHICANE_ROAD_X, -TEST_CHICANE_GRASS_Z, 1},
		{0, 0, 1},
		{TEST_CHICANE_OUTER_EDGE_X, TEST_CHICANE_OUTER_EDGE_Z, 1},
		{TEST_CHICANE_OUTER_EDGE_X + TEST_COORDINATE_NEIGHBOR, TEST_CHICANE_OUTER_EDGE_Z, 1},
		{TEST_CHICANE_OUTER_EDGE_X - TEST_COORDINATE_NEIGHBOR, TEST_CHICANE_OUTER_EDGE_Z, 0},
		{-TEST_CHICANE_OUTER_EDGE_X, -TEST_CHICANE_OUTER_EDGE_Z, 1},
		{-TEST_CHICANE_OUTER_EDGE_X - TEST_COORDINATE_NEIGHBOR, -TEST_CHICANE_OUTER_EDGE_Z, 1},
		{-TEST_CHICANE_OUTER_EDGE_X + TEST_COORDINATE_NEIGHBOR, -TEST_CHICANE_OUTER_EDGE_Z, 0},
	};
	legacy_s8 *arguments[] = {(legacy_s8 *)"game", (legacy_s8 *)"--cfg:off"};
	configure_chicane_fast_grass(sizeof(arguments) / sizeof(arguments[0]), arguments);
	for (legacy_s16 model = PHYSICAL_MODEL_CHICANE_RIGHT_LEFT;
		 model <= PHYSICAL_MODEL_CHICANE_LEFT_RIGHT; model++) {
		for (legacy_s16 rotation = 0; rotation < ANGLE_FULL_TURN; rotation += ANGLE_QUARTER_TURN) {
			for (legacy_s16 material = CAR_SURFACE_PAVED; material <= CAR_SURFACE_ICE; material++) {
				for (legacy_u16 index = 0; index < sizeof(points) / sizeof(points[0]); index++) {
					legacy_s16 x = model == PHYSICAL_MODEL_CHICANE_RIGHT_LEFT ? points[index].x
																			  : -points[index].x;
					probe_chicane_point(model, rotation, x, points[index].z,
										material - CAR_SURFACE_PAVED);
					assert(current_surf_type ==
						   (points[index].road ? material : CAR_SURFACE_GRASS));
					if (!points[index].road) {
						legacy_s16 chicane_height = terrainHeight;
						elements[terrainrows[TEST_ROW] + TEST_COLUMN] = 0;
						struct VECTOR position = {TEST_ORIGIN, 0, TEST_ORIGIN};
						build_track_object(&position, &position);
						assert(current_surf_type == CAR_SURFACE_GRASS);
						assert(terrainHeight == chicane_height);
					}
				}
			}
		}
	}
	/* A chicane's green material stays ordinary grass even on water, and
	 * raised terrain keeps the same height as grass outside the element. */
	probe_chicane_point(PHYSICAL_MODEL_CHICANE_RIGHT_LEFT, 0, 0, TEST_CHICANE_GRASS_Z, 0);
	terrain[trackrows[TEST_ROW] + TEST_COLUMN] = TEST_WATER_TERRAIN_TILE;
	struct VECTOR position = {TEST_ORIGIN, 0, TEST_ORIGIN};
	build_track_object(&position, &position);
	assert(current_surf_type == CAR_SURFACE_GRASS);
	terrain[trackrows[TEST_ROW] + TEST_COLUMN] = TERRAIN_RAISED_TILE;
	build_track_object(&position, &position);
	legacy_s16 raised_grass_height = terrainHeight;
	assert(current_surf_type == CAR_SURFACE_GRASS);
	elements[terrainrows[TEST_ROW] + TEST_COLUMN] = 0;
	build_track_object(&position, &position);
	assert(current_surf_type == CAR_SURFACE_GRASS);
	assert(terrainHeight == raised_grass_height);
	configure_chicane_fast_grass(0, NULL);
}

/* These hashes were captured from the pre-refactor collision implementation.
 * Each physical model has its own fingerprint so regressions identify the
 * affected model instead of depending on the new helper structure. */
static const legacy_u32 model_hashes[77] = {
	0xfb2535e5UL, 0x8631c64dUL, 0x5eb68b85UL, 0x3b135b55UL, 0x522b4795UL, 0xabc401c5UL,
	0xabc401c5UL, 0x1cf1704cUL, 0xa9424994UL, 0x119cfc19UL, 0x20d4567dUL, 0xd5885e0eUL,
	0x5ecaa3c4UL, 0x8f6e7ff5UL, 0xfb2535e5UL, 0xfb2535e5UL, 0xfb2535e5UL, 0xf1fb9600UL,
	0x981351aeUL, 0x567502c6UL, 0x567502c6UL, 0x11b34f83UL, 0x4b0389b4UL, 0xa7291535UL,
	0xbb89a9caUL, 0x45b292a3UL, 0xa03aa07aUL, 0xe6e9727bUL, 0x85223a1dUL, 0x8a9b17ccUL,
	0x05b13d8cUL, 0xaa905fedUL, 0xd2fc0c7fUL, 0xbde6e022UL, 0x37c51531UL, 0x15e1e380UL,
	0x08beb2cbUL, 0xfb2535e5UL, 0xfb2535e5UL, 0xfb2535e5UL, 0xfb2535e5UL, 0xfb2535e5UL,
	0xfb2535e5UL, 0xfb2535e5UL, 0xfb2535e5UL, 0xfb2535e5UL, 0xfb2535e5UL, 0xfb2535e5UL,
	0xfb2535e5UL, 0xfb2535e5UL, 0xfb2535e5UL, 0xfb2535e5UL, 0xfb2535e5UL, 0xfb2535e5UL,
	0xfb2535e5UL, 0xfb2535e5UL, 0xfb2535e5UL, 0xfb2535e5UL, 0xfb2535e5UL, 0xfb2535e5UL,
	0xfb2535e5UL, 0xfb2535e5UL, 0xfb2535e5UL, 0xfb2535e5UL, 0xfb2535e5UL, 0xfb2535e5UL,
	0x314d4e61UL, 0x06605b40UL, 0x1e8c0db1UL, 0x962714f9UL, 0x619e7ed0UL, 0xc369a558UL,
	0xfb2535e5UL, 0xfb2535e5UL, 0xfb2535e5UL, 0xfb2535e5UL, 0xfb2535e5UL,
};

int main(void)
{
	initialize_track();
	struct TRACKOBJECT saved_object = trkObjectList[1];
	legacy_u32 hash;
	for (legacy_s32 model = -1; model <= 75; model++) {
		hash = probe_model(model);
#ifdef TRACK_OBJECT_RECORD_BASELINE
		fprintf(stdout, "0x%08" LEGACY_PRIx32 "UL,%s", hash, (model + 2) % 5 ? " " : "\n");
#else
		if (hash != model_hashes[model + 1]) {
			fprintf(stderr,
					"Track object model %" LEGACY_PRId32 ": got %08" LEGACY_PRIx32
					", expected %08" LEGACY_PRIx32 "\n",
					model, hash, model_hashes[model + 1]);
			assert(0);
		}
#endif
	}
	trkObjectList[1] = saved_object;
	hash = probe_layouts();
#ifdef TRACK_OBJECT_RECORD_BASELINE
	fprintf(stdout, "\nlayout=0x%08" LEGACY_PRIx32 "UL\n", hash);
#else
	assert(hash == 0x49fa168bUL);
#endif
	test_road_boundaries();
	test_outside_track();
	test_chicane_fast_grass_options();
	test_chicane_grass_surfaces();
	return 0;
}
