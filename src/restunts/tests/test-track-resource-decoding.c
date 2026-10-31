#include <assert.h>

#include "../c/externs.h"
#include "../c/physics_internal.h"
#include "../c/track/track_types.h"
#include "../c/track/track_collision.h"

#define PLANE_RECORD_SIZE 34U
#define WALL_RECORD_SIZE 6U

struct PLANE far *planptr;
struct TRACK_WALL far *wallptr;

static legacy_s16 expected_s16(const legacy_u8 *source, legacy_u16 offset)
{
	return LEGACY_READ_S16_LE(source + offset);
}

int main(void)
{
	legacy_u8 plane_source[TRACK_PLAN_RESOURCE_COUNT * PLANE_RECORD_SIZE];
	for (legacy_u16 index = 0U; index < TRACK_PLAN_RESOURCE_COUNT * PLANE_RECORD_SIZE; index++) {
		plane_source[index] = (legacy_u8)(index * 29U + 3U);
	}
	legacy_u8 wall_source[TRACK_WALL_RESOURCE_COUNT * WALL_RECORD_SIZE];
	for (legacy_u16 index = 0U; index < TRACK_WALL_RESOURCE_COUNT * WALL_RECORD_SIZE; index++) {
		wall_source[index] = (legacy_u8)(index * 43U + 7U);
	}

	track_collision_resources_decode(plane_source, wall_source);
	legacy_u16 offset;
	for (legacy_u16 index = 0U; index < TRACK_PLAN_RESOURCE_COUNT; index++) {
		offset = (legacy_u16)(index * PLANE_RECORD_SIZE);
		assert(planptr[index].plane_yz == expected_s16(plane_source, offset));
		assert(planptr[index].plane_xy == expected_s16(plane_source, (legacy_u16)(offset + 2U)));
		assert(planptr[index].plane_origin.x ==
			   expected_s16(plane_source, (legacy_u16)(offset + 4U)));
		assert(planptr[index].plane_origin.y ==
			   expected_s16(plane_source, (legacy_u16)(offset + 6U)));
		assert(planptr[index].plane_origin.z ==
			   expected_s16(plane_source, (legacy_u16)(offset + 8U)));
		assert(planptr[index].plane_normal.x ==
			   expected_s16(plane_source, (legacy_u16)(offset + 10U)));
		assert(planptr[index].plane_normal.y ==
			   expected_s16(plane_source, (legacy_u16)(offset + 12U)));
		assert(planptr[index].plane_normal.z ==
			   expected_s16(plane_source, (legacy_u16)(offset + 14U)));
		for (legacy_u16 component = 0U; component < 9U; component++) {
			assert(planptr[index].plane_rotation.vals[component] ==
				   expected_s16(plane_source, (legacy_u16)(offset + 16U + component * 2U)));
		}
	}
	for (legacy_u16 index = 0U; index < TRACK_WALL_RESOURCE_COUNT; index++) {
		offset = (legacy_u16)(index * WALL_RECORD_SIZE);
		assert(wallptr[index].orientation == expected_s16(wall_source, offset));
		assert(wallptr[index].x == expected_s16(wall_source, (legacy_u16)(offset + 2U)));
		assert(wallptr[index].z == expected_s16(wall_source, (legacy_u16)(offset + 4U)));
	}
	return 0;
}
