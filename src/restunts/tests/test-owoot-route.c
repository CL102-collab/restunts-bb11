#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../c/externs.h"
#include "../c/owoot/owoot.h"
#include "../c/owoot/owoot_route.h"
#include "../c/track/track_objects.h"
#include "../c/track/trackdata_layout.h"

struct TRACKOBJECT trkObjectList[215];
legacy_s16 track_pieces_counter;
legacy_s8 *track_route_element_ids, *track_route_columns, *track_route_rows;
legacy_s8 *track_route_traversal_flags;
legacy_s16 *track_primary_route_links, *track_alternate_route_links;
legacy_u8 *track_terrain_map, *track_element_map;
legacy_s16 trackrows[30];
legacy_s16 terrainrows[30], hillHeightConsts[2] = {0, 450};

static legacy_s8 elements[8], columns[8], rows[8], flags[8];
static legacy_s16 next_pieces[8], alternate_pieces[8];
static legacy_u8 terrain[900], tiles[900];
static struct TRKOBJINFO infos[8];
static struct VECTOR sections[8][32];
static struct CARSTATE car;
static legacy_s16 axle_offset, lateral_axle_offset, diamond_tires, front_height_offset, tire_pitch;

legacy_s16 track_object_base_x(const struct TRACKOBJECT *object, legacy_u8 column)
{
	return column * 1024 + 512 + ((object->ss_multiTileFlag & 2) ? 512 : 0);
}

legacy_s16 track_object_base_z(const struct TRACKOBJECT *object, legacy_u8 row)
{
	return (29 - row) * 1024 + 512 - ((object->ss_multiTileFlag & 1) ? 512 : 0);
}

legacy_s16 get_track_route_point(legacy_s16 piece, struct VECTOR *output, legacy_s16 index,
								 legacy_s8 *speed)
{
	(void)speed;
	output[0] = output[1] = output[2] = sections[piece][index];
	output[1].z -= 120;
	output[2].z += 120;
	if (trkObjectList[elements[piece]].ss_physicalModel == PHYSICAL_MODEL_SLALOM) {
		if (index == 0) {
			output[1].z = 489;
		} else if (index == infos[piece].route_point_count - 1) {
			output[2].z = 535;
		}
	}
	return index == infos[piece].route_point_count - 1;
}

legacy_u16 owoot_wheel_footprint(const struct CARSTATE *carstate, legacy_u16 wheel,
								 struct VECTOR output[OWOOT_WHEEL_VERTEX_COUNT])
{
	legacy_s16 offset = wheel < 2 ? axle_offset : -axle_offset;
	legacy_s16 lateral = (wheel & 1) ? lateral_axle_offset : -lateral_axle_offset;
	if (diamond_tires) {
		static const legacy_s16 dx[4] = {-10, 0, 10, 0};
		static const legacy_s16 dz[4] = {0, -10, 0, 10};
		for (legacy_u16 point = 0; point < 4; point++) {
			output[point].x = carstate->car_position.lx / 64 + dx[point];
			output[point].y = carstate->car_position.ly / 64;
			output[point].z = carstate->car_position.lz / 64 + dz[point];
		}
		return 4;
	}
	static const legacy_s16 side_y[4] = {-4, 4, 4, -4};
	static const legacy_s16 side_z[4] = {-4, -4, 4, 4};
	for (legacy_u16 point = 0; point < OWOOT_WHEEL_VERTEX_COUNT; point++) {
		legacy_u16 corner = (point % 16) / 4;
		output[point].x = carstate->car_position.lx / 64 + offset + (point < 16 ? 4 : -4);
		output[point].y = carstate->car_position.ly / 64 + (wheel < 2 ? front_height_offset : 0) +
						  side_y[corner] + (point < 16 ? tire_pitch : -tire_pitch);
		output[point].z = carstate->car_position.lz / 64 + lateral + side_z[corner];
	}
	return OWOOT_WHEEL_VERTEX_COUNT;
}

