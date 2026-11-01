#include "shape3d_hires.h"

#if defined(RESTUNTS_SDL3)

#include <SDL3/SDL_stdinc.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>
#include "../fatal.h"
#include "hires.h"
#include "hypervision.h"
#include "../math/projection.h"
#include "render_workers.h"
#include "../shape3d/shape3d_internal.h"

#include "shape3d_hires_internal.h"

static void hires_shadow_world_vertices(const struct SHAPE3D_HIRES_VECTOR *vertices,
										legacy_u32 count, struct SHADOW_SURFACE *surface)
{
	for (legacy_u32 index = 0; index < count; index++) {
		const struct SHAPE3D_HIRES_VECTOR *view = &vertices[index];
		struct SHAPE3D_HIRES_VECTOR world = {
			shape3d_hires_state.shadow_inverse[0][0] * view->x +
				shape3d_hires_state.shadow_inverse[0][1] * view->y +
				shape3d_hires_state.shadow_inverse[0][2] * view->z,
			shape3d_hires_state.shadow_inverse[1][0] * view->x +
				shape3d_hires_state.shadow_inverse[1][1] * view->y +
				shape3d_hires_state.shadow_inverse[1][2] * view->z,
			shape3d_hires_state.shadow_inverse[2][0] * view->x +
				shape3d_hires_state.shadow_inverse[2][1] * view->y +
				shape3d_hires_state.shadow_inverse[2][2] * view->z};
		surface->vertices[index] = world;
		if (index == 0) {
			surface->minimum = surface->maximum = world;
		} else {
			surface->minimum.x = SDL_min(surface->minimum.x, world.x);
			surface->minimum.y = SDL_min(surface->minimum.y, world.y);
			surface->minimum.z = SDL_min(surface->minimum.z, world.z);
			surface->maximum.x = SDL_max(surface->maximum.x, world.x);
			surface->maximum.y = SDL_max(surface->maximum.y, world.y);
			surface->maximum.z = SDL_max(surface->maximum.z, world.z);
		}
	}
	surface->cars = 0;
	for (legacy_s32 car = 0; car < shape3d_hires_state.car_shadow_count; car++) {
		const struct HIRES_CAR_SHADOW *shadow = &shape3d_hires_state.car_shadows[car];
		if (surface->maximum.x >= shadow->minimum.x && surface->minimum.x <= shadow->maximum.x &&
			surface->maximum.y >= shadow->minimum.y && surface->minimum.y <= shadow->maximum.y &&
			surface->maximum.z >= shadow->minimum.z && surface->minimum.z <= shadow->maximum.z) {
			surface->cars |= 1U << car;
		}
	}
}

legacy_s32 shape3d_hires_shadows_active(void)
{
	return shape3d_hires_state.car_shadow_count != 0;
}

legacy_s32 shape3d_hires_shadow_overlap(const struct SHAPE3D_HIRES_VECTOR *vertices,
										legacy_u32 count)
{
	if (shape3d_hires_state.car_shadow_count == 0 || count == 0 ||
		count > HIRES_SHADOW_SURFACE_MAX_POINTS) {
		return 0;
	}
	struct SHADOW_SURFACE surface;
	hires_shadow_world_vertices(vertices, count, &surface);
	return surface.cars != 0;
}

