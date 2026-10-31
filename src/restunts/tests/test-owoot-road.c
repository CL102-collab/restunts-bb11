#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../c/owoot/owoot_road.c"

struct TRACKOBJECT trkObjectList[215];
legacy_s16 terrainrows[30];
legacy_s16 trackrows[30];
legacy_s16 hillHeightConsts[2] = {0, 450};
static legacy_u8 elements[900];
static legacy_u8 terrain[900];
legacy_u8 far *track_element_map = elements;
legacy_u8 far *track_terrain_map = terrain;

legacy_u8 subst_hillroad_track(legacy_u8 height, legacy_u8 tile)
{
	(void)height;
	(void)tile;
	return 2;
}

static struct VECTOR tire[8];
static legacy_s16 origin_x;
static legacy_s16 origin_z;

static void setup(legacy_s16 model, legacy_s16 rotation, legacy_s16 multiple)
{
	memset(elements, 0, sizeof(elements));
	memset(terrain, 0, sizeof(terrain));
	memset(trkObjectList, 0, sizeof(trkObjectList));
	for (legacy_s16 row = 0; row < TRACK_GRID_SIZE; row++) {
		terrainrows[row] = row * TRACK_GRID_SIZE;
		trackrows[row] = (TRACK_GRID_LAST_INDEX - row) * TRACK_GRID_SIZE;
	}
	trkObjectList[1].ss_physicalModel = (legacy_s8)model;
	trkObjectList[1].ss_rotY = rotation;
	trkObjectList[1].ss_multiTileFlag = (legacy_s8)multiple;
	trkObjectList[2] = trkObjectList[1];
	elements[terrainrows[10] + 10] = 1;
	origin_x = 10 * 1024 + ((multiple & 2) != 0 ? 1024 : 512);
	origin_z = 10 * 1024 + ((multiple & 1) != 0 ? 0 : 512);
	if ((multiple & 1) != 0) {
		elements[terrainrows[9] + 10] = TRACK_TILE_CONTINUATION_SOUTH;
	}
	if ((multiple & 2) != 0) {
		elements[terrainrows[10] + 11] = TRACK_TILE_CONTINUATION_EAST;
	}
	if (multiple == 3) {
		elements[terrainrows[9] + 11] = TRACK_TILE_CONTINUATION_SOUTHEAST;
	}
}

static void box(legacy_s16 min_x, legacy_s16 max_x, legacy_s16 min_y, legacy_s16 max_y,
				legacy_s16 min_z, legacy_s16 max_z)
{
	for (legacy_u16 rim = 0; rim < 2; rim++) {
		legacy_s16 x = rim == 0 ? min_x : max_x;
		for (legacy_u16 i = 0; i < 4; i++) {
			struct VECTOR *vertex = &tire[rim * 4 + i];
			vertex->x = origin_x + x;
			vertex->y = (i == 0 || i == 3) ? min_y : max_y;
			vertex->z = origin_z + (i < 2 ? min_z : max_z);
		}
	}
}

static void test_partial_wheel_and_height(void)
{
	setup(PHYSICAL_MODEL_ROAD, 0, 0);
	box(119, 139, 0, 20, -10, 10);
	assert(track_road_overlaps_wheel(tire, 4, 0));
	box(121, 141, 0, 20, -10, 10);
	assert(!track_road_overlaps_wheel(tire, 4, 0));
	box(-10, 10, 2000, 2020, -10, 10);
	assert(track_road_overlaps_wheel(tire, 4, 0));
	box(-10, 10, -40, -13, -10, 10);
	assert(!track_road_overlaps_wheel(tire, 4, 1));
	box(-10, 10, -40, -12, -10, 10);
	assert(track_road_overlaps_wheel(tire, 4, 1));
	assert(!track_road_overlaps_wheel(tire, 4, 0));
	box(-10, 10, -20, -1, -10, 10);
	assert(!track_road_overlaps_wheel(tire, 4, 0));
	box(-10, 10, -20, 0, -10, 10);
	assert(track_road_overlaps_wheel(tire, 4, 0));
	assert(!track_road_overlaps_wheel(tire, 2, 0));
	assert(!track_road_overlaps_wheel(tire, 17, 0));
}

