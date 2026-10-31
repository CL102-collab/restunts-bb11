#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "state_search.h"
#include "guide.h"
#include "../c/externs.h"
#include "../c/game_input.h"

#undef memcpy

#define STATE_BEAM_WIDTH 40U
#define STATE_CACHE_COUNT 64U
#define STATE_BATCH_COUNT 96U
#define STATE_PILOT_PROFILES 8U
#define STATE_SECTION_WIDTH 10U
#define STATE_SECTION_COUNT 256U
#define STATE_RESCUE_COUNT 8U

struct state_checkpoint {
	struct GENGHOST_SNAPSHOT snapshot;
	struct GENGHOST_DRIVER driver;
	legacy_u32 route, variant;
	legacy_uint length, valid;
	legacy_u8 inputs[GENGHOST_MAX_FRAMES];
};

struct GENGHOST_STATE_EXECUTOR {
	struct GENGHOST_OPTIONS options;
	struct GENGHOST_ROUTES routes;
	struct state_checkpoint *cache;
	legacy_uint cursor;
	legacy_u64 started;
	struct GENGHOST_GUIDE_POINT *guide;
	legacy_u8 guide_inputs[GENGHOST_MAX_FRAMES];
	legacy_uint guide_count, guide_length;
};

struct state_node {
	struct GENGHOST_STATE_FEATURES features;
	legacy_u32 route, variant;
	legacy_uint length;
	legacy_u8 inputs[GENGHOST_MAX_FRAMES];
	legacy_f64 score;
};

struct state_search {
	const struct GENGHOST_OPTIONS *options;
	struct GENGHOST_SEARCH_RESULT *result;
	struct GENGHOST_STATE_EXECUTOR *executor;
	genghost_state_batch_function batch;
	void *batch_context;
	genghost_save_function save;
	void *save_context;
	legacy_u64 started, next_progress, next_task;
	struct GENGHOST_STATE_TASK *tasks;
	struct GENGHOST_STATE_REPLY *replies;
	legacy_u8 *route_attempted, *route_finished;
	legacy_uint root_count, round;
	legacy_int cancelled, section_active;
	legacy_u32 incumbent_route, incumbent_variant;
	legacy_u64 incumbent_revision, section_revision;
	legacy_uint section_frames[STATE_SECTION_COUNT];
	legacy_uint section_count, section_cursor;
	legacy_u64 fine_tune_trials;
	struct state_node *rescue;
	legacy_uint rescue_count, rescue_cursor, scout_cursor;
};

static legacy_f64 state_elapsed(const struct state_search *search)
{
	return (legacy_f64)(SDL_GetTicksNS() - search->started) / (legacy_f64)SDL_NS_PER_SECOND;
}

static legacy_int state_expired(struct state_search *search)
{
	search->result->elapsed_seconds = state_elapsed(search);
	return search->cancelled || genghost_stop_requested() ||
		   (search->options->seconds > 0.0 &&
			search->result->elapsed_seconds >= search->options->seconds) ||
		   (search->options->evaluations &&
			search->result->evaluations >= search->options->evaluations) ||
		   (search->options->progress &&
			search->options->progress(search->result, search->options->progress_context));
}

static legacy_f64 state_clamp(legacy_f64 value, legacy_f64 low, legacy_f64 high)
{
	return value < low ? low : value > high ? high : value;
}

struct GENGHOST_STATE_EXECUTOR *
genghost_state_executor_create(const struct GENGHOST_OPTIONS *options)
{
	legacy_char error[256];
	struct GENGHOST_STATE_EXECUTOR *executor = calloc(1U, sizeof(*executor));
	if (!executor) {
		return NULL;
	}
	executor->options = *options;
	executor->started = SDL_GetTicksNS();
	executor->cache = calloc(STATE_CACHE_COUNT, sizeof(*executor->cache));
	executor->guide = calloc(genghost_sim_frame_limit(), sizeof(*executor->guide));
	if (!executor->cache || !executor->guide ||
		genghost_routes_build(&executor->routes, error, sizeof(error))) {
		if (executor->cache && executor->guide) {
			fprintf(stderr, "genghost state planner: %s\n", error);
		}
		genghost_state_executor_destroy(executor);
		return NULL;
	}
	return executor;
}

void genghost_state_executor_destroy(struct GENGHOST_STATE_EXECUTOR *executor)
{
	if (!executor) {
		return;
	}
	genghost_routes_free(&executor->routes);
	free(executor->cache);
	free(executor->guide);
	free(executor);
}

static void state_policy(struct GENGHOST_POLICY *policy, const struct GENGHOST_ROUTE *route,
						 legacy_uint variant, legacy_uint profile)
{
	legacy_uint barrel_count = genghost_barrel_seed_count(route);
	legacy_uint barrel_index = profile;
	legacy_int barrel_seed = profile < barrel_count;
	profile = barrel_seed ? 0U : profile - barrel_count;
	static const legacy_f64 speed[STATE_PILOT_PROFILES] = {1.0, 0.75, 0.55, 1.2,
														   0.9, 1.5,  0.85, 1.05};
	genghost_policy_default(policy);
	if (barrel_seed) {
		genghost_barrel_seed_policy(route, policy, barrel_index);
	}
	for (legacy_uint n = 0; n < GENGHOST_POLICY_SEGMENTS; ++n) {
		struct GENGHOST_GENE *gene = &policy->segment[n];
		gene->line_mode = variant != 0U;
		gene->dualway_choice = variant == 2U   ? 1U
							   : variant == 3U ? 2U
							   : variant >= 8U ? 3U + (legacy_u64)variant - 8U
											   : 0U;
		/* Later mixed proposals combine independently selected branch pairs. */
		if (variant & UINT32_C(0x80000000)) {
			legacy_u64 choice =
				((legacy_u64)variant << 32U) ^ ((legacy_u64)n * UINT64_C(0x9e3779b97f4a7c15));
			choice ^= choice >> 30U;
			choice *= UINT64_C(0xbf58476d1ce4e5b9);
			choice ^= choice >> 27U;
			choice *= UINT64_C(0x94d049bb133111eb);
			gene->dualway_choice = 3U + ((choice ^ (choice >> 31U)) % (UINT64_MAX - 3U));
		}
		if (!barrel_seed) {
			gene->speed_scale = speed[profile % STATE_PILOT_PROFILES];
			gene->stunt_mode = profile == 6U   ? GENGHOST_STUNT_HUMAN
							   : profile == 7U ? GENGHOST_STUNT_HUMAN_ALT
											   : GENGHOST_STUNT_CORKSCREW_CUT;
		}
		if (profile == 3U || profile == 5U) {
			gene->slide_brake = 0.0;
		}
		if (profile == 4U) {
			gene->lateral_offset = -0.35;
		}
		if (profile == 5U) {
			gene->powergear_bias = 1.0;
		}
		if (profile >= STATE_PILOT_PROFILES) {
			legacy_u32 word = (profile * UINT32_C(2654435761)) ^ (n * UINT32_C(2246822519));
			word ^= word >> 16U;
			word *= UINT32_C(3266489917);
			word ^= word >> 13U;
			gene->speed_scale *= 0.70 + (word & 255U) / 425.0;
			gene->lookahead_scale = 0.75 + ((word >> 8U) & 255U) / 510.0;
			gene->lateral_offset = (((word >> 16U) & 255U) - 127.5) / 200.0;
			gene->shift_rpm_scale = 0.80 + ((word >> 24U) & 255U) / 1275.0;
			gene->powergear_bias = word & 1U ? 1.0 : 0.5;
			gene->stunt_mode =
				(word >> 2U) % 4U == 0U ? GENGHOST_STUNT_HUMAN : GENGHOST_STUNT_CORKSCREW_CUT;
		}
	}
}