static void reset(void)
{
	memset(&car, 0, sizeof(car));
	axle_offset = lateral_axle_offset = diamond_tires = front_height_offset = tire_pitch = 0;
	memset(trkObjectList, 0, sizeof(trkObjectList));
	memset(infos, 0, sizeof(infos));
	memset(terrain, 0, sizeof(terrain));
	memset(tiles, 0, sizeof(tiles));
	memset(flags, 0, sizeof(flags));
	track_pieces_counter = 1;
	track_route_element_ids = elements;
	track_route_columns = columns;
	track_route_rows = rows;
	track_route_traversal_flags = flags;
	track_primary_route_links = next_pieces;
	track_alternate_route_links = alternate_pieces;
	track_terrain_map = terrain;
	track_element_map = tiles;
	for (legacy_u16 row = 0; row < 30; row++) {
		terrainrows[row] = trackrows[row] = row * 30;
	}
	for (legacy_u16 piece = 0; piece < 8; piece++) {
		elements[piece] = piece + 1;
		columns[piece] = piece;
		rows[piece] = 29;
		tiles[29 * 30 + piece] = piece + 1;
		next_pieces[piece] = alternate_pieces[piece] = -1;
		infos[piece].route_point_count = 3;
		infos[piece].si_entryPoint = 4;
		infos[piece].si_exitPoint = 3;
		trkObjectList[piece + 1].ss_trkObjInfoPtr = &infos[piece];
		trkObjectList[piece + 1].ss_physicalModel = PHYSICAL_MODEL_BANKED_ROAD;
		for (legacy_u16 index = 0; index < 3; index++) {
			sections[piece][index].x = piece * 1024 + 212 + index * 300;
			sections[piece][index].y = -1;
			sections[piece][index].z = 512;
		}
	}
}

static void move_to(legacy_s16 x, legacy_s16 y, legacy_s16 z)
{
	car.car_previous_position = car.car_position;
	car.car_position.lx = (legacy_s32)x * 64;
	car.car_position.ly = (legacy_s32)y * 64;
	car.car_position.lz = (legacy_s32)z * 64;
	for (legacy_u16 point = 0; point < 4; point++) {
		car.car_body_corner_positions[point].x =
			x + (point & 1 ? axle_offset + 4 : -axle_offset - 4);
		car.car_body_corner_positions[point].y = y;
		car.car_body_corner_positions[point].z =
			z + (point & 2 ? lateral_axle_offset + 4 : -lateral_axle_offset - 4);
	}
}

static void start_at(legacy_s16 x, legacy_s16 y, legacy_s16 z)
{
	move_to(x, y, z);
	car.car_previous_position = car.car_position;
}

static void test_swept_gates_and_checkpoint(void)
{
	reset();
	start_at(-20, 10, 512);
	move_to(20, 10, 512);
	assert(owoot_route_is_valid(&car, 0));
	struct CARSTATE checkpoint = car;
	move_to(900, 10, 512);
	assert(owoot_route_is_valid(&car, 0));
	assert(car.car_reserved_route_word2 == 3);
	move_to(1040, 10, 512);
	assert(owoot_route_is_valid(&car, 0));
	assert(car.car_reserved_route_word1 == 0);
	car = checkpoint;
	move_to(900, 10, 512);
	assert(owoot_route_is_valid(&car, 0));
	assert(car.car_reserved_route_word2 == 3);
}

static void test_backtracking(void)
{
	reset();
	start_at(-20, 10, 512);
	move_to(300, 10, 512);
	assert(owoot_route_is_valid(&car, 0));
	assert(car.car_reserved_route_word2 == 1);
	move_to(100, 10, 512);
	assert(owoot_route_is_valid(&car, 0));
	assert(car.car_reserved_route_word2 == 0);
	move_to(-20, 10, 512);
	assert(owoot_route_is_valid(&car, 0));
	assert(car.car_reserved_route_word1 == 0);
}