legacy_u32 shape3d_hires_shadow_polygon(const struct SHAPE3D_HIRES_VECTOR *vertices,
										legacy_u32 count, legacy_s32 grille)
{
	if (shape3d_hires_state.car_shadow_count == 0 || count < 3 ||
		count > HIRES_SHADOW_SURFACE_MAX_POINTS) {
		return 0;
	}
	struct SHADOW_SURFACE surface;
	hires_shadow_world_vertices(vertices, count, &surface);
	if (surface.cars == 0) {
		return 0;
	}
	/* Newell's normal also handles polygons with collinear leading vertices. */
	surface.normal = (struct SHAPE3D_HIRES_VECTOR){0, 0, 0};
	for (legacy_u32 index = 0; index < count; index++) {
		const struct SHAPE3D_HIRES_VECTOR *first = &surface.vertices[index];
		const struct SHAPE3D_HIRES_VECTOR *last = &surface.vertices[(index + 1) % count];
		surface.normal.x += (first->y - last->y) * (first->z + last->z);
		surface.normal.y += (first->z - last->z) * (first->x + last->x);
		surface.normal.z += (first->x - last->x) * (first->y + last->y);
	}
	legacy_f64 length =
		SDL_sqrt(surface.normal.x * surface.normal.x + surface.normal.y * surface.normal.y +
				 surface.normal.z * surface.normal.z);
	if (length < HIRES_SHADOW_INTERSECTION_EPSILON) {
		return 0;
	}
	surface.normal.x /= length;
	surface.normal.y /= length;
	surface.normal.z /= length;
	surface.distance = 0;
	for (legacy_u32 index = 0; index < count; index++) {
		surface.distance += surface.normal.x * surface.vertices[index].x +
							surface.normal.y * surface.vertices[index].y +
							surface.normal.z * surface.vertices[index].z;
	}
	surface.distance /= count;
	/* Drop the dominant normal coordinate for the point-in-polygon test. */
	surface.axis = shape3d_hires_absolute_coordinate(surface.normal.x) >
						   shape3d_hires_absolute_coordinate(surface.normal.y)
					   ? 0
					   : 1;
	if (shape3d_hires_absolute_coordinate(surface.normal.z) >
		(surface.axis == 0 ? shape3d_hires_absolute_coordinate(surface.normal.x)
						   : shape3d_hires_absolute_coordinate(surface.normal.y))) {
		surface.axis = 2;
	}
	if (surface.axis == 1) {
		/* A warped quad's fitted plane can extend outside its authored Y
		 * bounds. Include its projected corners when clipping intersections. */
		for (legacy_u32 index = 0; index < count; index++) {
			legacy_f64 y = (surface.distance - surface.normal.x * surface.vertices[index].x -
							surface.normal.z * surface.vertices[index].z) /
						   surface.normal.y;
			surface.minimum.y = SDL_min(surface.minimum.y, y);
			surface.maximum.y = SDL_max(surface.maximum.y, y);
		}
	}
	surface.count = count;
	surface.rendered_index = LEGACY_U32_MAX;
	surface.grille = grille;
	if (shape3d_hires_state.shadow_surface_count == shape3d_hires_state.shadow_surface_capacity) {
		size_t capacity = shape3d_hires_state.shadow_surface_capacity != 0
							  ? shape3d_hires_state.shadow_surface_capacity * 2
							  : HIRES_SHADOW_SURFACE_INITIAL_CAPACITY;
		/* Each plane intersects each of the two projection pieces at most twice. */
		if (capacity < shape3d_hires_state.shadow_surface_capacity || capacity > LEGACY_U32_MAX ||
			capacity > (size_t)-1 / sizeof(*shape3d_hires_state.shadow_surfaces) ||
			capacity > (size_t)-1 / (HIRES_SHADOW_SURFACE_MAX_HITS *
									 sizeof(*shape3d_hires_state.shadow_grille_hits))) {
			fatal_error("HyperVision shadow surfaces exceed addressable memory");
			return 0;
		}
		struct SHADOW_SURFACE *surfaces =
			realloc(shape3d_hires_state.shadow_surfaces, capacity * sizeof(*surfaces));
		legacy_f64 *hits = realloc(shape3d_hires_state.shadow_grille_hits,
								   capacity * HIRES_SHADOW_SURFACE_MAX_HITS * sizeof(*hits));
		if (surfaces == NULL || hits == NULL) {
			fatal_error("Cannot allocate HyperVision shadow surfaces");
			return 0;
		}
		shape3d_hires_state.shadow_surfaces = surfaces;
		shape3d_hires_state.shadow_grille_hits = hits;
		shape3d_hires_state.shadow_surface_capacity = capacity;
	}
	shape3d_hires_state.shadow_surfaces[shape3d_hires_state.shadow_surface_count++] = surface;
	for (legacy_s32 car = 0; car < shape3d_hires_state.car_shadow_count; car++) {
		if ((surface.cars & (1U << car)) != 0) {
			struct HIRES_CAR_SHADOW *shadow = &shape3d_hires_state.car_shadows[car];
			shadow->surface_top = SDL_max(shadow->surface_top, surface.maximum.y);
			if (grille) {
				shadow->grille_top = SDL_max(shadow->grille_top, surface.maximum.y);
			}
		}
	}
	return (legacy_u32)shape3d_hires_state.shadow_surface_count;
}

