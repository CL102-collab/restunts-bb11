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
	struct EXTENDED_GEOMETRY_BASIS basis;
	struct VECTOR origin, transformed;
	extended_geometry_object_basis(object, &basis);
	extended_geometry_object_origin(track, object, &origin);
	extended_geometry_forward(&basis, &local, &transformed);
	transformed.x = LEGACY_S16_WRAP_ADD(transformed.x, origin.x);
	transformed.y = LEGACY_S16_WRAP_ADD(transformed.y, origin.y);
	transformed.z = LEGACY_S16_WRAP_ADD(transformed.z, origin.z);
	return transformed;
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

static int64_t length_squared(const struct VECTOR *vector)
{
	return (int64_t)vector->x * vector->x + (int64_t)vector->y * vector->y +
		   (int64_t)vector->z * vector->z;
}

static void test_rigid_transform(void)
{
	for (int x = -1; x <= 1; x++) {
		for (int y = -1; y <= 1; y++) {
			struct CSTRK_OBJECT object = object_at(66, 41, 50);
			object.slope_x = (int8_t)x;
			object.slope_y = (int8_t)y;
			struct EXTENDED_GEOMETRY_BASIS basis;
			extended_geometry_object_basis(&object, &basis);
			static const struct VECTOR samples[] = {
				{1024, 0, 0}, {0, 1024, 0}, {0, 0, 1024}, {732, -151, -511}, {-1000, 450, 115}};
			struct VECTOR axes[3];
			for (unsigned index = 0; index < sizeof(samples) / sizeof(samples[0]); index++) {
				struct VECTOR transformed, recovered;
				extended_geometry_forward(&basis, &samples[index], &transformed);
				extended_geometry_inverse(&basis, &transformed, &recovered);
				assert(llabs(length_squared(&transformed) - length_squared(&samples[index])) <
					   4096);
				assert(abs(recovered.x - samples[index].x) <= 1);
				assert(abs(recovered.y - samples[index].y) <= 1);
				assert(abs(recovered.z - samples[index].z) <= 1);
				if (index < 3U) {
					axes[index] = transformed;
				}
			}
			for (unsigned first = 0; first < 3; first++) {
				for (unsigned second = first + 1; second < 3; second++) {
					int64_t dot = (int64_t)axes[first].x * axes[second].x +
								  (int64_t)axes[first].y * axes[second].y +
								  (int64_t)axes[first].z * axes[second].z;
					assert(llabs(dot) < 2048);
				}
			}
		}
	}
}

static void test_sloped_stunt_and_snapshot(void)
{
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	assert(cstrk_create(&track, 90, 90, error, sizeof(error)));
	for (unsigned row = 0; row <= 90U; row++) {
		for (unsigned column = 0; column <= 90U; column++) {
			int height = (int)column - 40;
			track.corner_heights[row * 91U + column] = (uint8_t)(height < 0	  ? 0
																 : height > 4 ? 4
																			  : height);
		}
	}
	struct CSTRK_OBJECT tunnel = object_at(66, 41, 50);
	tunnel.base_height = 1;
	tunnel.slope_x = 1;
	tunnel.surface = CSTRK_ROAD_DIRT;
	assert(cstrk_add_object(&track, &tunnel, error, sizeof(error)));
	activate(&track);
	struct VECTOR point = sample_world(41 * 1024 + 512, 675, 90 * 1024 - 50 * 1024 - 512);
	assert(current_surf_type == CAR_SURFACE_DIRT);
	assert(planindex == EXTENDED_GEOMETRY_PLANE_INDEX);
	assert(abs(selected_height(&point) - 675) <= 2);
	assert(current_planptr->plane_normal.x < 0);
	assert(current_planptr->plane_normal.y > 0);
	struct TRACK_COLLISION_SNAPSHOT saved;
	track_collision_capture(&saved);
	struct PLANE original_plane = *current_planptr;
	(void)sample_world(89 * 1024 + 512, 1800, 512);
	assert(current_planptr->plane_normal.x == 0);
	track_collision_restore(&saved);
	assert(memcmp(current_planptr, &original_plane, sizeof(original_plane)) == 0);
	assert(current_surf_type == CAR_SURFACE_DIRT);
	assert(wheel_overlaps(&track, &tunnel, 0, 0));
	assert(!wheel_overlaps(&track, &tunnel, 400, 0));
	/* The mesh and its side wall share one rigid transform. */
	struct VECTOR inside = object_world(&track, &tunnel, (struct VECTOR){100, 80, 0});
	struct VECTOR outside = object_world(&track, &tunnel, (struct VECTOR){140, 80, 0});
	assert(track_wall_intersects_segment(&inside, &outside));
	inside = object_world(&track, &tunnel, (struct VECTOR){100, 200, 0});
	outside = object_world(&track, &tunnel, (struct VECTOR){140, 200, 0});
	assert(!track_wall_intersects_segment(&inside, &outside));
	extended_track_unload();
	cstrk_free(&track);
}

