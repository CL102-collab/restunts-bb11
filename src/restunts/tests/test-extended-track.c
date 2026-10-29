/* Integration test using the original game's collision resources. Run from
 * the user's game-data directory. Missing proprietary assets skip with 77. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../c/externs.h"
#include "../c/cstrk.h"
#include "../c/extended_track.h"
#include "../c/extended_geometry.h"
#include "../c/extended_opponent.h"
#include "../c/memmgr.h"
#include "../c/track_collision.h"
#include "../c/track_objects.h"
#include "../c/trackdata_layout.h"
#include "../c/physics_internal.h"
#include "../c/owoot_road.h"

#undef memcpy
#undef printf
#undef abs

static char error[256];

static struct CSTRK_OBJECT object_at(unsigned id, unsigned x, unsigned y)
{
	struct CSTRK_OBJECT object = {0};
	object.legacy_id = (uint16_t)id;
	object.x = (uint8_t)x;
	object.y = (uint8_t)y;
	object.width = 1;
	object.height = 1;
	object.occupied_mask = 1;
	return object;
}

static void activate(struct CSTRK_TRACK *track)
{
	if (!extended_track_activate(track, error, sizeof(error))) {
		fprintf(stderr, "Extended track activation failed: %s\n", error);
		assert(0);
	}
}

static struct VECTOR sample_world(int32_t x, int32_t y, int32_t z)
{
	extended_geometry_set_context(x * 64, z * 64);
	struct VECTOR point = {LEGACY_S16_FROM_BITS((uint16_t)x), (legacy_s16)y,
						   LEGACY_S16_FROM_BITS((uint16_t)z)};
	build_track_object(&point, &point);
	return point;
}

static int selected_height(const struct VECTOR *point)
{
	assert(current_planptr != NULL && current_planptr->plane_normal.y != 0);
	int x = LEGACY_S16_WRAP_SUB(point->x, elem_xCenter) - current_planptr->plane_origin.x;
	int z = LEGACY_S16_WRAP_SUB(point->z, elem_zCenter) - current_planptr->plane_origin.z;
	return terrainHeight + current_planptr->plane_origin.y -
		   (int)(((int64_t)x * current_planptr->plane_normal.x +
				  (int64_t)z * current_planptr->plane_normal.z) /
				 current_planptr->plane_normal.y);
}

static struct VECTOR object_world(const struct CSTRK_TRACK *track,
								  const struct CSTRK_OBJECT *object, struct VECTOR local)
{
	struct VECTOR origin;
	extended_geometry_object_origin(track, object, &origin);
	local.x = LEGACY_S16_WRAP_ADD(local.x, origin.x);
	local.y = LEGACY_S16_WRAP_ADD(local.y, origin.y);
	local.z = LEGACY_S16_WRAP_ADD(local.z, origin.z);
	return local;
}

static int wheel_overlaps(const struct CSTRK_TRACK *track, const struct CSTRK_OBJECT *object,
						  int center_x, int center_y)
{
	struct VECTOR vertices[8];
	static const int offsets[4][2] = {{-8, -8}, {8, -8}, {8, 8}, {-8, 8}};
	for (unsigned ring = 0; ring < 2; ring++) {
		for (unsigned vertex = 0; vertex < 4; vertex++) {
			struct VECTOR local = {(legacy_s16)(center_x + offsets[vertex][0]),
								   (legacy_s16)(center_y + (ring ? 4 : -4)),
								   (legacy_s16)offsets[vertex][1]};
			vertices[ring * 4U + vertex] = object_world(track, object, local);
		}
	}
	extended_geometry_set_context((object->x * 1024 + object->width * 512) * 64,
								  (track->height * 1024 - object->y * 1024 - object->height * 512) *
									  64);
	return track_road_overlaps_wheel(vertices, 4, 0);
}

static void test_world_coordinates_and_surfaces(void)
{
	static const unsigned coordinates[] = {0, 1, 31, 32, 63, 64, 88, 89};
	static const int surfaces[] = {CAR_SURFACE_GRASS, CAR_SURFACE_ICE, CAR_SURFACE_DIRT,
								   CAR_SURFACE_WATER};
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	assert(cstrk_create(&track, 90, 90, error, sizeof(error)));
	memset(track.corner_heights, 4, 91U * 91U);
	for (unsigned row = 0; row < 8; row++) {
		for (unsigned column = 0; column < 8; column++) {
			track.cells[coordinates[row] * 90U + coordinates[column]].surface =
				(uint8_t)((row + column) % 4U);
		}
	}
	track.cells[70U * 90U + 70U].half_water_corner = CSTRK_WATER_NORTHWEST;
	struct CSTRK_OBJECT road = object_at(4, 89, 89);
	road.base_height = 4;
	road.surface = CSTRK_ROAD_ICE;
	assert(cstrk_add_object(&track, &road, error, sizeof(error)));
	activate(&track);
	assert(extended_track_width() == 90 && extended_track_height() == 90);
	assert(trackrows[0] == 89 * 90 && trackrows[89] == 0);
	assert(terrainrows[89] == 89 * 90);
	struct PLANE *saved_planes = planptr;
	planptr = NULL;
	assert(extended_track_elevation(0, 0, CSTRK_TILE_WORLD_SIZE / 2, CSTRK_TILE_WORLD_SIZE / 2) ==
		   CSTRK_MAX_HEIGHT * CSTRK_LEVEL_WORLD_HEIGHT);
	planptr = saved_planes;
	for (unsigned row = 0; row < 8; row++) {
		for (unsigned column = 0; column < 8; column++) {
			int32_t x = (int32_t)coordinates[column] * 1024 + 128;
			int32_t z = 90 * 1024 - (int32_t)coordinates[row] * 1024 - 128;
			struct VECTOR point = sample_world(x, 1800, z);
			int32_t recovered_x, recovered_z;
			extended_geometry_world_position(&point, &recovered_x, &recovered_z);
			assert(recovered_x == x && recovered_z == z);
			assert(planindex == EXTENDED_GEOMETRY_PLANE_INDEX);
			assert(abs(selected_height(&point) - 1800) <= 2);
			/* Road only covers its center strip, so these off-center
			 * terrain probes retain the independent terrain surface. */
			assert(current_surf_type == surfaces[(row + column) % 4U]);
		}
	}
	struct VECTOR point = sample_world(89 * 1024 + 512, 1800, 512);
	assert(current_surf_type == CAR_SURFACE_ICE);
	assert(abs(selected_height(&point) - 1800) <= 2);
	(void)sample_world(70 * 1024 + 100, 1800, 90 * 1024 - 70 * 1024 - 100);
	assert(current_surf_type == CAR_SURFACE_WATER);
	(void)sample_world(70 * 1024 + 900, 1800, 90 * 1024 - 70 * 1024 - 900);
	assert(current_surf_type == CAR_SURFACE_GRASS);
	assert(extended_track_object_at(89, 89) != NULL);
	assert(extended_track_object_at(90, 89) == NULL);
	assert(wheel_overlaps(&track, &road, 0, 0));
	assert(!wheel_overlaps(&track, &road, 400, 0));
	assert(wheel_overlaps(&track, &road, 0, 40));
	assert(!wheel_overlaps(&track, &road, 0, -40));
	extended_track_unload();
	cstrk_free(&track);
}