void shape3d_hires_set_shadow_surface(legacy_u32 index, legacy_u32 surface)
{
	if (index < shape3d_hires_state.primitive_count && surface != 0 &&
		surface <= shape3d_hires_state.shadow_surface_count) {
		shape3d_hires_state.primitives[index].shadow_surface = surface;
		shape3d_hires_state.shadow_surfaces[surface - 1].rendered_index = index;
	}
}

/* Sorted polygons identify their support directly. An ordered shape shares one
 * depth family, so resolve its support only among the few collected surfaces,
 * using the same scanline depth interpolation as the scene rasterizer. */
static legacy_u32 hires_shadow_receiver_surface(legacy_u32 family)
{
	return family != 0U && family <= shape3d_hires_state.primitive_count
			   ? shape3d_hires_state.primitives[family - 1U].shadow_surface
			   : 0U;
}

static legacy_s32 hires_shadow_surface_contains(const struct SHADOW_SURFACE *surface, legacy_f64 x,
												legacy_f64 y, legacy_f64 z)
{
	if (x < surface->minimum.x - HIRES_SHADOW_INTERSECTION_EPSILON ||
		x > surface->maximum.x + HIRES_SHADOW_INTERSECTION_EPSILON ||
		z < surface->minimum.z - HIRES_SHADOW_INTERSECTION_EPSILON ||
		z > surface->maximum.z + HIRES_SHADOW_INTERSECTION_EPSILON) {
		return 0;
	}
	legacy_f64 u = surface->axis == 0 ? y : x;
	legacy_f64 v = surface->axis == 2 ? y : z;
	legacy_s32 inside = 0;
	for (legacy_u32 index = 0; index < surface->count; index++) {
		const struct SHAPE3D_HIRES_VECTOR *first = &surface->vertices[index];
		const struct SHAPE3D_HIRES_VECTOR *last = &surface->vertices[(index + 1) % surface->count];
		legacy_f64 first_u = surface->axis == 0 ? first->y : first->x;
		legacy_f64 first_v = surface->axis == 2 ? first->y : first->z;
		legacy_f64 last_u = surface->axis == 0 ? last->y : last->x;
		legacy_f64 last_v = surface->axis == 2 ? last->y : last->z;
		legacy_f64 cross = (last_u - first_u) * (v - first_v) - (last_v - first_v) * (u - first_u);
		if (shape3d_hires_absolute_coordinate(cross) < HIRES_SHADOW_INTERSECTION_EPSILON &&
			u >= SDL_min(first_u, last_u) && u <= SDL_max(first_u, last_u) &&
			v >= SDL_min(first_v, last_v) && v <= SDL_max(first_v, last_v)) {
			return 1;
		}
		if ((first_v > v) != (last_v > v) && ((cross > 0) == (last_v > first_v))) {
			inside = !inside;
		}
	}
	return inside;
}

struct SHADOW_TRACE {
	const struct HIRES_CAR_SHADOW *shadow;
	legacy_f64 source_x, source_z, gap, transmission;
	size_t grille_count;
	legacy_s32 solid_receiver, grille_receiver;
};

