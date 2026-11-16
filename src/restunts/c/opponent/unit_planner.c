#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "unit_planner_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#if !defined(__DJGPP__) && (!defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)) &&       \
	!defined(RESTUNTS_UNIT_SYNC_ONLY)
#define UNIT_PLANNER_THREADS 1
#include <SDL3/SDL_cpuinfo.h>
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_thread.h>
#if defined(__linux__) || defined(__ANDROID__)
#include <sched.h>
#include <stdio.h>
#endif
#if defined(__EMSCRIPTEN__)
#include <emscripten/threading.h>
#endif
#endif

#define UNIT_DEFAULT_PROFILE_LOOKAHEAD 1.0
#define UNIT_WORKER_SEED 0x554E4954U
#define UNIT_WORKER_SEED_STEP 2654435761U
#define UNIT_RECENT_FRAME_LIMIT 2U
#define UNIT_RECENT_POSE_DISTANCE 384.0
#define UNIT_SYNC_WORK_LIMIT 16U
#define UNIT_SYNC_CANDIDATE_STRIDE 5U
#define UNIT_BROWSER_WORKERS 7U
#define UNIT_NO_PLAYER_POSITION 1000000000.0

struct UNIT_WORKER {
	struct UNIT_PLANNER *planner;
	struct UNIT_EVALUATION evaluation;
	legacy_u32 seed, candidates;
	legacy_u8 evaluating;
#ifdef UNIT_PLANNER_THREADS
	SDL_Thread *thread;
#endif
};

struct UNIT_PLANNER {
	struct UNIT_ROUTE_POINT *points;
	struct UNIT_CAR_LIMITS limits;
	struct UNIT_PROFILE route_profile;
	struct UNIT_PLANNER_STATS stats;
	struct UNIT_REQUEST request, published_request;
	struct UNIT_ROLLOUT published;
	struct UNIT_WORKER synchronous, *workers;
	legacy_f64 route_score;
	legacy_u64 geometry_generation;
	legacy_u32 count, worker_count, request_serial, published_serial;
	legacy_u32 sync_candidate;
	legacy_s32 requested_workers;
	legacy_u8 owoot, startup, stopping, have_request, allow_threads, native_preview;
#ifdef UNIT_PLANNER_THREADS
	SDL_Mutex *mutex;
	SDL_Condition *condition;
#endif
};

/* Planner lifecycle and native preview branches belong to the game thread;
 * workers consume copied geometry but never create or replace a planner. */
static legacy_u64 unit_geometry_generation;

static void planner_lock(struct UNIT_PLANNER *planner)
{
#ifdef UNIT_PLANNER_THREADS
	if (planner->mutex) {
		SDL_LockMutex(planner->mutex);
	}
#else
	(void)planner;
#endif
}

static void planner_unlock(struct UNIT_PLANNER *planner)
{
#ifdef UNIT_PLANNER_THREADS
	if (planner->mutex) {
		SDL_UnlockMutex(planner->mutex);
	}
#else
	(void)planner;
#endif
}

static legacy_int finite_limits(const struct UNIT_CAR_LIMITS *limits)
{
	if (!limits || !isfinite(limits->max_speed) || limits->max_speed <= 0.0 ||
		!isfinite(limits->acceleration) || limits->acceleration <= 0.0 ||
		!isfinite(limits->braking) || limits->braking <= 0.0 ||
		!isfinite(limits->lateral_acceleration) || limits->lateral_acceleration <= 0.0 ||
		!isfinite(limits->half_width) || limits->half_width < 0.0 ||
		!isfinite(limits->half_length) || limits->half_length < 0.0 ||
		!isfinite(limits->wheelbase) || limits->wheelbase < 0.0) {
		return 0;
	}
	for (legacy_u32 surface = 0;
		 surface < sizeof(limits->surface_grip) / sizeof(limits->surface_grip[0]); surface++) {
		if (!isfinite(limits->surface_grip[surface]) || limits->surface_grip[surface] < 0.0) {
			return 0;
		}
	}
	return 1;
}

