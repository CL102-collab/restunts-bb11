#ifndef RESTUNTS_GENGHOST_MANEUVERS_H
#define RESTUNTS_GENGHOST_MANEUVERS_H

#include "routes.h"

#define GENGHOST_MANEUVER_CAPACITY 24U

enum GENGHOST_MANEUVER_KIND {
	GENGHOST_MANEUVER_FOLLOW = 0,
	GENGHOST_MANEUVER_ACCELERATE,
	GENGHOST_MANEUVER_BRAKE,
	GENGHOST_MANEUVER_TILE_TOUCH,
	GENGHOST_MANEUVER_STUNT,
	GENGHOST_MANEUVER_STUNT_ALTERNATE,
	GENGHOST_MANEUVER_CHICANE_GRASS,
	GENGHOST_MANEUVER_LAUNCH,
	GENGHOST_MANEUVER_POWERGEAR,
	GENGHOST_MANEUVER_LANDING,
	GENGHOST_MANEUVER_SLALOM_CENTER,
	GENGHOST_MANEUVER_SLALOM_JUMP,
	GENGHOST_MANEUVER_BARREL_ROLL
};

/* Immutable, parameterized macro action. Geometry uses world units; headings
 * and speeds are measured from native physics, never substituted into it.
 * The caller may copy the action along with a checkpoint or refine its gene,
 * approach/exit points and duration before evaluating real replay inputs. */
struct GENGHOST_MANEUVER {
	enum GENGHOST_MANEUVER_KIND kind;
	legacy_uint horizon_frames;
	legacy_uint shift_mode;
	legacy_uint hold_throttle;
	legacy_uint road_only;
	struct GENGHOST_GENE gene;
	struct GENGHOST_WAYPOINT approach, exit;
	legacy_f64 feature_begin, feature_end;
	legacy_f64 approach_speed, exit_speed;
	legacy_f64 exit_heading_x, exit_heading_z;
	legacy_int physical_model;
};

/* Deterministic proposal ordering. Always begins with an unchanged normal
 * controller. Flags are the active simulator rules; OWOOT permits the road-only
 * slalom center, barrel-roll and slope shifts, while forbidding off-road macros. Disabled
 * power gear forbids its shift proposals. No native
 * state is advanced or written while generating proposals. */
size_t genghost_maneuvers(const struct GENGHOST_ROUTE *route, const struct GENGHOST_DRIVER *driver,
						  const struct GENGHOST_POLICY *policy, legacy_int powergear,
						  legacy_int collision, legacy_int owoot, struct GENGHOST_MANEUVER *actions,
						  size_t capacity);

/* Observe native state and produce one ordinary 20 Hz input byte. The caller
 * steps genghost_sim_step, scores native penalties and rejects crashes or
 * OWOOT violations using the original simulator. No generated action claims feasibility until
 * that simulation succeeds. elapsed starts at zero for each macro. */
legacy_u8 genghost_maneuver_step(const struct GENGHOST_ROUTE *route,
								 const struct GENGHOST_POLICY *policy,
								 struct GENGHOST_DRIVER *driver,
								 const struct GENGHOST_MANEUVER *action, legacy_uint elapsed,
								 legacy_int powergear, legacy_int collision, legacy_int owoot);
const legacy_char *genghost_maneuver_name(enum GENGHOST_MANEUVER_KIND kind);
/* Positive acceleration from the native unsigned negative-force division on
 * the next settled, grounded throttle tick. This is a readiness predicate,
 * not a persistent PG latch or a high-speed approximation. The caller gates
 * it with the active --pg rule. */
legacy_int genghost_maneuver_powergear_ready(void);

#endif