static void test_curbs_and_chicane(void)
{
	setup(PHYSICAL_MODEL_LARGE_CORNER, 0, 3);
	box(-43, -41, 0, 20, -43, -41);
	assert(!track_road_overlaps_wheel(tire, 4, 0));
	box(-24, -22, 0, 20, -24, -22);
	assert(track_road_overlaps_wheel(tire, 4, 0));
	setup(PHYSICAL_MODEL_CHICANE_RIGHT_LEFT, 0, 3);
	box(-5, 5, 0, 20, 495, 505);
	assert(!track_road_overlaps_wheel(tire, 4, 0));
	box(-355, -345, 0, 20, 495, 505);
	assert(track_road_overlaps_wheel(tire, 4, 0));
}

static void test_elevated_and_sloped_surfaces(void)
{
	setup(PHYSICAL_MODEL_ELEVATED_ROAD, 0, 0);
	box(-10, 10, 0, 20, -10, 10);
	assert(!track_road_overlaps_wheel(tire, 4, 0));
	/* Contact tolerance repairs small renderer/physics discrepancies only
	 * for a grounded wheel; a flight below the bridge cannot use it. */
	box(-10, 10, 420, 438, -10, 10);
	assert(track_road_overlaps_wheel(tire, 4, 1));
	assert(!track_road_overlaps_wheel(tire, 4, 0));
	box(-10, 10, 438, 449, -10, 10);
	assert(!track_road_overlaps_wheel(tire, 4, 0));
	box(-10, 10, 438, 450, -10, 10);
	assert(track_road_overlaps_wheel(tire, 4, 0));
	setup(PHYSICAL_MODEL_RAMP, 0, 0);
	box(-10, 10, 0, 20, -10, 10);
	assert(!track_road_overlaps_wheel(tire, 4, 0));
	box(-10, 10, 220, 240, -10, 10);
	assert(track_road_overlaps_wheel(tire, 4, 0));
	box(-10, 10, 0, 20, -514, -494);
	assert(track_road_overlaps_wheel(tire, 4, 0));
	setup(PHYSICAL_MODEL_ROAD, 0, 0);
	terrain[trackrows[10] + 10] = 7;
	box(-10, 10, 20, 40, 390, 410);
	assert(!track_road_overlaps_wheel(tire, 4, 0));
	box(-10, 10, 430, 450, 390, 410);
	assert(track_road_overlaps_wheel(tire, 4, 0));
	terrain[trackrows[10] + 10] = TERRAIN_RAISED_TILE;
	box(-10, 10, 0, 20, -10, 10);
	assert(!track_road_overlaps_wheel(tire, 4, 0));
	box(-10, 10, 440, 460, -10, 10);
	assert(track_road_overlaps_wheel(tire, 4, 0));
}

static void test_half_pipe_opening(void)
{
	setup(PHYSICAL_MODEL_HALF_PIPE, 0, 0);
	/* Replay R0019 frame 474 crosses the centre floor opening below the
	 * roof: there is no surface under this entire tire bounding volume. */
	box(41, 73, 109, 148, -47, 23);
	assert(!track_road_overlaps_wheel(tire, 4, 0));
	/* Outside the opening the same height lies above the sloping floor. */
	box(41, 73, 109, 148, 80, 90);
	assert(track_road_overlaps_wheel(tire, 4, 0));
	/* Crossing high enough to lie on/above the ceiling does have coverage. */
	box(41, 73, 235, 245, -47, 23);
	assert(track_road_overlaps_wheel(tire, 4, 0));
}

static void test_rotations_and_overpass(void)
{
	setup(PHYSICAL_MODEL_ROAD, ANGLE_QUARTER_TURN, 0);
	box(300, 320, 0, 20, 119, 129);
	assert(track_road_overlaps_wheel(tire, 4, 0));
	box(300, 320, 0, 20, 121, 131);
	assert(!track_road_overlaps_wheel(tire, 4, 0));
	setup(PHYSICAL_MODEL_OVERPASS, 0, 0);
	box(300, 320, 0, 20, -10, 10);
	assert(track_road_overlaps_wheel(tire, 4, 0));
	box(-10, 10, 0, 20, 300, 320);
	assert(!track_road_overlaps_wheel(tire, 4, 0));
	box(-10, 10, 440, 460, 300, 320);
	assert(track_road_overlaps_wheel(tire, 4, 0));
}