static legacy_int finite_vehicle(const struct UNIT_VEHICLE *vehicle)
{
	return vehicle && isfinite(vehicle->x) && isfinite(vehicle->y) && isfinite(vehicle->z) &&
		   isfinite(vehicle->yaw) && isfinite(vehicle->speed) && vehicle->speed >= 0.0 &&
		   isfinite(vehicle->vx) && isfinite(vehicle->vz) && isfinite(vehicle->half_width) &&
		   isfinite(vehicle->half_length) && vehicle->half_width >= 0.0 &&
		   vehicle->half_length >= 0.0 && isfinite(vehicle->steering) &&
		   isfinite(vehicle->steering_rate) && vehicle->steering_rate >= 0.0 &&
		   isfinite(vehicle->vy);
}

/* Work is deliberately resumable: a browser pump, stop request or startup
 * deadline never has to wait for a complete large track to be evaluated. */
static void startup_step(struct UNIT_WORKER *worker)
{
	struct UNIT_PLANNER *planner = worker->planner;
	if (!worker->evaluating) {
		struct UNIT_PROFILE profile;
		planner_lock(planner);
		profile = planner->route_profile;
		planner_unlock(planner);
		if (worker->candidates) {
			unit_planner_profile(&worker->seed, &profile);
		}
		unit_planner_evaluation_begin(&worker->evaluation, planner->count, &profile);
		worker->evaluating = 1U;
	}
	if (!unit_planner_evaluation_step(&worker->evaluation, planner->points, &planner->limits,
									  planner->owoot)) {
		return;
	}
	planner_lock(planner);
	planner->stats.route_candidates++;
	if (worker->evaluation.score < planner->route_score) {
		planner->route_score = worker->evaluation.score;
		planner->route_profile = worker->evaluation.profile;
	}
	/* The first usable opening is now the same verified geometric proposal
	 * as the complete route. Retain these counters for diagnostic clients. */
	planner->stats.opening_candidates = planner->stats.route_candidates;
	planner->stats.opening_ready = planner->stats.route_ready = 1U;
	planner_unlock(planner);
	worker->evaluating = 0U;
	worker->candidates++;
}

static void publish_rollout(struct UNIT_PLANNER *planner, const struct UNIT_REQUEST *request,
							legacy_u32 serial, const struct UNIT_ROLLOUT *result)
{
	planner_lock(planner);
	planner->stats.live_candidates++;
	if (serial == planner->request_serial &&
		(serial != planner->published_serial || result->score < planner->published.score)) {
		planner->published = *result;
		planner->published_request = *request;
		planner->published_serial = serial;
	}
	planner_unlock(planner);
}

#ifdef UNIT_PLANNER_THREADS
static legacy_int SDLCALL planner_worker(void *argument)
{
	struct UNIT_WORKER *worker = argument;
	struct UNIT_PLANNER *planner = worker->planner;
	legacy_u32 completed_serial = 0U;
	for (;;) {
		planner_lock(planner);
		while (!planner->startup && !planner->stopping &&
			   (!planner->have_request || completed_serial == planner->request_serial)) {
			SDL_WaitCondition(planner->condition, planner->mutex);
		}
		legacy_u8 stopping = planner->stopping;
		legacy_u8 startup = planner->startup;
		struct UNIT_REQUEST request = planner->request;
		struct UNIT_PROFILE profile = planner->route_profile;
		legacy_u32 serial = planner->request_serial;
		planner_unlock(planner);
		if (stopping) {
			return 0;
		}
		if (startup) {
			startup_step(worker);
			continue;
		}
		for (legacy_u32 candidate = 0U; candidate < UNIT_PLANNER_LIVE_CANDIDATES; candidate++) {
			struct UNIT_ROLLOUT result;
			if (unit_planner_rollout(planner->points, planner->count, &planner->limits,
									 planner->owoot, &profile, &request, candidate, &result)) {
				publish_rollout(planner, &request, serial, &result);
			}
			planner_lock(planner);
			legacy_u8 stale = planner->stopping || serial != planner->request_serial;
			planner_unlock(planner);
			if (stale) {
				break;
			}
		}
		completed_serial = serial;
	}
}
#endif

static void stop_workers(struct UNIT_PLANNER *planner)
{
#ifdef UNIT_PLANNER_THREADS
	planner_lock(planner);
	planner->stopping = 1U;
	if (planner->condition) {
		SDL_BroadcastCondition(planner->condition);
	}
	planner_unlock(planner);
	for (legacy_u32 index = 0; index < planner->worker_count; index++) {
		SDL_WaitThread(planner->workers[index].thread, NULL);
		free(planner->workers[index].evaluation.speeds);
	}
#endif
	free(planner->workers);
	planner->workers = NULL;
	planner->worker_count = 0U;
	planner->stats.workers = 0U;
	planner->stopping = 0U;
}

