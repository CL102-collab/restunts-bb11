#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../genghost/routes.h"
#include "../genghost/maneuvers.h"
#include "../c/extended_geometry.h"
#include "../c/game_input.h"
#include "../c/externs.h"
#include "../c/cstrk.h"
#include "../c/extended_track.h"
#include "../c/roll_ramp.h"
#include "../c/track_objects.h"
#include "../c/trackdata_layout.h"
#include "../c/memmgr.h"

#undef memcpy
#undef printf

#define TEST_ERROR_CAPACITY 256U
#define TEST_LEFT 72U
#define TEST_RIGHT 78U
#define TEST_TOP 8U
#define TEST_BOTTOM 16U
#define TEST_START_ROW 12U
#define TEST_BARREL_TOP 20U
#define TEST_BARREL_BOTTOM 55U
#define TEST_BARREL_START 22U
#define TEST_BARREL_ROW 43U
#define TEST_LANDING_ROW 48U
#define TEST_COMBINED_ROW 44U
#define TEST_ROTATIONS 4U
#define TEST_START_NORTH 1U
#define TEST_START_SOUTH 179U
#define TEST_ROAD_VERTICAL 4U
#define TEST_ROAD_HORIZONTAL 5U
#define TEST_TURN_NORTHWEST 6U
#define TEST_TURN_NORTHEAST 7U
#define TEST_TURN_SOUTHWEST 8U
#define TEST_TURN_SOUTHEAST 9U
#define TEST_COORDINATE_PAGE 65536L
#define TEST_LONG_ROWS 44U
#define TEST_LONG_LAST_ROW 87U
#define TEST_LONG_LAST_COLUMN 88U
#define TEST_LONG_MINIMUM_PIECES 3900U
#define TEST_LONG_START_ROW 40U
#define TEST_GRADE_FIRST_ROW 43U
#define TEST_GRADE_PIECE_ROW 44U
#define TEST_GRADE_ORIGIN_COLUMN 70U
#define TEST_GRADE_CHICANE_LAST_ID 61U
#define TEST_GRADE_BODY_HEIGHT 8L
#define TEST_POSITION_SCALE 64L
#define TEST_GRADE_APPROACH 512.0
#define TEST_GRADE_LOOKAHEAD 200.0

extern legacy_f64 sqrt(legacy_f64);

static legacy_char error[TEST_ERROR_CAPACITY];

static void add_piece(struct CSTRK_TRACK *track, legacy_u16 id, legacy_u8 x, legacy_u8 y)
{
	struct CSTRK_OBJECT object = {0};
	object.legacy_id = id;
	object.x = x;
	object.y = y;
	object.width = object.height = object.occupied_mask = 1U;
	assert(cstrk_add_object(track, &object, error, sizeof(error)));
}

static void activate(struct CSTRK_TRACK *track)
{
	if (!extended_track_activate(track, error, sizeof(error))) {
		fprintf(stderr, "Generator fixture activation: %s\n", error);
		assert(0);
	}
	assert(track_setup() == 0);
}

static void rotate_track(struct CSTRK_TRACK *track, legacy_u8 rotation)
{
	for (legacy_u32 index = 0; index < track->object_count; index++) {
		struct CSTRK_OBJECT *object = &track->objects[index];
		for (legacy_u8 turn = 0; turn < rotation; turn++) {
			legacy_u8 old_x = object->x, old_width = object->width, mask = 0;
			for (legacy_u8 y = 0; y < object->height; y++) {
				for (legacy_u8 x = 0; x < object->width; x++) {
					if (object->occupied_mask & (1U << (y * object->width + x))) {
						mask |= 1U << (x * object->height + object->height - 1U - y);
					}
				}
			}
			object->occupied_mask = mask;
			object->x = track->width - object->y - object->height;
			object->y = old_x;
			object->width = object->height;
			object->height = old_width;
		}
		object->rotation = rotation;
	}
}