static void test_native_mesh_rotations(void)
{
	enum { VERTEX_COUNT = 3, ORIENTATION_COUNT = 4 };
	static const struct VECTOR expected[ORIENTATION_COUNT][VERTEX_COUNT] = {
		{{512, 0, -512}, {257, 450, -389}, {-1000, -37, 115}},
		{{-512, 0, -512}, {-389, 450, -257}, {115, -37, 1000}},
		{{-512, 0, 512}, {-257, 450, 389}, {1000, -37, -115}},
		{{512, 0, 512}, {389, 450, 257}, {-115, -37, -1000}}};
	static const legacy_s16 orientations[ORIENTATION_COUNT] = {
		0, ANGLE_QUARTER_TURN, ANGLE_HALF_TURN, ANGLE_THREE_QUARTER_TURN};
	legacy_u8 vertices[VERTEX_COUNT * SHAPE3D_VERTEX_SIZE];
	legacy_u8 primitives[] = {3, 0, 19, 0, 1, 2, 0, 0};
	legacy_u8 masks[sizeof(legacy_u32)] = {0};
	for (legacy_u16 vertex = 0; vertex < VERTEX_COUNT; vertex++) {
		legacy_u8 *bytes = vertices + vertex * SHAPE3D_VERTEX_SIZE;
		LEGACY_WRITE_U16_LE(bytes, (legacy_u16)expected[0][vertex].x);
		LEGACY_WRITE_U16_LE(bytes + sizeof(legacy_s16), (legacy_u16)expected[0][vertex].y);
		LEGACY_WRITE_U16_LE(bytes + sizeof(legacy_s16) * 2U, (legacy_u16)expected[0][vertex].z);
	}
	legacy_u8 original_vertices[sizeof(vertices)];
	memcpy(original_vertices, vertices, sizeof(vertices));
	struct SHAPE3D source = {VERTEX_COUNT, vertices, 1, 1, primitives, masks, masks};
	for (legacy_u16 direction = 0; direction < ORIENTATION_COUNT; direction++) {
		struct SHAPE3D transformed;
		assert(extended_geometry_transform_shape(&source, &transformed, orientations[direction],
												 CSTRK_ROAD_PAVED, PHYSICAL_MODEL_ROAD));
		for (legacy_u16 vertex = 0; vertex < VERTEX_COUNT; vertex++) {
			const legacy_u8 *bytes =
				transformed.shape3d_vertex_bytes + vertex * SHAPE3D_VERTEX_SIZE;
			assert(LEGACY_READ_S16_LE(bytes) == expected[direction][vertex].x);
			assert(LEGACY_READ_S16_LE(bytes + sizeof(legacy_s16)) == expected[direction][vertex].y);
			assert(LEGACY_READ_S16_LE(bytes + sizeof(legacy_s16) * 2U) ==
				   expected[direction][vertex].z);
		}
		assert(memcmp(transformed.shape3d_primitives, primitives, sizeof(primitives)) == 0);
		assert(memcmp(vertices, original_vertices, sizeof(vertices)) == 0);
		extended_geometry_free_shape(&transformed);
	}
}

static void test_raised_stunt_and_snapshot(void)
{
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	assert(cstrk_create(&track, 90, 90, error, sizeof(error)));
	memset(track.corner_heights, 2, 91U * 91U);
	struct CSTRK_OBJECT tunnel = object_at(66, 41, 50);
	tunnel.base_height = 2;
	tunnel.surface = CSTRK_ROAD_DIRT;
	assert(cstrk_add_object(&track, &tunnel, error, sizeof(error)));
	activate(&track);
	struct VECTOR point = sample_world(41 * 1024 + 512, 900, 90 * 1024 - 50 * 1024 - 512);
	assert(current_surf_type == CAR_SURFACE_DIRT);
	assert(planindex == EXTENDED_GEOMETRY_PLANE_INDEX);
	assert(abs(selected_height(&point) - 900) <= 2);
	assert(current_planptr->plane_normal.x == 0);
	assert(current_planptr->plane_normal.y > 0);
	struct TRACK_COLLISION_SNAPSHOT saved;
	track_collision_capture(&saved);
	struct PLANE original_plane = *current_planptr;
	(void)sample_world(89 * 1024 + 512, 900, 512);
	track_collision_restore(&saved);
	assert(memcmp(current_planptr, &original_plane, sizeof(original_plane)) == 0);
	assert(current_surf_type == CAR_SURFACE_DIRT);
	assert(wheel_overlaps(&track, &tunnel, 0, 0));
	assert(!wheel_overlaps(&track, &tunnel, 400, 0));
	/* A raised plateau translates both the original mesh and its side wall. */
	struct VECTOR inside = object_world(&track, &tunnel, (struct VECTOR){100, 80, 0});
	struct VECTOR outside = object_world(&track, &tunnel, (struct VECTOR){140, 80, 0});
	assert(track_wall_intersects_segment(&inside, &outside));
	inside = object_world(&track, &tunnel, (struct VECTOR){100, 200, 0});
	outside = object_world(&track, &tunnel, (struct VECTOR){140, 200, 0});
	assert(!track_wall_intersects_segment(&inside, &outside));
	extended_track_unload();
	cstrk_free(&track);
}

static void test_nonclassic_terrain_is_rejected(void)
{
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	assert(cstrk_create(&track, 90, 90, error, sizeof(error)));
	size_t northwest = 64U * 91U + 64U;
	track.corner_heights[northwest + 1U] = 1;
	track.corner_heights[northwest + 91U] = 1;
	assert(!cstrk_validate(&track, error, sizeof(error)));
	assert(!extended_track_activate(&track, error, sizeof(error)));
	track.corner_heights[northwest + 92U] = 2;
	assert(!cstrk_validate(&track, error, sizeof(error)));
	assert(!extended_track_activate(&track, error, sizeof(error)));
	cstrk_free(&track);
}

#define HILL_TEST_DIMENSION 8U
#define HILL_TEST_COLUMN 3U
#define HILL_TEST_ROW 3U
#define HILL_TEST_NATIVE_COLUMN 10U
#define HILL_TEST_NATIVE_ROW 10U
#define HILL_TEST_SAMPLE_COUNT 5U
#define HILL_TEST_TERRAIN_PROBE_HEIGHT 900
#define HILL_TEST_SPAN_PROBE_HEIGHT 466
#define HILL_TEST_ROAD_CENTER_HEIGHT (CSTRK_LEVEL_WORLD_HEIGHT / 2)
#define HILL_TEST_GRASS_HEIGHT_HASH_SHIFT 8U
#define HILL_TEST_NON_GRASS_HEIGHT_OFFSET 2

static const legacy_u8 hill_test_masks[] = {0, 0, 0, 0, 0, 0,  15, 3,  9, 12,
											6, 1, 8, 4, 2, 11, 13, 14, 7};
static const legacy_s16 hill_test_positions[HILL_TEST_SAMPLE_COUNT] = {128, 256, 512, 768, 896};

struct NATIVE_HILL_CONTACT {
	struct PLANE plane;
	legacy_s16 height, surface, bias;
	legacy_s16 wall, wall_height, wall_orientation, wall_x, wall_z;
};

static legacy_s16 hill_contact_bias(const struct VECTOR *point, legacy_s16 surface)
{
	if (surface == CAR_SURFACE_GRASS) {
		return (
			legacy_s16)(((legacy_u16)(point->x ^ point->z) >> HILL_TEST_GRASS_HEIGHT_HASH_SHIFT) &
						1U);
	}
	return HILL_TEST_NON_GRASS_HEIGHT_OFFSET;
}

static void make_hill_test_track(struct CSTRK_TRACK *track, legacy_u8 terrain, legacy_u8 base)
{
	cstrk_init(track);
	assert(cstrk_create(track, HILL_TEST_DIMENSION, HILL_TEST_DIMENSION, error, sizeof(error)));
	size_t stride = track->width + 1U;
	memset(track->corner_heights, base, stride * (track->height + 1U));
	size_t northwest = HILL_TEST_ROW * stride + HILL_TEST_COLUMN;
	const size_t corners[] = {northwest, northwest + 1U, northwest + stride + 1U,
							  northwest + stride};
	for (legacy_u8 corner = 0; corner < 4U; corner++) {
		track->corner_heights[corners[corner]] += (hill_test_masks[terrain] >> corner) & 1U;
	}
	assert(cstrk_validate(track, error, sizeof(error)));
}