static void start_workers(struct UNIT_PLANNER *planner, legacy_u32 count)
{
#ifdef UNIT_PLANNER_THREADS
	if (!count || !planner->allow_threads) {
		return;
	}
	planner->workers = calloc(count, sizeof(*planner->workers));
	if (!planner->workers) {
		return;
	}
	for (legacy_u32 index = 0; index < count; index++) {
		struct UNIT_WORKER *worker = &planner->workers[index];
		worker->planner = planner;
		worker->seed = UNIT_WORKER_SEED + index * UNIT_WORKER_SEED_STEP;
		if (planner->startup) {
			worker->evaluation.speeds = malloc(planner->count * sizeof(*worker->evaluation.speeds));
			if (!worker->evaluation.speeds) {
				break;
			}
		}
		worker->thread = SDL_CreateThread(planner_worker, "UNIT planner", worker);
		if (!worker->thread) {
			free(worker->evaluation.speeds);
			worker->evaluation.speeds = NULL;
			break;
		}
		planner->worker_count++;
	}
	if (!planner->worker_count) {
		/* A depleted browser pool or thread allocation failure leaves the
		 * bounded synchronous fallback active. Resume may retry later, so no
		 * unused allocation may survive and be overwritten by that retry. */
		free(planner->workers);
		planner->workers = NULL;
	}
	planner->stats.workers = planner->worker_count;
#else
	(void)planner;
	(void)count;
#endif
}

#ifdef UNIT_PLANNER_THREADS
static legacy_u32 available_workers(void)
{
	legacy_s32 detected = SDL_GetNumLogicalCPUCores();
	legacy_u32 count = detected > 0 ? (legacy_u32)detected : 1U;
#if defined(__linux__) || defined(__ANDROID__)
	cpu_set_t affinity;
	if (!sched_getaffinity(0, sizeof(affinity), &affinity)) {
		legacy_u32 allowed = (legacy_u32)CPU_COUNT(&affinity);
		if (allowed && allowed < count) {
			count = allowed;
		}
	}
	FILE *file = fopen("/sys/fs/cgroup/cpu.max", "r");
	legacy_u64 quota = 0U, period = 0U;
	if (file) {
		if (fscanf(file, "%" LEGACY_PRIu64 " %" LEGACY_PRIu64, &quota, &period) != 2) {
			quota = period = 0U;
		}
		fclose(file);
	}
	if (quota && period) {
		legacy_u64 available = quota / period + (quota % period != 0U);
		if (available < count) {
			count = (legacy_u32)available;
		}
	}
#endif
	return count;
}
#endif

static legacy_int same_route_point(const struct UNIT_ROUTE_POINT *a,
								   const struct UNIT_ROUTE_POINT *b)
{
	return a->x == b->x && a->y == b->y && a->z == b->z && a->route_index == b->route_index &&
		   a->route_point == b->route_point && a->half_width == b->half_width &&
		   a->surface == b->surface && a->stunt == b->stunt &&
		   a->banked_corner == b->banked_corner && a->clearance_only == b->clearance_only &&
		   a->speed_limit == b->speed_limit && a->speed_floor == b->speed_floor &&
		   a->fallback_speed_limit == b->fallback_speed_limit &&
		   a->shortcut_safe == b->shortcut_safe && a->height_unspecified == b->height_unspecified &&
		   a->align_center == b->align_center;
}

