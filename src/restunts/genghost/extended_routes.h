#ifndef RESTUNTS_GENGHOST_EXTENDED_ROUTES_H
#define RESTUNTS_GENGHOST_EXTENDED_ROUTES_H

#include "routes.h"

/* Cohesive route geometry and control engine shared by legacy and extended tracks. */
void extended_genghost_route_piece_frame(legacy_int piece, legacy_f64 *x, legacy_f64 *z,
										 legacy_s16 *orientation);
legacy_f64 extended_genghost_route_piece_height(legacy_int piece, legacy_f64 x, legacy_f64 z);
void extended_genghost_route_target_height(struct GENGHOST_WAYPOINT *target);
legacy_u64 extended_genghost_dualway_selector(legacy_u32 gate, legacy_u64 selector);
legacy_f64 extended_genghost_route_piece_distance(const struct GENGHOST_ROUTE *route, size_t piece);
void extended_genghost_dualway_seed_policy(const struct GENGHOST_ROUTES *set, size_t index,
										   struct GENGHOST_POLICY *policy, legacy_uint seed);
void extended_genghost_route_release(struct GENGHOST_ROUTE *route);
legacy_int extended_genghost_route_policy(const struct GENGHOST_ROUTES *set, size_t index,
										  const struct GENGHOST_POLICY *policy,
										  struct GENGHOST_ROUTE *result, legacy_char *error,
										  size_t error_size);
void extended_genghost_routes_free(struct GENGHOST_ROUTES *set);
legacy_int extended_genghost_routes_build(struct GENGHOST_ROUTES *set, legacy_char *error,
										  size_t error_size);
void extended_genghost_policy_default(struct GENGHOST_POLICY *policy);
legacy_uint extended_genghost_barrel_seed_count(const struct GENGHOST_ROUTE *route);
void extended_genghost_barrel_seed_policy(const struct GENGHOST_ROUTE *route,
										  struct GENGHOST_POLICY *policy, legacy_uint seed);
void extended_genghost_driver_reset(struct GENGHOST_DRIVER *driver);
void extended_genghost_driver_observe(const struct GENGHOST_ROUTE *route,
									  struct GENGHOST_DRIVER *driver);
void extended_genghost_region_crossing(const struct GENGHOST_REGION *portal, legacy_f64 ax,
									   legacy_f64 az, legacy_f64 bx, legacy_f64 bz, legacy_f64 *px,
									   legacy_f64 *pz);
const struct GENGHOST_STUNT_RECIPE *extended_genghost_stunt_recipe(legacy_int model,
																   legacy_int collision_enabled);
legacy_int extended_genghost_stunt_catalog_model(legacy_int model);
legacy_int extended_genghost_slalom_center_fits(void);
legacy_int extended_genghost_stunt_target(const struct GENGHOST_ROUTE *route,
										  const struct GENGHOST_DRIVER *driver,
										  const struct GENGHOST_GENE *gene, legacy_f64 lookahead,
										  legacy_int collision, struct GENGHOST_WAYPOINT *target,
										  legacy_f64 *protected_end);
legacy_int extended_genghost_powergear_kind(void);
legacy_u8 extended_genghost_steering_key(legacy_f64 wanted);
legacy_u8 extended_genghost_driver_step(const struct GENGHOST_ROUTE *route,
										const struct GENGHOST_POLICY *policy,
										struct GENGHOST_DRIVER *driver,
										legacy_int powergear_enabled, legacy_int collision_enabled,
										legacy_int owoot);

legacy_int extended_genghost_native_cut_allowed(const struct GENGHOST_WAYPOINT *point);

#endif