static void capture_native_hill(
	legacy_u8 terrain, legacy_u8 element, legacy_s16 height,
	struct NATIVE_HILL_CONTACT contacts[HILL_TEST_SAMPLE_COUNT][HILL_TEST_SAMPLE_COUNT])
{
	assert(!extended_track_active());
	memset(track_element_map, 0, TRACKDATA_MAP_SIZE);
	memset(track_terrain_map, 0, TRACKDATA_MAP_SIZE);
	track_element_map[terrainrows[HILL_TEST_NATIVE_ROW] + HILL_TEST_NATIVE_COLUMN] = element;
	track_terrain_map[trackrows[HILL_TEST_NATIVE_ROW] + HILL_TEST_NATIVE_COLUMN] = terrain;
	for (legacy_u8 y = 0; y < HILL_TEST_SAMPLE_COUNT; y++) {
		for (legacy_u8 x = 0; x < HILL_TEST_SAMPLE_COUNT; x++) {
			struct VECTOR point = sample_world(
				HILL_TEST_NATIVE_COLUMN * CSTRK_TILE_WORLD_SIZE + hill_test_positions[x], height,
				(HILL_TEST_NATIVE_ROW + 1U) * CSTRK_TILE_WORLD_SIZE - hill_test_positions[y]);
			struct NATIVE_HILL_CONTACT *contact = &contacts[y][x];
			contact->plane = *current_planptr;
			contact->height = (legacy_s16)selected_height(&point);
			contact->surface = current_surf_type;
			contact->bias = hill_contact_bias(&point, current_surf_type);
			contact->wall = wallindex;
			contact->wall_height = wallHeight;
			contact->wall_orientation = wallOrientation;
			contact->wall_x = LEGACY_S16_WRAP_SUB(wallStartX, elem_xCenter);
			contact->wall_z = LEGACY_S16_WRAP_SUB(wallStartZ, elem_zCenter);
		}
	}
}

static void check_native_hill_contact(const struct NATIVE_HILL_CONTACT *original,
									  const struct VECTOR *point, legacy_u8 base,
									  legacy_s16 surface)
{
	assert(current_surf_type == surface);
	assert(memcmp(&current_planptr->plane_normal, &original->plane.plane_normal,
				  sizeof(current_planptr->plane_normal)) == 0);
	assert(memcmp(&current_planptr->plane_rotation, &original->plane.plane_rotation,
				  sizeof(current_planptr->plane_rotation)) == 0);
	assert(current_planptr->plane_yz == original->plane.plane_yz);
	assert(current_planptr->plane_xy == original->plane.plane_xy);
	legacy_s32 expected = original->height - original->bias + base * CSTRK_LEVEL_WORLD_HEIGHT +
						  hill_contact_bias(point, surface);
	assert(selected_height(point) == expected);
	assert(wallindex == original->wall);
	assert(wallHeight == original->wall_height);
	if (wallindex >= 0) {
		assert(wallOrientation == original->wall_orientation);
		assert(LEGACY_S16_WRAP_SUB(wallStartX, elem_xCenter) == original->wall_x);
		assert(LEGACY_S16_WRAP_SUB(wallStartZ, elem_zCenter) == original->wall_z);
	}
}

static void test_original_hill_terrain(void)
{
	static const legacy_u8 terrains[] = {0, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18};
	legacy_u8 saved_elements[TRACKDATA_MAP_SIZE], saved_terrain[TRACKDATA_MAP_SIZE];
	memcpy(saved_elements, track_element_map, sizeof(saved_elements));
	memcpy(saved_terrain, track_terrain_map, sizeof(saved_terrain));
	for (size_t kind = 0; kind < sizeof(terrains) / sizeof(terrains[0]); kind++) {
		legacy_u8 terrain = terrains[kind];
		struct NATIVE_HILL_CONTACT original[HILL_TEST_SAMPLE_COUNT][HILL_TEST_SAMPLE_COUNT];
		capture_native_hill(terrain, 0, HILL_TEST_TERRAIN_PROBE_HEIGHT, original);
		legacy_u8 last_base = terrain == 0 ? CSTRK_MAX_HEIGHT : CSTRK_MAX_HEIGHT - 1U;
		for (legacy_u8 base = 0; base <= last_base; base++) {
			struct CSTRK_TRACK track;
			make_hill_test_track(&track, terrain, base);
			activate(&track);
			for (legacy_u8 y = 0; y < HILL_TEST_SAMPLE_COUNT; y++) {
				for (legacy_u8 x = 0; x < HILL_TEST_SAMPLE_COUNT; x++) {
					struct VECTOR point = sample_world(
						HILL_TEST_COLUMN * CSTRK_TILE_WORLD_SIZE + hill_test_positions[x],
						base * CSTRK_LEVEL_WORLD_HEIGHT + HILL_TEST_TERRAIN_PROBE_HEIGHT,
						(HILL_TEST_DIMENSION - HILL_TEST_ROW) * CSTRK_TILE_WORLD_SIZE -
							hill_test_positions[y]);
					check_native_hill_contact(&original[y][x], &point, base, CAR_SURFACE_GRASS);
					assert(extended_track_elevation(HILL_TEST_COLUMN, HILL_TEST_ROW,
													hill_test_positions[x],
													hill_test_positions[y]) ==
						   original[y][x].height - original[y][x].bias +
							   base * CSTRK_LEVEL_WORLD_HEIGHT);
				}
			}
			extended_track_unload();
			cstrk_free(&track);
		}
	}
	memcpy(track_element_map, saved_elements, sizeof(saved_elements));
	memcpy(track_terrain_map, saved_terrain, sizeof(saved_terrain));
}