static legacy_uint restore_prefix(struct GENGHOST_STATE_EXECUTOR *executor,
								  const struct GENGHOST_STATE_TASK *task,
								  const struct GENGHOST_ROUTE *route,
								  struct GENGHOST_DRIVER *driver, legacy_u64 *simulated_frames)
{
	struct state_checkpoint *best = NULL;
	for (legacy_uint n = 0; n < STATE_CACHE_COUNT; ++n) {
		struct state_checkpoint *saved = &executor->cache[n];
		if (saved->valid && saved->route == task->route_index &&
			saved->variant == task->route_variant && saved->length <= task->prefix_length &&
			(!best || saved->length > best->length) &&
			!memcmp(saved->inputs, task->prefix, saved->length)) {
			best = saved;
		}
	}
	legacy_uint length = 0U;
	if (best) {
		genghost_sim_restore(&best->snapshot);
		*driver = best->driver;
		length = best->length;
	} else {
		genghost_sim_reset();
		genghost_driver_reset(driver);
	}
	for (; length < task->prefix_length; ++length) {
		legacy_int event = genghost_sim_step(task->prefix[length]);
		++*simulated_frames;
		genghost_driver_observe(route, driver);
		if (event) {
			++length;
			break;
		}
	}
	if (length == task->prefix_length && (!best || best->length != length)) {
		struct state_checkpoint *saved = &executor->cache[executor->cursor++ % STATE_CACHE_COUNT];
		genghost_sim_capture(&saved->snapshot);
		saved->driver = *driver;
		saved->route = task->route_index;
		saved->variant = task->route_variant;
		saved->length = length;
		saved->valid = 1U;
		memcpy(saved->inputs, task->prefix, length);
	}
	return length;
}

static void state_features(struct GENGHOST_STATE_REPLY *reply, const struct GENGHOST_ROUTE *route,
						   legacy_int powergear)
{
	const struct CARSTATE *car = &state.playerstate;
	struct GENGHOST_STATE_FEATURES *feature = &reply->features;
	feature->x = car->car_position.lx / 64;
	feature->y = car->car_position.ly / 64;
	feature->z = car->car_position.lz / 64;
	feature->heading = (legacy_uint)car->car_rotate.x & 1023U;
	feature->pitch = car->car_rotate.y;
	feature->roll = car->car_rotate.z;
	feature->speed = car->car_actual_speed;
	feature->gear = car->car_current_gear;
	feature->airborne = car->car_sumSurfAllWheels == CAR_WHEEL_CONTACT_NONE;
	feature->powergear = powergear && genghost_maneuver_powergear_ready();
	feature->route = car->car_route_index;
	feature->confirmed_route = state.game_player_confirmed_route;
	feature->previous_route = state.game_player_previous_route;
	feature->route_status = state.game_player_route_status;
	feature->confirmations = state.game_route_confirmation_count;
	feature->frames = reply->race.frames;
	feature->penalty_frames = reply->race.penalty_frames;
	feature->progress = reply->driver.progress;
	feature->off_route = reply->driver.off_route;
	feature->route_length = route->length;
}

static void prepare_guide(struct GENGHOST_STATE_EXECUTOR *executor,
						  const struct GENGHOST_STATE_TASK *task, legacy_u64 *simulated_frames)
{
	if (executor->guide_length == task->reference_length &&
		!memcmp(executor->guide_inputs, task->reference, task->reference_length)) {
		return;
	}
	genghost_sim_reset();
	executor->guide_count = 0U;
	for (legacy_uint n = 0U; n < task->reference_length; ++n) {
		genghost_guide_capture(&executor->guide[n], task->reference[n]);
		++executor->guide_count;
		legacy_int event = genghost_sim_step(task->reference[n]);
		++*simulated_frames;
		if (event) {
			break;
		}
	}
	memcpy(executor->guide_inputs, task->reference, task->reference_length);
	executor->guide_length = task->reference_length;
}

static legacy_uint guide_start(const struct GENGHOST_STATE_EXECUTOR *executor,
							   const struct GENGHOST_STATE_TASK *task)
{
	const struct CARSTATE *car = &state.playerstate;
	legacy_uint first = task->reference_first > 8U ? task->reference_first - 8U : 0U;
	legacy_uint end = task->prefix_length + 240U;
	if (end > executor->guide_count) {
		end = executor->guide_count;
	}
	if (first >= end) {
		first = 0U;
	}
	legacy_f64 best_cost = 1.0e100;
	legacy_uint best = first;
	for (legacy_uint n = first; n < end; ++n) {
		const struct GENGHOST_GUIDE_POINT *point = &executor->guide[n];
		legacy_f64 dx = (point->position.lx - (legacy_f64)car->car_position.lx) / 64.0;
		legacy_f64 dy = (point->position.ly - (legacy_f64)car->car_position.ly) / 64.0;
		legacy_f64 dz = (point->position.lz - (legacy_f64)car->car_position.lz) / 64.0;
		legacy_int yaw = (point->rotation.x - car->car_rotate.x) % ANGLE_FULL_TURN;
		if (yaw < -ANGLE_HALF_TURN) {
			yaw += ANGLE_FULL_TURN;
		} else if (yaw > ANGLE_HALF_TURN) {
			yaw -= ANGLE_FULL_TURN;
		}
		legacy_int pitch = (point->rotation.y - car->car_rotate.y) % ANGLE_FULL_TURN;
		legacy_int roll = (point->rotation.z - car->car_rotate.z) % ANGLE_FULL_TURN;
		if (pitch < -ANGLE_HALF_TURN) {
			pitch += ANGLE_FULL_TURN;
		} else if (pitch > ANGLE_HALF_TURN) {
			pitch -= ANGLE_FULL_TURN;
		}
		if (roll < -ANGLE_HALF_TURN) {
			roll += ANGLE_FULL_TURN;
		} else if (roll > ANGLE_HALF_TURN) {
			roll -= ANGLE_FULL_TURN;
		}
		legacy_f64 speed = (point->speed - (legacy_f64)car->car_actual_speed) / 256.0;
		legacy_f64 cost = dx * dx + dy * dy * 2.0 + dz * dz + yaw * yaw * 8.0 +
						  pitch * pitch * 8.0 + roll * roll * 8.0 + speed * speed * 0.25;
		if (cost < best_cost) {
			best_cost = cost;
			best = n;
		}
	}
	return best;
}