static struct UNIT_PLANNER *planner_create(const struct UNIT_ROUTE_POINT *points, legacy_u32 count,
										   const struct UNIT_CAR_LIMITS *limits, legacy_u8 owoot,
										   legacy_s32 requested_workers, struct UNIT_PLANNER *seed)
{
	if (!points || count < 2U || count > UNIT_PLANNER_ROUTE_LIMIT || !finite_limits(limits)) {
		return NULL;
	}
	for (legacy_u32 index = 0; index < count; index++) {
		if (!isfinite(points[index].x) || !isfinite(points[index].y) ||
			!isfinite(points[index].z) || !isfinite(points[index].half_width) ||
			points[index].half_width < 0.0 || !isfinite(points[index].speed_limit) ||
			points[index].speed_limit < 0.0 || !isfinite(points[index].speed_floor) ||
			points[index].speed_floor < 0.0 || !isfinite(points[index].fallback_speed_limit) ||
			points[index].fallback_speed_limit < 0.0 || points[index].stunt > UNIT_STUNT_MAX) {
			return NULL;
		}
	}
	struct UNIT_PLANNER *planner = calloc(1U, sizeof(*planner));
	if (!planner) {
		return NULL;
	}
	planner->points = malloc(count * sizeof(*planner->points));
	planner->synchronous.evaluation.speeds = malloc(count * sizeof(legacy_f64));
	if (!planner->points || !planner->synchronous.evaluation.speeds) {
		unit_planner_destroy(planner);
		return NULL;
	}
	memcpy(planner->points, points, count * sizeof(*points));
	planner->geometry_generation = ++unit_geometry_generation;
	if (!planner->geometry_generation) {
		planner->geometry_generation = ++unit_geometry_generation;
	}
	planner->limits = *limits;
	planner->count = count;
	planner->owoot = !!owoot;
	planner->startup = 1U;
	planner->requested_workers = requested_workers;
	planner->route_score = HUGE_VAL;
	planner->route_profile.lookahead = UNIT_DEFAULT_PROFILE_LOOKAHEAD;
	if (seed) {
		legacy_u8 same_route = count == seed->count;
		for (legacy_u32 index = 0U; same_route && index < count; index++) {
			same_route = same_route_point(&planner->points[index], &seed->points[index]);
		}
		planner_lock(seed);
		planner->route_profile = seed->route_profile;
		if (same_route && seed->stats.route_ready) {
			planner->stats.opening_ready = planner->stats.route_ready = 1U;
			planner->stats.opening_candidates = seed->stats.opening_candidates;
			planner->stats.route_candidates = seed->stats.route_candidates;
			planner->route_score = seed->route_score;
		}
		planner_unlock(seed);
	}
	planner->synchronous.planner = planner;
	planner->synchronous.seed = UNIT_WORKER_SEED;
	legacy_u32 workers = 0U;
#ifdef UNIT_PLANNER_THREADS
	planner->mutex = SDL_CreateMutex();
	planner->condition = SDL_CreateCondition();
	planner->allow_threads = requested_workers >= 0 && planner->mutex && planner->condition;
#if defined(__EMSCRIPTEN__)
	planner->allow_threads = planner->allow_threads && emscripten_has_threading_support();
#endif
	if (planner->allow_threads) {
		workers = requested_workers > 0 ? (legacy_u32)requested_workers : available_workers();
		if (workers > UNIT_PLANNER_MAX_WORKERS) {
			workers = UNIT_PLANNER_MAX_WORKERS;
		}
#if defined(__EMSCRIPTEN__)
		if (workers > UNIT_BROWSER_WORKERS) {
			workers = UNIT_BROWSER_WORKERS;
		}
#endif
		legacy_u32 memory_limit = UNIT_PLANNER_WORKER_MEMORY / (count * sizeof(legacy_f64));
		if (workers > memory_limit) {
			workers = memory_limit;
		}
	}
#else
	(void)requested_workers;
#endif
	if (seed) {
		stop_workers(seed);
	}
	start_workers(planner, workers);
	return planner;
}

struct UNIT_PLANNER *unit_planner_create(const struct UNIT_ROUTE_POINT *points, legacy_u32 count,
										 const struct UNIT_CAR_LIMITS *limits, legacy_u8 owoot,
										 legacy_s32 requested_workers)
{
	return planner_create(points, count, limits, owoot, requested_workers, NULL);
}

struct UNIT_PLANNER *unit_planner_clone_sync(struct UNIT_PLANNER *planner)
{
	if (!planner) {
		return NULL;
	}
	struct UNIT_PLANNER *clone =
		planner_create(planner->points, planner->count, &planner->limits, planner->owoot, -1, NULL);
	if (!clone) {
		return NULL;
	}
	planner_lock(planner);
	clone->geometry_generation = planner->geometry_generation;
	clone->route_profile = planner->route_profile;
	clone->stats.opening_ready = planner->stats.opening_ready;
	clone->stats.route_ready = planner->stats.route_ready;
	planner_unlock(planner);
	clone->startup = 0U;
	clone->native_preview = 1U;
	return clone;
}