static void test_tire_sweep_and_reverse_stunt(void)
{
	reset();
	axle_offset = 60;
	start_at(-20, 10, 650);
	move_to(200, 10, 650);
	assert(owoot_route_is_valid(&car, 0));
	move_to(240, 10, 650);
	assert(owoot_route_is_valid(&car, 0));
	assert(car.car_reserved_route_word2 == 0);
	move_to(280, 10, 620);
	assert(owoot_route_is_valid(&car, 0));
	assert(car.car_reserved_route_word2 == 1);

	reset();
	start_at(1040, 10, 512);
	move_to(900, 10, 512);
	assert(owoot_route_is_valid(&car, 0));
	move_to(100, 10, 512);
	assert(owoot_route_is_valid(&car, 0));
	assert(car.car_reserved_route_word2 == 3);
	move_to(-20, 10, 512);
	assert(owoot_route_is_valid(&car, 0));
	assert(car.car_reserved_route_word1 == 0);
}

static void test_jump_over_unrelated_stunt(void)
{
	reset();
	start_at(480, 500, 512);
	move_to(540, 500, 512);
	assert(owoot_route_is_valid(&car, 1));
	assert(car.car_reserved_route_word1 == 0);
	assert(!owoot_route_is_valid(&car, 0));
}

static void test_stunt_shortcut_and_tunnel_roof(void)
{
	reset();
	trkObjectList[1].ss_physicalModel = PHYSICAL_MODEL_LOOP;
	sections[0][0].y = sections[0][2].y = 0;
	sections[0][1].y = 500;
	start_at(-20, 10, 512);
	move_to(20, 10, 512);
	assert(owoot_route_is_valid(&car, 0));
	move_to(900, 10, 512);
	assert(owoot_route_is_valid(&car, 0));
	move_to(1040, 10, 512);
	assert(!owoot_route_is_valid(&car, 0));

	for (legacy_s16 height = 10; height < 220; height += 190) {
		reset();
		trkObjectList[1].ss_physicalModel = PHYSICAL_MODEL_TUNNEL;
		start_at(-20, height, 512);
		move_to(20, height, 512);
		assert(owoot_route_is_valid(&car, 0));
		move_to(900, height, 512);
		assert(owoot_route_is_valid(&car, 0));
		move_to(1040, height, 512);
		assert(owoot_route_is_valid(&car, 0) == (height == 10));
	}
}

static legacy_s16 traverse_portal(legacy_s16 model, legacy_s16 height, legacy_s16 lateral)
{
	reset();
	trkObjectList[1].ss_physicalModel = model;
	start_at(-20, height, 512 + lateral);
	move_to(20, height, 512 + lateral);
	assert(owoot_route_is_valid(&car, 0));
	move_to(900, height, 512 + lateral);
	assert(owoot_route_is_valid(&car, 0));
	move_to(1040, height, 512 + lateral);
	return owoot_route_is_valid(&car, 0);
}

static void test_pipe_aperture_and_open_entrance(void)
{
	/* The mock tire extends four units below its center. */
	assert(traverse_portal(PHYSICAL_MODEL_TUNNEL, 147, 0));
	assert(!traverse_portal(PHYSICAL_MODEL_TUNNEL, 148, 0));
	assert(traverse_portal(PHYSICAL_MODEL_TUNNEL, 10, 123));
	assert(!traverse_portal(PHYSICAL_MODEL_TUNNEL, 10, 124));
	assert(traverse_portal(PHYSICAL_MODEL_PIPE, 10, 0));
	assert(traverse_portal(PHYSICAL_MODEL_PIPE, 200, 48));
	assert(!traverse_portal(PHYSICAL_MODEL_PIPE, 240, 0));
	assert(!traverse_portal(PHYSICAL_MODEL_PIPE, 220, 88));
	assert(!traverse_portal(PHYSICAL_MODEL_HALF_PIPE, 240, 0));
	assert(traverse_portal(PHYSICAL_MODEL_PIPE_ENTRANCE, 500, 0));
}

static void test_crossing_tire_must_fit_portal(void)
{
	reset();
	trkObjectList[1].ss_physicalModel = PHYSICAL_MODEL_PIPE;
	axle_offset = 100;
	front_height_offset = 200;
	start_at(-120, 100, 512);
	move_to(-60, 100, 512);
	assert(owoot_route_is_valid(&car, 0));
	/* Only the high front tires crossed the first portal. The low rear
	 * tires are inside its aperture but still behind its plane. */
	assert(car.car_reserved_route_word2 == 0);
	move_to(120, 100, 512);
	assert(owoot_route_is_valid(&car, 0));
	assert(car.car_reserved_route_word2 == 1);
}