legacy_int genghost_state_execute(const void *request, void *response, void *opaque)
{
	const struct GENGHOST_STATE_TASK *task = request;
	struct GENGHOST_STATE_REPLY *reply = response;
	struct GENGHOST_STATE_EXECUTOR *executor = opaque;
	memset(reply, 0, sizeof(*reply));
	reply->id = task->id;
	if (!executor || task->route_index >= executor->routes.count ||
		task->prefix_length > genghost_sim_frame_limit() ||
		task->kind > GENGHOST_STATE_TASK_REFERENCE ||
		task->reference_length > genghost_sim_frame_limit() ||
		(task->kind == GENGHOST_STATE_TASK_REFERENCE &&
		 (!task->reference_length || task->reference_first >= task->reference_length))) {
		return -1;
	}
	struct GENGHOST_POLICY policy;
	state_policy(&policy, &executor->routes.routes[task->route_index], task->route_variant,
				 task->kind == GENGHOST_STATE_TASK_POLICY ? task->action_index : 0U);
	if (task->route_variant >= 4U && task->route_variant <= 7U) {
		genghost_dualway_seed_policy(&executor->routes, task->route_index, &policy,
									 task->route_variant - 2U);
	}
	struct GENGHOST_ROUTE owned = {0};
	legacy_char error[256];
	legacy_int materialized = genghost_route_policy(&executor->routes, task->route_index, &policy,
													&owned, error, sizeof(error));
	if (materialized < 0) {
		return -1;
	}
	const struct GENGHOST_ROUTE *route =
		materialized ? &owned : &executor->routes.routes[task->route_index];
	reply->switch_count = (legacy_uint)route->switch_count;
	reply->tile_active = !executor->options.owoot && task->route_variant != 0U;
	memcpy(reply->inputs, task->prefix, task->prefix_length);
	if (task->kind == GENGHOST_STATE_TASK_INPUT_INTERVAL) {
		legacy_uint end = task->first + task->width;
		if (end < task->first || end > task->prefix_length) {
			end = task->prefix_length;
		}
		for (legacy_uint n = task->first; n < end; ++n) {
			legacy_uint mask = task->variation == 0U ? INPUT_STEERING_MASK
							   : task->variation == 1U
								   ? INPUT_PEDAL_MASK
								   : (INPUT_SHIFT_UP_FLAG | INPUT_SHIFT_DOWN_FLAG);
			reply->inputs[n] = (legacy_u8)((reply->inputs[n] & ~mask) | (task->replacement & mask));
		}
		genghost_sim_evaluate(reply->inputs, task->prefix_length, &reply->race);
		reply->length = task->prefix_length;
		reply->simulated_frames += reply->race.frames;
		genghost_driver_reset(&reply->driver);
		genghost_driver_observe(route, &reply->driver);
		reply->available = 1U;
	} else {
		if (task->kind == GENGHOST_STATE_TASK_REFERENCE) {
			prepare_guide(executor, task, &reply->simulated_frames);
		}
		reply->length =
			restore_prefix(executor, task, route, &reply->driver, &reply->simulated_frames);
		genghost_sim_result(&reply->race);
		if (reply->length != task->prefix_length || reply->race.finished || reply->race.crashed) {
			goto done;
		}
		struct GENGHOST_MANEUVER actions[GENGHOST_MANEUVER_CAPACITY];
		struct GENGHOST_MANEUVER action;
		memset(&action, 0, sizeof(action));
		legacy_uint horizon =
			reply->length < genghost_sim_frame_limit() - genghost_sim_frame_rate()
				? genghost_sim_frame_limit() - genghost_sim_frame_rate() - reply->length
				: 0U;
		if (task->kind == GENGHOST_STATE_TASK_MANEUVER) {
			reply->action_count = (legacy_uint)genghost_maneuvers(
				route, &reply->driver, &policy, executor->options.powergear,
				executor->options.collision, executor->options.owoot, actions,
				GENGHOST_MANEUVER_CAPACITY);
			if (task->action_index >= reply->action_count) {
				goto done;
			}
			action = actions[task->action_index];
			reply->action_kind = (legacy_uint)action.kind;
			if (action.kind == GENGHOST_MANEUVER_TILE_TOUCH ||
				action.kind == GENGHOST_MANEUVER_CHICANE_GRASS) {
				reply->tile_active = 1U;
			}
			if (action.horizon_frames < horizon) {
				horizon = action.horizon_frames;
			}
			/* Coarse strategy first, then small deterministic parameter probes.
			 * Each change is validated by real ticks, never copied into a state. */
			legacy_uint variation = task->variation % 7U;
			if (variation == 1U || variation == 2U) {
				action.gene.lateral_offset += variation == 1U ? -0.16 : 0.16;
			}
			if (variation == 3U || variation == 4U) {
				action.gene.speed_scale *= variation == 3U ? 0.85 : 1.15;
			}
			if (variation == 5U || variation == 6U) {
				action.gene.shift_rpm_scale *= variation == 5U ? 0.85 : 1.05;
			}
		}
		legacy_f64 furthest = reply->driver.progress;
		legacy_uint no_progress = 0U;
		legacy_uint guide_cursor =
			task->kind == GENGHOST_STATE_TASK_REFERENCE ? guide_start(executor, task) : 0U;
		for (legacy_uint n = 0; n < horizon; ++n) {
			if (n % 32U == 0U && executor->options.seconds > 0.0 &&
				(legacy_f64)(SDL_GetTicksNS() - executor->started) /
						(legacy_f64)SDL_NS_PER_SECOND >=
					executor->options.seconds) {
				break;
			}
			legacy_u8 input;
			if (task->kind == GENGHOST_STATE_TASK_REFERENCE) {
				input = genghost_guide_step(executor->guide, executor->guide_count, &guide_cursor,
											task->action_index ? 0.55 : 0.0);
			} else if (task->kind == GENGHOST_STATE_TASK_POLICY) {
				input = genghost_driver_step(route, &policy, &reply->driver,
											 executor->options.powergear,
											 executor->options.collision, executor->options.owoot);
			} else {
				input = genghost_maneuver_step(
					route, &policy, &reply->driver, &action, n, executor->options.powergear,
					executor->options.collision, executor->options.owoot);
			}
			legacy_int was_airborne =
				state.playerstate.car_sumSurfAllWheels == CAR_WHEEL_CONTACT_NONE;
			legacy_int was_ready =
				executor->options.powergear && genghost_maneuver_powergear_ready();
			if (executor->options.powergear &&
				(input & INPUT_PEDAL_MASK) == INPUT_ACCELERATE_FLAG &&
				genghost_maneuver_powergear_ready()) {
				++reply->powergear_ticks;
			}
			reply->inputs[reply->length++] = input;
			legacy_int event = genghost_sim_step(input);
			++reply->simulated_frames;
			++reply->action_ticks;
			genghost_driver_observe(route, &reply->driver);
			if (reply->driver.progress > furthest + 8.0) {
				furthest = reply->driver.progress;
				no_progress = 0U;
			} else {
				++no_progress;
			}
			legacy_int now_airborne =
				state.playerstate.car_sumSurfAllWheels == CAR_WHEEL_CONTACT_NONE;
			legacy_int now_ready =
				executor->options.powergear && genghost_maneuver_powergear_ready();
			/* Branch at the useful physical transition, before a fixed long macro
			 * can crash after a successful launch or shift/boost preparation. */
			legacy_int transition = task->kind == GENGHOST_STATE_TASK_MANEUVER && n >= 3U &&
									(was_airborne != now_airborne || (!was_ready && now_ready));
			if (transition) {
				reply->transition_stop = 1U;
			}
			if (event || transition || no_progress > 300U || genghost_stop_requested()) {
				break;
			}
		}
		genghost_sim_result(&reply->race);
		reply->stalled_frames = no_progress;
		reply->available = 1U;
	}
done:
	state_features(reply, route, executor->options.powergear);
	genghost_route_release(&owned);
	return 0;
}

static legacy_int state_local_batch(void *opaque, const struct GENGHOST_STATE_TASK *tasks,
									struct GENGHOST_STATE_REPLY *replies, size_t count)
{
	for (size_t n = 0; n < count; ++n) {
		if (genghost_state_execute(&tasks[n], &replies[n], opaque)) {
			return -1;
		}
	}
	return 0;
}

static legacy_f64 node_score(const struct GENGHOST_STATE_FEATURES *feature)
{
	/* This is a ranking heuristic only, never a claimed admissible bound. Native
	 * penalties and verified whole-lap finish time alone choose saved replays. */
	legacy_f64 speed = feature->speed / 256.0;
	legacy_f64 pace = state_clamp(30.0 + speed * 0.20, 30.0, 78.0);
	legacy_f64 remaining =
		feature->route_length > feature->progress ? feature->route_length - feature->progress : 0.0;
	return feature->frames + feature->penalty_frames + remaining / pace +
		   feature->off_route / 40.0 - (feature->powergear ? 120.0 : 0.0) -
		   (feature->airborne && speed > 170.0 ? 35.0 : 0.0);
}

static legacy_int same_bucket(const struct state_node *node, const struct GENGHOST_STATE_TASK *task,
							  const struct GENGHOST_STATE_REPLY *reply)
{
	const struct GENGHOST_STATE_FEATURES *a = &node->features, *b = &reply->features;
	return node->route == task->route_index && node->variant == task->route_variant &&
		   a->x / 384 == b->x / 384 && a->z / 384 == b->z / 384 && a->y / 96 == b->y / 96 &&
		   a->heading / 64 == b->heading / 64 && a->pitch / 64 == b->pitch / 64 &&
		   a->roll / 64 == b->roll / 64 && a->speed / (24 * 256) == b->speed / (24 * 256) &&
		   a->gear == b->gear && a->airborne == b->airborne && a->powergear == b->powergear &&
		   a->route == b->route && a->confirmed_route == b->confirmed_route &&
		   a->previous_route == b->previous_route && a->route_status == b->route_status &&
		   a->confirmations == b->confirmations && a->penalty_frames == b->penalty_frames;
}