struct UNIT_PLANNER *unit_planner_clone_continuation(struct UNIT_PLANNER *planner)
{
	if (!planner || planner->startup || planner->worker_count) {
		return NULL;
	}
	struct UNIT_PLANNER *clone = unit_planner_clone_sync(planner);
	if (!clone) {
		return NULL;
	}
	if (!unit_planner_restore_continuation(clone, planner)) {
		unit_planner_destroy(clone);
		return NULL;
	}
	return clone;
}

legacy_int unit_planner_restore_continuation(struct UNIT_PLANNER *destination,
											 struct UNIT_PLANNER *source)
{
	if (!destination || !source || destination->startup || source->startup ||
		destination->worker_count || source->worker_count ||
		destination->geometry_generation != source->geometry_generation ||
		destination->count != source->count || destination->owoot != source->owoot) {
		return 0;
	}
	if (destination == source) {
		return 1;
	}
	/* Both owners are paused. Clone identity proves geometry/car/rule equality
	 * without rescanning an entire large track inside the native tick budget. */
	destination->route_profile = source->route_profile;
	destination->sync_candidate = source->sync_candidate;
	destination->request = source->request;
	destination->published_request = source->published_request;
	destination->published = source->published;
	destination->request_serial = source->request_serial;
	destination->published_serial = source->published_serial;
	destination->have_request = source->have_request;
	destination->stats = source->stats;
	return 1;
}

legacy_int unit_planner_adopt_profiles(struct UNIT_PLANNER *planner, struct UNIT_PLANNER *validated)
{
	if (!planner || !validated || planner->have_request || planner->count != validated->count ||
		planner->owoot != validated->owoot ||
		memcmp(&planner->limits, &validated->limits, sizeof(planner->limits))) {
		return 0;
	}
	for (legacy_u32 index = 0U; index < planner->count; index++) {
		if (!same_route_point(&planner->points[index], &validated->points[index])) {
			return 0;
		}
	}
	unit_planner_finish_startup(planner);
	planner_lock(validated);
	struct UNIT_PROFILE route = validated->route_profile;
	legacy_u8 opening_ready = validated->stats.opening_ready,
			  route_ready = validated->stats.route_ready;
	planner_unlock(validated);
	planner_lock(planner);
	planner->route_profile = route;
	planner->stats.opening_ready = opening_ready;
	planner->stats.route_ready = route_ready;
	planner_unlock(planner);
	return 1;
}

struct UNIT_PLANNER *unit_planner_rebuild(struct UNIT_PLANNER *planner,
										  const struct UNIT_ROUTE_POINT *points, legacy_u32 count)
{
	if (!planner || !planner->startup) {
		return NULL;
	}
	struct UNIT_PLANNER *replacement = planner_create(
		points, count, &planner->limits, planner->owoot, planner->requested_workers, planner);
	if (replacement) {
		unit_planner_destroy(planner);
	}
	return replacement;
}

void unit_planner_destroy(struct UNIT_PLANNER *planner)
{
	if (!planner) {
		return;
	}
	stop_workers(planner);
#ifdef UNIT_PLANNER_THREADS
	if (planner->condition) {
		SDL_DestroyCondition(planner->condition);
	}
	if (planner->mutex) {
		SDL_DestroyMutex(planner->mutex);
	}
#endif
	free(planner->synchronous.evaluation.speeds);
	free(planner->points);
	free(planner);
}

void unit_planner_finish_startup(struct UNIT_PLANNER *planner)
{
	if (!planner || !planner->startup) {
		return;
	}
	stop_workers(planner);
	planner->startup = 0U;
	start_workers(planner, 1U);
}

legacy_int unit_planner_pause_live(struct UNIT_PLANNER *planner)
{
	if (!planner || planner->startup) {
		return 0;
	}
	stop_workers(planner);
	return 1;
}

legacy_int unit_planner_resume_live(struct UNIT_PLANNER *planner)
{
	if (!planner || planner->startup) {
		return 0;
	}
	if (!planner->worker_count) {
		start_workers(planner, 1U);
	}
	return 1;
}

void unit_planner_work(struct UNIT_PLANNER *planner, legacy_u32 work_units)
{
	if (!planner || planner->worker_count || !planner->startup) {
		return;
	}
	if (work_units > UNIT_SYNC_WORK_LIMIT) {
		work_units = UNIT_SYNC_WORK_LIMIT;
	}
	for (legacy_u32 work = 0; work < work_units; work++) {
		startup_step(&planner->synchronous);
	}
}

