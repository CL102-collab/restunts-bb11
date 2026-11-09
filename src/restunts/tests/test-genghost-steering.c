#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../c/externs.h"
#include "../c/state_internal.h"
#include "../c/game_input.h"
#include "../c/car/car_resources.h"
#include "../genghost/extended_routes.h"
#undef memset

struct GAMESTATE state;
legacy_u16 framespersec;
legacy_u8 car_steering_rate[2] = {CAR_STEERING_RATE_DEFAULT, CAR_STEERING_RATE_DEFAULT};
static legacy_s8 steering[256];
legacy_s8 *steerWhlRespTable_ptr = steering;

int main(void)
{
	static const legacy_s16 angles[] = {-240, -127, -44, -24, -1, 0, 1, 24, 44, 127, 240};
	static const legacy_f64 targets[] = {-127.0, -44.0, -24.0, -0.5, 0.0, 0.5, 24.0, 44.0, 127.0};
	car_steering_rate[PLAYER_CAR_INDEX] = CAR_STEERING_RATE_MAX;
	for (legacy_u16 low = 0; low < 2; low++) {
		framespersec = low ? GAME_FRAME_RATE_LOW : GAME_FRAME_RATE_NORMAL;
		memset(steering, 0, sizeof(steering));
		steering[1] = low ? 16 : 8;
		steering[2] = -steering[1];
		for (legacy_u16 angle = 0; angle < sizeof(angles) / sizeof(angles[0]); angle++) {
			for (legacy_u16 target = 0; target < sizeof(targets) / sizeof(targets[0]); target++) {
				state.playerstate.car_steeringAngle = angles[angle];
				state.playerstate.car_actual_speed = 1;
				legacy_u8 selected = extended_genghost_steering_key(targets[target]);
				assert((selected & ~INPUT_STEERING_MASK) == 0);
				legacy_s8 input = selected >> INPUT_STEERING_SHIFT;
				assert(input <= 2);
				legacy_s16 predicted =
					player_steering_next_angle(&state.playerstate, input, CAR_STEERING_RATE_MAX);
				legacy_f64 delta = predicted - targets[target];
				for (legacy_s8 choice = 0; choice <= 2; choice++) {
					legacy_f64 alternative = player_steering_next_angle(&state.playerstate, choice,
																		CAR_STEERING_RATE_MAX) -
											 targets[target];
					assert(delta * delta <= alternative * alternative);
				}
				update_player_steering_input(input);
				assert(state.playerstate.car_steeringAngle == predicted);
			}
		}
	}
	puts("Custom genghost steering prediction agrees with runtime for both frame rates.");
	return 0;
}
