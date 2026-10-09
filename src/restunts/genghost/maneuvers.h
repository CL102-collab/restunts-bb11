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
	GENGHOST_MANEUVER_SLALOM_JUMP
};

/* Immutable, parameterized macro action. Geometry uses world units; headings
 * and speeds are measured from native physics, never substituted into it.
 * The caller may copy the action along with a checkpoint or refine its gene,
 * approach/exit points and duration before evaluating real replay inputs. */
struct GENGHOST_MANEUVER {
	enum GENGHOST_MANEUVER_KIND kind;
	unsigned int horizon_frames;
	unsigned int shift_mode;
	unsigned int hold_throttle;
	unsigned int road_only;
	struct GENGHOST_GENE gene;
	struct GENGHOST_WAYPOINT approach, exit;
	double feature_begin, feature_end;
	double approach_speed, exit_speed;
	double exit_heading_x, exit_heading_z;
	int physical_model;
};

/* Deterministic proposal ordering. Always begins with an unchanged normal
 * controller. Flags are the active simulator rules; OWOOT permits the road-only
 * slalom center and slope shifts, while forbidding off-road macros. Disabled
 * power gear forbids its shift proposals. No native
 * state is advanced or written while generating proposals. */
size_t genghost_maneuvers(const struct GENGHOST_ROUTE *route, const struct GENGHOST_DRIVER *driver,
						  const struct GENGHOST_POLICY *policy, int powergear, int collision,
						  int owoot, struct GENGHOST_MANEUVER *actions, size_t capacity);

/* Observe native state and produce one ordinary 20 Hz input byte. The caller
 * steps genghost_sim_step, scores native penalties and rejects crashes or
 * OWOOT violations using the original simulator. No generated action claims feasibility until
 * that simulation succeeds. elapsed starts at zero for each macro. */
unsigned char genghost_maneuver_step(const struct GENGHOST_ROUTE *route,
									 const struct GENGHOST_POLICY *policy,
									 struct GENGHOST_DRIVER *driver,
									 const struct GENGHOST_MANEUVER *action, unsigned int elapsed,
									 int powergear, int collision, int owoot);
const char *genghost_maneuver_name(enum GENGHOST_MANEUVER_KIND kind);
/* Positive acceleration from the native unsigned negative-force division on
 * the next settled, grounded throttle tick. This is a readiness predicate,
 * not a persistent PG latch or a high-speed approximation. The caller gates
 * it with the active --pg rule. */
int genghost_maneuver_powergear_ready(void);

#endif