static void test_original_hill_roads(void)
{
	static const legacy_u8 roads[4][6] = {{4, 14, 24, 39, 59, 98},
										  {5, 15, 25, 36, 56, 95},
										  {4, 14, 24, 38, 58, 97},
										  {5, 15, 25, 37, 57, 96}};
	legacy_u8 saved_elements[TRACKDATA_MAP_SIZE], saved_terrain[TRACKDATA_MAP_SIZE];
	memcpy(saved_elements, track_element_map, sizeof(saved_elements));
	memcpy(saved_terrain, track_terrain_map, sizeof(saved_terrain));
	for (legacy_u8 direction = 0; direction < 4U; direction++) {
		legacy_u8 terrain = HILL_TERRAIN_FIRST + direction;
		for (legacy_u8 variant = 0; variant < 6U; variant++) {
			legacy_u8 surface = variant < 3U ? variant : CSTRK_ROAD_PAVED;
			legacy_s16 height =
				variant < 3U ? HILL_TEST_TERRAIN_PROBE_HEIGHT : HILL_TEST_SPAN_PROBE_HEIGHT;
			struct NATIVE_HILL_CONTACT original[HILL_TEST_SAMPLE_COUNT][HILL_TEST_SAMPLE_COUNT];
			capture_native_hill(terrain, roads[direction][variant], height, original);
			for (legacy_u8 base = 0; base < CSTRK_MAX_HEIGHT; base++) {
				struct CSTRK_TRACK track;
				make_hill_test_track(&track, terrain, base);
				struct CSTRK_OBJECT object =
					object_at(roads[direction][variant], HILL_TEST_COLUMN, HILL_TEST_ROW);
				size_t northwest = HILL_TEST_ROW * (track.width + 1U) + HILL_TEST_COLUMN;
				object.base_height = track.corner_heights[northwest];
				object.slope_x = track.corner_heights[northwest + 1U] - object.base_height;
				object.slope_y =
					track.corner_heights[northwest + track.width + 1U] - object.base_height;
				object.surface = surface;
				assert(cstrk_add_object(&track, &object, error, sizeof(error)));
				activate(&track);
				assert(extended_track_object_id(
						   extended_track_object_at(HILL_TEST_COLUMN, HILL_TEST_ROW)) ==
					   subst_hillroad_track(terrain, roads[direction][variant]));
				if (variant < 3U) {
					/* OWOOT contacts must follow the native inclined road, rather
					 * than the flat model originally stored in the editor cell. */
					assert(wheel_overlaps(&track, &object, 0, HILL_TEST_ROAD_CENTER_HEIGHT));
					assert(!wheel_overlaps(&track, &object, 0, 0));
				}
				for (legacy_u8 y = 0; y < HILL_TEST_SAMPLE_COUNT; y++) {
					for (legacy_u8 x = 0; x < HILL_TEST_SAMPLE_COUNT; x++) {
						struct VECTOR point = sample_world(
							HILL_TEST_COLUMN * CSTRK_TILE_WORLD_SIZE + hill_test_positions[x],
							base * CSTRK_LEVEL_WORLD_HEIGHT + height,
							(HILL_TEST_DIMENSION - HILL_TEST_ROW) * CSTRK_TILE_WORLD_SIZE -
								hill_test_positions[y]);
						check_native_hill_contact(&original[y][x], &point, base,
												  original[y][x].surface);
					}
				}
				/* Rotating an otherwise valid hill piece away from its incline
				 * must fail rather than silently tilting the original model. */
				track.objects[0].rotation = 1;
				assert(!extended_track_activate(&track, error, sizeof(error)));
				extended_track_unload();
				cstrk_free(&track);
			}
		}
	}
	memcpy(track_element_map, saved_elements, sizeof(saved_elements));
	memcpy(track_terrain_map, saved_terrain, sizeof(saved_terrain));
}

static void test_hill_shore_geometry(void)
{
	static const legacy_u8 terrains[] = {0, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18};
	static const legacy_s16 materials[] = {CAR_SURFACE_GRASS, CAR_SURFACE_ICE, CAR_SURFACE_DIRT,
										   CAR_SURFACE_WATER};
	legacy_u8 saved_elements[TRACKDATA_MAP_SIZE], saved_terrain[TRACKDATA_MAP_SIZE];
	memcpy(saved_elements, track_element_map, sizeof(saved_elements));
	memcpy(saved_terrain, track_terrain_map, sizeof(saved_terrain));
	for (size_t kind = 0; kind < sizeof(terrains) / sizeof(terrains[0]); kind++) {
		legacy_u8 terrain = terrains[kind];
		struct NATIVE_HILL_CONTACT original[HILL_TEST_SAMPLE_COUNT][HILL_TEST_SAMPLE_COUNT];
		capture_native_hill(terrain, 0, HILL_TEST_TERRAIN_PROBE_HEIGHT, original);
		for (legacy_u8 base = 0; base < CSTRK_MAX_HEIGHT; base++) {
			struct CSTRK_TRACK track;
			make_hill_test_track(&track, terrain, base);
			struct CSTRK_CELL *cell = &track.cells[HILL_TEST_ROW * track.width + HILL_TEST_COLUMN];
			for (legacy_u8 primary = CSTRK_TERRAIN_GRASS; primary <= CSTRK_TERRAIN_WATER;
				 primary++) {
				for (legacy_u8 secondary = CSTRK_TERRAIN_GRASS; secondary <= CSTRK_TERRAIN_WATER;
					 secondary++) {
					if (primary == secondary) {
						continue;
					}
					cell->surface = primary;
					cell->corner_surface = secondary;
					for (legacy_u8 corner = CSTRK_WATER_NORTHWEST; corner <= CSTRK_WATER_SOUTHWEST;
						 corner++) {
						cell->half_water_corner = corner;
						activate(&track);
						for (legacy_u8 y = 0; y < HILL_TEST_SAMPLE_COUNT; y++) {
							for (legacy_u8 x = 0; x < HILL_TEST_SAMPLE_COUNT; x++) {
								legacy_s16 u = hill_test_positions[x], v = hill_test_positions[y];
								legacy_u8 secondary_half =
									(corner == CSTRK_WATER_NORTHWEST &&
									 u + v < CSTRK_TILE_WORLD_SIZE) ||
									(corner == CSTRK_WATER_NORTHEAST && v < u) ||
									(corner == CSTRK_WATER_SOUTHEAST &&
									 u + v >= CSTRK_TILE_WORLD_SIZE) ||
									(corner == CSTRK_WATER_SOUTHWEST && v >= u);
								struct VECTOR point = sample_world(
									HILL_TEST_COLUMN * CSTRK_TILE_WORLD_SIZE + u,
									base * CSTRK_LEVEL_WORLD_HEIGHT +
										HILL_TEST_TERRAIN_PROBE_HEIGHT,
									(HILL_TEST_DIMENSION - HILL_TEST_ROW) * CSTRK_TILE_WORLD_SIZE -
										v);
								check_native_hill_contact(
									&original[y][x], &point, base,
									materials[secondary_half ? secondary : primary]);
							}
						}
						extended_track_unload();
					}
				}
			}
			cstrk_free(&track);
		}
	}
	memcpy(track_element_map, saved_elements, sizeof(saved_elements));
	memcpy(track_terrain_map, saved_terrain, sizeof(saved_terrain));
}

#define SHORE_TEST_COLUMN 64U
#define SHORE_TEST_ROW 64U
#define SHORE_TEST_TILE_SIZE 1024U
#define SHORE_TEST_NEAR 128U
#define SHORE_TEST_FAR 896U
#define SHORE_TEST_CORNER_COUNT 4U
#define SHORE_TEST_OPPOSITE_OFFSET 2U
#define SHORE_TEST_HALF_COUNT 2U
#define SHORE_TEST_HEIGHT_TOLERANCE 2

static void test_shore_materials(void)
{
	static const legacy_s16 surface_types[] = {CAR_SURFACE_GRASS, CAR_SURFACE_ICE, CAR_SURFACE_DIRT,
											   CAR_SURFACE_WATER};
	static const legacy_s16 probes[SHORE_TEST_CORNER_COUNT][2] = {
		{SHORE_TEST_NEAR, SHORE_TEST_NEAR},
		{SHORE_TEST_FAR, SHORE_TEST_NEAR},
		{SHORE_TEST_FAR, SHORE_TEST_FAR},
		{SHORE_TEST_NEAR, SHORE_TEST_FAR}};
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	assert(cstrk_create(&track, CSTRK_MAX_DIMENSION, CSTRK_MAX_DIMENSION, error, sizeof(error)));
	struct CSTRK_CELL *cell = &track.cells[SHORE_TEST_ROW * track.width + SHORE_TEST_COLUMN];
	for (legacy_u8 primary = CSTRK_TERRAIN_GRASS; primary <= CSTRK_TERRAIN_WATER; primary++) {
		for (legacy_u8 secondary = CSTRK_TERRAIN_GRASS; secondary <= CSTRK_TERRAIN_WATER;
			 secondary++) {
			if (primary == secondary) {
				continue;
			}
			cell->surface = primary;
			cell->corner_surface = secondary;
			for (legacy_u8 corner = CSTRK_WATER_NORTHWEST; corner <= CSTRK_WATER_SOUTHWEST;
				 corner++) {
				cell->half_water_corner = corner;
				activate(&track);
				legacy_u8 selected = corner - CSTRK_WATER_NORTHWEST;
				legacy_u8 opposite =
					(selected + SHORE_TEST_OPPOSITE_OFFSET) % SHORE_TEST_CORNER_COUNT;
				legacy_u8 indices[SHORE_TEST_HALF_COUNT];
				indices[0] = selected;
				indices[1] = opposite;
				for (legacy_u8 half = 0; half < sizeof(indices) / sizeof(indices[0]); half++) {
					legacy_s32 world_x =
						SHORE_TEST_COLUMN * SHORE_TEST_TILE_SIZE + probes[indices[half]][0];
					legacy_s32 world_z = (track.height - SHORE_TEST_ROW) * SHORE_TEST_TILE_SIZE -
										 probes[indices[half]][1];
					struct VECTOR point = sample_world(world_x, 0, world_z);
					assert(current_surf_type == surface_types[half == 0 ? secondary : primary]);
					assert(abs(selected_height(&point)) <= SHORE_TEST_HEIGHT_TOLERANCE);
				}
			}
		}
	}
	extended_track_unload();
	cstrk_free(&track);
}