static legacy_s32 hires_shadow_surface_hit(const struct SHADOW_SURFACE *surface,
										   struct SHADOW_TRACE *trace, legacy_f64 gap,
										   legacy_f64 first, legacy_f64 last)
{
	if (gap < first - HIRES_SHADOW_INTERSECTION_EPSILON ||
		gap > last + HIRES_SHADOW_INTERSECTION_EPSILON) {
		return 0;
	}
	const struct HIRES_CAR_SHADOW *shadow = trace->shadow;
	legacy_f64 shrink = 1 + gap / HIRES_CAR_SHADOW_SHRINK_HEIGHT;
	legacy_f64 offset = shape3d_hires_shadow_light_offset(shadow, gap);
	legacy_f64 x =
		shadow->position.x + offset * shape3d_hires_state.shadow_light_x + trace->source_x / shrink;
	legacy_f64 z =
		shadow->position.z + offset * shape3d_hires_state.shadow_light_z + trace->source_z / shrink;
	if (!hires_shadow_surface_contains(surface, x, shadow->position.y - gap, z)) {
		return 0;
	}
	if (gap >= trace->gap - HIRES_CAR_SHADOW_RECEIVER_TOLERANCE) {
		if (surface->grille) {
			trace->grille_receiver = 1;
		} else {
			trace->solid_receiver = 1;
		}
		return 0;
	}
	if (!surface->grille) {
		return 1;
	}
	/* Mesh triangles, reversed faces and coplanar panels form one layer. */
	for (size_t index = 0; index < trace->grille_count; index++) {
		if (shape3d_hires_absolute_coordinate(shape3d_hires_state.shadow_grille_hits[index] -
											  gap) <= HIRES_CAR_SHADOW_RECEIVER_TOLERANCE) {
			return 0;
		}
	}
	shape3d_hires_state.shadow_grille_hits[trace->grille_count++] = gap;
	trace->transmission *= HIRES_SHADOW_GRILLE_TRANSMISSION;
	return 0;
}

static legacy_s32 hires_shadow_surface_segment(const struct SHADOW_SURFACE *surface,
											   struct SHADOW_TRACE *trace, legacy_f64 first,
											   legacy_f64 last, legacy_f64 offset, legacy_f64 slope)
{
	const struct HIRES_CAR_SHADOW *shadow = trace->shadow;
	const struct SHAPE3D_HIRES_VECTOR *normal = &surface->normal;
	legacy_f64 light_normal = normal->x * shape3d_hires_state.shadow_light_x +
							  normal->z * shape3d_hires_state.shadow_light_z;
	legacy_f64 constant =
		normal->x * (shadow->position.x + shape3d_hires_state.shadow_light_x * offset) +
		normal->y * shadow->position.y +
		normal->z * (shadow->position.z + shape3d_hires_state.shadow_light_z * offset) -
		surface->distance;
	legacy_f64 linear = light_normal * slope - normal->y;
	/* Follow the existing shrinking footprint, including the directional offset
	 * clamp. Multiplying the plane equation by (1 + gap / height) yields a
	 * quadratic, so ramps and road edges need no marching or shadow map. */
	legacy_f64 a = linear / HIRES_CAR_SHADOW_SHRINK_HEIGHT;
	legacy_f64 b = linear + constant / HIRES_CAR_SHADOW_SHRINK_HEIGHT;
	legacy_f64 c = constant + normal->x * trace->source_x + normal->z * trace->source_z;
	if (shape3d_hires_absolute_coordinate(a) < HIRES_SHADOW_INTERSECTION_EPSILON) {
		return shape3d_hires_absolute_coordinate(b) >= HIRES_SHADOW_INTERSECTION_EPSILON &&
			   hires_shadow_surface_hit(surface, trace, -c / b, first, last);
	}
	legacy_f64 discriminant = b * b - 4 * a * c;
	if (discriminant < 0) {
		return 0;
	}
	legacy_f64 root = SDL_sqrt(discriminant);
	legacy_f64 q = -0.5 * (b + (b < 0 ? -root : root));
	if (q == 0) {
		return hires_shadow_surface_hit(surface, trace, -b / (2 * a), first, last);
	}
	return hires_shadow_surface_hit(surface, trace, q / a, first, last) ||
		   hires_shadow_surface_hit(surface, trace, c / q, first, last);
}