static void rectangle(struct CSTRK_TRACK *track, legacy_u8 barrel, legacy_u8 combined)
{
	assert(cstrk_create(track, CSTRK_MAX_DIMENSION, CSTRK_MAX_DIMENSION, error, sizeof(error)));
	legacy_u8 top = barrel || combined ? TEST_BARREL_TOP : TEST_TOP;
	legacy_u8 bottom = barrel || combined ? TEST_BARREL_BOTTOM : TEST_BOTTOM;
	legacy_u8 start = barrel || combined ? TEST_BARREL_START : TEST_START_ROW;
	add_piece(track, TEST_TURN_NORTHWEST, TEST_LEFT, top);
	add_piece(track, TEST_TURN_NORTHEAST, TEST_RIGHT, top);
	add_piece(track, TEST_TURN_SOUTHWEST, TEST_LEFT, bottom);
	add_piece(track, TEST_TURN_SOUTHEAST, TEST_RIGHT, bottom);
	for (legacy_u8 x = TEST_LEFT + 1U; x < TEST_RIGHT; x++) {
		add_piece(track, TEST_ROAD_HORIZONTAL, x, top);
		add_piece(track, TEST_ROAD_HORIZONTAL, x, bottom);
	}
	for (legacy_u8 y = top + 1U; y < bottom; y++) {
		add_piece(track, TEST_ROAD_VERTICAL, TEST_RIGHT, y);
		if (!barrel || y < TEST_BARREL_ROW || y >= TEST_LANDING_ROW) {
			legacy_u16 id = y == start ? (barrel || combined ? TEST_START_SOUTH : TEST_START_NORTH)
							: combined && y == TEST_COMBINED_ROW ? CSTRK_SLALOM_FIRST_ID
																 : TEST_ROAD_VERTICAL;
			add_piece(track, id, TEST_LEFT, y);
			track->objects[track->object_count - 1U].tunnel_slalom =
				combined && y == TEST_COMBINED_ROW;
		}
	}
	if (barrel) {
		struct CSTRK_OBJECT ramp = {0};
		ramp.legacy_id = CSTRK_ROLL_RAMP_FIRST_ID;
		ramp.x = TEST_LEFT;
		ramp.y = TEST_BARREL_ROW;
		ramp.width = 1U;
		ramp.height = CSTRK_ROLL_RAMP_TILE_COUNT;
		ramp.occupied_mask = CSTRK_ROLL_RAMP_OCCUPIED_MASK;
		ramp.roll_ramp = 1U;
		assert(cstrk_add_object(track, &ramp, error, sizeof(error)));
	}
}

static void test_placed_geometry(void)
{
	for (legacy_u8 rotation = 0; rotation < TEST_ROTATIONS; rotation++) {
		for (legacy_u8 surface = CSTRK_ROAD_PAVED; surface <= CSTRK_ROAD_ICE; surface++) {
			struct CSTRK_TRACK track;
			cstrk_init(&track);
			rectangle(&track, 0, 0);
			memset(track.corner_heights, CSTRK_MAX_HEIGHT,
				   (size_t)(track.width + 1U) * (track.height + 1U));
			for (legacy_u32 index = 0; index < track.object_count; index++) {
				track.objects[index].surface = surface;
				track.objects[index].base_height = CSTRK_MAX_HEIGHT;
			}
			rotate_track(&track, rotation);
			activate(&track);
			struct GENGHOST_ROUTES routes;
			assert(genghost_routes_build(&routes, error, sizeof(error)) == 0);
			assert(routes.count == 1U);
			for (size_t index = 0; index < routes.routes[0].point_count; index++) {
				const struct GENGHOST_WAYPOINT *point = &routes.routes[0].points[index];
				assert(point->x >= 0 && point->x <= CSTRK_MAX_DIMENSION * CSTRK_TILE_WORLD_SIZE);
				assert(point->z >= 0 && point->z <= CSTRK_MAX_DIMENSION * CSTRK_TILE_WORLD_SIZE);
				assert(point->height_specified &&
					   point->y == CSTRK_MAX_HEIGHT * CSTRK_LEVEL_WORLD_HEIGHT);
				assert(point->surface == surface);
				if (!rotation) {
					assert(point->x > TEST_COORDINATE_PAGE && point->z > TEST_COORDINATE_PAGE);
				}
				if (index) {
					assert(point->distance - routes.routes[0].points[index - 1U].distance <
						   CSTRK_TILE_WORLD_SIZE * 2U);
				}
			}
			genghost_routes_free(&routes);
			extended_track_unload();
			cstrk_free(&track);
		}
	}
}

