#include "routes.h"
#include "extended_routes.h"

/* Preserve the public planner API while the format-aware engine owns its logic. */

void genghost_route_piece_frame(legacy_int piece, legacy_f64 *x, legacy_f64 *z,
								legacy_s16 *orientation)
{
	extended_genghost_route_piece_frame(piece, x, z, orientation);
}

legacy_f64 genghost_route_piece_height(legacy_int piece, legacy_f64 x, legacy_f64 z)
{
	return extended_genghost_route_piece_height(piece, x, z);
}

void genghost_route_target_height(struct GENGHOST_WAYPOINT *target)
{
	extended_genghost_route_target_height(target);
}

legacy_u64 genghost_dualway_selector(legacy_u32 gate, legacy_u64 selector)
{
	return extended_genghost_dualway_selector(gate, selector);
}

legacy_f64 genghost_route_piece_distance(const struct GENGHOST_ROUTE *route, size_t piece)
{
	return extended_genghost_route_piece_distance(route, piece);
}

void genghost_dualway_seed_policy(const struct GENGHOST_ROUTES *set, size_t index,
								  struct GENGHOST_POLICY *policy, legacy_uint seed)
{
	extended_genghost_dualway_seed_policy(set, index, policy, seed);
}

void genghost_route_release(struct GENGHOST_ROUTE *route)
{
	extended_genghost_route_release(route);
}

legacy_int genghost_route_policy(const struct GENGHOST_ROUTES *set, size_t index,
								 const struct GENGHOST_POLICY *policy,
								 struct GENGHOST_ROUTE *result, legacy_char *error,
								 size_t error_size)
{
	return extended_genghost_route_policy(set, index, policy, result, error, error_size);
}

void genghost_routes_free(struct GENGHOST_ROUTES *set)
{
	extended_genghost_routes_free(set);
}

legacy_int genghost_routes_build(struct GENGHOST_ROUTES *set, legacy_char *error, size_t error_size)
{
	return extended_genghost_routes_build(set, error, error_size);
}

void genghost_policy_default(struct GENGHOST_POLICY *policy)
{
	extended_genghost_policy_default(policy);
}

legacy_uint genghost_barrel_seed_count(const struct GENGHOST_ROUTE *route)
{
	return extended_genghost_barrel_seed_count(route);
}

void genghost_barrel_seed_policy(const struct GENGHOST_ROUTE *route, struct GENGHOST_POLICY *policy,
								 legacy_uint seed)
{
	extended_genghost_barrel_seed_policy(route, policy, seed);
}

void genghost_driver_reset(struct GENGHOST_DRIVER *driver)
{
	extended_genghost_driver_reset(driver);
}

void genghost_driver_observe(const struct GENGHOST_ROUTE *route, struct GENGHOST_DRIVER *driver)
{
	extended_genghost_driver_observe(route, driver);
}

void genghost_region_crossing(const struct GENGHOST_REGION *portal, legacy_f64 ax, legacy_f64 az,
							  legacy_f64 bx, legacy_f64 bz, legacy_f64 *px, legacy_f64 *pz)
{
	extended_genghost_region_crossing(portal, ax, az, bx, bz, px, pz);
}

const struct GENGHOST_STUNT_RECIPE *genghost_stunt_recipe(legacy_int model,
														  legacy_int collision_enabled)
{
	return extended_genghost_stunt_recipe(model, collision_enabled);
}

legacy_int genghost_stunt_catalog_model(legacy_int model)
{
	return extended_genghost_stunt_catalog_model(model);
}

legacy_int genghost_slalom_center_fits(void)
{
	return extended_genghost_slalom_center_fits();
}

legacy_int genghost_stunt_target(const struct GENGHOST_ROUTE *route,
								 const struct GENGHOST_DRIVER *driver,
								 const struct GENGHOST_GENE *gene, legacy_f64 lookahead,
								 legacy_int collision, struct GENGHOST_WAYPOINT *target,
								 legacy_f64 *protected_end)
{
	return extended_genghost_stunt_target(route, driver, gene, lookahead, collision, target,
										  protected_end);
}

legacy_int genghost_powergear_kind(void)
{
	return extended_genghost_powergear_kind();
}

legacy_u8 genghost_steering_key(legacy_f64 wanted)
{
	return extended_genghost_steering_key(wanted);
}

legacy_u8 genghost_driver_step(const struct GENGHOST_ROUTE *route,
							   const struct GENGHOST_POLICY *policy, struct GENGHOST_DRIVER *driver,
							   legacy_int powergear_enabled, legacy_int collision_enabled,
							   legacy_int owoot)
{
	return extended_genghost_driver_step(route, policy, driver, powergear_enabled,
										 collision_enabled, owoot);
}