static legacy_int same_category(const struct GENGHOST_STATE_FEATURES *a,
								const struct GENGHOST_STATE_FEATURES *b)
{
	return a->gear == b->gear && a->airborne == b->airborne && a->powergear == b->powergear;
}

static legacy_uint category_count(const struct state_node *nodes, legacy_uint count,
								  const struct GENGHOST_STATE_FEATURES *feature)
{
	legacy_uint matches = 0U;
	for (legacy_uint n = 0U; n < count; ++n) {
		if (same_category(&nodes[n].features, feature)) {
			++matches;
		}
	}
	return matches;
}

static void insert_node(struct state_search *search, struct state_node *nodes, legacy_uint *count,
						const struct GENGHOST_STATE_TASK *task,
						const struct GENGHOST_STATE_REPLY *reply)
{
	if (!reply->available || reply->race.crashed || reply->race.finished ||
		reply->length >= genghost_sim_frame_limit() - genghost_sim_frame_rate() ||
		reply->length <= task->prefix_length || reply->features.off_route > 4096.0) {
		return;
	}
	if (reply->length > 500U && reply->features.progress < 512.0 &&
		reply->features.speed < 5 * 256) {
		return;
	}
	legacy_f64 score = node_score(&reply->features);
	legacy_uint slot = *count;
	for (legacy_uint n = 0; n < *count; ++n) {
		if (nodes[n].route == task->route_index && nodes[n].variant == task->route_variant &&
			nodes[n].length == reply->length &&
			!memcmp(nodes[n].inputs, reply->inputs, reply->length)) {
			++search->result->state_duplicates;
			return;
		}
		/* A coarse bucket is deliberate bounded-beam pruning, not exact native
		 * state equivalence. Different gears, PG readiness, landing and native
		 * route/penalty states retain separate alternatives. */
		if (same_bucket(&nodes[n], task, reply)) {
			if (nodes[n].score <= score) {
				return;
			}
			slot = n;
			break;
		}
	}
	if (slot == *count && *count >= STATE_BEAM_WIDTH) {
		legacy_int new_category = category_count(nodes, *count, &reply->features) == 0U;
		slot = *count;
		for (legacy_uint n = 0U; n < *count; ++n) {
			/* Keep a representative of each gear/contact/PG preparation until
			 * another candidate in that category proves a better continuation. */
			if (!same_category(&nodes[n].features, &reply->features) &&
				category_count(nodes, *count, &nodes[n].features) == 1U) {
				continue;
			}
			if (slot == *count || nodes[n].score > nodes[slot].score) {
				slot = n;
			}
		}
		if (slot == *count || (!new_category && score >= nodes[slot].score)) {
			return;
		}
		if (new_category && score >= nodes[slot].score) {
			++search->result->state_diversity_preserved;
		}
	}
	if (slot == *count) {
		++*count;
	}
	struct state_node *node = &nodes[slot];
	node->features = reply->features;
	node->route = task->route_index;
	node->variant = task->route_variant;
	node->length = reply->length;
	node->score = score;
	memcpy(node->inputs, reply->inputs, reply->length);
	if (*count > search->result->state_frontier_peak) {
		search->result->state_frontier_peak = *count;
	}
}

static legacy_int state_save(struct state_search *search, const struct GENGHOST_STATE_REPLY *reply)
{
	if (!reply->race.finished || (search->result->race.finished &&
								  reply->race.total_frames >= search->result->race.total_frames)) {
		return 0;
	}
	legacy_uint length =
		search->options->fine_tune_gears_only ? reply->length : reply->race.finish_frame;
	if (length > genghost_sim_frame_limit()) {
		return -1;
	}
	legacy_u8 *inputs = malloc(genghost_sim_frame_limit());
	if (!inputs) {
		return -1;
	}
	memcpy(inputs, reply->inputs, length);
	if (!search->options->fine_tune_gears_only) {
		legacy_uint tail = genghost_sim_frame_limit() - length;
		if (tail > genghost_sim_frame_rate()) {
			tail = genghost_sim_frame_rate();
		}
		memset(inputs + length, 0, tail);
		length += tail;
	}
	struct GENGHOST_RESULT verified;
	if (genghost_sim_evaluate(inputs, length, &verified) || !verified.finished ||
		verified.total_frames != reply->race.total_frames) {
		fputs("genghost state planner: full replay validation failed\n", stderr);
		free(inputs);
		return -1;
	}
	search->result->simulated_frames += verified.frames;
	++search->incumbent_revision;
	search->result->race = verified;
	search->result->length = length;
	memcpy(search->result->inputs, inputs, length);
	free(inputs);
	search->result->elapsed_seconds = state_elapsed(search);
	fprintf(stderr,
			"state best %.2fs (driving %.2fs + penalty %.2fs), %" LEGACY_PRIu64 " candidates\n",
			verified.total_frames / (legacy_f64)genghost_sim_frame_rate(),
			verified.finish_frame / (legacy_f64)genghost_sim_frame_rate(),
			verified.penalty_frames / (legacy_f64)genghost_sim_frame_rate(),
			(legacy_u64)search->result->evaluations);
	return search->save && search->save(search->result, search->save_context) ? -1 : 0;
}

static size_t state_budget(struct state_search *search, size_t requested)
{
	if (search->options->evaluations) {
		legacy_u64 left = search->options->evaluations - search->result->evaluations;
		if (requested > left) {
			requested = (size_t)left;
		}
	}
	return requested;
}

static void state_task_digest(struct state_search *search, const struct GENGHOST_STATE_TASK *task)
{
	legacy_u64 hash = search->result->state_task_hash;
	if (!hash) {
		hash = UINT64_C(14695981039346656037);
	}
	const legacy_u64 values[] = {task->id,
								 task->route_index,
								 task->route_variant,
								 task->prefix_length,
								 task->action_index,
								 task->variation,
								 task->kind,
								 task->first,
								 task->width,
								 task->replacement,
								 task->reference_length,
								 task->reference_first};
	for (size_t n = 0; n < sizeof(values) / sizeof(values[0]); ++n) {
		for (legacy_uint byte = 0; byte < 8U; ++byte) {
			hash ^= (values[n] >> (byte * 8U)) & 255U;
			hash *= UINT64_C(1099511628211);
		}
	}
	for (legacy_uint n = 0; n < task->prefix_length; ++n) {
		hash ^= task->prefix[n];
		hash *= UINT64_C(1099511628211);
	}
	for (legacy_uint n = 0; n < task->reference_length; ++n) {
		hash ^= task->reference[n];
		hash *= UINT64_C(1099511628211);
	}
	search->result->state_task_hash = hash;
}

static void remember_rescue(struct state_search *search, const struct GENGHOST_STATE_TASK *task,
							const struct GENGHOST_STATE_REPLY *reply)
{
	if (task->kind != GENGHOST_STATE_TASK_POLICY || !reply->available || reply->race.finished ||
		reply->length < 96U || reply->features.progress < 512.0) {
		return;
	}
	legacy_uint back = reply->stalled_frames > 120U ? reply->stalled_frames + 32U : 64U;
	legacy_uint length = reply->length > back ? reply->length - back : 0U;
	if (!length) {
		return;
	}
	legacy_f64 score = -reply->features.progress + reply->race.penalty_frames * 20.0;
	legacy_uint slot = search->rescue_count;
	for (legacy_uint n = 0U; n < search->rescue_count; ++n) {
		if (search->rescue[n].route == task->route_index &&
			search->rescue[n].variant == task->route_variant &&
			search->rescue[n].length == length &&
			!memcmp(search->rescue[n].inputs, reply->inputs, length)) {
			return;
		}
	}
	if (slot >= STATE_RESCUE_COUNT) {
		slot = 0U;
		for (legacy_uint n = 1U; n < search->rescue_count; ++n) {
			if (search->rescue[n].score > search->rescue[slot].score) {
				slot = n;
			}
		}
		if (score >= search->rescue[slot].score) {
			return;
		}
	} else {
		++search->rescue_count;
	}
	struct state_node *node = &search->rescue[slot];
	memset(node, 0, sizeof(*node));
	node->route = task->route_index;
	node->variant = task->route_variant;
	node->length = length;
	node->score = score;
	memcpy(node->inputs, reply->inputs, length);
}