static void test_modifier_geometry(void)
{
	for (legacy_u8 barrel = 0; barrel <= 1U; barrel++) {
		for (legacy_u8 rotation = 0; rotation < TEST_ROTATIONS; rotation++) {
			struct CSTRK_TRACK track;
			cstrk_init(&track);
			rectangle(&track, barrel, !barrel);
			rotate_track(&track, rotation);
			activate(&track);
			struct GENGHOST_ROUTES routes;
			assert(genghost_routes_build(&routes, error, sizeof(error)) == 0);
			assert(routes.count == 1U);
			size_t modified = 0;
			for (size_t index = 0; index < routes.routes[0].point_count; index++) {
				const struct GENGHOST_WAYPOINT *point = &routes.routes[0].points[index];
				if (barrel && point->roll_ramp) {
					modified++;
					assert(point->speed == 100.0);
				} else if (!barrel && point->tunnel_slalom) {
					modified++;
					assert(point->physical_model == PHYSICAL_MODEL_SLALOM);
				}
			}
			assert(modified > 0);
			if (barrel) {
				assert(modified == ROLL_RAMP_ROUTE_POINT_COUNT);
			}
			genghost_routes_free(&routes);
			extended_track_unload();
			cstrk_free(&track);
		}
	}
}

static legacy_f64 graded_height(legacy_f64 x, legacy_f64 z, legacy_u8 rotation)
{
	legacy_f64 size = CSTRK_MAX_DIMENSION * CSTRK_TILE_WORLD_SIZE;
	for (legacy_u8 turn = 0; turn < rotation; turn++) {
		legacy_f64 old_x = x;
		x = size - z;
		z = old_x;
	}
	return (CSTRK_MAX_DIMENSION - z / CSTRK_TILE_WORLD_SIZE - TEST_GRADE_FIRST_ROW) *
		   CSTRK_LEVEL_WORLD_HEIGHT;
}