legacy_int unit_planner_plan(struct UNIT_PLANNER *planner, const struct UNIT_VEHICLE *unit,
							 const struct UNIT_VEHICLE *player, legacy_u32 route_cursor,
							 legacy_u32 generation, struct UNIT_PLAN *plan)
{
	if (!planner || !plan || !finite_vehicle(unit) || (player && !finite_vehicle(player)) ||
		route_cursor >= planner->count) {
		return 0;
	}
	struct UNIT_REQUEST request = {0};
	request.unit = *unit;
	if (player) {
		request.player = *player;
	} else {
		request.player.x = UNIT_NO_PLAYER_POSITION;
	}
	request.cursor = route_cursor;
	request.generation = generation;
	request.native_preview = planner->native_preview;
	planner_lock(planner);
	struct UNIT_PROFILE profile = planner->route_profile;
	struct UNIT_ROLLOUT published = planner->published;
	struct UNIT_REQUEST old_request = planner->published_request;
	legacy_u8 have_published = planner->published_serial != 0U;
	planner->request = request;
	planner->have_request = 1U;
	legacy_u32 serial = ++planner->request_serial;
	if (!serial) {
		serial = ++planner->request_serial;
		planner->published_serial = 0U;
	}
#ifdef UNIT_PLANNER_THREADS
	if (planner->condition) {
		SDL_SignalCondition(planner->condition);
	}
#endif
	planner_unlock(planner);
	struct UNIT_ROLLOUT best;
	if (!unit_planner_rollout(planner->points, planner->count, &planner->limits, planner->owoot,
							  &profile, &request, 0U, &best)) {
		return 0;
	}
	legacy_u32 budget = planner->worker_count ? 0U : UNIT_PLANNER_SYNC_CANDIDATES;
	for (legacy_u32 work = 0; work < budget; work++) {
		legacy_u32 candidate = 1U + (planner->sync_candidate++ * UNIT_SYNC_CANDIDATE_STRIDE) %
										(UNIT_PLANNER_LIVE_CANDIDATES - 1U);
		struct UNIT_ROLLOUT result;
		if (unit_planner_rollout(planner->points, planner->count, &planner->limits, planner->owoot,
								 &profile, &request, candidate, &result)) {
			publish_rollout(planner, &request, serial, &result);
			if (result.score < best.score) {
				best = result;
			}
		}
	}
	legacy_f64 dx = old_request.unit.x - unit->x, dz = old_request.unit.z - unit->z;
	legacy_u32 cursor_distance = old_request.cursor > route_cursor
									 ? old_request.cursor - route_cursor
									 : route_cursor - old_request.cursor;
	legacy_u8 same_neighborhood = old_request.cursor < planner->count &&
								  cursor_distance < UNIT_PLANNER_LOCAL_POINTS &&
								  planner->points[old_request.cursor].route_index ==
									  planner->points[route_cursor].route_index;
	if (have_published && generation >= old_request.generation &&
		generation - old_request.generation <= UNIT_RECENT_FRAME_LIMIT && same_neighborhood &&
		dx * dx + dz * dz <= UNIT_RECENT_POSE_DISTANCE * UNIT_RECENT_POSE_DISTANCE) {
		/* Re-evaluate the worker's maneuver against current car/player positions.
		 * Wall-clock scheduling can change proposals, never collision freshness. */
		struct UNIT_ROLLOUT refreshed;
		if (unit_planner_rollout(planner->points, planner->count, &planner->limits, planner->owoot,
								 &profile, &request, published.candidate, &refreshed) &&
			refreshed.score < best.score) {
			best = refreshed;
			if (planner->worker_count) {
				best.plan.from_worker = 1U;
				planner_lock(planner);
				planner->stats.worker_accepted++;
				planner_unlock(planner);
			}
		}
	}
	*plan = best.plan;
	return 1;
}

void unit_planner_stats(struct UNIT_PLANNER *planner, struct UNIT_PLANNER_STATS *stats)
{
	if (!stats) {
		return;
	}
	memset(stats, 0, sizeof(*stats));
	if (planner) {
		planner_lock(planner);
		*stats = planner->stats;
		planner_unlock(planner);
	}
}