static legacy_int state_dispatch(struct state_search *search, size_t count,
								 struct state_node *nodes, legacy_uint *node_count)
{
	if (!count) {
		return 0;
	}
	legacy_int dispatched =
		search->batch(search->batch_context, search->tasks, search->replies, count);
	if (dispatched == 2) {
		search->cancelled = 1;
		return 0;
	}
	if (dispatched) {
		return -1;
	}
	for (size_t n = 0; n < count; ++n) {
		const struct GENGHOST_STATE_TASK *task = &search->tasks[n];
		const struct GENGHOST_STATE_REPLY *reply = &search->replies[n];
		if (reply->id != task->id) {
			return -1;
		}
		state_task_digest(search, task);
		search->result->simulated_frames += reply->simulated_frames;
		if (!reply->available) {
			continue;
		}
		++search->result->evaluations;
		++search->result->state_evaluations;
		if (reply->transition_stop) {
			++search->result->state_transition_stops;
		}
		if (search->section_active) {
			++search->result->state_section_evaluations;
		}
		if (task->kind == GENGHOST_STATE_TASK_REFERENCE) {
			++search->result->guided_evaluations;
		} else if (task->kind == GENGHOST_STATE_TASK_INPUT_INTERVAL) {
			++search->result->input_evaluations;
			++search->result->state_input_evaluations;
		} else if (task->kind == GENGHOST_STATE_TASK_MANEUVER) {
			++search->result->policy_evaluations;
			++search->result->state_macro_evaluations;
		} else {
			++search->result->policy_evaluations;
		}
		if (reply->action_kind == GENGHOST_MANEUVER_POWERGEAR) {
			++search->result->state_pg_evaluations;
		}
		if (reply->action_kind == GENGHOST_MANEUVER_CHICANE_GRASS) {
			++search->result->state_chicane_evaluations;
		}
		if (reply->powergear_ticks || reply->features.powergear) {
			++search->result->state_pg_states;
		}
		if (reply->switch_count) {
			++search->result->dualway_evaluations;
		}
		if (reply->tile_active) {
			++search->result->tile_evaluations;
		}
		if (reply->driver.tactic_ticks) {
			++search->result->tactic_evaluations;
		}
		if (!search->options->fine_tune && !search->route_attempted[task->route_index]) {
			search->route_attempted[task->route_index] = 1U;
			++search->result->routes_attempted;
		}
		if (!search->options->fine_tune && reply->race.finished &&
			!search->route_finished[task->route_index]) {
			search->route_finished[task->route_index] = 1U;
			++search->result->routes_finished;
		}
		if (reply->race.finished &&
			(!search->result->race.finished ||
			 reply->race.total_frames < search->result->race.total_frames)) {
			if (task->kind == GENGHOST_STATE_TASK_REFERENCE) {
				++search->result->guided_improvements;
			}
			if (reply->switch_count) {
				++search->result->dualway_improvements;
			}
			if (reply->tile_active) {
				++search->result->tile_improvements;
			}
			if (reply->driver.tactic_ticks) {
				++search->result->tactic_improvements;
			}
			search->incumbent_route = task->route_index;
			search->incumbent_variant = task->route_variant;
		}
		if (state_save(search, reply)) {
			return -1;
		}
		remember_rescue(search, task, reply);
		if (nodes) {
			insert_node(search, nodes, node_count, task, reply);
		}
	}
	if (SDL_GetTicksNS() >= search->next_progress) {
		fprintf(stderr,
				"state progress: %" LEGACY_PRIu64 " candidates, %" LEGACY_PRIu64
				" macros, %" LEGACY_PRIu64 " PG proposals, %" LEGACY_PRIu64 " chicane "
				"grass proposals, %zu/%zu routes attempted\n",
				(legacy_u64)search->result->evaluations,
				(legacy_u64)search->result->state_macro_evaluations,
				(legacy_u64)search->result->state_pg_evaluations,
				(legacy_u64)search->result->state_chicane_evaluations,
				search->result->routes_attempted, search->result->routes);
		search->next_progress = SDL_GetTicksNS() + 5000000000ULL;
	}
	return 0;
}

static void task_root(struct state_search *search, struct GENGHOST_STATE_TASK *task,
					  legacy_uint root)
{
	memset(task, 0, sizeof(*task));
	task->id = ++search->next_task;
	legacy_uint variants = search->options->owoot					? 1U
						   : search->executor->routes.dualway_pairs ? 4U
																	: 2U;
	task->route_index = root / variants;
	task->route_variant = root % variants;
}

/* Fine-tune actual replay control boundaries rather than rediscovering a
 * driving policy. Keep unrelated input channels and most of the suffix exact;
 * a minority of candidates rejoin the same incumbent by native position. */