static void test_only_portal_plane_slice_counts(void)
{
	reset();
	trkObjectList[1].ss_physicalModel = PHYSICAL_MODEL_PIPE;
	axle_offset = 100;
	tire_pitch = 8;
	start_at(-107, 232, 512);
	move_to(-102, 232, 512);
	assert(owoot_route_is_valid(&car, 0));
	/* The leading rim is entirely above the roof. Its trailing rim lies
	 * below the roof, but has not reached the portal plane yet. */
	assert(car.car_reserved_route_word2 == 0);
	move_to(-92, 232, 512);
	assert(owoot_route_is_valid(&car, 0));
	assert(car.car_reserved_route_word2 == 1);
}

static void test_finite_mouth_entry_and_exit(void)
{
	reset();
	trkObjectList[1].ss_physicalModel = PHYSICAL_MODEL_TUNNEL;
	start_at(-20, 300, 512);
	move_to(20, 300, 512);
	assert(owoot_route_is_valid(&car, 0));
	move_to(100, 10, 512);
	assert(owoot_route_is_valid(&car, 0));
	move_to(900, 10, 512);
	assert(owoot_route_is_valid(&car, 0));
	move_to(1040, 10, 512);
	assert(!owoot_route_is_valid(&car, 0));

	/* Only a front tire passes the mouth: activate while the center and
	 * rear tires remain outside, and retain that valid traversal. */
	reset();
	trkObjectList[1].ss_physicalModel = PHYSICAL_MODEL_PIPE;
	axle_offset = 60;
	front_height_offset = -200;
	start_at(-100, 300, 512);
	move_to(-50, 300, 512);
	assert(owoot_route_is_valid(&car, 0));
	assert(car.car_reserved_route_word1 == 1 && car.car_reserved_route_word2 == 1);
	move_to(700, 300, 512);
	assert(owoot_route_is_valid(&car, 0));
	move_to(1100, 300, 512);
	assert(owoot_route_is_valid(&car, 0));

	/* The low rear tire crosses the exit after the center left the tile. */
	reset();
	trkObjectList[1].ss_physicalModel = PHYSICAL_MODEL_PIPE;
	axle_offset = 60;
	front_height_offset = 200;
	start_at(-100, 100, 512);
	move_to(100, 100, 512);
	assert(owoot_route_is_valid(&car, 0));
	move_to(700, 100, 512);
	assert(owoot_route_is_valid(&car, 0));
	move_to(1040, 100, 512);
	assert(owoot_route_is_valid(&car, 0));
	assert(car.car_reserved_route_word1 == 1 && car.car_reserved_route_word2 == 2);
	move_to(1100, 100, 512);
	assert(owoot_route_is_valid(&car, 0));
	assert(car.car_reserved_route_word1 == 0);
}

static void configure_adjacent_tubes(legacy_s16 receiving_model)
{
	reset();
	track_pieces_counter = 2;
	trkObjectList[1].ss_physicalModel = PHYSICAL_MODEL_PIPE;
	trkObjectList[2].ss_physicalModel = receiving_model;
	axle_offset = 60;
}

