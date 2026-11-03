#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../c/externs.h"
#include "../c/state_internal.h"
#include "../c/track/track_objects.h"

struct GAMESTATE state;
struct TRACKOBJECT trkObjectList[215];
legacy_s16 terrainrows[30];
legacy_s16 track_row_positions[30], track_row_centers[30];
legacy_s16 track_column_positions[30], track_column_centers[30];
legacy_s16 hillHeightConsts[2] = {0, 450};
legacy_s8 far *track_route_element_ids;
legacy_s8 far *track_route_traversal_flags;
legacy_s8 far *track_route_columns;
legacy_s8 far *track_route_rows;
legacy_u8 far *track_terrain_map;
legacy_u8 oppnentSped[16];
legacy_s8 *steerWhlRespTable_ptr;
legacy_u16 framespersec;
legacy_u16 elapsed_time2, legacy_closed_hihat_offset;

static struct VECTOR forward_vectors[256], reverse_vectors[256];
static struct TRKOBJINFO route_info[2];
static legacy_s8 elements[1], flags[1], columns[1], rows[1], steering[256];
static legacy_u8 terrain[900];

struct VECTOR *headless_track_vector_from_legacy_offset(legacy_u16 offset)
{
	assert(offset == 0x1234);
	return reverse_vectors;
}

static void reset_route_points(void)
{
	memset(trkObjectList, 0, sizeof(trkObjectList));
	memset(route_info, 0, sizeof(route_info));
	memset(terrain, 0, sizeof(terrain));
	track_route_element_ids = elements;
	track_route_traversal_flags = flags;
	track_route_columns = columns;
	track_route_rows = rows;
	track_terrain_map = terrain;
	elements[0] = 1;
	flags[0] = 0;
	columns[0] = rows[0] = 5;
	trkObjectList[1].ss_trkObjInfoPtr = route_info;
	for (legacy_u32 i = 0; i < 2; i++) {
		route_info[i].route_vectors = forward_vectors;
		route_info[i].route_point_count = 3;
		route_info[i].opponent_speed_code = 3;
	}
	for (legacy_u32 i = 0; i < 30; i++) {
		terrainrows[i] = i * 30;
		track_row_positions[i] = track_column_positions[i] = i * 1024;
		track_row_centers[i] = track_column_centers[i] = i * 1024 + 512;
	}
	elapsed_time2 = 30720;
	legacy_closed_hihat_offset = 0;
	for (legacy_u32 i = 0; i < 256; i++) {
		forward_vectors[i].x = LEGACY_S16_FROM_BITS((legacy_u16)(i * 997));
		forward_vectors[i].y = i % 3 == 0 ? -1 : (legacy_s16)(i * 13);
		forward_vectors[i].z = LEGACY_S16_FROM_BITS((legacy_u16)(i * 619));
		reverse_vectors[i].x = LEGACY_S16_WRAP_NEGATE(forward_vectors[i].z);
		reverse_vectors[i].y = forward_vectors[i].y;
		reverse_vectors[i].z = forward_vectors[i].x;
	}
	for (legacy_u32 i = 0; i < 16; i++) {
		oppnentSped[i] = i * 11;
	}
}

static void test_route_point_boundaries(void)
{
	reset_route_points();
	legacy_s8 speed;
	struct VECTOR result[4];
	assert(get_track_route_point(0, result, 2, &speed) == 1);
	assert(speed == 33);
	assert(get_track_route_point(0, result, 255, 0) == 0);
	assert(get_track_route_point(0, result, 0, 0) == 0);
	assert(result[0].y == -1);
	assert(LEGACY_READ_U16_LE((legacy_u8 *)result + 18) == 0);
	flags[0] = 16;
	get_track_route_point(0, result, 0, 0);
	assert(result[1].x == LEGACY_S16_WRAP_ADD(forward_vectors[5].x, 5632));
	route_info[0].reverse_path_offset_low = 0x34;
	route_info[0].reverse_path_offset_high = 0x12;
	get_track_route_point(0, result, 0, 0);
	assert(result[1].x == LEGACY_S16_WRAP_ADD(reverse_vectors[4].x, 5632));
	assert(LEGACY_READ_U16_LE((legacy_u8 *)result + 18) == 1);
	trkObjectList[1].ss_surfaceType = -1;
	get_track_route_point(0, result, 0, &speed);
	assert(speed == 0);
}