static legacy_int fine_tune_inputs(struct state_search *search)
{
	legacy_uint *boundaries = malloc(genghost_sim_frame_limit() * sizeof(*boundaries));
	if (!boundaries) {
		return -1;
	}
	legacy_uint boundary_count = 0U;
	legacy_uint finish = search->result->race.finish_frame;
	for (legacy_uint frame = 1U; frame < finish; ++frame) {
		if (search->result->inputs[frame] != search->result->inputs[frame - 1U]) {
			boundaries[boundary_count++] = frame;
		}
	}
	/* Full replay candidates can be expensive on older Android devices. Small
	 * batches return to deadline/cancellation checks promptly. */
	size_t count = state_budget(search, 12U);
	for (size_t n = 0U; n < count; ++n) {
		struct GENGHOST_STATE_TASK *task = &search->tasks[n];
		task_root(search, task, 0U);
		task->kind = GENGHOST_STATE_TASK_INPUT_INTERVAL;
		task->route_index = search->incumbent_route;
		task->route_variant = search->incumbent_variant;
		task->prefix_length = search->result->length;
		memcpy(task->prefix, search->result->inputs, task->prefix_length);
		if (search->options->fine_tune_gears_only) {
			legacy_uint first = 0U, end = 0U;
			/* Invalid moves still evaluate the unchanged lap, consuming the
			 * requested budget without falling back to other input channels. */
			genghost_shift_inputs(task->prefix, finish, search->fine_tune_trials++, &first, &end);
			task->first = first;
			continue;
		}
		legacy_u32 word = (legacy_u32)(task->id * 2654435761U) ^ search->options->seed;
		word ^= word >> 16U;
		word *= 2246822519U;
		word ^= word >> 13U;
		legacy_uint mode = (legacy_uint)(task->id % 12U);
		legacy_uint first = boundary_count ? boundaries[word % boundary_count] : word % finish;
		static const legacy_uint widths[] = {1U, 1U, 2U, 2U, 3U, 4U, 6U, 8U};
		legacy_uint width = widths[(word >> 16U) % 8U];
		legacy_uint changed_end = first;
		legacy_u64 trial = search->fine_tune_trials++;
		legacy_int shift =
			trial % 2U == 0U &&
			genghost_shift_inputs(task->prefix, finish, trial / 2U, &first, &changed_end);
		if (shift) {
			/* Replay the exact continuation so only shift timing changes. */
		} else if (mode < 8U && first) {
			static const legacy_uint masks[] = {INPUT_PEDAL_MASK, INPUT_STEERING_MASK,
												INPUT_SHIFT_UP_FLAG | INPUT_SHIFT_DOWN_FLAG};
			legacy_uint channel = (word >> 8U) % 3U;
			legacy_uint changed = task->prefix[first] ^ task->prefix[first - 1U];
			for (legacy_uint k = 0U; k < 3U && !(changed & masks[channel]); ++k) {
				channel = (channel + 1U) % 3U;
			}
			legacy_uint mask = masks[channel];
			legacy_u8 before = task->prefix[first - 1U] & mask;
			legacy_u8 after = task->prefix[first] & mask;
			legacy_uint end = first + 1U;
			while (end < finish && (task->prefix[end] & mask) == after) {
				++end;
			}
			legacy_int later = mode % 2U;
			if (mode >= 6U && end < finish) {
				/* Move an entire steering/gear pulse without changing its
				 * duration or the timestamps of all later inputs. */
				if (width > end - first) {
					width = end - first;
				}
				if (later && width > finish - end) {
					width = finish - end;
				}
				if (!later && width > first) {
					width = first;
				}
				legacy_u8 following = task->prefix[end] & mask;
				legacy_uint available = 0U;
				while (available < width &&
					   (later ? (task->prefix[end + available] & mask) == following
							  : (task->prefix[first - available - 1U] & mask) == before)) {
					++available;
				}
				width = available;
				legacy_uint left = later ? first : first - width;
				legacy_uint right = later ? end : end - width;
				for (legacy_uint k = 0U; k < width; ++k) {
					task->prefix[left + k] =
						(legacy_u8)((task->prefix[left + k] & ~mask) | (later ? before : after));
					task->prefix[right + k] = (legacy_u8)((task->prefix[right + k] & ~mask) |
														  (later ? after : following));
				}
				first = left;
				changed_end = right + width;
			} else if (later) {
				if (width > end - first) {
					width = end - first;
				}
				changed_end = first + width;
				for (legacy_uint k = first; k < changed_end; ++k) {
					task->prefix[k] = (legacy_u8)((task->prefix[k] & ~mask) | before);
				}
			} else {
				legacy_uint left = first;
				while (left && first - left < width && (task->prefix[left - 1U] & mask) == before) {
					--left;
				}
				changed_end = first;
				for (legacy_uint k = left; k < first; ++k) {
					task->prefix[k] = (legacy_u8)((task->prefix[k] & ~mask) | after);
				}
				first = left;
			}
		} else {
			if (width > finish - first) {
				width = finish - first;
			}
			changed_end = first + width;
			if (mode == 8U) {
				memmove(task->prefix + first, task->prefix + first + width,
						task->prefix_length - first - width);
				task->prefix_length -= width;
			} else if (mode == 9U) {
				if (width <= genghost_sim_frame_limit() - task->prefix_length) {
					memmove(task->prefix + first + width, task->prefix + first,
							task->prefix_length - first);
					memset(task->prefix + first, task->prefix[first + width], width);
					task->prefix_length += width;
				}
			} else {
				static const legacy_u8 steering[] = {0U, INPUT_STEER_LEFT_FLAG,
													 INPUT_STEER_RIGHT_FLAG};
				static const legacy_u8 pedals[] = {INPUT_ACCELERATE_FLAG, INPUT_ACCELERATE_FLAG, 0U,
												   INPUT_BRAKE_FLAG};
				legacy_uint mask = mode == 10U ? INPUT_STEERING_MASK : INPUT_PEDAL_MASK;
				legacy_u8 replacement =
					mode == 10U ? steering[(word >> 8U) % 3U] : pedals[(word >> 8U) % 4U];
				for (legacy_uint k = first; k < changed_end; ++k) {
					task->prefix[k] = (legacy_u8)((task->prefix[k] & ~mask) | replacement);
				}
			}
		}
		task->first = first;
		/* kind 2 with width zero evaluates the already edited input sequence.
		 * kind 3 keeps that local edit and retimes only the incumbent suffix. */
		if (!shift && task->id % 8U == 0U && changed_end > first && changed_end < finish &&
			mode != 8U && mode != 9U) {
			task->kind = GENGHOST_STATE_TASK_REFERENCE;
			task->prefix_length = changed_end;
			task->reference_length = finish;
			task->reference_first = first;
			task->action_index = (word >> 20U) % 2U;
			memcpy(task->reference, search->result->inputs, finish);
		}
	}
	free(boundaries);
	return state_dispatch(search, count, NULL, NULL);
}

static legacy_int refine_inputs(struct state_search *search)
{
	if (!search->result->race.finished || state_expired(search)) {
		return 0;
	}
	if (search->options->fine_tune) {
		return fine_tune_inputs(search);
	}
	size_t count = state_budget(search, 12U);
	for (size_t n = 0; n < count; ++n) {
		struct GENGHOST_STATE_TASK *task = &search->tasks[n];
		task_root(search, task, 0U);
		task->kind = GENGHOST_STATE_TASK_INPUT_INTERVAL;
		task->route_index = search->incumbent_route;
		task->route_variant = search->incumbent_variant;
		task->prefix_length = search->result->length;
		memcpy(task->prefix, search->result->inputs, task->prefix_length);
		legacy_u32 word = (legacy_u32)(task->id * 2654435761U) ^ search->options->seed;
		task->first = word % search->result->race.finish_frame;
		task->width = 1U + ((word >> 16U) % 8U);
		task->variation = (legacy_uint)(n % 3U);
		if (task->variation == 0U) {
			task->replacement = n % 2U ? INPUT_STEER_LEFT_FLAG : INPUT_STEER_RIGHT_FLAG;
		} else if (task->variation == 1U) {
			task->replacement = n % 2U ? INPUT_ACCELERATE_FLAG : INPUT_BRAKE_FLAG;
		} else {
			task->replacement = n % 2U ? INPUT_SHIFT_UP_FLAG : 0U;
		}
	}
	return state_dispatch(search, count, NULL, NULL);
}

/* Store widely spaced generic sections and native approach/takeoff/gear
 * transitions from the current verified lap. This trace is only a source of
 * replay prefixes; the simulator still decides every changed continuation. */
static void section_add(struct state_search *search, legacy_uint frame)
{
	for (legacy_uint n = 0; n < search->section_count; ++n) {
		legacy_uint saved = search->section_frames[n];
		if (frame > saved ? frame - saved < 12U : saved - frame < 12U) {
			return;
		}
	}
	if (search->section_count < STATE_SECTION_COUNT) {
		search->section_frames[search->section_count++] = frame;
	}
}

static legacy_int trace_sections(struct state_search *search)
{
	if (search->section_revision == search->incumbent_revision) {
		return 0;
	}
	struct GENGHOST_ROUTE owned = {0};
	struct GENGHOST_POLICY policy;
	legacy_char error[256];
	state_policy(&policy, &search->executor->routes.routes[search->incumbent_route],
				 search->incumbent_variant, 0U);
	if (search->incumbent_variant >= 4U && search->incumbent_variant <= 7U) {
		genghost_dualway_seed_policy(&search->executor->routes, search->incumbent_route, &policy,
									 search->incumbent_variant - 2U);
	}
	legacy_int materialized = genghost_route_policy(
		&search->executor->routes, search->incumbent_route, &policy, &owned, error, sizeof(error));
	if (materialized < 0) {
		return -1;
	}
	const struct GENGHOST_ROUTE *route =
		materialized ? &owned : &search->executor->routes.routes[search->incumbent_route];
	search->section_count = 0U;
	section_add(search, 0U);
	genghost_sim_reset();
	struct GENGHOST_DRIVER driver;
	genghost_driver_reset(&driver);
	legacy_uint spacing = search->result->race.finish_frame / 100U;
	if (spacing < 40U) {
		spacing = 40U;
	}
	legacy_int previous_model = -1, previous_gear = -1, previous_airborne = 0, previous_uphill = 0;
	for (legacy_uint frame = 0; frame < search->result->race.finish_frame; ++frame) {
		const struct CARSTATE *car = &state.playerstate;
		legacy_int model = driver.waypoint < route->point_count
							   ? route->points[driver.waypoint].physical_model
							   : -1;
		legacy_int airborne = car->car_sumSurfAllWheels == CAR_WHEEL_CONTACT_NONE;
		legacy_int uphill = car->car_rotate.y > 12 && car->car_rotate.y < 192 &&
							car->car_sumSurfRearWheels != CAR_WHEEL_CONTACT_NONE;
		if (frame % spacing == 0U) {
			section_add(search, frame);
		}
		if ((model != previous_model && genghost_stunt_catalog_model(model)) ||
			(airborne && !previous_airborne) || (uphill && !previous_uphill)) {
			section_add(search, frame > 48U ? frame - 48U : 0U);
		}
		if (car->car_current_gear != previous_gear) {
			section_add(search, frame > 24U ? frame - 24U : 0U);
		}
		previous_model = model;
		previous_gear = car->car_current_gear;
		previous_airborne = airborne;
		previous_uphill = uphill;
		legacy_int event = genghost_sim_step(search->result->inputs[frame]);
		++search->result->simulated_frames;
		genghost_driver_observe(route, &driver);
		if (event) {
			break;
		}
	}
	search->section_revision = search->incumbent_revision;
	genghost_route_release(&owned);
	return 0;
}