static void test_resource_admission(void)
{
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	assert(cstrk_create(&track, 90, 90, error, sizeof(error)));
	for (unsigned row = 0; row <= 90; row++) {
		for (unsigned column = 41; column <= 90; column++) {
			track.corner_heights[row * 91U + column] = 1;
		}
	}
	struct CSTRK_OBJECT tunnel = object_at(66, 40, 40);
	tunnel.slope_x = 1;
	assert(cstrk_add_object(&track, &tunnel, error, sizeof(error)));
	/* The original editor never permits a tunnel on a hill incline. */
	assert(!extended_track_activate(&track, error, sizeof(error)));
	for (unsigned row = 0; row <= 90; row++) {
		for (unsigned column = 42; column <= 90; column++) {
			track.corner_heights[row * 91U + column] = 2;
		}
	}
	assert(!extended_track_activate(&track, error, sizeof(error)));
	cstrk_free(&track);

	assert(cstrk_create(&track, 90, 90, error, sizeof(error)));
	struct CSTRK_OBJECT loop = object_at(64, 40, 40);
	loop.height = 2;
	loop.occupied_mask = 3;
	loop.surface = CSTRK_ROAD_DIRT;
	assert(cstrk_add_object(&track, &loop, error, sizeof(error)));
	activate(&track);
	extended_track_unload();
	track.objects[0] = object_at(68, 40, 40); /* Full pipe. */
	track.objects[0].surface = CSTRK_ROAD_ICE;
	activate(&track);
	extended_track_unload();
	track.objects[0] = object_at(159, 40, 40); /* Stored scenery surface is ignored. */
	track.objects[0].surface = CSTRK_ROAD_DIRT;
	activate(&track);
	extended_track_unload();
	track.objects[0] = object_at(200, 40, 40); /* Internal overlay, not a placeable tile. */
	assert(!extended_track_activate(&track, error, sizeof(error)));
	cstrk_free(&track);
}

static void test_corkscrew_surfaces(void)
{
	static const unsigned ids[] = {117, 121, 85};
	static const struct VECTOR contacts[][2] = {{{-512, 0, -512}, {-512, 450, 512}},
												{{512, 0, -512}, {512, 450, 512}},
												{{57, 35, -800}, {-57, 35, 800}}};
	for (unsigned kind = 0; kind < 3; kind++) {
		struct CSTRK_TRACK track;
		cstrk_init(&track);
		assert(cstrk_create(&track, 90, 90, error, sizeof(error)));
		struct CSTRK_OBJECT object = object_at(ids[kind], 40, 40);
		object.width = kind == 2 ? 1 : 2;
		object.height = 2;
		object.occupied_mask = kind == 2 ? 3 : 15;
		assert(cstrk_add_object(&track, &object, error, sizeof(error)));
		struct PLANE paved_planes[2];
		legacy_s16 paved_indices[2];
		for (unsigned surface = 0; surface < 3; surface++) {
			track.objects[0].surface = (uint8_t)surface;
			activate(&track);
			for (unsigned contact = 0; contact < 2; contact++) {
				struct VECTOR point = object_world(&track, &object, contacts[kind][contact]);
				(void)sample_world(
					object.x * 1024 + object.width * 512 + contacts[kind][contact].x, point.y,
					90 * 1024 - object.y * 1024 - object.height * 512 + contacts[kind][contact].z);
				assert(current_surf_type == (legacy_s16)(surface + CAR_SURFACE_PAVED));
				assert(planindex > EXTENDED_GEOMETRY_PLANE_INDEX);
				if (surface == CSTRK_ROAD_PAVED) {
					paved_planes[contact] = *current_planptr;
					paved_indices[contact] = planindex;
				} else {
					assert(planindex == paved_indices[contact]);
					assert(memcmp(current_planptr, &paved_planes[contact],
								  sizeof(*current_planptr)) == 0);
				}
			}
		}
		extended_track_unload();
		cstrk_free(&track);
	}
}

#define CURVED_SURFACE_COLUMN 40U
#define CURVED_SURFACE_ROW 40U
#define CURVED_SURFACE_LEVEL 2U
#define CURVED_SURFACE_ROTATIONS 4U
#define CURVED_SURFACE_CONTACTS 3U
#define CURVED_SURFACE_LONG_SIDE 2U
#define CURVED_SURFACE_LOOP_ID 64U
#define CURVED_SURFACE_ENTRANCE_ID 70U
#define CURVED_SURFACE_PIPE_ID 68U
#define CURVED_SURFACE_HALF_PIPE_ID 83U
#define CURVED_SURFACE_OFF_ROAD_X 450

static struct VECTOR sample_curved_surface(const struct CSTRK_TRACK *track,
										   const struct CSTRK_OBJECT *object, struct VECTOR local)
{
	const struct CSTRK_OBJECT *active = extended_track_object_at(object->x, object->y);
	legacy_u16 id = (legacy_u16)extended_track_object_id(active);
	legacy_s16 x = local.x, z = local.z;
	switch (trkObjectList[id].ss_rotY & ANGLE_MASK) {
		case ANGLE_QUARTER_TURN:
			local.x = z;
			local.z = LEGACY_S16_WRAP_NEGATE(x);
			break;
		case ANGLE_HALF_TURN:
			local.x = LEGACY_S16_WRAP_NEGATE(x);
			local.z = LEGACY_S16_WRAP_NEGATE(z);
			break;
		case ANGLE_THREE_QUARTER_TURN:
			local.x = LEGACY_S16_WRAP_NEGATE(z);
			local.z = x;
			break;
	}
	return sample_world(object->x * CSTRK_TILE_WORLD_SIZE +
							object->width * (CSTRK_TILE_WORLD_SIZE / 2U) + local.x,
						object->base_height * CSTRK_LEVEL_WORLD_HEIGHT + local.y,
						(track->height - object->y) * CSTRK_TILE_WORLD_SIZE -
							object->height * (CSTRK_TILE_WORLD_SIZE / 2U) + local.z);
}