static legacy_s32 hires_shadow_occlusion_needed(const struct HIRES_CAR_SHADOW *shadow, legacy_f64 y)
{
	/* Normal driving on an opaque surface needs no intersection work. */
	return shape3d_hires_state.shadow_surface_count != 0 &&
		   (y < shadow->surface_top - HIRES_CAR_SHADOW_RECEIVER_TOLERANCE ||
			y <= shadow->grille_top + HIRES_CAR_SHADOW_RECEIVER_TOLERANCE);
}

static legacy_f64 hires_shadow_transmission(const struct HIRES_CAR_SHADOW *shadow, legacy_f64 x,
											legacy_f64 y, legacy_f64 z, legacy_u32 receiver)
{

	legacy_f64 gap = SDL_max(0, shadow->position.y - y);
	legacy_f64 shrink = 1 + gap / HIRES_CAR_SHADOW_SHRINK_HEIGHT;
	legacy_f64 displacement = shape3d_hires_shadow_light_offset(shadow, gap);
	struct SHADOW_TRACE trace = {
		shadow,
		(x - shadow->position.x - displacement * shape3d_hires_state.shadow_light_x) * shrink,
		(z - shadow->position.z - displacement * shape3d_hires_state.shadow_light_z) * shrink,
		gap,
		1,
		0,
		0,
		0};
	legacy_f64 limit =
		SDL_min(shadow->half_width, shadow->half_length) * HIRES_CAR_SHADOW_OFFSET_LIMIT_SCALE;
	legacy_f64 offset =
		shadow->height * HIRES_CAR_SHADOW_HEIGHT_SCALE * shape3d_hires_state.shadow_light_cotangent;
	legacy_f64 turn =
		shape3d_hires_state.shadow_light_cotangent > 0
			? SDL_max(0, (limit - offset) / shape3d_hires_state.shadow_light_cotangent)
			: DBL_MAX;
	legacy_u32 car = 1U << (shadow - shape3d_hires_state.car_shadows);
	for (size_t index = 0; index < shape3d_hires_state.shadow_surface_count; index++) {
		const struct SHADOW_SURFACE *surface = &shape3d_hires_state.shadow_surfaces[index];
		/* A warped road quad's rasterized depth need not lie on its Newell
		 * plane. Its identity prevents that approximation from self-blocking. */
		if (index + 1 == receiver) {
			trace.grille_receiver |= surface->grille;
			trace.solid_receiver |= !surface->grille;
			continue;
		}
		if ((surface->cars & car) == 0 ||
			surface->maximum.y < y - HIRES_CAR_SHADOW_RECEIVER_TOLERANCE) {
			continue;
		}
		legacy_f64 first =
			SDL_max(-HIRES_CAR_SHADOW_RECEIVER_TOLERANCE, shadow->position.y - surface->maximum.y);
		legacy_f64 last = SDL_min(gap + HIRES_CAR_SHADOW_RECEIVER_TOLERANCE,
								  shadow->position.y - surface->minimum.y);
		if (first > last) {
			continue;
		}
		/* Flat decks dominate normal driving; their intersection needs no roots. */
		if (shape3d_hires_absolute_coordinate(surface->normal.x) <
				HIRES_SHADOW_INTERSECTION_EPSILON &&
			shape3d_hires_absolute_coordinate(surface->normal.z) <
				HIRES_SHADOW_INTERSECTION_EPSILON) {
			if (hires_shadow_surface_hit(
					surface, &trace, shadow->position.y - surface->vertices[0].y, first, last)) {
				return 0;
			}
		} else if ((first < turn && hires_shadow_surface_segment(
										surface, &trace, first, SDL_min(last, turn), offset,
										shape3d_hires_state.shadow_light_cotangent)) ||
				   (last >= turn && hires_shadow_surface_segment(
										surface, &trace, SDL_max(first, turn), last, limit, 0))) {
			return 0;
		}
	}
	return trace.transmission *
		   (trace.grille_receiver && !trace.solid_receiver ? HIRES_SHADOW_GRILLE_TRANSMISSION : 1);
}