static void test_graded_catalog_targets(void)
{
	static const legacy_s16 models[] = {PHYSICAL_MODEL_ELEVATED_CORNER,
										PHYSICAL_MODEL_CHICANE_LEFT_RIGHT, PHYSICAL_MODEL_SLALOM};
	static const legacy_u8 turn_masks[] = {9U, 6U, 9U, 6U};
	const struct CARSTATE saved_car = state.playerstate;
	for (legacy_u8 model = 0; model < sizeof(models) / sizeof(models[0]); model++) {
		legacy_u16 id = 1U;
		while (id <= CSTRK_MAX_LEGACY_ID && trkObjectList[id].ss_physicalModel != models[model]) {
			id++;
		}
		assert(id <= CSTRK_MAX_LEGACY_ID);
		for (legacy_u8 rotation = 0; rotation < TEST_ROTATIONS; rotation++) {
			struct CSTRK_TRACK track;
			cstrk_init(&track);
			assert(cstrk_create(&track, CSTRK_MAX_DIMENSION, CSTRK_MAX_DIMENSION, error,
								sizeof(error)));
			struct CSTRK_OBJECT object = {0};
			object.legacy_id = id;
			object.x = TEST_GRADE_ORIGIN_COLUMN;
			object.y = TEST_GRADE_PIECE_ROW;
			legacy_u8 flags = (legacy_u8)trkObjectList[id].ss_multiTileFlag;
			object.width = 1U + !!(flags & MULTI_TILE_COLUMN_EDGE_FLAG);
			object.height = 1U + !!(flags & MULTI_TILE_ROW_EDGE_FLAG);
			object.occupied_mask = (1U << (object.width * object.height)) - 1U;
			if (models[model] == PHYSICAL_MODEL_ELEVATED_CORNER) {
				object.occupied_mask =
					turn_masks[(legacy_u16)trkObjectList[id].ss_rotY / ANGLE_QUARTER_TURN];
			} else if (models[model] == PHYSICAL_MODEL_CHICANE_LEFT_RIGHT) {
				object.occupied_mask = id <= TEST_GRADE_CHICANE_LAST_ID ? 6U : 9U;
			}
			assert(cstrk_add_object(&track, &object, error, sizeof(error)));
			rotate_track(&track, rotation);
			for (legacy_u16 row = 0; row <= track.height; row++) {
				legacy_u8 height = row <= TEST_GRADE_FIRST_ROW ? 0U
								   : row >= TEST_GRADE_FIRST_ROW + CSTRK_MAX_HEIGHT
									   ? CSTRK_MAX_HEIGHT
									   : row - TEST_GRADE_FIRST_ROW;
				for (legacy_u16 column = 0; column <= track.width; column++) {
					legacy_u16 x = column, y = row;
					for (legacy_u8 turn = 0; turn < rotation; turn++) {
						legacy_u16 old_x = x;
						x = track.width - y;
						y = old_x;
					}
					track.corner_heights[y * (track.width + 1U) + x] = height;
				}
			}
			struct CSTRK_OBJECT *placed = &track.objects[0];
			legacy_u32 corner = placed->y * (track.width + 1U) + placed->x;
			placed->base_height = track.corner_heights[corner];
			placed->slope_x = track.corner_heights[corner + 1U] - placed->base_height;
			placed->slope_y = track.corner_heights[corner + track.width + 1U] - placed->base_height;
			assert(extended_track_activate(&track, error, sizeof(error)));
			const struct CSTRK_OBJECT *active = &extended_track_model()->objects[0];
			legacy_u16 resolved = extended_track_object_id(active);
			track_route_columns[0] = active->x;
			track_route_rows[0] = active->y;
			track_route_element_ids[0] = resolved;
			track_route_traversal_flags[0] = 0;
			const struct TRKOBJINFO *info =
				extended_track_route_info(active->x, active->y, resolved);
			assert(info != NULL && info->route_point_count >= 2);
			struct GENGHOST_ROUTE route = {0};
			route.point_count = (legacy_u8)info->route_point_count;
			route.points = calloc(route.point_count, sizeof(*route.points));
			assert(route.points != NULL);
			for (size_t point = 0; point < route.point_count; point++) {
				struct VECTOR geometry[ROUTE_GEOMETRY_POINT_COUNT];
				get_track_route_point(0, geometry, point, NULL);
				struct GENGHOST_WAYPOINT *waypoint = &route.points[point];
				waypoint->x =
					extended_geometry_unwrap(geometry[0].x, active->x * CSTRK_TILE_WORLD_SIZE);
				waypoint->y = geometry[0].y;
				waypoint->z = extended_geometry_unwrap(geometry[0].z, (track.height - active->y) *
																		  CSTRK_TILE_WORLD_SIZE);
				waypoint->height_specified = 1;
				waypoint->physical_model = models[model];
				if (point) {
					legacy_f64 dx = waypoint->x - route.points[point - 1U].x;
					legacy_f64 dz = waypoint->z - route.points[point - 1U].z;
					route.length += sqrt(dx * dx + dz * dz);
				}
				waypoint->distance = route.length;
			}
			legacy_f64 direction_x = route.points[route.point_count - 1U].x - route.points[0].x;
			legacy_f64 direction_z = route.points[route.point_count - 1U].z - route.points[0].z;
			legacy_f64 direction_length =
				sqrt(direction_x * direction_x + direction_z * direction_z);
			legacy_f64 car_x =
				route.points[0].x - direction_x * TEST_GRADE_APPROACH / direction_length;
			legacy_f64 car_z =
				route.points[0].z - direction_z * TEST_GRADE_APPROACH / direction_length;
			state.playerstate.car_position.lx = (legacy_s32)(car_x * TEST_POSITION_SCALE);
			state.playerstate.car_position.ly =
				(legacy_s32)((graded_height(car_x, car_z, rotation) + TEST_GRADE_BODY_HEIGHT) *
							 TEST_POSITION_SCALE);
			state.playerstate.car_position.lz = (legacy_s32)(car_z * TEST_POSITION_SCALE);
			state.playerstate.car_sumSurfAllWheels = CARSTATE_WHEEL_COUNT;
			struct GENGHOST_DRIVER driver = {0};
			struct GENGHOST_POLICY policy;
			genghost_policy_default(&policy);
			policy.segment[0].stunt_mode = GENGHOST_STUNT_HUMAN;
			struct GENGHOST_WAYPOINT target;
			legacy_f64 protected_end;
			legacy_int found =
				genghost_stunt_target(&route, &driver, &policy.segment[0], TEST_GRADE_LOOKAHEAD, 1,
									  &target, &protected_end);
			if (!found) {
				fprintf(stderr, "Missing graded target: model %d ID %u rotation %u\n",
						models[model], id, rotation);
			}
			assert(found);
			assert(target.height_specified);
			legacy_f64 expected = graded_height(target.x, target.z, rotation);
			assert(target.y >= expected - 1.0 && target.y <= expected + 1.0);
			if (models[model] == PHYSICAL_MODEL_CHICANE_LEFT_RIGHT) {
				struct GENGHOST_MANEUVER actions[GENGHOST_MANEUVER_CAPACITY];
				size_t count = genghost_maneuvers(&route, &driver, &policy, 0, 1, 0, actions,
												  GENGHOST_MANEUVER_CAPACITY);
				legacy_uint chords = 0;
				for (size_t index = 0; index < count; index++) {
					if (actions[index].kind == GENGHOST_MANEUVER_CHICANE_GRASS) {
						const struct GENGHOST_WAYPOINT *ends[] = {&actions[index].approach,
																  &actions[index].exit};
						for (legacy_u8 end = 0; end < sizeof(ends) / sizeof(ends[0]); end++) {
							legacy_f64 height = graded_height(ends[end]->x, ends[end]->z, rotation);
							assert(ends[end]->height_specified);
							assert(ends[end]->y >= height - 1.0 && ends[end]->y <= height + 1.0);
						}
						chords++;
					}
				}
				assert(chords > 0);
			}
			free(route.points);
			extended_track_unload();
			cstrk_free(&track);
		}
	}
	state.playerstate = saved_car;
}

