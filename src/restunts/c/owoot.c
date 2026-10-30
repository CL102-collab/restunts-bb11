#include "owoot.h"
#include "owoot_route.h"
#include "owoot_road.h"
#include "crash_state.h"
#include "externs.h"

legacy_s16 owoot_enabled = 0;

void configure_owoot(legacy_s16 argc, legacy_s8 *argv[])
{
	owoot_enabled = 0;
	for (legacy_s16 index = 1; index < argc; index++) {
		if (stricmp(argv[index], "--owoot") == 0) {
			owoot_enabled = 1;
		}
	}
}

void owoot_update_player(struct CARSTATE *car, legacy_s16 car_index)
{
	if (!owoot_enabled || car_index != PLAYER_CAR_INDEX ||
		state.game_inputmode != GAME_INPUT_MODE_ACTIVE ||
		car->car_crashBmpFlag != CRASH_EVENT_NONE) {
		return;
	}

	/* Both checks advance state even while a wheel is over road, so leaving a
	 * ramp cannot grant an exemption after an unrelated later jump. */
	legacy_s16 allowed_jump = owoot_jump_is_valid(car);
	if (allowed_jump == OWOOT_JUMP_FAILED) {
		update_crash_state(CRASH_EVENT_COLLISION, PLAYER_CAR_INDEX);
		return;
	}
	legacy_s16 follows_route = owoot_route_is_valid(car, allowed_jump);
	if (follows_route && allowed_jump) {
		return;
	}
	if (follows_route) {
		struct VECTOR footprint[OWOOT_WHEEL_VERTEX_COUNT];
		for (legacy_u16 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
			legacy_u16 count = owoot_wheel_footprint(car, wheel, footprint);
			legacy_s16 road_contact = car->car_surfaceWhl[wheel] >= CAR_SURFACE_PAVED &&
									  car->car_surfaceWhl[wheel] <= CAR_SURFACE_ICE;
			if (track_road_overlaps_wheel(footprint, count / OWOOT_WHEEL_RIM_COUNT, road_contact)) {
				return;
			}
		}
	}
	update_crash_state(CRASH_EVENT_COLLISION, PLAYER_CAR_INDEX);
}