static struct RECTANGLE hires_shadow_bounds(const struct HIRES_CAR_SHADOW *shadow)
{
	legacy_s32 width = hires_render_width();
	legacy_s32 height = hires_render_height();
	struct RECTANGLE bounds = {width, 0, height, 0};
	/* The flat ground bounds the bottom of the small receiving volume. */
	legacy_f64 bottom =
		shadow->projected ? shadow->position.y : shadow->position.y - HIRES_CAR_SHADOW_REACH;
	if (bottom < shape3d_hires_state.shadow_ground_y) {
		bottom = shape3d_hires_state.shadow_ground_y;
	}
	legacy_f64 extension = shape3d_hires_shadow_light_offset(shadow, HIRES_CAR_SHADOW_REACH);
	legacy_f64 extend_x = extension * (shape3d_hires_state.shadow_light_x * shadow->cosine -
									   shape3d_hires_state.shadow_light_z * shadow->sine);
	legacy_f64 extend_z = extension * (shape3d_hires_state.shadow_light_x * shadow->sine +
									   shape3d_hires_state.shadow_light_z * shadow->cosine);
	struct SHAPE3D_HIRES_VECTOR vertices[HIRES_CAR_SHADOW_BOUNDS_CORNERS];
	for (legacy_s32 corner = 0; corner < HIRES_CAR_SHADOW_BOUNDS_CORNERS; corner++) {
		legacy_f64 x = (corner & HIRES_CAR_SHADOW_CORNER_MAX_X)
						   ? shadow->max_x + SDL_max(0, extend_x)
						   : shadow->min_x + SDL_min(0, extend_x);
		legacy_f64 z = (corner & HIRES_CAR_SHADOW_CORNER_MAX_Z)
						   ? shadow->max_z + SDL_max(0, extend_z)
						   : shadow->min_z + SDL_min(0, extend_z);
		legacy_f64 world_x = shadow->position.x + x * shadow->cosine + z * shadow->sine;
		legacy_f64 world_y =
			(corner & HIRES_CAR_SHADOW_CORNER_BOTTOM) ? bottom : shadow->position.y;
		legacy_f64 world_z = shadow->position.z - x * shadow->sine + z * shadow->cosine;
		struct SHAPE3D_HIRES_VECTOR view = {(world_x * shape3d_hires_state.shadow_view.m._11 +
											 world_y * shape3d_hires_state.shadow_view.m._12 +
											 world_z * shape3d_hires_state.shadow_view.m._13) /
												TRIG_FIXED_ONE,
											(world_x * shape3d_hires_state.shadow_view.m._21 +
											 world_y * shape3d_hires_state.shadow_view.m._22 +
											 world_z * shape3d_hires_state.shadow_view.m._23) /
												TRIG_FIXED_ONE,
											(world_x * shape3d_hires_state.shadow_view.m._31 +
											 world_y * shape3d_hires_state.shadow_view.m._32 +
											 world_z * shape3d_hires_state.shadow_view.m._33) /
												TRIG_FIXED_ONE};
		vertices[corner] = view;
	}
	/* Clip the twelve prism edges, including near-camera cars, rather than
	 * turning a single behind-camera corner into a full-screen scan. */
	struct SHAPE3D_HIRES_POINT
		points[HIRES_CAR_SHADOW_BOUNDS_CORNERS + HIRES_CAR_SHADOW_BOUNDS_EDGES];
	legacy_s32 count = 0;
	for (legacy_s32 corner = 0; corner < HIRES_CAR_SHADOW_BOUNDS_CORNERS; corner++) {
		if (vertices[corner].z >= HIRES_NEAR_CLIP_Z) {
			shape3d_hires_project(&vertices[corner], &points[count++]);
		}
		for (legacy_s32 edge = HIRES_CAR_SHADOW_CORNER_MAX_X;
			 edge <= HIRES_CAR_SHADOW_CORNER_BOTTOM; edge *= 2) {
			if ((corner & edge) == 0 && ((vertices[corner].z < HIRES_NEAR_CLIP_Z) !=
										 (vertices[corner | edge].z < HIRES_NEAR_CLIP_Z))) {
				shape3d_hires_project_intersection(&vertices[corner], &vertices[corner | edge],
												   &points[count++]);
			}
		}
	}
	for (legacy_s32 index = 0; index < count; index++) {
		struct SHAPE3D_HIRES_POINT point = points[index];
		legacy_s16 px = (legacy_s16)(point.x < 0 ? 0 : point.x > width ? width : point.x);
		legacy_s16 py = (legacy_s16)(point.y < 0 ? 0 : point.y > height ? height : point.y);
		if (px < bounds.left) {
			bounds.left = px;
		}
		if (px + 1 > bounds.right) {
			bounds.right = px + 1;
		}
		if (py < bounds.top) {
			bounds.top = py;
		}
		if (py + 1 > bounds.bottom) {
			bounds.bottom = py + 1;
		}
	}
	return bounds;
}