struct TEST_PATH_CELL {
	legacy_u8 x, y;
};

static legacy_u8 connection(struct TEST_PATH_CELL from, struct TEST_PATH_CELL to)
{
	return to.x > from.x ? 1U : to.y > from.y ? 2U : to.x < from.x ? 4U : 8U;
}

static void test_long_route(void)
{
	struct TEST_PATH_CELL path[CSTRK_MAX_OBJECTS];
	legacy_uint count = 0;
	path[count++] = (struct TEST_PATH_CELL){0, 1};
	path[count++] = (struct TEST_PATH_CELL){1, 1};
	for (legacy_u8 row = 0; row < TEST_LONG_ROWS; row++) {
		legacy_u8 y = 1U + row * 2U;
		if (!(row & 1U)) {
			for (legacy_u8 x = 2; x <= TEST_LONG_LAST_COLUMN; x++) {
				path[count++] = (struct TEST_PATH_CELL){x, y};
			}
		} else {
			for (legacy_u8 x = TEST_LONG_LAST_COLUMN; x >= 2U; x--) {
				path[count++] = (struct TEST_PATH_CELL){x, y};
			}
		}
		if (row + 1U < TEST_LONG_ROWS) {
			path[count++] = (struct TEST_PATH_CELL){row & 1U ? 2U : TEST_LONG_LAST_COLUMN, y + 1U};
		}
	}
	path[count++] = (struct TEST_PATH_CELL){1, TEST_LONG_LAST_ROW};
	path[count++] = (struct TEST_PATH_CELL){0, TEST_LONG_LAST_ROW};
	for (legacy_u8 y = TEST_LONG_LAST_ROW - 1U; y >= 2U; y--) {
		path[count++] = (struct TEST_PATH_CELL){0, y};
	}
	assert(count > TEST_LONG_MINIMUM_PIECES);
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	assert(cstrk_create(&track, CSTRK_MAX_DIMENSION, CSTRK_MAX_DIMENSION, error, sizeof(error)));
	for (legacy_uint index = 0; index < count; index++) {
		struct TEST_PATH_CELL current = path[index];
		legacy_u8 sides = connection(current, path[(index + count - 1U) % count]) |
						  connection(current, path[(index + 1U) % count]);
		legacy_u16 id = sides == 3U	   ? TEST_TURN_NORTHWEST
						: sides == 6U  ? TEST_TURN_NORTHEAST
						: sides == 9U  ? TEST_TURN_SOUTHWEST
						: sides == 12U ? TEST_TURN_SOUTHEAST
						: sides == 5U  ? TEST_ROAD_HORIZONTAL
									   : TEST_ROAD_VERTICAL;
		if (current.x == 0 && current.y == TEST_LONG_START_ROW) {
			id = TEST_START_NORTH;
		}
		add_piece(&track, id, current.x, current.y);
	}
	activate(&track);
	struct GENGHOST_ROUTES routes;
	assert(genghost_routes_build(&routes, error, sizeof(error)) == 0);
	assert(routes.count == 1U && routes.routes[0].piece_count == count + 1U);
	genghost_routes_free(&routes);
	extended_track_unload();
	cstrk_free(&track);
}

int main(void)
{
	mmgr_init_conventional_arena();
	init_row_tables();
	init_trackdata();
	test_placed_geometry();
	test_modifier_geometry();
	test_graded_catalog_targets();
	test_long_route();
	puts("Extended generator geometry: wide coordinates, road materials, modifiers and long "
		 "routes.");
	return 0;
}