static void test_half_water_saddle(void)
{
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	assert(cstrk_create(&track, 90, 90, error, sizeof(error)));
	size_t northwest = 64U * 91U + 64U;
	track.corner_heights[northwest + 1U] = 1;
	track.corner_heights[northwest + 91U] = 1;
	static const unsigned corners[] = {CSTRK_WATER_NONE, CSTRK_WATER_NORTHWEST,
									   CSTRK_WATER_NORTHEAST, CSTRK_WATER_SOUTHEAST,
									   CSTRK_WATER_SOUTHWEST};
	for (unsigned corner = 0; corner < 5; corner++) {
		track.cells[64U * 90U + 64U].half_water_corner = (uint8_t)corners[corner];
		activate(&track);
		for (int point = 256; point <= 768; point += 256) {
			int height = (corner == CSTRK_WATER_NORTHWEST || corner == CSTRK_WATER_SOUTHEAST)
							 ? (point == 512 ? 450 : 225)
							 : 0;
			assert(extended_track_elevation(64, 64, point, point) == height);
			struct VECTOR world =
				sample_world(64 * 1024 + point, height, 90 * 1024 - 64 * 1024 - point);
			assert(abs(selected_height(&world) - height) <= 3);
		}
	}
	extended_track_unload();
	cstrk_free(&track);
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
	/* One isolated incline is too short to support a tilted stunt. */
	assert(!extended_track_activate(&track, error, sizeof(error)));
	for (unsigned row = 0; row <= 90; row++) {
		for (unsigned column = 42; column <= 90; column++) {
			track.corner_heights[row * 91U + column] = 2;
		}
	}
	activate(&track);
	extended_track_unload();
	cstrk_free(&track);

	assert(cstrk_create(&track, 90, 90, error, sizeof(error)));
	struct CSTRK_OBJECT loop = object_at(64, 40, 40);
	loop.height = 2;
	loop.occupied_mask = 3;
	loop.surface = CSTRK_ROAD_DIRT;
	assert(cstrk_add_object(&track, &loop, error, sizeof(error)));
	assert(!extended_track_activate(&track, error, sizeof(error)));
	track.objects[0].surface = CSTRK_ROAD_PAVED;
	activate(&track);
	extended_track_unload();
	track.objects[0] = object_at(68, 40, 40); /* Full pipe. */
	track.objects[0].surface = CSTRK_ROAD_ICE;
	assert(!extended_track_activate(&track, error, sizeof(error)));
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
	legacy_u8 vertices[18] = {0}, masks[24] = {0};
	static const legacy_u8 original[] = {19, 21, 22, 23, 24, 4};
	legacy_u8 primitives[37] = {0};
	for (unsigned face = 0; face < 6; face++) {
		primitives[face * 6] = 3;
		primitives[face * 6 + 2] = original[face];
		primitives[face * 6 + 4] = 1;
		primitives[face * 6 + 5] = 2;
	}
	struct SHAPE3D source = {3, vertices, 6, 1, primitives, masks, masks};
	struct CSTRK_OBJECT object = object_at(117, 40, 40);
	struct EXTENDED_GEOMETRY_BASIS basis;
	extended_geometry_object_basis(&object, &basis);
	static const legacy_s16 models[] = {PHYSICAL_MODEL_CORKSCREW_UP_DOWN_A,
										PHYSICAL_MODEL_CORKSCREW_UP_DOWN_B,
										PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT};
	for (unsigned kind = 0; kind < 3; kind++) {
		for (unsigned surface = 0; surface < 3; surface++) {
			struct SHAPE3D transformed;
			assert(extended_geometry_transform_shape(&source, &transformed, &basis, 0, surface,
													 models[kind]));
			assert(memcmp(transformed.shape3d_vertex_bytes, vertices, sizeof(vertices)) == 0);
			for (unsigned face = 0; face < 6; face++) {
				unsigned expected = original[face];
				if (surface && face < 2) {
					expected = face ? (surface == 1 ? 27 : 30) : (surface == 1 ? 25 : 28);
				}
				if (surface && kind == 2 && face >= 2 && face <= 4) {
					expected = (surface == 1 ? 25 : 28) + face - 2;
				}
				assert(transformed.shape3d_primitives[face * 6 + 2] == expected);
			}
			extended_geometry_free_shape(&transformed);
		}
	}
	/* Scenery paint columns and road-looking source colors both stay unchanged. */
	for (unsigned paints = 1; paints <= 4; paints++) {
		legacy_u8 scenery_primitives[55] = {0};
		for (unsigned face = 0; face < 6; face++) {
			unsigned offset = face * (5 + paints);
			scenery_primitives[offset] = 3;
			for (unsigned paint = 0; paint < paints; paint++) {
				scenery_primitives[offset + 2 + paint] = original[face] + paint;
			}
			scenery_primitives[offset + 3 + paints] = 1;
			scenery_primitives[offset + 4 + paints] = 2;
		}
		source.shape3d_numpaints = (legacy_u16)paints;
		source.shape3d_primitives = scenery_primitives;
		for (unsigned surface = 0; surface < 3; surface++) {
			struct SHAPE3D transformed;
			assert(extended_geometry_transform_shape(&source, &transformed, &basis, 0, surface,
													 PHYSICAL_MODEL_BARN));
			for (unsigned face = 0; face < 6; face++) {
				assert(transformed.shape3d_primitives[face * 6 + 2] == original[face]);
			}
			extended_geometry_free_shape(&transformed);
		}
	}
}