static legacy_u8 hires_shadow_opacity(const struct HIRES_CAR_SHADOW *shadow, legacy_f64 x,
									  legacy_f64 y, legacy_f64 z)
{
	legacy_f64 gap = shadow->position.y - y;
	if (shadow->projected) {
		/* This mask is already cast onto the registered flat contact plane. */
		if (shape3d_hires_absolute_coordinate(gap) > HIRES_CAR_SHADOW_RECEIVER_TOLERANCE) {
			return 0;
		}
		gap = 0;
	}
	if (gap < -HIRES_CAR_SHADOW_RECEIVER_TOLERANCE || gap >= HIRES_CAR_SHADOW_REACH) {
		return 0;
	}
	if (gap < 0) {
		gap = 0;
	}
	legacy_f64 offset = shape3d_hires_shadow_light_offset(shadow, gap);
	x -= shadow->position.x + offset * shape3d_hires_state.shadow_light_x;
	z -= shadow->position.z + offset * shape3d_hires_state.shadow_light_z;
	legacy_f64 shrink = 1 + gap / HIRES_CAR_SHADOW_SHRINK_HEIGHT;
	legacy_f64 u = (x * shadow->cosine - z * shadow->sine) * shrink;
	legacy_f64 v = (x * shadow->sine + z * shadow->cosine) * shrink;
	legacy_f64 coverage;
	if (shadow->silhouette != NULL) {
		coverage = shape3d_hires_shadow_silhouette_sample(shadow->silhouette, u, v);
	} else {
		/* Retain a modest footprint if a custom model has no usable geometry. */
		u = shape3d_hires_absolute_coordinate(u / shadow->half_width);
		v = shape3d_hires_absolute_coordinate(v / shadow->half_length);
		legacy_f64 contour = SDL_max(SDL_max(u, v), (u + v) / HIRES_CAR_SHADOW_FALLBACK_DIAGONAL);
		coverage = SDL_min(1, (1 - contour) * HIRES_CAR_SHADOW_FALLBACK_EDGE_SCALE);
	}
	if (coverage <= 0) {
		return 0;
	}
	/* Fade the last part of the short reach so a high jump has no cutoff. */
	legacy_f64 fade = (HIRES_CAR_SHADOW_REACH - gap) / HIRES_CAR_SHADOW_FADE_HEIGHT;
	if (fade > 1) {
		fade = 1;
	}
	return (legacy_u8)(HIRES_CAR_SHADOW_MAX_OPACITY * coverage * fade);
}