static void test_clipped_volume(void)
{
	/* The sloped plane cuts both rims and the axle edges. An XZ-only hull
	 * with a single wheel height cannot distinguish these adjacent cases. */
	struct OWOOT_ROAD_TRIANGLE triangle = {{{-10, 0, -10}, {10, 20, -10}, {10, 20, 10}}};
	origin_x = 0;
	origin_z = 0;
	box(-2, 2, -30, -13, -2, 2);
	assert(!owoot_road_triangle_overlaps_wheel(&triangle, tire, 4, OWOOT_ROAD_CONTACT_TOLERANCE));
	box(-2, 2, -30, 5, -2, 2);
	assert(owoot_road_triangle_overlaps_wheel(&triangle, tire, 4, OWOOT_ROAD_CONTACT_TOLERANCE));
	/* A narrow road entirely inside the projected tire exercises triangle
	 * containment, rather than only sampling wheel vertices and edges. */
	struct OWOOT_ROAD_TRIANGLE small = {{{-1, 0, -1}, {1, 0, -1}, {0, 0, 1}}};
	box(-20, 20, 0, 20, -20, 20);
	assert(owoot_road_triangle_overlaps_wheel(&small, tire, 4, 0));
}

static void test_closed_pipe_aperture(void)
{
	origin_x = origin_z = 0;
	box(-10, 10, 10, 30, -5, 5);
	assert(track_pipe_aperture_overlaps_wheel(tire, 8));
	box(120, 130, 245, 255, -5, 5);
	assert(!track_pipe_aperture_overlaps_wheel(tire, 8));
	/* The former rectangular portal also admitted this upper corner,
	 * despite the pipe wall passing through (84,204) and (31,235). */
	box(100, 110, 210, 220, -5, 5);
	assert(!track_pipe_aperture_overlaps_wheel(tire, 8));
	box(83, 85, 203, 205, -5, 5);
	assert(track_pipe_aperture_overlaps_wheel(tire, 8));
	/* Touching the exterior roof is not traversal through the interior. */
	box(-10, 10, 235, 250, -5, 5);
	assert(!track_pipe_aperture_overlaps_wheel(tire, 8));
	box(-10, 10, 234, 250, -5, 5);
	assert(track_pipe_aperture_overlaps_wheel(tire, 8));
	box(115, 130, 100, 140, -5, 5);
	assert(!track_pipe_aperture_overlaps_wheel(tire, 8));
	box(114, 130, 100, 140, -5, 5);
	assert(track_pipe_aperture_overlaps_wheel(tire, 8));
	/* Containment works even when no tire vertex lies in the aperture. */
	box(-200, 200, -10, 300, -5, 5);
	assert(track_pipe_aperture_overlaps_wheel(tire, 8));
}

static void test_tunnel_aperture(void)
{
	origin_x = origin_z = 0;
	box(-10, 10, 144, 160, -5, 5);
	assert(!track_tunnel_aperture_overlaps_wheel(tire, 8));
	box(-10, 10, 143, 160, -5, 5);
	assert(track_tunnel_aperture_overlaps_wheel(tire, 8));
	box(120, 130, 40, 60, -5, 5);
	assert(!track_tunnel_aperture_overlaps_wheel(tire, 8));
	/* None of the vertices is inside, but two edges cross the aperture. */
	box(-130, 130, 40, 60, -5, 5);
	assert(track_tunnel_aperture_overlaps_wheel(tire, 8));
	box(-130, 130, -10, 160, -5, 5);
	assert(track_tunnel_aperture_overlaps_wheel(tire, 8));
	/* The angled slice's X/Y bounds overlap the upper-right corner while
	 * the slice itself stays outside it. */
	struct VECTOR angled[4] = {{110, 160, 0}, {140, 130, 0}, {150, 140, 0}, {120, 170, 0}};
	assert(!track_tunnel_aperture_overlaps_wheel(angled, 4));
	struct VECTOR touching[3] = {{120, 144, 0}, {140, 130, 0}, {130, 160, 0}};
	assert(!track_tunnel_aperture_overlaps_wheel(touching, 3));
	assert(!track_tunnel_aperture_overlaps_wheel(tire, 0));
}

static void copy_test_tire(struct VECTOR *output)
{
	for (legacy_u16 index = 0; index < 8; index++) {
		output[index] = tire[index];
	}
}