static legacy_int compare_nodes(const void *left, const void *right)
{
	const struct state_node *a = left, *b = right;
	if (a->score != b->score) {
		return a->score < b->score ? -1 : 1;
	}
	if (a->route != b->route) {
		return a->route < b->route ? -1 : 1;
	}
	if (a->variant != b->variant) {
		return a->variant < b->variant ? -1 : 1;
	}
	if (a->length != b->length) {
		return a->length < b->length ? -1 : 1;
	}
	return memcmp(a->inputs, b->inputs, a->length);
}

static void task_from_node(struct state_search *search, struct GENGHOST_STATE_TASK *task,
						   const struct state_node *node)
{
	memset(task, 0, sizeof(*task));
	task->id = ++search->next_task;
	task->route_index = node->route;
	task->route_variant = node->variant;
	task->prefix_length = node->length;
	memcpy(task->prefix, node->inputs, node->length);
}

static legacy_int expand_nodes(struct state_search *search, const struct state_node *parents,
							   legacy_uint parent_count, struct state_node *nodes,
							   legacy_uint *node_count, legacy_uint variation,
							   legacy_int switch_proposals)
{
	size_t cursor = 0U, total = (size_t)parent_count * GENGHOST_MANEUVER_CAPACITY;
	while (cursor < total && !state_expired(search)) {
		size_t count = state_budget(search, total - cursor);
		if (count > STATE_BATCH_COUNT) {
			count = STATE_BATCH_COUNT;
		}
		for (size_t n = 0U; n < count; ++n) {
			legacy_uint parent = (legacy_uint)((cursor + n) / GENGHOST_MANEUVER_CAPACITY);
			legacy_uint action = (legacy_uint)((cursor + n) % GENGHOST_MANEUVER_CAPACITY);
			struct GENGHOST_STATE_TASK *task = &search->tasks[n];
			task_from_node(search, task, &parents[parent]);
			/* Rotate independent actions per parent: otherwise fixed worker
			 * striding can assign all costly launches/rollouts to one core. */
			task->action_index = (action + parent + search->round) % GENGHOST_MANEUVER_CAPACITY;
			task->variation = variation;
			if (switch_proposals && !search->options->owoot && search->result->dualway_pairs &&
				action % 6U == 5U) {
				legacy_uint epoch = search->round / 4U;
				task->route_variant = epoch < 4U ? 4U + epoch : 8U + epoch - 4U;
				if ((action / 6U) % 2U && epoch >= 4U) {
					task->route_variant |= UINT32_C(0x80000000);
				}
			}
		}
		if (state_dispatch(search, count, nodes, node_count)) {
			return -1;
		}
		cursor += count;
	}
	return 0;
}

static legacy_uint section_prune(struct state_node *nodes, legacy_uint count)
{
	qsort(nodes, count, sizeof(*nodes), compare_nodes);
	if (count <= STATE_SECTION_WIDTH) {
		return count;
	}
	struct state_node *kept = malloc(STATE_SECTION_WIDTH * sizeof(*kept));
	if (!kept) {
		/* The sorted leading nodes remain a valid beam under memory pressure. */
		return STATE_SECTION_WIDTH;
	}
	legacy_u8 selected[STATE_BEAM_WIDTH] = {0};
	legacy_uint used = 0U;
	for (legacy_uint n = 0U; n < count && used < STATE_SECTION_WIDTH; ++n) {
		legacy_int represented = 0;
		for (legacy_uint k = 0U; k < used; ++k) {
			if (same_category(&kept[k].features, &nodes[n].features)) {
				represented = 1;
			}
		}
		if (!represented) {
			kept[used++] = nodes[n];
			selected[n] = 1U;
		}
	}
	for (legacy_uint n = 0U; n < count && used < STATE_SECTION_WIDTH; ++n) {
		if (!selected[n]) {
			kept[used++] = nodes[n];
		}
	}
	memcpy(nodes, kept, used * sizeof(*nodes));
	free(kept);
	return used;
}

static legacy_int refine_section(struct state_search *search)
{
	if (state_expired(search) || (!search->result->race.finished && !search->rescue_count)) {
		return 0;
	}
	if (search->result->race.finished && (trace_sections(search) || !search->section_count)) {
		return -1;
	}
	struct state_node *frontier = calloc(STATE_BEAM_WIDTH, sizeof(*frontier));
	struct state_node *next = calloc(STATE_BEAM_WIDTH, sizeof(*next));
	if (!frontier || !next) {
		free(frontier);
		free(next);
		return -1;
	}
	/* A separate beam protects grid-start PG discovery from the easier task of
	 * following late prefixes of an already completed lap. */
	search->section_active = 1;
	++search->result->state_section_starts;
	legacy_uint cursor = search->section_cursor++;
	legacy_u8 *reference = malloc(genghost_sim_frame_limit());
	if (!reference) {
		search->section_active = 0;
		free(frontier);
		free(next);
		return -1;
	}
	legacy_uint reference_length =
		search->result->race.finished ? search->result->race.finish_frame : 0U;
	if (reference_length) {
		legacy_uint chosen = (cursor + search->section_count / 3U) % search->section_count;
		frontier[0].route = search->incumbent_route;
		frontier[0].variant = search->incumbent_variant;
		frontier[0].length = search->section_frames[chosen];
		memcpy(frontier[0].inputs, search->result->inputs, frontier[0].length);
		memcpy(reference, search->result->inputs, reference_length);
	} else {
		frontier[0] = search->rescue[search->rescue_cursor++ % search->rescue_count];
		++search->result->state_rescue_starts;
	}
	legacy_uint reference_first = frontier[0].length;
	legacy_uint count = 1U;
	legacy_int status = 0;
	for (legacy_uint depth = 0U; depth < 3U && count && !state_expired(search); ++depth) {
		legacy_uint next_count = 0U;
		if (expand_nodes(
				search, frontier, count, next, &next_count,
				cursor / (search->section_count ? search->section_count : STATE_RESCUE_COUNT), 0)) {
			status = -1;
			goto done;
		}
		next_count = section_prune(next, next_count);
		struct state_node *swap = frontier;
		frontier = next;
		next = swap;
		count = next_count;
	}
	/* Complete each exit with two native controllers before judging the local
	 * change. A slower section with a fast/PG-ready exit can win the full lap. */
	if (!state_expired(search)) {
		legacy_uint kinds = reference_length && !search->options->fine_tune ? 3U : 2U;
		size_t batch_count = state_budget(search, (size_t)count * kinds);
		for (size_t n = 0U; n < batch_count; ++n) {
			legacy_uint parent = (legacy_uint)n / kinds;
			legacy_uint kind = ((legacy_uint)n % kinds + parent) % kinds;
			struct GENGHOST_STATE_TASK *task = &search->tasks[n];
			task_from_node(search, task, &frontier[parent]);
			if (reference_length && kind < 2U) {
				task->kind = GENGHOST_STATE_TASK_REFERENCE;
				task->reference_length = reference_length;
				task->reference_first = reference_first;
				memcpy(task->reference, reference, reference_length);
				task->action_index = kind;
			} else {
				task->kind = GENGHOST_STATE_TASK_POLICY;
				task->action_index = reference_length ? 0U : kind;
			}
		}
		if (state_dispatch(search, batch_count, NULL, NULL)) {
			status = -1;
		}
	}
done:
	search->section_active = 0;
	free(reference);
	free(frontier);
	free(next);
	return status;
}

