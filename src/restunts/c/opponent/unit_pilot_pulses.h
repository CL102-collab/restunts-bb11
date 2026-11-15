#ifndef RESTUNTS_UNIT_PILOT_PULSES_H
#define RESTUNTS_UNIT_PILOT_PULSES_H

#include "../legacy.h"
#include "../extended/extended_barrel_search.h"

#define UNIT_PILOT_PULSE_ATTEMPT_LIMIT 2048U

struct CARSTATE;
struct UNIT_ROUTE;
struct UNIT_PILOT_PULSES;

struct UNIT_PILOT_PULSE_PROFILE {
	legacy_f64 speed, barrel_offset, exit_fraction, exit_speed, secondary_speed;
	legacy_u8 secondary_ramp_approach;
};

struct UNIT_PILOT_PULSE_DIAGNOSTIC {
	struct EXTENDED_BARREL_PARAMETERS parameters;
	struct UNIT_PILOT_PULSE_PROFILE profile;
	legacy_u32 attempts, span, bank;
	legacy_f64 fitness;
	legacy_u16 progress;
	legacy_u8 active;
};

/* One allocation per pilot manager. Native snapshots and stepping remain
 * entirely owned by the caller; these hooks only observe and propose inputs. */
struct UNIT_PILOT_PULSES *unit_pilot_pulses_create(legacy_u32 seed);
void unit_pilot_pulses_destroy(struct UNIT_PILOT_PULSES *pulses);
/* Call when establishing a new approach snapshot, not on a chunk yield.
 * First/last delimit the source barrel piece, including its flight guide. */
legacy_int unit_pilot_pulses_begin(struct UNIT_PILOT_PULSES *pulses, const struct UNIT_ROUTE *route,
								   legacy_u32 first, legacy_u32 last, legacy_u16 frame_rate);
void unit_pilot_pulses_start_trial(struct UNIT_PILOT_PULSES *pulses, legacy_u32 frame,
								   const struct UNIT_PILOT_PULSE_PROFILE *profile);
/* Call for scalar trials too, so the native ramp traversal is measured before
 * repair. Pulse overrides are one-shot and only emitted during active repair. */
void unit_pilot_pulses_before_step(struct UNIT_PILOT_PULSES *pulses, const struct CARSTATE *car,
								   legacy_u32 frame);
void unit_pilot_pulses_finish_trial(struct UNIT_PILOT_PULSES *pulses, const struct CARSTATE *car,
									legacy_u32 frame, legacy_u8 accepted);
/* After ordinary policies fail, vary native pulse controls, relative approach
 * speed and bounded lateral trim around the best measured scalar profile.
 * Restore the caller's approach snapshot and start a
 * fresh trial with the returned profile. Never accepts a maneuver itself. */
legacy_int unit_pilot_pulses_next(struct UNIT_PILOT_PULSES *pulses,
								  struct UNIT_PILOT_PULSE_PROFILE *profile);
legacy_int unit_pilot_pulses_active(const struct UNIT_PILOT_PULSES *pulses);

legacy_int unit_pilot_pulses_last_trial(const struct UNIT_PILOT_PULSES *pulses,
										struct UNIT_PILOT_PULSE_DIAGNOSTIC *diagnostic);

#endif