static int resource_plane_height(unsigned index, int x, int z)
{
	const struct PLANE *plane = &planptr[index];
	assert(plane->plane_normal.y != 0);
	return plane->plane_origin.y -
		   (int)(((int64_t)plane->plane_normal.x * (x - plane->plane_origin.x) +
				  (int64_t)plane->plane_normal.z * (z - plane->plane_origin.z)) /
				 plane->plane_normal.y);
}

static void test_intrinsic_portal_profiles(void)
{
	static const unsigned ids[] = {34, 49, 38};
	static const unsigned planes[] = {8, 24, 12};
	for (unsigned model = 0; model < 3; model++) {
		struct CSTRK_TRACK track;
		cstrk_init(&track);
		assert(cstrk_create(&track, 90, 90, error, sizeof(error)));
		struct CSTRK_OBJECT object = object_at(ids[model], 40, 40);
		assert(cstrk_add_object(&track, &object, error, sizeof(error)));
		activate(&track);
		const struct CSTRK_OBJECT *placed = extended_track_object_origin_at(40, 40);
		for (int x = -100; x <= 100; x += 100) {
			for (int z = -512; z <= 512; z += 1024) {
				legacy_s16 height;
				assert(track_extended_portal_height(placed, (legacy_s16)x, (legacy_s16)z, &height));
				assert(height == resource_plane_height(planes[model], x, z));
				if (model == 0) {
					assert(height == 450);
				}
			}
		}
		extended_track_unload();
		cstrk_free(&track);
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
	test_rigid_transform();
	test_world_coordinates_and_surfaces();
	test_sloped_stunt_and_snapshot();
	test_half_water_saddle();
	test_shore_materials();
	test_resource_admission();
	test_corkscrew_surfaces();
	test_scenery_ignores_road_surface();
	test_extended_material_variants();
	test_intrinsic_portal_profiles();
	test_shared_road_anchor();
	test_opponent_shortest_branch();
	test_sparse_catalog_footprints();
	test_long_route();
	assert(!extended_track_active() && extended_track_width() == 30);
	assert(track_element_map == legacy_elements && track_terrain_map == legacy_terrain);
	assert(track_primary_route_links == legacy_links);
	assert(trackrows[0] == 29 * 30 && trackrows[29] == 0);
	puts("Extended 90x90 terrain, surfaces, rigid stunts, sparse overlays and long routes passed.");
	return 0;
}