static void test_adjacent_finite_portals(void)
{
	for (legacy_s16 direction = -1; direction <= 1; direction += 2) {
		for (legacy_s16 front_offset = -200; front_offset <= 200; front_offset += 200) {
			configure_adjacent_tubes(PHYSICAL_MODEL_PIPE);
			front_height_offset = front_offset;
			legacy_s16 height = front_offset < 0 ? 300 : 100;
			legacy_s16 start = direction > 0 ? -100 : 2150;
			start_at(start, height, 512);
			for (legacy_s16 step = 1; step < 1126; step++) {
				move_to(start + direction * step * 2, height, 512);
				assert(owoot_route_is_valid(&car, 0));
			}
			assert(car.car_reserved_route_word1 == 0);
		}
	}

	for (legacy_u16 model_index = 0; model_index < 2; model_index++) {
		configure_adjacent_tubes(PHYSICAL_MODEL_PIPE);
		trkObjectList[1].ss_physicalModel =
			model_index == 0 ? PHYSICAL_MODEL_PIPE_ENTRANCE : PHYSICAL_MODEL_BANKED_ROAD;
		front_height_offset = -200;
		start_at(-100, 300, 512);
		for (legacy_s16 position = -98; position <= 2150; position += 2) {
			move_to(position, 300, 512);
			assert(owoot_route_is_valid(&car, 0));
		}
		assert(car.car_reserved_route_word1 == 0);
	}

	configure_adjacent_tubes(PHYSICAL_MODEL_PIPE);
	start_at(-100, 100, 512);
	move_to(900, 100, 512);
	assert(owoot_route_is_valid(&car, 0));
	move_to(1000, 100, 512);
	assert(owoot_route_is_valid(&car, 0));
	assert(car.car_reserved_route_word1 == 2 && car.car_reserved_route_word2 == 1);
	move_to(1000, 100, 512);
	assert(owoot_route_is_valid(&car, 0));
	move_to(900, 100, 512);
	assert(owoot_route_is_valid(&car, 0));
	assert(car.car_reserved_route_word1 == 1 && car.car_reserved_wheel_state[0] == -1);
	move_to(-100, 100, 512);
	assert(owoot_route_is_valid(&car, 0));

	configure_adjacent_tubes(PHYSICAL_MODEL_PIPE);
	start_at(-100, 100, 512);
	move_to(900, 100, 512);
	assert(owoot_route_is_valid(&car, 0));
	move_to(1150, 100, 512);
	assert(owoot_route_is_valid(&car, 0));
	assert(car.car_reserved_route_word1 == 2 && car.car_reserved_route_word2 == 1);
	move_to(2150, 100, 512);
	assert(owoot_route_is_valid(&car, 0));

	/* A valid pipe exit cannot approve a different destination aperture. */
	for (legacy_s16 raised = 0; raised < 2; raised++) {
		configure_adjacent_tubes(PHYSICAL_MODEL_TUNNEL);
		terrain[29 * 30 + 1] = raised ? TERRAIN_RAISED_TILE : 0;
		legacy_s16 height = raised ? 100 : 200;
		start_at(-100, height, 512);
		move_to(900, height, 512);
		assert(owoot_route_is_valid(&car, 0));
		move_to(1150, height, 512);
		assert(owoot_route_is_valid(&car, 0));
		assert(car.car_reserved_route_word1 == 2 && car.car_reserved_route_word2 == 0);
		move_to(2150, height, 512);
		assert(!owoot_route_is_valid(&car, 0));
	}
}

static void configure_gap(legacy_s16 gap_tiles)
{
	reset();
	track_pieces_counter = 2;
	trkObjectList[1].ss_physicalModel = PHYSICAL_MODEL_RAMP;
	trkObjectList[2].ss_physicalModel = PHYSICAL_MODEL_RAMP;
	infos[0].si_exitType = 1;
	infos[1].si_entryType = 1;
	next_pieces[0] = 1;
	columns[1] = 1 + gap_tiles;
	for (legacy_u16 index = 0; index < 3; index++) {
		sections[1][index].x += gap_tiles * 1024;
	}
	car.car_sumSurfAllWheels = 4;
	car.car_surfaceWhl[0] = CAR_SURFACE_PAVED;
	start_at(700, 450, 512);
	move_to(750, 450, 512);
}

static void test_jump_provenance_and_gap_limit(void)
{
	configure_gap(1);
	assert(!owoot_jump_is_valid(&car));
	assert(car.car_reserved_wheel_state[1] == 1);
	assert(car.car_reserved_wheel_state[2] == 2);
	struct CARSTATE checkpoint = car;
	car.car_sumSurfAllWheels = 0;
	move_to(1400, 500, 512);
	assert(owoot_jump_is_valid(&car));
	car.car_sumSurfAllWheels = 1;
	car.car_surfaceWhl[0] = CAR_SURFACE_GRASS;
	assert(!owoot_jump_is_valid(&car));
	assert(car.car_reserved_wheel_state[1] == 0);
	car = checkpoint;
	car.car_sumSurfAllWheels = 0;
	move_to(1400, 500, 512);
	assert(owoot_jump_is_valid(&car));
	move_to(3100, 450, 512);
	assert(!owoot_jump_is_valid(&car));

	configure_gap(2);
	assert(!owoot_jump_is_valid(&car));
	assert(car.car_reserved_wheel_state[1] == 0);
	car.car_sumSurfAllWheels = 0;
	move_to(1400, 500, 512);
	assert(!owoot_jump_is_valid(&car));

	configure_gap(1);
	car.car_sumSurfAllWheels = 0;
	move_to(1400, 500, 512);
	assert(!owoot_jump_is_valid(&car));
}