static void test_route_points_at_right_edge(void)
{
	reset_route_points();
	columns[0] = 29;
	trkObjectList[1].ss_multiTileFlag = 2;
	forward_vectors[0].x = -60;
	forward_vectors[0].y = 0;
	forward_vectors[0].z = 512;
	forward_vectors[1].x = 60;
	forward_vectors[1].y = 0;
	forward_vectors[1].z = -512;

	/* The original word after the 30-column table is elapsed_time2. */
	elapsed_time2 = 1234;
	struct VECTOR result[4];
	get_track_route_point(0, result, 0, 0);
	assert(result[0].x == 1234);
	assert(result[1].x == 1174);
	assert(result[2].x == 1294);
	assert(result[1].z == 6144);
	assert(result[2].z == 5120);

	elapsed_time2 = 0xf000;
	get_track_route_point(0, result, 0, 0);
	assert(result[0].x == -4096);
	assert(result[1].x == -4156);
	assert(result[2].x == -4036);
}

static legacy_u32 route_point_fingerprint(void)
{
	reset_route_points();
	legacy_u32 hash = 2166136261UL;
	static const legacy_s16 orientations[] = {0, 256, 512, 768, 17};
	static const legacy_u8 counts[] = {0, 1, 3, 127, 128, 255};
	struct VECTOR result[4];
	legacy_s8 speed;
	for (legacy_u32 sample = 0; sample < 240; sample++) {
		flags[0] = (sample & 1) | ((sample & 2) ? 16 : 0);
		route_info[sample & 1].route_orientation = orientations[sample % 5];
		route_info[sample & 1].route_point_count = (legacy_s8)counts[sample % 6];
		route_info[sample & 1].reverse_path_offset_low = sample & 4 ? 0x34 : 0;
		route_info[sample & 1].reverse_path_offset_high = sample & 4 ? 0x12 : 0;
		trkObjectList[1].ss_multiTileFlag = (sample >> 3) & 3;
		trkObjectList[1].ss_surfaceType = sample & 32 ? -1 : 2;
		terrain[terrainrows[5] + 5] = sample & 64 ? 6 : 0;
		for (legacy_u32 route = 0; route < 256; route++) {
			memset(result, 0x5a, sizeof(result));
			speed = 99;
			legacy_s16 last = get_track_route_point(0, result, route, route & 1 ? &speed : 0);
			hash = (hash ^ (legacy_u16)last) * 16777619UL;
			hash = (hash ^ (legacy_u8)speed) * 16777619UL;
			const legacy_u8 *bytes = (const legacy_u8 *)result;
			for (legacy_u32 i = 0; i < sizeof(result); i++) {
				hash = (hash ^ bytes[i]) * 16777619UL;
			}
		}
	}
	return hash;
}

static legacy_u32 steering_fingerprint(void)
{
	steerWhlRespTable_ptr = steering;
	for (legacy_u32 i = 0; i < 256; i++) {
		steering[i] = i % 4 == 0 ? 0 : LEGACY_S8_FROM_BITS((legacy_u8)(i * 17));
	}
	legacy_u32 hash = 2166136261UL;
	static const legacy_s16 angles[] = {-32768, -241, -240, -8,	 -2,  -1,	0,
										1,		2,	  8,	240, 241, 32767};
	for (legacy_u32 speed = 0; speed < 64; speed++) {
		for (legacy_u32 angle = 0; angle < sizeof(angles) / sizeof(angles[0]); angle++) {
			for (legacy_u32 input = 0; input < 4; input++) {
				for (legacy_u32 rate = 0; rate < 2; rate++) {
					state.playerstate.car_steeringAngle = angles[angle];
					state.playerstate.car_actual_speed = speed * 1024;
					framespersec = rate ? 10 : 20;
					update_player_steering_input(input);
					hash = (hash ^ (legacy_u16)state.playerstate.car_steeringAngle) * 16777619UL;
				}
			}
		}
	}
	return hash;
}

int main(void)
{
	test_route_point_boundaries();
	test_route_points_at_right_edge();
	legacy_u32 route_hash = route_point_fingerprint();
	legacy_u32 steering_hash = steering_fingerprint();
#ifdef PHYSICS_RECORD_BASELINE
	fprintf(stdout, "%08" LEGACY_PRIx32 " %08" LEGACY_PRIx32 "\n", route_hash, steering_hash);
#else
	/* Fingerprints captured from the pre-refactor implementation. */
	assert(route_hash == 0xdc6e8189UL);
	assert(steering_hash == 0xc84980fdUL);
#endif
	return 0;
}
