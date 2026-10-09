#ifndef RESTUNTS_GENGHOST_ROUTES_H
#define RESTUNTS_GENGHOST_ROUTES_H

#include <stddef.h>
#include <stdint.h>

#define GENGHOST_POLICY_SEGMENTS 64U
#define GENGHOST_STUNT_CORKSCREW_CUT 5U
#define GENGHOST_STUNT_HUMAN 6U
#define GENGHOST_STUNT_HUMAN_ALT 7U
#define GENGHOST_STUNT_SLALOM_CENTER 8U

/* Coordinates are the game's world units (1024 per tile); speeds are mph.
 * Offset is a fraction of the center-to-edge vector: +/-1 is a road edge.
 * Values beyond an edge are useful when the active rules allow grass. */
struct GENGHOST_GENE {
	/* 0 keeps this branch; 1/2 seed short/near crossings; later values index
	 * every eligible departure/arrival pair without allocating a population each. */
	uint64_t dualway_choice;
	double speed_scale;
	double lateral_offset;
	double lookahead_scale;
	double shift_rpm_scale;
	unsigned int shortcut_skip;
	unsigned int line_mode;
	/* Local proposals: 0 normal, 1 ground, 2/3 edge, 4 launch, 5 cork cut,
	 * 6 collision-aware human recipe, 7 its alternative, 8 slalom center only. */
	unsigned int stunt_mode;
	double corner_scale;
	double powergear_bias;
	double slide_brake;
};

struct GENGHOST_POLICY {
	struct GENGHOST_GENE segment[GENGHOST_POLICY_SEGMENTS];
};

struct GENGHOST_WAYPOINT {
	double x, y, z;
	double edge_x, edge_y, edge_z;
	double distance;
	double speed;
	int piece;
	size_t piece_index;
	int physical_model;
	int height_specified;
};

struct GENGHOST_REGION {
	double min_x, max_x, min_z, max_z;
};

/* Find the shortest broken line from A through an axis-aligned region to B.
 * The returned crossing lies in the region; direct crossings advance toward
 * B as far as possible while remaining inside. */
void genghost_region_crossing(const struct GENGHOST_REGION *region, double ax, double az, double bx,
							  double bz, double *px, double *pz);

struct GENGHOST_DUALWAY;

struct GENGHOST_ROUTE {
	struct GENGHOST_DUALWAY *dualways;
	size_t dualway_count, switch_count;
	/* Optional ordered mandatory tile touches: 1 approach/confirmation, 2 cross. */
	unsigned char *portals;
	int *pieces;
	size_t piece_count;
	struct GENGHOST_WAYPOINT *points;
	size_t point_count;
	double length;
};

struct GENGHOST_ROUTES {
	struct GENGHOST_ROUTE *routes;
	size_t count;
	size_t branch_count;
	uint64_t dualway_pairs;
};

/* This is independent optimizer state and may be copied alongside a physics
 * snapshot. The driver never writes engine state, only replay input bytes. */
struct GENGHOST_DRIVER {
	size_t waypoint;
	size_t next_portal;
	unsigned int segment;
	double progress;
	double off_route;
	double target_speed;
	unsigned int ticks;
	/* Actual frames driven toward a stunt proposal, not gene presence. */
	unsigned int tactic_ticks;
};

/* Enumerate every simple directed route from start/finish back to itself.
 * On allocation/size failure return nonzero with an explicit error, never a
 * silently truncated route set. Call after genghost_sim_init/track_setup. */
int genghost_routes_build(struct GENGHOST_ROUTES *routes, char *error, size_t error_size);
/* Materialize policy-selected switches using immutable base-route distances.
 * Return 1 with owned geometry, 0 for the original route (result is empty),
 * or -1 with an error. Release owned geometry after the entire evaluation. */
int genghost_route_policy(const struct GENGHOST_ROUTES *set, size_t index,
						  const struct GENGHOST_POLICY *policy, struct GENGHOST_ROUTE *result,
						  char *error, size_t error_size);
/* Choose stay/short/near recipes independently of the arbitrary pair selector.
 * The caller already used the low two gate bits to decide whether to mutate. */
uint64_t genghost_dualway_selector(uint32_t gate, uint64_t selector);
/* Distance of a particular route occurrence, including the repeated finish. */
double genghost_route_piece_distance(const struct GENGHOST_ROUTE *route, size_t piece_index);
/* Seed early private departures and late private arrivals for each branch,
 * using its own endpoint counts; preserve the short/near recipes for seeds 0/1. */
void genghost_dualway_seed_policy(const struct GENGHOST_ROUTES *set, size_t index,
								  struct GENGHOST_POLICY *policy, unsigned int seed);
void genghost_route_release(struct GENGHOST_ROUTE *route);
void genghost_routes_free(struct GENGHOST_ROUTES *routes);
void genghost_policy_default(struct GENGHOST_POLICY *policy);
void genghost_driver_reset(struct GENGHOST_DRIVER *driver);
void genghost_driver_observe(const struct GENGHOST_ROUTE *route, struct GENGHOST_DRIVER *driver);
/* Every supported stunt has an explicit recipe for original/fixed collisions.
 * Equal entries mean the useful line does not rely on a collision bug. These
 * are proposals; native replay completion and penalties decide their value. */
enum GENGHOST_STUNT_RECIPE_KIND {
	GENGHOST_RECIPE_CHORD = 1,
	GENGHOST_RECIPE_GROUND_CROSS,
	GENGHOST_RECIPE_OUTSIDE,
	GENGHOST_RECIPE_LAUNCH,
	GENGHOST_RECIPE_MEDIAN,
	GENGHOST_RECIPE_CLEAR_LANE,
	GENGHOST_RECIPE_CORK_CUT,
	GENGHOST_RECIPE_LOW_EDGE,
	GENGHOST_RECIPE_UNDERPASS,
	GENGHOST_RECIPE_FAST_PASS,
	GENGHOST_RECIPE_SLALOM_CENTER
};
struct GENGHOST_STUNT_RECIPE {
	enum GENGHOST_STUNT_RECIPE_KIND kind;
	const char *name;
};
const struct GENGHOST_STUNT_RECIPE *genghost_stunt_recipe(int model, int collision_enabled);
int genghost_stunt_catalog_model(int model);
/* The real body/wheel width fits between the inner slalom stone faces. */
int genghost_slalom_center_fits(void);
/* Pure target planner: reads current car dimensions/position, never writes
 * native physics. OWOOT permits only the physically clear slalom center recipe. */
int genghost_stunt_target(const struct GENGHOST_ROUTE *route, const struct GENGHOST_DRIVER *driver,
						  const struct GENGHOST_GENE *gene, double lookahead, int collision_enabled,
						  struct GENGHOST_WAYPOINT *target, double *protected_end);
/* Read the current player and car parameters from the original engine. The
 * Physics flags must reflect --pg and --lc; owoot is the active road rule.
 * Solid-crossing proposals require original collisions; clear ground cuts and
 * edge jumps remain available with collision fixes. OWOOT permits only a clear slalom center pass. */
unsigned char genghost_driver_step(const struct GENGHOST_ROUTE *route,
								   const struct GENGHOST_POLICY *policy,
								   struct GENGHOST_DRIVER *driver, int powergear_enabled,
								   int collision_enabled, int owoot);
/* Choose the input whose next-frame steering angle is closest to the target. */
unsigned char genghost_steering_key(double wanted);
/* 0 = not useful, 1 = flexible, 2 = rigid and gear ceiling allows >=225 mph. */
int genghost_powergear_kind(void);

#endif