static void test_loop_and_pipe_surfaces(void)
{
	static const legacy_u16 ids[] = {CURVED_SURFACE_LOOP_ID, CURVED_SURFACE_ENTRANCE_ID,
									 CURVED_SURFACE_PIPE_ID, CURVED_SURFACE_HALF_PIPE_ID};
	/* These native local points cover flat approach pavement, an inclined
	 * driving face, and an upper/outer face rather than only the tile centre. */
	static const struct VECTOR contacts[][CURVED_SURFACE_CONTACTS] = {
		{{0, 0, 800}, {-200, 200, 300}, {-200, 900, 64}},
		{{0, 0, 0}, {57, 35, 150}, {110, 100, 150}},
		{{0, 0, 0}, {57, 35, 150}, {0, 250, 150}},
		{{0, 0, 0}, {57, 35, 150}, {110, 100, 150}}};
	for (legacy_u8 kind = 0; kind < sizeof(ids) / sizeof(ids[0]); kind++) {
		for (legacy_u8 rotation = 0; rotation < CURVED_SURFACE_ROTATIONS; rotation++) {
			struct CSTRK_TRACK track;
			cstrk_init(&track);
			assert(cstrk_create(&track, CSTRK_MAX_DIMENSION, CSTRK_MAX_DIMENSION, error,
								sizeof(error)));
			memset(track.corner_heights, CURVED_SURFACE_LEVEL,
				   (size_t)(track.width + 1U) * (track.height + 1U));
			struct CSTRK_OBJECT object =
				object_at(ids[kind], CURVED_SURFACE_COLUMN, CURVED_SURFACE_ROW);
			object.base_height = CURVED_SURFACE_LEVEL;
			object.rotation = rotation;
			if (kind == 0) {
				object.width = (rotation & 1U) ? CURVED_SURFACE_LONG_SIDE : 1U;
				object.height = (rotation & 1U) ? 1U : CURVED_SURFACE_LONG_SIDE;
				object.occupied_mask = (1U << (object.width * object.height)) - 1U;
			}
			assert(cstrk_add_object(&track, &object, error, sizeof(error)));
			struct PLANE paved_planes[CURVED_SURFACE_CONTACTS];
			legacy_s16 paved_indices[CURVED_SURFACE_CONTACTS];
			for (legacy_u8 surface = CSTRK_ROAD_PAVED; surface <= CSTRK_ROAD_ICE; surface++) {
				track.objects[0].surface = surface;
				legacy_u8 *bytes;
				size_t byte_count;
				assert(cstrk_encode(&track, &bytes, &byte_count, error, sizeof(error)));
				struct CSTRK_TRACK loaded;
				cstrk_init(&loaded);
				assert(cstrk_decode(bytes, byte_count, &loaded, error, sizeof(error)));
				free(bytes);
				assert(loaded.object_count == 1);
				assert(loaded.objects[0].legacy_id == ids[kind]);
				assert(loaded.objects[0].rotation == rotation);
				assert(loaded.objects[0].surface == surface);
				activate(&loaded);
				for (legacy_u8 contact = 0; contact < CURVED_SURFACE_CONTACTS; contact++) {
					(void)sample_curved_surface(&loaded, &object, contacts[kind][contact]);
					assert(current_surf_type == (legacy_s16)(CAR_SURFACE_PAVED + surface));
					assert(planindex >= EXTENDED_GEOMETRY_PLANE_INDEX);
					if (contact != 0 || kind != 0) {
						assert(planindex > EXTENDED_GEOMETRY_PLANE_INDEX);
					}
					if (contact == 1) {
						assert(current_planptr->plane_normal.x != 0 ||
							   current_planptr->plane_normal.z != 0);
					}
					if (surface == CSTRK_ROAD_PAVED) {
						paved_planes[contact] = *current_planptr;
						paved_indices[contact] = planindex;
					} else {
						assert(planindex == paved_indices[contact]);
						assert(memcmp(current_planptr, &paved_planes[contact],
									  sizeof(*current_planptr)) == 0);
					}
				}
				(void)sample_curved_surface(&loaded, &object,
											(struct VECTOR){CURVED_SURFACE_OFF_ROAD_X, 0, 0});
				assert(current_surf_type == CAR_SURFACE_GRASS);
				extended_track_unload();
				cstrk_free(&loaded);
			}
			cstrk_free(&track);
		}
	}
}

static void test_scenery_ignores_road_surface(void)
{
	static const unsigned ids[] = {151, 152, 153, 154, 155, 159, 163, 167, 171, 175};
	static const legacy_s16 ground_surfaces[] = {CAR_SURFACE_GRASS, CAR_SURFACE_ICE,
												 CAR_SURFACE_DIRT, CAR_SURFACE_WATER};
	for (unsigned kind = 0; kind < sizeof(ids) / sizeof(ids[0]); kind++) {
		struct CSTRK_TRACK track;
		cstrk_init(&track);
		assert(cstrk_create(&track, 90, 90, error, sizeof(error)));
		struct CSTRK_OBJECT object = object_at(ids[kind], 40, 40);
		assert(cstrk_add_object(&track, &object, error, sizeof(error)));
		for (unsigned ground = 0; ground < 4; ground++) {
			track.cells[40U * 90U + 40U].surface = (uint8_t)ground;
			struct PLANE original_plane;
			legacy_s16 original_wall = 0;
			for (unsigned surface = 0; surface < 3; surface++) {
				track.objects[0].surface = (uint8_t)surface;
				activate(&track);
				struct VECTOR point = object_world(&track, &object, (struct VECTOR){0, 80, 0});
				struct VECTOR next = object_world(&track, &object, (struct VECTOR){400, 80, 0});
				extended_geometry_set_context((40 * 1024 + 512) * 64,
											  (90 * 1024 - 40 * 1024 - 512) * 64);
				build_track_object(&point, &next);
				assert(current_surf_type == ground_surfaces[ground]);
				if (surface == CSTRK_ROAD_PAVED) {
					original_plane = *current_planptr;
					original_wall = wallindex;
				} else {
					assert(memcmp(current_planptr, &original_plane, sizeof(*current_planptr)) == 0);
					assert(wallindex == original_wall);
				}
			}
		}
		extended_track_unload();
		cstrk_free(&track);
	}
}