static void test_slalom_barrier_projection(void)
{
	struct VECTOR wheels[4][OWOOT_WHEEL_VERTEX_MAX];
	legacy_u16 counts[4] = {8, 0, 0, 0};
	struct VECTOR center = {0, 0, 0};
	struct VECTOR motion = {0, 0, 0};
	origin_x = origin_z = 0;
	box(40, 50, 1000, 1020, -260, -250);
	copy_test_tire(wheels[0]);
	assert(track_slalom_wheel_envelope_crosses_barrier(wheels, counts, 0, &center, 0, &motion));
	box(13, 23, 1000, 1020, -260, -250);
	copy_test_tire(wheels[0]);
	assert(!track_slalom_wheel_envelope_crosses_barrier(wheels, counts, 0, &center, 0, &motion));
	/* A complete jump across the thin wall has both tick endpoints clear. */
	box(40, 50, 1000, 1020, -220, -210);
	copy_test_tire(wheels[0]);
	motion.z = 90;
	assert(track_slalom_wheel_envelope_crosses_barrier(wheels, counts, 0, &center, 0, &motion));
	motion.z = -90;
	box(40, 50, 1000, 1020, -310, -300);
	copy_test_tire(wheels[0]);
	assert(track_slalom_wheel_envelope_crosses_barrier(wheels, counts, 0, &center, 0, &motion));
	/* XZ bounds alone would reject this angled footprint near the corner. */
	motion.z = 0;
	counts[0] = 4;
	wheels[0][0] = (struct VECTOR){0, 1000, -260};
	wheels[0][1] = (struct VECTOR){30, 1000, -290};
	wheels[0][2] = (struct VECTOR){28, 1000, -292};
	wheels[0][3] = (struct VECTOR){-2, 1000, -262};
	assert(!track_slalom_wheel_envelope_crosses_barrier(wheels, counts, 0, &center, 0, &motion));
	/* Tires on both sides do not permit the car's centre to jump the wall. */
	counts[0] = counts[1] = 8;
	box(0, 10, 1000, 1020, -260, -250);
	copy_test_tire(wheels[0]);
	box(110, 120, 1000, 1020, -260, -250);
	copy_test_tire(wheels[1]);
	assert(track_slalom_wheel_envelope_crosses_barrier(wheels, counts, 0, &center, 0, &motion));
	counts[1] = 0;
	struct VECTOR body[4] = {{0, 1000, -260}, {50, 1000, -260}, {50, 1000, -250}, {0, 1000, -250}};
	assert(track_slalom_wheel_envelope_crosses_barrier(wheels, counts, body, &center, 0, &motion));
	counts[1] = 8;
	/* Rotation and raised terrain do not change the projected obstacle. */
	center.x = 8000;
	center.y = 450;
	center.z = 9000;
	for (legacy_u16 wheel = 0; wheel < 2; wheel++) {
		for (legacy_u16 point = 0; point < 8; point++) {
			legacy_s16 x = wheels[wheel][point].x;
			wheels[wheel][point].x = center.x + wheels[wheel][point].z;
			wheels[wheel][point].z = center.z - x;
		}
	}
	assert(track_slalom_wheel_envelope_crosses_barrier(wheels, counts, 0, &center,
													   ANGLE_QUARTER_TURN, &motion));
}

static void test_every_drivable_model(void)
{
	for (legacy_s16 model = 0; model <= PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT; model++) {
		const struct OWOOT_ROAD_MODEL *geometry = &owoot_road_models[model];
		if (geometry->triangle_count == 0) {
			continue;
		}
		setup(model, 0, 3);
		for (legacy_u16 i = 0; i < geometry->triangle_count; i++) {
			const struct OWOOT_ROAD_TRIANGLE far *triangle =
				&owoot_road_triangles[geometry->first_triangle + i];
			legacy_s16 x =
				(triangle->vertex[0].x + triangle->vertex[1].x + triangle->vertex[2].x) / 3;
			legacy_s16 z =
				(triangle->vertex[0].z + triangle->vertex[1].z + triangle->vertex[2].z) / 3;
			box(x - 2, x + 2, 2000, 2020, z - 2, z + 2);
			assert(track_road_overlaps_wheel(tire, 4, 0));
		}
	}
}

int main(void)
{
	test_partial_wheel_and_height();
	test_curbs_and_chicane();
	test_elevated_and_sloped_surfaces();
	test_half_pipe_opening();
	test_rotations_and_overpass();
	test_clipped_volume();
	test_every_drivable_model();
	test_closed_pipe_aperture();
	test_tunnel_aperture();
	test_slalom_barrier_projection();
	puts("OWOOT road geometry checks passed.");
	return 0;
}