static void test_corkscrew_phase_progress(void)
{
	static const legacy_s16 side[13] = {0, -58, -100, -115, -100, -58, 0, 58, 100, 115, 100, 58, 0};
	static const legacy_s16 height[13] = {0, 17, 61, 119, 177, 219, 235, 219, 177, 119, 61, 17, 0};
	for (legacy_s16 offset = -100; offset <= 100; offset += 200) {
		reset();
		trkObjectList[1].ss_physicalModel = PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT;
		infos[0].route_point_count = 13;
		for (legacy_s16 point = 0; point < 13; point++) {
			sections[0][point].x = 212 + point * 50;
			sections[0][point].y = height[point];
			sections[0][point].z = 512 + side[point];
		}
		start_at(-20, 4, 512);
		move_to(220, 4, 512);
		assert(owoot_route_is_valid(&car, 0));
		for (legacy_s16 point = 1; point < 12; point++) {
			move_to(300 + point * 30 + offset, height[point], 512 + side[point]);
			assert(owoot_route_is_valid(&car, 0));
		}
		move_to(900, 4, 512);
		assert(owoot_route_is_valid(&car, 0));
		assert(car.car_reserved_route_word2 == 5);
		move_to(1040, 4, 512);
		assert(owoot_route_is_valid(&car, 0));
		assert(car.car_reserved_route_word1 == 0);
	}
	reset();
	trkObjectList[1].ss_physicalModel = PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT;
	infos[0].route_point_count = 13;
	for (legacy_s16 point = 0; point < 13; point++) {
		sections[0][point].x = 212 + point * 50;
		sections[0][point].y = height[point];
		sections[0][point].z = 512 + side[point];
	}
	start_at(-20, 4, 512);
	move_to(220, 4, 512);
	assert(owoot_route_is_valid(&car, 0));
	move_to(900, 4, 512);
	assert(owoot_route_is_valid(&car, 0));
	move_to(1040, 4, 512);
	assert(!owoot_route_is_valid(&car, 0));
}

static void test_front_contact_arms_gap_before_center(void)
{
	configure_gap(1);
	track_pieces_counter = 3;
	columns[2] = 1;
	trkObjectList[3].ss_physicalModel = PHYSICAL_MODEL_PIPE;
	tiles[29 * 30 + 1] = 3;
	tiles[29 * 30 + 2] = 2;
	axle_offset = 60;
	move_to(995, 450, 512);
	legacy_s16 jump = owoot_jump_is_valid(&car);
	assert(jump);
	assert(owoot_route_is_valid(&car, jump));
	assert(car.car_reserved_route_word1 == 0);
}

static void test_rear_contact_and_reverse_jump(void)
{
	configure_gap(1);
	assert(!owoot_jump_is_valid(&car));
	move_to(1050, 450, 512);
	car.car_wheel_contact_positions[0].x = 1000;
	car.car_wheel_contact_positions[0].y = 450;
	car.car_wheel_contact_positions[0].z = 512;
	assert(owoot_jump_is_valid(&car));
	assert(car.car_reserved_wheel_state[1] == 1);
	car.car_sumSurfAllWheels = 0;
	move_to(1400, 500, 512);
	assert(owoot_jump_is_valid(&car));

	configure_gap(1);
	start_at(2500, 450, 512);
	move_to(2450, 450, 512);
	assert(!owoot_jump_is_valid(&car));
	assert(car.car_reserved_wheel_state[1] == 2);
	assert(car.car_reserved_wheel_state[2] == 1);
	car.car_sumSurfAllWheels = 0;
	move_to(1800, 500, 512);
	assert(owoot_jump_is_valid(&car));
}