static void test_extended_material_variants(void)
{
	enum {
		MATERIAL_VERTEX_COUNT = 3,
		MATERIAL_FACE_COUNT = 9,
		MATERIAL_PRIMITIVE_BYTES = 6,
		MATERIAL_MAX_PAINTS = 4
	};
	legacy_u8 vertices[MATERIAL_VERTEX_COUNT * SHAPE3D_VERTEX_SIZE] = {0};
	legacy_u8 masks[MATERIAL_FACE_COUNT * SHAPE3D_VISIBILITY_MASK_SIZE] = {0};
	static const legacy_u8 original[] = {19, 21, 22, 23, 24, 4, 31, 32, 33};
	legacy_u8 primitives[MATERIAL_FACE_COUNT * MATERIAL_PRIMITIVE_BYTES + 1U] = {0};
	for (legacy_u8 face = 0; face < MATERIAL_FACE_COUNT; face++) {
		primitives[face * 6] = 3;
		primitives[face * 6 + 2] = original[face];
		primitives[face * 6 + 4] = 1;
		primitives[face * 6 + 5] = 2;
	}
	struct SHAPE3D source = {
		MATERIAL_VERTEX_COUNT, vertices, MATERIAL_FACE_COUNT, 1, primitives, masks, masks};
	static const legacy_s16 models[] = {PHYSICAL_MODEL_CORKSCREW_UP_DOWN_A,
										PHYSICAL_MODEL_CORKSCREW_UP_DOWN_B,
										PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT,
										PHYSICAL_MODEL_LOOP,
										PHYSICAL_MODEL_PIPE_ENTRANCE,
										PHYSICAL_MODEL_PIPE,
										PHYSICAL_MODEL_HALF_PIPE};
	for (legacy_u8 kind = 0; kind < sizeof(models) / sizeof(models[0]); kind++) {
		for (legacy_u8 surface = CSTRK_ROAD_PAVED; surface <= CSTRK_ROAD_ICE; surface++) {
			struct SHAPE3D transformed;
			assert(
				extended_geometry_transform_shape(&source, &transformed, 0, surface, models[kind]));
			assert(memcmp(transformed.shape3d_vertex_bytes, vertices, sizeof(vertices)) == 0);
			for (legacy_u8 face = 0; face < MATERIAL_FACE_COUNT; face++) {
				legacy_u8 expected = original[face];
				if (surface && face < 2) {
					expected = face ? (surface == 1 ? 27 : 30) : (surface == 1 ? 25 : 28);
				}
				if (surface && kind >= 2 && face >= 2 && face <= 4) {
					expected = (surface == 1 ? 25 : 28) + face - 2;
				}
				assert(transformed.shape3d_primitives[face * 6 + 2] == expected);
			}
			extended_geometry_free_shape(&transformed);
		}
	}
	/* Scenery paint columns and road-looking source colors both stay unchanged. */
	for (legacy_u8 paints = 1; paints <= MATERIAL_MAX_PAINTS; paints++) {
		legacy_u8
			scenery_primitives[MATERIAL_FACE_COUNT * (SHAPE3D_PRIMITIVE_HEADER_SIZE +
													  MATERIAL_MAX_PAINTS + MATERIAL_VERTEX_COUNT) +
							   1U] = {0};
		for (legacy_u8 face = 0; face < MATERIAL_FACE_COUNT; face++) {
			legacy_u16 offset = face * (5 + paints);
			scenery_primitives[offset] = 3;
			for (legacy_u8 paint = 0; paint < paints; paint++) {
				scenery_primitives[offset + 2 + paint] = original[face] + paint;
			}
			scenery_primitives[offset + 3 + paints] = 1;
			scenery_primitives[offset + 4 + paints] = 2;
		}
		source.shape3d_numpaints = (legacy_u16)paints;
		source.shape3d_primitives = scenery_primitives;
		for (legacy_u8 surface = CSTRK_ROAD_PAVED; surface <= CSTRK_ROAD_ICE; surface++) {
			struct SHAPE3D transformed;
			assert(extended_geometry_transform_shape(&source, &transformed, 0, surface,
													 PHYSICAL_MODEL_BARN));
			for (legacy_u8 face = 0; face < MATERIAL_FACE_COUNT; face++) {
				assert(transformed.shape3d_primitives[face * 6 + 2] == original[face]);
			}
			extended_geometry_free_shape(&transformed);
		}
	}
}

static void add_tile(struct CSTRK_TRACK *track, unsigned id, unsigned x, unsigned y)
{
	struct CSTRK_OBJECT object = object_at(id, x, y);
	assert(cstrk_add_object(track, &object, error, sizeof(error)));
}

static void big_turn_route_point(struct VECTOR result[ROUTE_GEOMETRY_POINT_COUNT])
{
	for (legacy_s16 index = 0; index < track_pieces_counter; index++) {
		if ((uint8_t)track_route_element_ids[index] == 13U) {
			(void)get_track_route_point(index, result, 0, NULL);
			return;
		}
	}
	assert(0 && "Big turn is missing from the connected route");
}

static void test_shared_road_anchor(void)
{
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	assert(cstrk_create(&track, 90, 90, error, sizeof(error)));
	struct CSTRK_OBJECT turn = object_at(13, 40, 40);
	turn.width = turn.height = 2;
	turn.occupied_mask = 14;
	assert(cstrk_add_object(&track, &turn, error, sizeof(error)));
	add_tile(&track, 6, 37, 37);
	add_tile(&track, 7, 41, 37);
	add_tile(&track, 8, 37, 41);
	for (unsigned x = 38; x <= 40; x++) {
		add_tile(&track, 5, x, 37);
	}
	for (unsigned x = 38; x <= 39; x++) {
		add_tile(&track, 5, x, 41);
	}
	for (unsigned y = 38; y <= 40; y++) {
		add_tile(&track, y == 39 ? 1 : 4, 37, y);
	}
	add_tile(&track, 4, 41, 38);
	add_tile(&track, 4, 41, 39);
	activate(&track);
	int result = track_setup();
	if (result != 0) {
		fprintf(stderr, "Big-turn route baseline failed %d at %u,%u\n", result,
				track_validation_column, track_validation_row);
	}
	assert(result == 0);
	legacy_s16 original_count = track_pieces_counter;
	struct VECTOR before[ROUTE_GEOMETRY_POINT_COUNT] = {0};
	struct VECTOR after[ROUTE_GEOMETRY_POINT_COUNT] = {0};
	big_turn_route_point(before);
	extended_track_unload();
	/* This straight uses the turn's unclaimed northwest quadrant. Its anchor
	 * equals the turn's, so an anchor-to-one-object map cannot represent both. */
	add_tile(&track, 4, 40, 40);
	activate(&track);
	assert(extended_track_object_at(40, 40)->legacy_id == 4);
	assert(extended_track_object_for_id(40, 40, 13)->width == 2);
	assert(extended_track_object_for_id(40, 40, 4)->width == 1);
	result = track_setup();
	if (result != 0) {
		fprintf(stderr, "Shared-anchor road route failed %d at %u,%u\n", result,
				track_validation_column, track_validation_row);
	}
	assert(result == 0 && track_pieces_counter == original_count);
	big_turn_route_point(after);
	assert(memcmp(before, after, sizeof(before)) == 0);
	extended_track_unload();
	cstrk_free(&track);
}

static void test_opponent_shortest_branch(void)
{
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	assert(cstrk_create(&track, 2, 2, error, sizeof(error)));
	activate(&track);
	track_pieces_counter = 4;
	for (unsigned index = 0; index < 4; index++) {
		track_alternate_route_links[index] = -1;
		track_primary_route_links[index] = (legacy_s16)(index + 1);
		track_route_element_ids[index] = 4;
	}
	track_primary_route_links[1] = track_primary_route_links[2] = 3;
	track_primary_route_links[3] = 0;
	track_alternate_route_links[0] = 2;
	track_route_element_ids[2] = 5;
	legacy_u8 costs[256] = {0};
	costs[4] = 200;
	costs[5] = 1;
	assert(extended_opponent_build_route(costs));
	static const unsigned expected[] = {0, 2, 3, 0, 0, 1};
	for (unsigned index = 0; index < 6; index++) {
		assert(LEGACY_READ_U16_LE((const legacy_u8 *)opponent_route_track_indices + index * 2U) ==
			   expected[index]);
	}
	legacy_u8 previous[12];
	memcpy(previous, opponent_route_track_indices, sizeof(previous));
	track_primary_route_links[3] = 99;
	assert(!extended_opponent_build_route(costs));
	assert(memcmp(previous, opponent_route_track_indices, sizeof(previous)) == 0);
	extended_track_unload();
	cstrk_free(&track);
}

