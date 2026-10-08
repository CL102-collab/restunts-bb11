#ifndef RESTUNTS_GENGHOST_ROUTES_H
#define RESTUNTS_GENGHOST_ROUTES_H

#include <stddef.h>

#define GENGHOST_POLICY_SEGMENTS 64U

/* Coordinates are the game's world units (1024 per tile); speeds are mph.
 * Offset is a fraction of the center-to-edge vector: +/-1 is a road edge.
 * Values beyond an edge are useful when the active rules allow grass. */
struct GENGHOST_GENE {
	double speed_scale;
	double lateral_offset;
	double lookahead_scale;
	double shift_rpm_scale;
	unsigned int shortcut_skip;
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
	int physical_model;
	int height_specified;
};

struct GENGHOST_ROUTE {
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
};

/* This is independent optimizer state and may be copied alongside a physics
 * snapshot. The driver never writes engine state, only replay input bytes. */
struct GENGHOST_DRIVER {
	size_t waypoint;
	unsigned int segment;
	double progress;
	double off_route;
	double target_speed;
	unsigned int ticks;
};

/* Enumerate every simple directed route from start/finish back to itself.
 * On allocation/size failure return nonzero with an explicit error, never a
 * silently truncated route set. Call after genghost_sim_init/track_setup. */
int genghost_routes_build(struct GENGHOST_ROUTES *routes, char *error, size_t error_size);
void genghost_routes_free(struct GENGHOST_ROUTES *routes);
void genghost_policy_default(struct GENGHOST_POLICY *policy);
void genghost_driver_reset(struct GENGHOST_DRIVER *driver);
void genghost_driver_observe(const struct GENGHOST_ROUTE *route, struct GENGHOST_DRIVER *driver);
/* Read the current player and car parameters from the original engine. The
 * powergear flag must reflect --pg:on/off; owoot the active road restriction. */
unsigned char genghost_driver_step(const struct GENGHOST_ROUTE *route,
								   const struct GENGHOST_POLICY *policy,
								   struct GENGHOST_DRIVER *driver, int powergear_enabled,
								   int owoot);
/* Choose the input whose next-frame steering angle is closest to the target. */
unsigned char genghost_steering_key(double wanted);
/* 0 = not useful, 1 = flexible, 2 = rigid and gear ceiling allows >=225 mph. */
int genghost_powergear_kind(void);

#endif