static void test_receiving_contact_preserves_gap(void)
{
	configure_gap(1);
	assert(!owoot_jump_is_valid(&car));
	car.car_sumSurfAllWheels = 0;
	move_to(1400, 500, 512);
	assert(owoot_jump_is_valid(&car));
	axle_offset = 60;
	move_to(2043, 450, 512);
	car.car_sumSurfAllWheels = 1;
	car.car_surfaceWhl[0] = CAR_SURFACE_PAVED;
	car.car_wheel_contact_positions[0].x = 2100;
	car.car_wheel_contact_positions[0].y = 450;
	car.car_wheel_contact_positions[0].z = 512;
	assert(owoot_jump_is_valid(&car));
	move_to(2150, 450, 512);
	assert(!owoot_jump_is_valid(&car));

	configure_gap(1);
	assert(!owoot_jump_is_valid(&car));
	car.car_sumSurfAllWheels = 0;
	move_to(1400, 500, 512);
	assert(owoot_jump_is_valid(&car));
	trkObjectList[2].ss_physicalModel = PHYSICAL_MODEL_OVERPASS;
	move_to(2043, 10, 512);
	car.car_sumSurfAllWheels = 1;
	car.car_wheel_contact_positions[0].x = 2100;
	car.car_wheel_contact_positions[0].y = 0;
	car.car_wheel_contact_positions[0].z = 512;
	assert(!owoot_jump_is_valid(&car));
}

static void test_approach_and_overpass(void)
{
	configure_gap(1);
	track_pieces_counter = 3;
	columns[1] = 1;
	columns[2] = 3;
	trkObjectList[2].ss_physicalModel = PHYSICAL_MODEL_ELEVATED_ROAD;
	trkObjectList[3].ss_physicalModel = PHYSICAL_MODEL_RAMP;
	infos[1].si_entryType = infos[1].si_exitType = 1;
	infos[2].si_entryType = 1;
	next_pieces[1] = 2;
	for (legacy_u16 index = 0; index < 3; index++) {
		sections[1][index].x -= 1024;
		sections[2][index].x += 1024;
	}
	assert(!owoot_jump_is_valid(&car));
	assert(car.car_reserved_wheel_state[1] == 2);
	assert(car.car_reserved_wheel_state[2] == 3);
	car.car_sumSurfAllWheels = 0;
	move_to(1400, 500, 512);
	assert(!owoot_jump_is_valid(&car));
	assert(car.car_reserved_wheel_state[1] == 2);
	move_to(2400, 500, 512);
	assert(owoot_jump_is_valid(&car));

	configure_gap(1);
	trkObjectList[1].ss_physicalModel = PHYSICAL_MODEL_OVERPASS;
	start_at(700, 10, 512);
	move_to(750, 10, 512);
	assert(!owoot_jump_is_valid(&car));
	assert(car.car_reserved_wheel_state[1] == 0);
}

static void test_slalom_requires_barrier_clearance(void)
{
	reset();
	trkObjectList[1].ss_physicalModel = PHYSICAL_MODEL_SLALOM;
	trkObjectList[1].ss_rotY = ANGLE_QUARTER_TURN;
	lateral_axle_offset = 50;
	start_at(-20, 500, 512);
	move_to(400, 500, 512);
	assert(!owoot_route_is_valid(&car, 0));

	reset();
	trkObjectList[1].ss_physicalModel = PHYSICAL_MODEL_SLALOM;
	trkObjectList[1].ss_rotY = ANGLE_QUARTER_TURN;
	lateral_axle_offset = 50;
	start_at(-20, 500, 570);
	move_to(400, 500, 570);
	assert(owoot_route_is_valid(&car, 0));
	move_to(650, 500, 454);
	assert(owoot_route_is_valid(&car, 0));
	move_to(900, 500, 454);
	assert(owoot_route_is_valid(&car, 0));
	move_to(1040, 500, 454);
	assert(owoot_route_is_valid(&car, 0));
}