static void test_sparse_catalog_footprints(void)
{
	static const unsigned groups[] = {10, 20, 30, 52, 105};
	static const unsigned masks[] = {7, 11, 13, 14};
	for (unsigned group = 0; group < sizeof(groups) / sizeof(groups[0]); group++) {
		for (unsigned variant = 0; variant < 4; variant++) {
			struct CSTRK_TRACK track;
			cstrk_init(&track);
			assert(cstrk_create(&track, 90, 90, error, sizeof(error)));
			struct CSTRK_OBJECT corner = object_at(groups[group] + variant, 86, 86);
			corner.width = corner.height = 2;
			corner.occupied_mask = (uint8_t)masks[variant];
			assert(cstrk_add_object(&track, &corner, error, sizeof(error)));
			unsigned empty = 0;
			while (corner.occupied_mask & (1U << empty)) {
				empty++;
			}
			struct CSTRK_OBJECT decoration = object_at(159, 86 + empty % 2U, 86 + empty / 2U);
			assert(cstrk_add_object(&track, &decoration, error, sizeof(error)));
			activate(&track);
			const struct CSTRK_OBJECT *found = extended_track_object_at(decoration.x, decoration.y);
			assert(found != NULL && found->legacy_id == 159);
			assert(extended_track_object_origin_at(86, 86)->legacy_id == corner.legacy_id);
			struct VECTOR inside =
				sample_world(decoration.x * 1024 + 512, 0, 90 * 1024 - decoration.y * 1024 - 512);
			struct VECTOR outside = inside;
			outside.x = LEGACY_S16_WRAP_ADD(outside.x, 200);
			build_track_object(&inside, &outside);
			assert(wallindex == 163 && wallHeight == 425); /* Original barn's east wall. */
			/* Resource admission must reject a forged smaller footprint. */
			cstrk_remove_object(&track, 1);
			track.objects[0].width = track.objects[0].height = 1;
			track.objects[0].occupied_mask = 1;
			assert(!extended_track_activate(&track, error, sizeof(error)));
			assert(extended_track_active());
			extended_track_unload();
			cstrk_free(&track);
		}
	}
	/* Both diagonal chicane handednesses survive additional half-turns. */
	for (unsigned id = 60; id <= 63; id++) {
		struct CSTRK_TRACK track;
		cstrk_init(&track);
		assert(cstrk_create(&track, 90, 90, error, sizeof(error)));
		struct CSTRK_OBJECT chicane = object_at(id, 87, 87);
		chicane.width = chicane.height = 2;
		chicane.occupied_mask = (uint8_t)(id <= 61 ? 6 : 9);
		chicane.rotation = 2;
		assert(cstrk_add_object(&track, &chicane, error, sizeof(error)));
		activate(&track);
		assert(extended_track_object_id(extended_track_object_origin_at(87, 87)) != 0U);
		extended_track_unload();
		cstrk_free(&track);
	}
}

struct PATH_CELL {
	uint8_t x, y;
};

static unsigned side_of(struct PATH_CELL origin, struct PATH_CELL neighbor)
{
	if (neighbor.x > origin.x) {
		return 1U; /* east */
	}
	if (neighbor.y > origin.y) {
		return 2U; /* south */
	}
	if (neighbor.x < origin.x) {
		return 4U; /* west */
	}
	return 8U; /* north */
}

static void test_long_route(void)
{
	struct PATH_CELL path[8100];
	unsigned count = 0;
	path[count++] = (struct PATH_CELL){0, 1};
	path[count++] = (struct PATH_CELL){1, 1};
	for (unsigned row = 0; row < 44; row++) {
		unsigned y = 1U + row * 2U;
		if (row % 2U == 0U) {
			for (unsigned x = 2; x <= 88; x++) {
				path[count++] = (struct PATH_CELL){(uint8_t)x, (uint8_t)y};
			}
		} else {
			for (unsigned x = 88; x >= 2; x--) {
				path[count++] = (struct PATH_CELL){(uint8_t)x, (uint8_t)y};
			}
		}
		if (row < 43U) {
			unsigned x = row % 2U == 0U ? 88U : 2U;
			path[count++] = (struct PATH_CELL){(uint8_t)x, (uint8_t)(y + 1U)};
		}
	}
	path[count++] = (struct PATH_CELL){1, 87};
	path[count++] = (struct PATH_CELL){0, 87};
	for (unsigned y = 86; y >= 2; y--) {
		path[count++] = (struct PATH_CELL){0, (uint8_t)y};
	}
	assert(count > 3900U && count < 8100U);
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	assert(cstrk_create(&track, 90, 90, error, sizeof(error)));
	track.objects = calloc(count, sizeof(*track.objects));
	assert(track.objects != NULL);
	track.object_count = count;
	for (unsigned index = 0; index < count; index++) {
		struct PATH_CELL cell = path[index];
		unsigned sides = side_of(cell, path[(index + count - 1U) % count]) |
						 side_of(cell, path[(index + 1U) % count]);
		unsigned id = sides == 3U	 ? 6U
					  : sides == 6U	 ? 7U
					  : sides == 9U	 ? 8U
					  : sides == 12U ? 9U
					  : sides == 5U	 ? 5U
									 : 4U;
		if (cell.x == 0 && cell.y == 40) {
			id = 1;
		}
		track.objects[index] = object_at(id, cell.x, cell.y);
	}
	activate(&track);
	int result = track_setup();
	if (result != 0) {
		fprintf(stderr, "Long-route setup error %d at %u,%u\n", result, track_validation_column,
				track_validation_row);
	}
	assert(result == 0);
	assert(track_pieces_counter == (legacy_s16)count);
	assert(start_finish_column == 0 && start_finish_row == 40);
	int visited_far = 0;
	for (unsigned index = 0; index < count; index++) {
		if ((uint8_t)track_route_columns[index] == 88U) {
			visited_far = 1;
		}
		assert(track_primary_route_links[index] >= 0 &&
			   track_primary_route_links[index] < (int)count);
	}
	assert(visited_far);
	/* Opponent routing used a 902-word stack path in the original game.
	 * Exercise the real speed resource and a route several times that size. */
	gameconfig.game_opponenttype = 1;
	load_opponent_data();
	for (unsigned index = 0; index < count; index++) {
		unsigned actual =
			LEGACY_READ_U16_LE((const legacy_u8 *)opponent_route_track_indices + index * 2U);
		if (actual != index) {
			fprintf(stderr, "Opponent route %u: expected %u, got %u (count %u)\n", index, index,
					actual, count);
		}
		assert(actual == index);
	}
	assert(LEGACY_READ_U16_LE((const legacy_u8 *)opponent_route_track_indices + count * 2U) == 0);
	gameconfig.game_opponenttype = 0;
	extended_track_unload();
	cstrk_free(&track);
}

int main(void)
{
	FILE *resource = fopen("GAME.PRE", "rb");
	if (resource == NULL) {
		resource = fopen("GAME.RES", "rb");
	}
	if (resource == NULL) {
		fprintf(stderr,
				"Extended-track integration requires original GAME.PRE or GAME.RES; skipped.\n");
		return 77;
	}
	fclose(resource);
	mmgr_init_conventional_arena();
	init_row_tables();
	init_trackdata();
	load_track_collision_resources();
	assert(planptr != NULL && wallptr != NULL);
	legacy_u8 *legacy_elements = track_element_map;
	legacy_u8 *legacy_terrain = track_terrain_map;
	legacy_s16 *legacy_links = track_primary_route_links;
	test_native_mesh_rotations();
	test_world_coordinates_and_surfaces();
	test_raised_stunt_and_snapshot();
	test_nonclassic_terrain_is_rejected();
	test_original_hill_terrain();
	test_original_hill_roads();
	test_hill_shore_geometry();
	test_shore_materials();
	test_resource_admission();
	test_corkscrew_surfaces();
	test_loop_and_pipe_surfaces();
	test_scenery_ignores_road_surface();
	test_extended_material_variants();
	test_shared_road_anchor();
	test_opponent_shortest_branch();
	test_sparse_catalog_footprints();
	test_long_route();
	assert(!extended_track_active() && extended_track_width() == 30);
	assert(track_element_map == legacy_elements && track_terrain_map == legacy_terrain);
	assert(track_primary_route_links == legacy_links);
	assert(trackrows[0] == 29 * 30 && trackrows[29] == 0);
	puts("Extended 90x90 terrain, original hills, surfaces, sparse overlays and long routes "
		 "passed.");
	return 0;
}