static legacy_int scout_routes(struct state_search *search)
{
	if (search->result->race.finished || state_expired(search)) {
		return 0;
	}
	size_t count = state_budget(search, 8U);
	for (size_t n = 0U; n < count; ++n) {
		legacy_uint ordinal = search->scout_cursor++;
		task_root(search, &search->tasks[n], ordinal % search->root_count);
		search->tasks[n].kind = GENGHOST_STATE_TASK_POLICY;
		search->tasks[n].action_index = STATE_PILOT_PROFILES + ordinal / search->root_count;
	}
	return state_dispatch(search, count, NULL, NULL);
}

legacy_int genghost_state_search(const struct GENGHOST_OPTIONS *options,
								 const legacy_u8 *seed_inputs, legacy_uint seed_length,
								 struct GENGHOST_SEARCH_RESULT *result, genghost_save_function save,
								 void *save_context, genghost_state_batch_function batch,
								 void *batch_context)
{
	struct GENGHOST_OPTIONS normalized = *options;
	if (normalized.fine_tune_gears_only) {
		normalized.fine_tune = 1;
	}
	options = &normalized;
	struct state_search search = {0};
	struct state_node *frontier = calloc(STATE_BEAM_WIDTH, sizeof(*frontier));
	struct state_node *next = calloc(STATE_BEAM_WIDTH, sizeof(*next));
	legacy_uint frontier_count = 0U;
	legacy_int status = -1;
	memset(result, 0, sizeof(*result));
	search.options = options;
	search.result = result;
	search.save = save;
	search.save_context = save_context;
	search.started = SDL_GetTicksNS();
	search.next_progress = search.started + 5000000000ULL;
	if (seed_length > genghost_sim_frame_limit() || (seed_length && !seed_inputs) ||
		(options->fine_tune && !seed_length)) {
		fputs("genghost: fine-tune requires a completed compatible replay\n", stderr);
		goto done;
	}
	search.executor = genghost_state_executor_create(options);
	search.tasks = calloc(STATE_BATCH_COUNT, sizeof(*search.tasks));
	search.replies = calloc(STATE_BATCH_COUNT, sizeof(*search.replies));
	search.rescue = calloc(STATE_RESCUE_COUNT, sizeof(*search.rescue));
	if (!frontier || !next || !search.executor || !search.tasks || !search.replies ||
		!search.rescue) {
		goto done;
	}
	search.batch = batch ? batch : state_local_batch;
	search.batch_context = batch ? batch_context : search.executor;
	result->routes = search.executor->routes.count;
	result->dualway_pairs = search.executor->routes.dualway_pairs;
	search.route_attempted = calloc(result->routes, 1U);
	search.route_finished = calloc(result->routes, 1U);
	legacy_uint variants = options->owoot ? 1U : result->dualway_pairs ? 4U : 2U;
	if (!search.route_attempted || !search.route_finished ||
		result->routes > UINT32_MAX / variants) {
		goto done;
	}
	search.root_count = (legacy_uint)result->routes * variants;
	fprintf(stderr,
			"state planner: %zu routes, %u geometry alternatives; beam %u with native "
			"state/gear/penalty diversity\n",
			result->routes, search.root_count, STATE_BEAM_WIDTH);
	if (seed_length) {
		if (seed_length > genghost_sim_frame_limit()) {
			goto done;
		}
		struct GENGHOST_STATE_REPLY *reply = &search.replies[0];
		memcpy(reply->inputs, seed_inputs, seed_length);
		reply->length = seed_length;
		genghost_sim_evaluate(seed_inputs, seed_length, &reply->race);
		++result->evaluations;
		result->simulated_frames += reply->race.frames;
		result->seed_finished = reply->race.finished;
		result->seed_total_frames = reply->race.total_frames;
		if (options->fine_tune && (!reply->race.finished || !reply->race.finish_frame)) {
			fputs("genghost: fine-tune replay does not finish under the selected rules\n", stderr);
			goto done;
		}
		if (state_save(&search, reply)) {
			goto done;
		}
	}
	if (options->fine_tune) {
		/* Every candidate descends from the current completed replay. Section
		 * exits rejoin its guide, never a fresh whole-route driving policy. */
		for (search.round = 0U; !state_expired(&search); ++search.round) {
			if (refine_inputs(&search) || (!options->fine_tune_gears_only &&
										   search.round % 32U == 31U && refine_section(&search))) {
				goto done;
			}
		}
		status = 0;
		goto done;
	}
	/* Whole-route controllers provide a verified incumbent and scout failures.
	 * Their prefixes are reusable, while the beam also starts at the grid so
	 * preparatory shifts/launches are not constrained to the scout's strategy. */
	legacy_uint pilot_count = STATE_PILOT_PROFILES;
	for (size_t route = 0; route < search.executor->routes.count; ++route) {
		legacy_uint count = STATE_PILOT_PROFILES +
							genghost_barrel_seed_count(&search.executor->routes.routes[route]);
		if (count > pilot_count) {
			pilot_count = count;
		}
	}
	for (legacy_uint profile = 0U; profile < pilot_count && !state_expired(&search); ++profile) {
		for (legacy_uint first = 0U; first < search.root_count && !state_expired(&search);) {
			size_t count = state_budget(&search, search.root_count - first);
			if (count > STATE_BATCH_COUNT) {
				count = STATE_BATCH_COUNT;
			}
			size_t used = 0;
			for (size_t n = 0; n < count; ++n) {
				legacy_uint root = first + (legacy_uint)n;
				const struct GENGHOST_ROUTE *route =
					&search.executor->routes.routes[root / variants];
				if (profile >= STATE_PILOT_PROFILES + genghost_barrel_seed_count(route)) {
					continue;
				}
				task_root(&search, &search.tasks[used], root);
				search.tasks[used].kind = GENGHOST_STATE_TASK_POLICY;
				search.tasks[used].action_index = profile;
				used++;
			}
			if (used && state_dispatch(&search, used, NULL, NULL)) {
				goto done;
			}
			first += (legacy_uint)count;
		}
	}
	if (!result->race.finished && refine_section(&search)) {
		goto done;
	}
	for (search.round = 0U; !state_expired(&search); ++search.round) {
		if (!frontier_count) {
			legacy_uint start = (search.round / 2U) % search.root_count;
			for (legacy_uint n = 0U; n < search.root_count && n < STATE_BEAM_WIDTH; ++n) {
				struct GENGHOST_STATE_TASK *root = &search.tasks[0];
				task_root(&search, root, (start + n) % search.root_count);
				memset(&frontier[n], 0, sizeof(frontier[n]));
				frontier[n].route = root->route_index;
				frontier[n].variant = root->route_variant;
				++frontier_count;
			}
		}
		legacy_uint next_count = 0U;
		/* Every frontier parent funds all deterministic maneuvers before another
		 * layer. Arrival ordering never determines which candidates survive. */
		if (expand_nodes(&search, frontier, frontier_count, next, &next_count, search.round / 24U,
						 1)) {
			goto done;
		}
		struct state_node *swap = frontier;
		frontier = next;
		next = swap;
		frontier_count = next_count;
		if (search.round % 3U == 2U && (scout_routes(&search) || refine_section(&search))) {
			goto done;
		}
		if (refine_inputs(&search)) {
			goto done;
		}
	}
	status = result->race.finished ? 0 : 2;
done:
	result->elapsed_seconds = state_elapsed(&search);
	free(frontier);
	free(next);
	free(search.tasks);
	free(search.replies);
	free(search.rescue);
	free(search.route_attempted);
	free(search.route_finished);
	genghost_state_executor_destroy(search.executor);
	return status;
}