static void test_slalom_cannot_bypass_both_barriers(void)
{
	reset();
	trkObjectList[1].ss_physicalModel = PHYSICAL_MODEL_SLALOM;
	trkObjectList[1].ss_rotY = ANGLE_QUARTER_TURN;
	lateral_axle_offset = 50;
	start_at(-20, 10, 680);
	move_to(900, 10, 680);
	assert(owoot_route_is_valid(&car, 0));
	move_to(1040, 10, 680);
	assert(!owoot_route_is_valid(&car, 0));
}

static void test_replaced_continuation_does_not_enter_stunt(void)
{
	reset();
	trkObjectList[1].ss_physicalModel = PHYSICAL_MODEL_BANKED_CORNER;
	trkObjectList[1].ss_multiTileFlag = 2;
	trkObjectList[2].ss_physicalModel = PHYSICAL_MODEL_ELEVATED_ROAD;
	sections[0][2].x = 1836;
	start_at(2100, 900, 512);
	move_to(1950, 900, 512);
	assert(owoot_route_is_valid(&car, 0));
	assert(car.car_reserved_route_word1 == 0);
	tiles[29 * 30 + 1] = TRACK_TILE_CONTINUATION_EAST;
	start_at(2100, 900, 512);
	move_to(1950, 900, 512);
	assert(owoot_route_is_valid(&car, 0));
	assert(car.car_reserved_route_word1 == 1);
}

static void test_gap_to_multitile_elevated_connector(void)
{
	configure_gap(1);
	/* An eastbound ramp lands at the northwest elevated end of a reversed
	 * 2x2 corkscrew. Its geometric center is not aligned with the ramp. */
	trkObjectList[2].ss_physicalModel = PHYSICAL_MODEL_CORKSCREW_UP_DOWN_A;
	trkObjectList[2].ss_multiTileFlag = 3;
	columns[1] = 2;
	rows[1] = 29;
	infos[1].si_exitPoint = 4;
	infos[1].si_exitType = 1;
	flags[1] = 16;
	assert(!owoot_jump_is_valid(&car));
	assert(car.car_reserved_wheel_state[1] == 1);
	assert(car.car_reserved_wheel_state[2] == 2);
	car.car_sumSurfAllWheels = 0;
	move_to(1400, 500, 512);
	assert(owoot_jump_is_valid(&car));
}

static void test_gap_requires_actual_tire_overlap(void)
{
	configure_gap(1);
	assert(!owoot_jump_is_valid(&car));
	car.car_sumSurfAllWheels = 0;
	diamond_tires = 1;
	/* The tire's bounding box overlaps the northwest tile corner, but
	 * its diamond hull initially misses it. Exact tangency qualifies. */
	move_to(1017, 500, 1031);
	assert(!owoot_jump_is_valid(&car));
	move_to(1019, 500, 1029);
	assert(owoot_jump_is_valid(&car));
	/* The whole gap tile is exempt, including ground outside the width
	 * of the launch and landing roads. The neighboring tile is not. */
	move_to(1400, 500, 900);
	assert(owoot_jump_is_valid(&car));
	move_to(1500, 500, 1040);
	assert(!owoot_jump_is_valid(&car));
	move_to(1600, 500, 1034);
	assert(owoot_jump_is_valid(&car));
}

int main(void)
{
	test_swept_gates_and_checkpoint();
	test_backtracking();
	test_tire_sweep_and_reverse_stunt();
	test_jump_over_unrelated_stunt();
	test_stunt_shortcut_and_tunnel_roof();
	test_pipe_aperture_and_open_entrance();
	test_crossing_tire_must_fit_portal();
	test_only_portal_plane_slice_counts();
	test_finite_mouth_entry_and_exit();
	test_adjacent_finite_portals();
	test_jump_provenance_and_gap_limit();
	test_corkscrew_phase_progress();
	test_rear_contact_and_reverse_jump();
	test_front_contact_arms_gap_before_center();
	test_receiving_contact_preserves_gap();
	test_approach_and_overpass();
	test_slalom_requires_barrier_clearance();
	test_gap_requires_actual_tire_overlap();
	test_slalom_cannot_bypass_both_barriers();
	test_replaced_continuation_does_not_enter_stunt();
	test_gap_to_multitile_elevated_connector();
	puts("OWOOT route and jump tests passed");
	return 0;
}