void shape3d_hires_draw_shadows(void)
{
	legacy_s32 scale = hires_render_scale();
	legacy_s32 width = hires_render_width();
	if (!hires_enabled() || scale == HIRES_MINIMUM_SCALE ||
		shape3d_hires_state.car_shadow_count == 0 || projection_focal_length_x == 0 ||
		projection_focal_length_y == 0) {
		return;
	}
	struct HIRES_RASTER_TARGET target;
	if (!hires_raster_prepare(&target)) {
		return;
	}
	legacy_f64 focal_x = projection_focal_length_x * scale;
	legacy_f64 focal_y = projection_focal_length_y * scale;
	legacy_f64 ray_step[3];
	for (legacy_s32 axis = 0; axis < 3; axis++) {
		ray_step[axis] = shape3d_hires_state.shadow_inverse[axis][0] / focal_x;
	}
	legacy_s32 started = 0;
	for (legacy_s32 car = 0; car < shape3d_hires_state.car_shadow_count; car++) {
		const struct HIRES_CAR_SHADOW *shadow = &shape3d_hires_state.car_shadows[car];
		struct RECTANGLE bounds = hires_shadow_bounds(shadow);
		if (bounds.left < target.left) {
			bounds.left = (legacy_s16)target.left;
		}
		if (bounds.right > target.right) {
			bounds.right = (legacy_s16)target.right;
		}
		if (bounds.top < target.top) {
			bounds.top = (legacy_s16)target.top;
		}
		if (bounds.bottom > target.bottom) {
			bounds.bottom = (legacy_s16)target.bottom;
		}
		for (legacy_s32 y = bounds.top; y < bounds.bottom; y++) {
			legacy_f64 view_x =
				(bounds.left + 0.5 - (legacy_s16)projection_center_x * scale) / focal_x;
			legacy_f64 view_y = ((legacy_s16)projection_center_y * scale - y - 0.5) / focal_y;
			legacy_f64 ray[3];
			for (legacy_s32 axis = 0; axis < 3; axis++) {
				ray[axis] = shape3d_hires_state.shadow_inverse[axis][0] * view_x +
							shape3d_hires_state.shadow_inverse[axis][1] * view_y +
							shape3d_hires_state.shadow_inverse[axis][2];
			}
			for (legacy_s32 x = bounds.left; x < bounds.right; x++) {
				legacy_u32 family = 0;
				size_t pixel = (size_t)y * width + x;
				if (x >= target.depth_left && x < target.depth_right && y >= target.depth_top &&
					y < target.depth_bottom) {
					family = target.depth_family[pixel];
				}
				legacy_f64 depth = 0;
				if (family != 0) {
					if (family <= shape3d_hires_state.primitive_count &&
						shape3d_hires_state.shapes[shape3d_hires_state.primitives[family - 1].shape]
							.shadow_receiver) {
						depth = 1.0 / target.inverse_depth[pixel];
					}
				} else if (ray[1] < 0 && shape3d_hires_state.shadow_ground_y < 0) {
					/* Flat grass is the skybox's ground fill, not queued geometry. */
					depth = shape3d_hires_state.shadow_ground_y / ray[1];
				}
				if (depth >= HIRES_NEAR_CLIP_Z) {
					legacy_u8 opacity = hires_shadow_opacity(shadow, ray[0] * depth, ray[1] * depth,
															 ray[2] * depth);
					if (opacity != 0 && hires_shadow_occlusion_needed(shadow, ray[1] * depth)) {
						opacity = (legacy_u8)(opacity * hires_shadow_transmission(
															shadow, ray[0] * depth, ray[1] * depth,
															ray[2] * depth,
															hires_shadow_receiver_surface(family)));
					}
					if (opacity != 0) {
						if (!started) {
							if (!hires_shadow_begin()) {
								return;
							}
							started = 1;
						}
						hires_shadow_pixel(x, y, opacity);
					}
				}
				for (legacy_s32 axis = 0; axis < 3; axis++) {
					ray[axis] += ray_step[axis];
				}
			}
		}
	}
}

#endif
