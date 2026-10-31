#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <SDL3/SDL.h>
#include "search.h"
#include "guide.h"
#include "../c/externs.h"
#include "../c/crash_state.h"
#include "../c/trackdata_layout.h"
#include "../c/game_input.h"

#undef memcpy

#define POPULATION 6U
#define MAX_SEARCH_FAMILIES 3U
#define CHECKPOINT_INTERVAL 64U

volatile sig_atomic_t genghost_interrupted;

static const legacy_u64 STOP_FILE_POLL_NS = 100000000ULL;
static const legacy_char *stop_file_path;
static legacy_u64 stop_file_next_poll;

void genghost_set_stop_file(const legacy_char *path)
{
	stop_file_path = path;
	stop_file_next_poll = 0;
}

legacy_int genghost_stop_requested(void)
{
	if (!genghost_interrupted && stop_file_path != NULL) {
		const legacy_u64 now = SDL_GetTicksNS();
		if (now >= stop_file_next_poll) {
			stop_file_next_poll = now + STOP_FILE_POLL_NS;
			SDL_PathInfo info;
			if (SDL_GetPathInfo(stop_file_path, &info) && info.type == SDL_PATHTYPE_FILE &&
				info.size != 0) {
				genghost_interrupted = 1;
			}
		}
	}
	return genghost_interrupted != 0;
}

legacy_int genghost_shift_inputs(legacy_u8 *inputs, legacy_uint length, legacy_u64 proposal,
								 legacy_uint *first, legacy_uint *end)
{
	static const legacy_int offsets[] = {-1, 1, -2, 2, -4, 4, -8, 8, -16, 16, -32, 32};
	const legacy_uint mask = INPUT_SHIFT_UP_FLAG | INPUT_SHIFT_DOWN_FLAG;
	if (!inputs || !length || length > GENGHOST_MAX_FRAMES || !first || !end) {
		return 0;
	}
	legacy_uint count = 0U;
	for (legacy_uint frame = 0U; frame < length;) {
		legacy_uint shift = inputs[frame] & mask;
		legacy_uint next = frame + 1U;
		while (next < length && (inputs[next] & mask) == shift) {
			++next;
		}
		if (shift == INPUT_SHIFT_UP_FLAG || shift == INPUT_SHIFT_DOWN_FLAG) {
			++count;
		}
		frame = next;
	}
	if (!count) {
		return 0;
	}
	legacy_uint selected = (legacy_uint)(proposal % count);
	legacy_int offset = offsets[(proposal / count) % (sizeof(offsets) / sizeof(offsets[0]))];
	for (legacy_uint frame = 0U; frame < length;) {
		legacy_uint shift = inputs[frame] & mask;
		legacy_uint next = frame + 1U;
		while (next < length && (inputs[next] & mask) == shift) {
			++next;
		}
		if ((shift == INPUT_SHIFT_UP_FLAG || shift == INPUT_SHIFT_DOWN_FLAG) && !selected--) {
			legacy_int moved = (legacy_int)frame + offset;
			legacy_int moved_end = (legacy_int)next + offset;
			if (moved < 0 || moved_end > (legacy_int)length) {
				return 0;
			}
			legacy_uint left = (legacy_uint)moved, right = (legacy_uint)moved_end;
			legacy_uint begin = left < frame ? left : frame;
			legacy_uint finish = right > next ? right : next;
			/* Keep separate pulses separate, including when the destination
			 * overlaps the original hold. Never jump over another shift. */
			for (legacy_uint i = begin ? begin - 1U : begin; i < length && i <= finish; ++i) {
				if ((i < frame || i >= next) && (inputs[i] & mask)) {
					return 0;
				}
			}
			for (legacy_uint i = frame; i < next; ++i) {
				inputs[i] = (legacy_u8)(inputs[i] & ~mask);
			}
			for (legacy_uint i = left; i < right; ++i) {
				inputs[i] = (legacy_u8)((inputs[i] & ~mask) | shift);
			}
			*first = begin;
			*end = finish;
			return 1;
		}
		frame = next;
	}
	return 0;
}

struct individual {
	struct GENGHOST_POLICY policy;
	legacy_f64 fitness;
};

struct route_search {
	struct individual population[POPULATION];
	legacy_uint population_count;
	legacy_u8 *best_inputs;
	legacy_uint length;
	legacy_f64 fitness;
	struct GENGHOST_RESULT race;
	struct GENGHOST_POLICY best_policy;
	legacy_u64 trials;
	legacy_u64 revision;
	legacy_int completed_attempt;
	legacy_int demonstration;
	legacy_int ordinary_discovery;
	size_t catalog_cursor;
	legacy_u64 catalog_next_trial;
	legacy_int catalog_exhausted;
	legacy_uint catalog_parent_frontier;
	size_t catalog_deferred_cursor;
	legacy_uint catalog_deferred_segment;
	legacy_uint catalog_requeues;
	legacy_int catalog_deferred;
};

struct checkpoint_cache {
	struct GENGHOST_SNAPSHOT *checkpoints;
	struct GENGHOST_DRIVER *driver_checkpoints;
	legacy_uint checkpoint_count;
	legacy_uint boundary_count;
	legacy_uint *boundaries;
	struct GENGHOST_GUIDE_POINT *guide;
	legacy_uint guide_count;
	size_t checkpoint_route;
	legacy_u64 checkpoint_trial;
	legacy_int driver_traced;
};

struct search_context {
	struct checkpoint_cache *cache;
	struct checkpoint_cache caches[MAX_SEARCH_FAMILIES];
	const struct GENGHOST_OPTIONS *options;
	struct GENGHOST_SEARCH_RESULT *result;
	legacy_u32 random;
	legacy_u64 started;
	legacy_u64 next_progress;
	genghost_save_function save;
	void *save_context;
	legacy_int guided_candidate;
	legacy_int portal_family;
	legacy_int tactic_candidate;
	legacy_int corkscrew_cut_available;
	legacy_int catalog_mutations_available;
	legacy_int dualway_family;
	legacy_int dualway_available;
	legacy_int dualway_candidate;
};

static legacy_int allocate_cache(struct checkpoint_cache *cache)
{
	legacy_uint count = genghost_sim_frame_limit() / CHECKPOINT_INTERVAL + 1U;
	cache->checkpoints = calloc(count, sizeof(*cache->checkpoints));
	cache->driver_checkpoints = calloc(count, sizeof(*cache->driver_checkpoints));
	cache->guide = calloc(genghost_sim_frame_limit(), sizeof(*cache->guide));
	cache->boundaries = malloc(genghost_sim_frame_limit() * sizeof(*cache->boundaries));
	return !cache->checkpoints || !cache->driver_checkpoints || !cache->guide || !cache->boundaries;
}

static void free_cache(struct checkpoint_cache *cache)
{
	free(cache->checkpoints);
	free(cache->driver_checkpoints);
	free(cache->boundaries);
	free(cache->guide);
}

static legacy_f64 elapsed(const struct search_context *context)
{
	return (legacy_f64)(SDL_GetTicksNS() - context->started) / (legacy_f64)SDL_NS_PER_SECOND;
}

static legacy_int expired(const struct search_context *context)
{
	if (context->options->progress) {
		context->result->elapsed_seconds = elapsed(context);
		if (context->options->progress(context->result, context->options->progress_context)) {
			return 1;
		}
	}
	return genghost_stop_requested() ||
		   (context->options->seconds > 0.0 && elapsed(context) >= context->options->seconds) ||
		   (context->options->evaluations &&
			context->result->evaluations >= context->options->evaluations);
}

static legacy_u32 random_word(struct search_context *context)
{
	legacy_u32 value = context->random;
	value ^= value << 13;
	value ^= value >> 17;
	value ^= value << 5;
	return context->random = value;
}

static legacy_f64 random_signed(struct search_context *context)
{
	return (legacy_f64)(random_word(context) & 0xffffffU) / 8388607.5 - 1.0;
}

static legacy_f64 clamp(legacy_f64 value, legacy_f64 low, legacy_f64 high)
{
	return value < low ? low : value > high ? high : value;
}

static void mutate_policy(struct search_context *context, struct GENGHOST_POLICY *policy,
						  legacy_int broad)
{
	legacy_uint first = random_word(context) % GENGHOST_POLICY_SEGMENTS;
	legacy_uint count = broad ? GENGHOST_POLICY_SEGMENTS : 1U + random_word(context) % 4U;
	for (legacy_uint n = 0; n < count; ++n) {
		struct GENGHOST_GENE *gene = &policy->segment[(first + n) % GENGHOST_POLICY_SEGMENTS];
		if (context->dualway_family && context->dualway_available) {
			legacy_u32 gate = random_word(context);
			if (gate % 4U == 0U) {
				legacy_u32 high = random_word(context);
				legacy_u32 low = random_word(context);
				legacy_u64 word = ((legacy_u64)high << 32U) | low;
				gene->dualway_choice = genghost_dualway_selector(gate, word);
			}
		}
		gene->speed_scale = clamp(gene->speed_scale + random_signed(context) * 0.28, 0.3, 3.0);
		gene->lookahead_scale =
			clamp(gene->lookahead_scale + random_signed(context) * 0.28, 0.3, 2.8);
		gene->lateral_offset =
			clamp(gene->lateral_offset + random_signed(context) * 0.4,
				  context->options->owoot ? -0.85 : -2.0, context->options->owoot ? 0.85 : 2.0);
		gene->shift_rpm_scale =
			clamp(gene->shift_rpm_scale + random_signed(context) * 0.12, 0.45, 1.05);
		if (random_word(context) % 4U == 0U) {
			gene->slide_brake = gene->slide_brake > 0.5 ? 0.0 : 1.0;
		}
		if (!context->options->owoot && random_word(context) % 4U == 0U) {
			legacy_uint choice = random_word(context);
			gene->shortcut_skip = choice % 5U;
			if (context->portal_family) {
				legacy_f64 delta = ((choice >> 8U) & 255U) / 127.5 - 1.0;
				gene->corner_scale = clamp(gene->corner_scale + delta * 0.2, 0.5, 1.8);
			}
			if (context->portal_family && (choice >> 4U) % 4U == 0U) {
				gene->line_mode = !gene->line_mode;
			}
			/* Fund each localized catalog recipe before allowing it in random
			 * refinement. Broad mutations must not force every obstacle at once. */
			if ((context->result->race.finished || context->corkscrew_cut_available ||
				 context->catalog_mutations_available) &&
				context->portal_family && !broad && (choice >> 6U) % 4U == 0U) {
				if (context->catalog_mutations_available) {
					gene->stunt_mode = (choice >> 12U) % (GENGHOST_STUNT_HUMAN_ALT + 1U);
				} else if (!context->result->race.finished) {
					gene->stunt_mode = (choice >> 12U) % 2U ? GENGHOST_STUNT_CORKSCREW_CUT : 0U;
				} else {
					gene->stunt_mode =
						(choice >> 12U) %
						(context->corkscrew_cut_available ? GENGHOST_STUNT_CORKSCREW_CUT + 1U : 5U);
				}
			}
		}
		if (context->options->powergear) {
			gene->powergear_bias =
				clamp(gene->powergear_bias + random_signed(context) * 0.3, 0.0, 1.0);
		}
	}
}

static void choose_policy(struct search_context *context, struct route_search *search,
						  const struct GENGHOST_ROUTE *route, struct GENGHOST_POLICY *policy)
{
	if (search->trials < genghost_barrel_seed_count(route)) {
		/* Tire-contact timing makes neighboring launch speeds physically distinct.
		 * Fund these complete pilots before repairing a failed coarse parent. */
		genghost_barrel_seed_policy(route, policy, (legacy_uint)search->trials);
		return;
	}
	legacy_int ordinary_fallback = search->ordinary_discovery && !context->result->race.finished &&
								   search->trials >= POPULATION && search->trials < POPULATION * 2U;
	if (search->trials < POPULATION || ordinary_fallback) {
		legacy_uint initial = (legacy_uint)(search->trials % POPULATION);
		static const legacy_f64 speeds[POPULATION] = {1.0, 0.75, 0.55, 1.2, 0.9, 1.5};
		genghost_policy_default(policy);
		for (legacy_uint i = 0; i < GENGHOST_POLICY_SEGMENTS; ++i) {
			policy->segment[i].speed_scale *= speeds[initial];
			/* Seed the short horizontal-cork line across driving profiles so
			 * refinement does not discard it because of a slower later sector.
			 * If these fail to finish, explicitly fund the ordinary profiles
			 * before attempting repairs; local mutations also retain both paths. */
			if (context->corkscrew_cut_available && !context->options->owoot &&
				!ordinary_fallback) {
				policy->segment[i].stunt_mode = GENGHOST_STUNT_CORKSCREW_CUT;
			}
			if (context->options->owoot && initial >= 3U && genghost_slalom_center_fits()) {
				/* OWOOT permits a physically clear slalom center passage. Keep
				 * the first three ordinary profiles and fund it in the others;
				 * the restricted driver ignores this gene for every other stunt. */
				policy->segment[i].stunt_mode = GENGHOST_STUNT_SLALOM_CENTER;
			}
			if (context->portal_family) {
				policy->segment[i].line_mode = 1U;
				if (context->dualway_family && context->dualway_available) {
					policy->segment[i].dualway_choice = 1U + initial % 2U;
				}
			}
			if (initial >= 3U) {
				policy->segment[i].slide_brake = 0.0;
			}
			if (initial == 4U) {
				policy->segment[i].lateral_offset = -0.35;
			}
			if (initial == 5U) {
				policy->segment[i].powergear_bias = 1.0;
			}
			if (context->portal_family) {
				static const legacy_uint skips[POPULATION] = {1U, 2U, 2U, 1U, 1U, 2U};
				policy->segment[i].speed_scale = initial % 2U ? 1.5 : 1.2;
				policy->segment[i].shortcut_skip = skips[initial];
				policy->segment[i].slide_brake = initial >= 4U ? 1.0 : 0.0;
				policy->segment[i].powergear_bias = initial % 2U ? 1.0 : 0.5;
				policy->segment[i].lateral_offset = 0.0;
			}
		}
		return;
	}
	legacy_uint first = random_word(context) % search->population_count;
	legacy_uint second = random_word(context) % search->population_count;
	if (search->population[second].fitness > search->population[first].fitness) {
		first = second;
	}
	*policy = search->population[first].policy;
	if (random_word(context) % 4U == 0U) {
		second = random_word(context) % search->population_count;
		legacy_uint cut = random_word(context) % GENGHOST_POLICY_SEGMENTS;
		for (legacy_uint i = cut; i < GENGHOST_POLICY_SEGMENTS; ++i) {
			policy->segment[i] = search->population[second].policy.segment[i];
		}
	}
	mutate_policy(context, policy, random_word(context) % 8U == 0U);
}

static void insert_policy(struct route_search *search, const struct GENGHOST_POLICY *policy,
						  legacy_f64 fitness)
{
	legacy_uint slot = search->population_count;
	if (slot == POPULATION) {
		slot = 0;
		for (legacy_uint i = 1; i < POPULATION; ++i) {
			if (search->population[i].fitness < search->population[slot].fitness) {
				slot = i;
			}
		}
		if (fitness <= search->population[slot].fitness) {
			return;
		}
	} else {
		++search->population_count;
	}
	search->population[slot].policy = *policy;
	search->population[slot].fitness = fitness;
}

static legacy_f64 candidate_fitness(const struct GENGHOST_ROUTE *route,
									const struct GENGHOST_DRIVER *driver,
									const struct GENGHOST_RESULT *race)
{
	if (race->finished) {
		return 1000000000.0 - (legacy_f64)race->total_frames * 1000.0 - race->finish_frame * 0.01;
	}
	return 1000000.0 * clamp(driver->progress / route->length, 0.0, 1.0) - driver->off_route * 2.0 -
		   race->frames * 0.03 - race->penalty_frames * 10.0;
}

static legacy_int save_candidate(struct search_context *context, legacy_u8 *inputs,
								 legacy_uint length, const struct GENGHOST_RESULT *race)
{
	if (!race->finished || (context->result->race.finished &&
							race->total_frames >= context->result->race.total_frames)) {
		return 0;
	}
	if (context->options->fine_tune_gears_only) {
		/* Every candidate retains the entire source timeline, including input
		 * channels after an improved finish. Only shift buttons may differ. */
		if (context->result->length) {
			length = context->result->length;
		}
		if (length > genghost_sim_frame_limit()) {
			return -1;
		}
	} else {
		/* Store the normal one-second finish tail, never truncate or claim a finish
		 * based on route geometry. Replay it again from a clean canonical state. */
		length = race->finish_frame;
		if (length > genghost_sim_frame_limit()) {
			return -1;
		}
		legacy_uint tail = genghost_sim_frame_limit() - length;
		if (tail > genghost_sim_frame_rate()) {
			tail = genghost_sim_frame_rate();
		}
		memset(inputs + length, 0, tail);
		length += tail;
	}
	struct GENGHOST_RESULT verified;
	if (genghost_sim_evaluate(inputs, length, &verified) || !verified.finished ||
		verified.total_frames != race->total_frames) {
		fputs("genghost: candidate failed full replay validation\n", stderr);
		return -1;
	}
	context->result->simulated_frames += verified.frames;
	if (context->tactic_candidate) {
		++context->result->tactic_improvements;
	}
	if (context->portal_family == 1) {
		++context->result->tile_improvements;
	}
	if (context->dualway_candidate) {
		++context->result->dualway_improvements;
	}
	if (context->guided_candidate) {
		++context->result->guided_improvements;
	}
	memcpy(context->result->inputs, inputs, length);
	context->result->length = length;
	context->result->race = verified;
	context->result->elapsed_seconds = elapsed(context);
	fprintf(stderr, "best %.2fs (driving %.2fs + penalty %.2fs), %" LEGACY_PRIu64 " candidates\n",
			verified.total_frames / (legacy_f64)genghost_sim_frame_rate(),
			verified.finish_frame / (legacy_f64)genghost_sim_frame_rate(),
			verified.penalty_frames / (legacy_f64)genghost_sim_frame_rate(),
			(legacy_u64)context->result->evaluations);
	return context->save(context->result, context->save_context) ? -1 : 0;
}

static legacy_uint run_policy(struct search_context *context, const struct GENGHOST_ROUTE *route,
							  const struct GENGHOST_POLICY *policy, legacy_u8 *inputs,
							  struct GENGHOST_DRIVER *driver, struct GENGHOST_RESULT *race)
{
	legacy_uint length = 0, last_progress_tick = 0;
	legacy_f64 furthest = 0.0;
	genghost_sim_reset();
	genghost_driver_reset(driver);
	while (length < genghost_sim_frame_limit() - genghost_sim_frame_rate()) {
		if (length % 64U == 0U && expired(context)) {
			break;
		}
		legacy_u8 input =
			genghost_driver_step(route, policy, driver, context->options->powergear,
								 context->options->collision, context->options->owoot);
		inputs[length++] = input;
		legacy_int event = genghost_sim_step(input);
		++context->result->simulated_frames;
		genghost_driver_observe(route, driver);
		if (driver->progress > furthest + 8.0) {
			furthest = driver->progress;
			last_progress_tick = length;
		}
		if (event || length - last_progress_tick > 400U) {
			break;
		}
	}
	genghost_sim_result(race);
	return length;
}

static void cache_checkpoints(struct search_context *context, size_t route_index,
							  const struct GENGHOST_ROUTE *route, const struct route_search *search,
							  legacy_int trace_driver)
{
	if (context->cache->checkpoint_count && context->cache->checkpoint_route == route_index &&
		context->cache->checkpoint_trial == search->revision &&
		(!trace_driver || context->cache->driver_traced)) {
		return;
	}
	genghost_sim_reset();
	struct GENGHOST_DRIVER driver;
	genghost_driver_reset(&driver);
	context->cache->checkpoint_count = 0;
	context->cache->boundary_count = 0;
	context->cache->guide_count = 0;
	for (legacy_uint i = 0; i < search->length; ++i) {
		if (i && search->best_inputs[i] != search->best_inputs[i - 1U]) {
			context->cache->boundaries[context->cache->boundary_count++] = i;
		}
		if (i % CHECKPOINT_INTERVAL == 0U) {
			context->cache->driver_checkpoints[context->cache->checkpoint_count] = driver;
			genghost_sim_capture(&context->cache->checkpoints[context->cache->checkpoint_count++]);
		}
		if (search->race.finished && i < search->race.finish_frame) {
			genghost_guide_capture(&context->cache->guide[context->cache->guide_count++],
								   search->best_inputs[i]);
		}
		legacy_int event = genghost_sim_step(search->best_inputs[i]);
		if (!search->race.finished || trace_driver) {
			genghost_driver_observe(route, &driver);
		}
		++context->result->simulated_frames;
		if (event) {
			break;
		}
	}
	context->cache->checkpoint_route = route_index;
	context->cache->checkpoint_trial = search->revision;
	context->cache->driver_traced = trace_driver || !search->race.finished;
}

/* A catalog proposal targets one actual stunt occurrence and its approach,
 * instead of setting every policy segment. Queue both lines under the active collision
 * rules after the ordinary warmup, with at least four ordinary attempts between
 * proposals. No finished lap or human demonstration is needed to fund them. */
static legacy_int choose_catalog_policy(struct search_context *context,
										const struct GENGHOST_ROUTES *routes, size_t route_index,
										struct route_search *search, struct GENGHOST_POLICY *policy,
										legacy_uint *first_segment, legacy_char *error,
										size_t error_size)
{
	/* A probe beyond the parent's reached prefix cannot exercise its recipe.
	 * Retry that deferred tail when the parent reaches it, at most twice; an
	 * improving ordinary lineage must not leave later stunts permanently unfunded. */
	if (!context->options->owoot && search->catalog_exhausted && search->catalog_deferred &&
		search->catalog_requeues < 2U &&
		search->catalog_parent_frontier >= search->catalog_deferred_segment) {
		search->catalog_cursor = search->catalog_deferred_cursor;
		search->catalog_exhausted = 0;
		search->catalog_deferred = 0;
		++search->catalog_requeues;
	}
	legacy_uint warmup = search->ordinary_discovery && !context->result->race.finished
							 ? POPULATION * 2U
							 : POPULATION;
	if (context->options->owoot || search->catalog_exhausted || search->trials < warmup ||
		search->trials < search->catalog_next_trial) {
		return 0;
	}
	if (search->best_inputs) {
		*policy = search->best_policy;
	} else {
		genghost_policy_default(policy);
	}
	struct GENGHOST_ROUTE effective;
	const struct GENGHOST_ROUTE *route = &routes->routes[route_index];
	legacy_int changed =
		genghost_route_policy(routes, route_index, policy, &effective, error, error_size);
	if (changed < 0) {
		return -1;
	}
	if (changed) {
		route = &effective;
	}
	size_t ordinal = 0U;
	legacy_int found = 0;
	for (size_t begin = 0U; begin < route->point_count;) {
		size_t end = begin;
		while (end + 1U < route->point_count &&
			   route->points[end + 1U].piece_index == route->points[begin].piece_index) {
			++end;
		}
		if (end > begin && genghost_stunt_catalog_model(route->points[begin].physical_model)) {
			if (ordinal == search->catalog_cursor / 2U) {
				legacy_f64 start = clamp(route->points[begin].distance - 900.0, 0.0, route->length);
				legacy_uint first = (legacy_uint)(start / route->length * GENGHOST_POLICY_SEGMENTS);
				legacy_uint last = (legacy_uint)(route->points[end].distance / route->length *
												 GENGHOST_POLICY_SEGMENTS);
				if (first >= GENGHOST_POLICY_SEGMENTS) {
					first = GENGHOST_POLICY_SEGMENTS - 1U;
				}
				if (last >= GENGHOST_POLICY_SEGMENTS) {
					last = GENGHOST_POLICY_SEGMENTS - 1U;
				}
				legacy_uint mode =
					search->catalog_cursor % 2U ? GENGHOST_STUNT_HUMAN_ALT : GENGHOST_STUNT_HUMAN;
				for (legacy_uint segment = first; segment <= last; ++segment) {
					policy->segment[segment].stunt_mode = mode;
				}
				*first_segment = first;
				++search->catalog_cursor;
				search->catalog_next_trial = search->trials + 4U;
				found = 1;
				break;
			}
			++ordinal;
		}
		begin = end + 1U;
	}
	if (!found) {
		search->catalog_exhausted = 1;
	}
	if (changed) {
		genghost_route_release(&effective);
	}
	return found;
}

static legacy_uint run_catalog_policy(struct search_context *context, size_t route_index,
									  const struct GENGHOST_ROUTE *route,
									  const struct route_search *search,
									  const struct GENGHOST_POLICY *policy,
									  legacy_uint first_segment, legacy_u8 *inputs,
									  struct GENGHOST_DRIVER *driver, struct GENGHOST_RESULT *race)
{
	if (!search->best_inputs || !search->length) {
		return run_policy(context, route, policy, inputs, driver, race);
	}
	cache_checkpoints(context, route_index, route, search, 1);
	legacy_uint checkpoint = 0U;
	for (legacy_uint next = 1U; next < context->cache->checkpoint_count; ++next) {
		if (context->cache->driver_checkpoints[next].segment >= first_segment) {
			break;
		}
		checkpoint = next;
	}
	legacy_uint length = checkpoint * CHECKPOINT_INTERVAL;
	memcpy(inputs, search->best_inputs, length);
	genghost_sim_restore(&context->cache->checkpoints[checkpoint]);
	*driver = context->cache->driver_checkpoints[checkpoint];
	driver->tactic_ticks = 0U;
	legacy_uint last_progress_tick = length;
	legacy_f64 furthest = driver->progress;
	legacy_int replay_prefix = 1;
	while (length < genghost_sim_frame_limit() - genghost_sim_frame_rate()) {
		if (length % 64U == 0U && expired(context)) {
			break;
		}
		if (driver->segment >= first_segment || length >= search->length) {
			replay_prefix = 0;
		}
		legacy_u8 input =
			replay_prefix
				? search->best_inputs[length]
				: genghost_driver_step(route, policy, driver, context->options->powergear,
									   context->options->collision, context->options->owoot);
		inputs[length++] = input;
		legacy_int event = genghost_sim_step(input);
		++context->result->simulated_frames;
		genghost_driver_observe(route, driver);
		if (driver->progress > furthest + 8.0) {
			furthest = driver->progress;
			last_progress_tick = length;
		}
		if (event || length - last_progress_tick > 400U) {
			break;
		}
	}
	genghost_sim_result(race);
	return length;
}

/* Human laps encode useful control boundaries: brake entry/release, steering
 * corrections and gear changes. Move those boundaries without overwriting the
 * other controls. Small coordinated timing edits preserve far more of an expert
 * line than replacing a random long block of all the inputs. */
static legacy_uint mutate_inputs(struct search_context *context, const struct route_search *search,
								 legacy_u8 *inputs, legacy_uint *start)
{
	legacy_uint length = search->race.finished ? search->race.finish_frame : search->length;
	if (length > search->length) {
		length = search->length;
	}
	memcpy(inputs, search->best_inputs, length);
	*start = random_word(context) % length;
	legacy_uint operation = random_word(context) % 12U;
	legacy_uint boundary =
		context->cache->boundary_count
			? context->cache->boundaries[random_word(context) % context->cache->boundary_count]
			: *start;
	if (boundary >= length) {
		boundary = length - 1U;
	}
	if ((operation < 6U || operation == 8U) && boundary > 0U) {
		static const legacy_uint masks[] = {3U, 12U, 48U};
		static const legacy_uint widths[] = {1U, 1U, 2U, 2U, 3U, 4U, 6U, 8U};
		legacy_uint changed = inputs[boundary] ^ inputs[boundary - 1U];
		legacy_uint channel = random_word(context) % 3U;
		for (legacy_uint n = 0; n < 3U && !(changed & masks[channel]); ++n) {
			channel = (channel + 1U) % 3U;
		}
		legacy_uint mask = masks[channel];
		legacy_u8 before = inputs[boundary - 1U] & mask;
		legacy_u8 after = inputs[boundary] & mask;
		legacy_uint width = widths[random_word(context) % 8U];
		legacy_int later = random_word(context) % 2U;
		if (mask == 3U && random_word(context) % 4U != 0U) {
			/* Usually test less braking/coasting, while retaining exploratory
			 * earlier braking for tighter and faster corner exits. */
			later = after != 1U;
		}
		legacy_uint run_end = boundary + 1U;
		while (run_end < length && (inputs[run_end] & mask) == after) {
			++run_end;
		}
		if (operation == 8U && run_end < length) {
			/* Shift an entire correction/gear pulse, preserving its duration
			 * and keeping all subsequent input timestamps unchanged. */
			if (width > run_end - boundary) {
				width = run_end - boundary;
			}
			if (later && width > length - run_end) {
				width = length - run_end;
			}
			if (!later && width > boundary) {
				width = boundary;
			}
			legacy_u8 following = inputs[run_end] & mask;
			if (context->options->fine_tune) {
				legacy_uint available = 0U;
				while (available < width &&
					   (later ? (inputs[run_end + available] & mask) == following
							  : (inputs[boundary - available - 1U] & mask) == before)) {
					++available;
				}
				width = available;
			}
			legacy_uint first = later ? boundary : boundary - width;
			legacy_uint second = later ? run_end : run_end - width;
			for (legacy_uint i = 0; i < width; ++i) {
				inputs[first + i] =
					(legacy_u8)((inputs[first + i] & ~mask) | (later ? before : after));
				inputs[second + i] =
					(legacy_u8)((inputs[second + i] & ~mask) | (later ? after : following));
			}
			*start = first;
		} else if (later) {
			if (width > run_end - boundary) {
				width = run_end - boundary;
			}
			for (legacy_uint i = boundary; i < boundary + width; ++i) {
				inputs[i] = (legacy_u8)((inputs[i] & ~mask) | before);
			}
			*start = boundary;
		} else {
			legacy_uint first = boundary;
			while (first && boundary - first < width && (inputs[first - 1U] & mask) == before) {
				--first;
			}
			for (legacy_uint i = first; i < boundary; ++i) {
				inputs[i] = (legacy_u8)((inputs[i] & ~mask) | after);
			}
			*start = first;
		}
		return length;
	}
	legacy_uint width = 1U << (random_word(context) % 6U);
	if (operation == 7U || operation == 11U) {
		*start = boundary;
		width = 1U + random_word(context) % 8U;
	}
	if (width > length - *start) {
		width = length - *start;
	}
	switch (operation) {
		case 7:
			memmove(inputs + *start, inputs + *start + width, length - *start - width);
			length -= width;
			break;
		case 11:
			if (length + width < genghost_sim_frame_limit() - genghost_sim_frame_rate()) {
				memmove(inputs + *start + width, inputs + *start, length - *start);
				memset(inputs + *start, inputs[*start + width], width);
				length += width;
			}
			break;
		case 9: {
			static const legacy_u8 steering[] = {0U, 4U, 8U};
			legacy_u8 mask = steering[random_word(context) % 3U];
			for (legacy_uint i = *start; i < *start + width; ++i) {
				inputs[i] = (legacy_u8)((inputs[i] & ~12U) | mask);
			}
			break;
		}
		default: {
			static const legacy_u8 pedal[] = {1U, 1U, 0U, 2U};
			legacy_u8 mask = pedal[random_word(context) % 4U];
			for (legacy_uint i = *start; i < *start + width; ++i) {
				inputs[i] = (legacy_u8)((inputs[i] & ~3U) | mask);
			}
			break;
		}
	}
	return length;
}

static legacy_uint run_mutation(struct search_context *context, size_t route_index,
								const struct GENGHOST_ROUTE *route,
								const struct route_search *search, legacy_u8 *inputs,
								struct GENGHOST_DRIVER *driver, struct GENGHOST_RESULT *race)
{
	cache_checkpoints(context, route_index, route, search, 0);
	legacy_uint start, changed_end;
	legacy_uint length = search->race.finish_frame;
	legacy_int shift = 0;
	legacy_int gears_only = context->options->fine_tune_gears_only;
	if (gears_only) {
		memcpy(inputs, search->best_inputs, search->length);
		start = changed_end = 0U;
		shift = genghost_shift_inputs(inputs, length, search->trials, &start, &changed_end);
		length = search->length;
	} else if (context->options->fine_tune && search->trials % 2U == 0U) {
		memcpy(inputs, search->best_inputs, length);
		shift = genghost_shift_inputs(inputs, length, search->trials / 2U, &start, &changed_end);
	}
	if (!shift && !gears_only) {
		length = mutate_inputs(context, search, inputs, &start);
	} else if (shift && !gears_only) {
		/* A shift may trade a later finish for fewer penalty frames. Keep the
		 * parent's tail available so the native total can still improve. */
		memcpy(inputs + length, search->best_inputs + length, search->length - length);
		length = search->length;
	}
	legacy_uint common = 0, parent_length = context->cache->guide_count;
	while (common < length && common < parent_length &&
		   inputs[length - common - 1U] == search->best_inputs[parent_length - common - 1U]) {
		++common;
	}
	changed_end = length - common;
	legacy_uint guide_cursor = parent_length - common;
	/* Most attempts retain the exact suffix. A minority re-time its demonstrated
	 * controls by position, so a faster sector can rejoin the line instead of
	 * applying every later corner input at the old wall-clock timestamp. */
	legacy_int guided =
		!gears_only && !shift && search->trials % 16U < 2U && common > 16U && changed_end > start;
	legacy_uint limit = guided ? length + 120U : length;
	if (limit > genghost_sim_frame_limit()) {
		limit = genghost_sim_frame_limit();
	}
	context->guided_candidate = guided;
	if (guided) {
		++context->result->guided_evaluations;
	}
	legacy_uint checkpoint = start / CHECKPOINT_INTERVAL;
	if (checkpoint >= context->cache->checkpoint_count) {
		checkpoint = 0;
	}
	genghost_sim_restore(&context->cache->checkpoints[checkpoint]);
	*driver = context->cache->driver_checkpoints[checkpoint];
	legacy_uint i = checkpoint * CHECKPOINT_INTERVAL;
	for (; i < limit; ++i) {
		if (i % 64U == 0U && expired(context)) {
			break;
		}
		if (guided && i >= changed_end) {
			inputs[i] = genghost_guide_step(context->cache->guide, context->cache->guide_count,
											&guide_cursor, search->trials % 16U ? 0.35 : 0.0);
		}
		legacy_int event = genghost_sim_step(inputs[i]);
		++context->result->simulated_frames;
		if (event) {
			++i;
			break;
		}
	}
	genghost_sim_result(race);
	return i;
}

static legacy_uint repair_suffix(struct search_context *context, size_t route_index,
								 const struct GENGHOST_ROUTE *route,
								 const struct route_search *search, struct GENGHOST_POLICY *policy,
								 legacy_u8 *inputs, struct GENGHOST_DRIVER *driver,
								 struct GENGHOST_RESULT *race)
{
	cache_checkpoints(context, route_index, route, search, 0);
	legacy_uint rollback = 32U << (random_word(context) % 5U);
	legacy_uint start = search->length > rollback ? search->length - rollback : 0U;
	legacy_uint checkpoint = start / CHECKPOINT_INTERVAL;
	if (checkpoint >= context->cache->checkpoint_count) {
		checkpoint = 0;
	}
	legacy_uint length = checkpoint * CHECKPOINT_INTERVAL;
	memcpy(inputs, search->best_inputs, length);
	genghost_sim_restore(&context->cache->checkpoints[checkpoint]);
	*driver = context->cache->driver_checkpoints[checkpoint];
	driver->tactic_ticks = 0U;
	*policy = search->best_policy;
	/* Retain the successful prefix and search the failing sector with a fresh
	 * speed/line/shift choice. This builds a lap incrementally instead of
	 * repeatedly losing earlier hard-won stunt entries. */
	for (legacy_uint n = driver->segment; n < GENGHOST_POLICY_SEGMENTS && n < driver->segment + 3U;
		 ++n) {
		struct GENGHOST_GENE *gene = &policy->segment[n];
		gene->speed_scale = clamp(gene->speed_scale + random_signed(context) * 0.5, 0.3, 3.0);
		gene->lookahead_scale =
			clamp(gene->lookahead_scale + random_signed(context) * 0.5, 0.3, 2.8);
		gene->lateral_offset =
			clamp(gene->lateral_offset + random_signed(context) * 0.65,
				  context->options->owoot ? -0.85 : -2.0, context->options->owoot ? 0.85 : 2.0);
		gene->shift_rpm_scale =
			clamp(gene->shift_rpm_scale + random_signed(context) * 0.2, 0.45, 1.05);
		if (random_word(context) % 3U == 0U) {
			gene->slide_brake = gene->slide_brake > 0.5 ? 0.0 : 1.0;
		}
		if (!context->options->owoot && random_word(context) % 3U == 0U) {
			legacy_uint choice = random_word(context);
			gene->shortcut_skip = choice % 5U;
			if (context->portal_family) {
				legacy_f64 delta = ((choice >> 8U) & 255U) / 127.5 - 1.0;
				gene->corner_scale = clamp(gene->corner_scale + delta * 0.2, 0.5, 1.8);
			}
			if (context->portal_family && (choice >> 4U) % 4U == 0U) {
				gene->line_mode = !gene->line_mode;
			}
			if ((context->result->race.finished || context->corkscrew_cut_available ||
				 context->catalog_mutations_available) &&
				context->portal_family && (choice >> 6U) % 2U == 0U) {
				if (context->catalog_mutations_available) {
					gene->stunt_mode = (choice >> 12U) % (GENGHOST_STUNT_HUMAN_ALT + 1U);
				} else if (!context->result->race.finished) {
					gene->stunt_mode = (choice >> 12U) % 2U ? GENGHOST_STUNT_CORKSCREW_CUT : 0U;
				} else {
					gene->stunt_mode =
						(choice >> 12U) %
						(context->corkscrew_cut_available ? GENGHOST_STUNT_CORKSCREW_CUT + 1U : 5U);
				}
			}
		}
	}
	legacy_uint last_progress_tick = length;
	legacy_f64 furthest = driver->progress;
	while (length < genghost_sim_frame_limit() - genghost_sim_frame_rate()) {
		if (length % 64U == 0U && expired(context)) {
			break;
		}
		legacy_u8 input =
			genghost_driver_step(route, policy, driver, context->options->powergear,
								 context->options->collision, context->options->owoot);
		inputs[length++] = input;
		legacy_int event = genghost_sim_step(input);
		++context->result->simulated_frames;
		genghost_driver_observe(route, driver);
		if (driver->progress > furthest + 8.0) {
			furthest = driver->progress;
			last_progress_tick = length;
		}
		if (event || length - last_progress_tick > 400U) {
			break;
		}
	}
	genghost_sim_result(race);
	return length;
}

/* A fine-tune parent is always the current canonically verified incumbent.
 * Reuse its native checkpoints and real control boundaries, spending no budget
 * on autonomous policy warmup or unrelated route populations. */
static legacy_int refine_seed(struct search_context *context, const struct GENGHOST_ROUTE *route,
							  struct route_search *parent, legacy_u8 *inputs)
{
	while (!expired(context)) {
		struct GENGHOST_DRIVER driver;
		struct GENGHOST_RESULT race;
		context->guided_candidate = 0;
		legacy_uint length = run_mutation(context, 0U, route, parent, inputs, &driver, &race);
		++parent->trials;
		++context->result->evaluations;
		++context->result->input_evaluations;
		legacy_uint previous_total = context->result->race.total_frames;
		if (save_candidate(context, inputs, length, &race)) {
			return -1;
		}
		if (context->result->race.total_frames < previous_total) {
			parent->race = context->result->race;
			parent->length = context->result->length;
			memcpy(parent->best_inputs, context->result->inputs, parent->length);
			++parent->revision;
			context->cache->checkpoint_count = 0U;
		}
		legacy_u64 now = SDL_GetTicksNS();
		if (now >= context->next_progress) {
			fprintf(stderr,
					"fine-tune progress: %" LEGACY_PRIu64 " candidates, best %.2fs, %.1fs\n",
					(legacy_u64)context->result->evaluations,
					context->result->race.total_frames / (legacy_f64)genghost_sim_frame_rate(),
					elapsed(context));
			context->next_progress = now + 5000000000ULL;
		}
	}
	return 0;
}

/* Keep whole route sweeps together so each family preserves its own candidate
 * sequence. A new family receives its initial policies before score selection;
 * routes without compatible switching geometry do not delay that warmup. */
static legacy_uint choose_family(const struct GENGHOST_ROUTES *routes,
								 const struct route_search *searches, legacy_uint family_count,
								 legacy_u64 round)
{
	if (family_count == 1U) {
		return 0U;
	}
	legacy_uint family = round % 3U == 2U;
	legacy_uint pending[MAX_SEARCH_FAMILIES] = {0};
	legacy_int initialized = 1;
	legacy_f64 best_score = DBL_MAX;
	legacy_uint winner = 0U;
	for (legacy_uint candidate = 0; candidate < family_count; ++candidate) {
		for (size_t index = 0; index < routes->count; ++index) {
			if (candidate == 2U && !routes->routes[index].dualway_count) {
				continue;
			}
			const struct route_search *search = &searches[candidate * routes->count + index];
			if (search->trials < POPULATION) {
				initialized = 0;
				pending[candidate] = 1U;
			}
			if (search->race.finished && search->race.total_frames < best_score) {
				best_score = search->race.total_frames;
				winner = candidate;
			}
		}
	}
	if (family_count > 2U && !initialized) {
		for (legacy_uint offset = 0; offset < family_count; ++offset) {
			legacy_uint candidate = (legacy_uint)((round + offset) % family_count);
			if (pending[candidate]) {
				return candidate;
			}
		}
	}
	if (initialized && best_score < DBL_MAX) {
		family = winner;
		if (round % 4U == 3U) {
			legacy_uint offset = 1U + (legacy_uint)((round / 4U) % (family_count - 1U));
			family = (winner + offset) % family_count;
		}
	}
	return family;
}

legacy_int genghost_search(const struct GENGHOST_OPTIONS *options, const legacy_u8 *seed_inputs,
						   legacy_uint seed_length, struct GENGHOST_SEARCH_RESULT *result,
						   genghost_save_function save, void *save_context)
{
	struct GENGHOST_OPTIONS normalized = *options;
	if (normalized.fine_tune_gears_only) {
		normalized.fine_tune = 1;
	}
	options = &normalized;
	struct GENGHOST_ROUTES routes = {0};
	struct search_context context = {0};
	legacy_char error[256];
	legacy_int status = 0;
	legacy_uint family_count = 1U;
	legacy_u64 family_round = 0U;
	legacy_u32 family_random[MAX_SEARCH_FAMILIES];
	legacy_u8 *inputs = malloc(genghost_sim_frame_limit());
	struct route_search *searches = NULL;
	memset(result, 0, sizeof(*result));
	context.cache = &context.caches[0];
	context.options = options;
	context.result = result;
	context.random = options->seed ? options->seed : 1U;
	family_random[0] = context.random;
	family_random[1] = context.random ^ 0x9e3779b9U;
	if (!family_random[1]) {
		family_random[1] = 1U;
	}
	family_random[2] = context.random ^ 0x85ebca6bU;
	if (!family_random[2]) {
		family_random[2] = 1U;
	}
	context.started = SDL_GetTicksNS();
	context.next_progress = context.started + 5000000000ULL;
	context.save = save;
	context.save_context = save_context;
	if (seed_length > genghost_sim_frame_limit() || (seed_length && !seed_inputs) ||
		(options->fine_tune && !seed_length)) {
		fputs("genghost: fine-tune requires a completed compatible replay\n", stderr);
		status = -1;
		goto done;
	}
	if (!inputs || allocate_cache(context.cache)) {
		status = -1;
		goto done;
	}
	if (genghost_routes_build(&routes, error, sizeof(error))) {
		fprintf(stderr, "genghost: %s\n", error);
		status = -1;
		goto done;
	}
	result->routes = routes.count;
	result->dualway_pairs = routes.dualway_pairs;
	fprintf(stderr, "%zu complete route combinations, %zu branch nodes; %s power gear\n",
			routes.count, routes.branch_count,
			options->powergear && genghost_powergear_kind() ? "searching for" : "no beneficial");
	searches = calloc(routes.count, sizeof(*searches));
	if (!searches) {
		status = -1;
		goto done;
	}
	for (size_t i = 0; i < routes.count; ++i) {
		searches[i].fitness = -DBL_MAX;
	}
	if (seed_length) {
		struct GENGHOST_RESULT seed_result;
		memcpy(inputs, seed_inputs, seed_length);
		genghost_sim_evaluate(inputs, seed_length, &seed_result);
		result->seed_finished = seed_result.finished;
		result->seed_total_frames = seed_result.total_frames;
		++result->evaluations;
		result->simulated_frames += seed_result.frames;
		if (options->fine_tune && (!seed_result.finished || !seed_result.finish_frame)) {
			fputs("genghost: fine-tune replay does not finish under the selected rules\n", stderr);
			status = -1;
			goto done;
		}
		if (save_candidate(&context, inputs, seed_length, &seed_result)) {
			status = -1;
			goto done;
		}
		if (result->race.finished && result->length) {
			/* Keep the source as one input refinement parent, independently
			 * of the autonomous driving attempts on every route combination. */
			struct route_search *seed_parent = &searches[0];
			seed_parent->best_inputs = malloc(genghost_sim_frame_limit());
			if (!seed_parent->best_inputs) {
				status = -1;
				goto done;
			}
			memcpy(seed_parent->best_inputs, result->inputs, result->length);
			seed_parent->length = result->length;
			seed_parent->race = seed_result;
			seed_parent->fitness = 1000000000.0 - seed_result.total_frames * 1000.0;
			genghost_policy_default(&seed_parent->best_policy);
			seed_parent->demonstration = 1;
			seed_parent->catalog_parent_frontier = GENGHOST_POLICY_SEGMENTS;
		}
	}
	if (options->fine_tune) {
		status = refine_seed(&context, &routes.routes[0], &searches[0], inputs);
		goto done;
	}
	/* Keep an independent tile-line population for unrestricted scratch driving.
	 * Classic and tile populations have distinct RNG streams and replay parents;
	 * the best validated output and the overall evaluation/time budget are shared.
	 * A compatible human demonstration and OWOOT retain the original single-family
	 * schedule, seed policies and random stream exactly. */
	if (!options->owoot && !result->seed_finished) {
		if (routes.count > SIZE_MAX / (2U * sizeof(*searches))) {
			status = -1;
			goto done;
		}
		struct route_search *grown = realloc(searches, routes.count * 2U * sizeof(*searches));
		if (!grown) {
			status = -1;
			goto done;
		}
		searches = grown;
		memset(searches + routes.count, 0, routes.count * sizeof(*searches));
		for (size_t i = routes.count; i < routes.count * 2U; ++i) {
			searches[i].fitness = -DBL_MAX;
		}
		if (allocate_cache(&context.caches[1])) {
			status = -1;
			goto done;
		}
		family_count = 2U;
	}
	/* Visit every route each round. Give completed parents a short batch of
	 * refinements to reuse their checkpoints and human control boundaries,
	 * while unfinished routes keep receiving policy and repair attempts. */
	while (!expired(&context)) {
		/* Preserve the original road/tile discovery until a replay has actually
		 * completed and passed canonical validation. Switching then receives an
		 * independent population, random stream and checkpoint cache. */
		if (family_count == 2U && routes.dualway_pairs && result->race.finished && result->length) {
			if (routes.count > SIZE_MAX / (MAX_SEARCH_FAMILIES * sizeof(*searches))) {
				status = -1;
				goto done;
			}
			struct route_search *grown =
				realloc(searches, routes.count * MAX_SEARCH_FAMILIES * sizeof(*searches));
			if (!grown) {
				status = -1;
				goto done;
			}
			searches = grown;
			memset(searches + routes.count * 2U, 0, routes.count * sizeof(*searches));
			for (size_t i = routes.count * 2U; i < routes.count * MAX_SEARCH_FAMILIES; ++i) {
				searches[i].fitness = -DBL_MAX;
			}
			if (allocate_cache(&context.caches[2])) {
				status = -1;
				goto done;
			}
			family_count = MAX_SEARCH_FAMILIES;
		}
		legacy_uint family = choose_family(&routes, searches, family_count, family_round++);
		context.portal_family = (legacy_int)family;
		context.dualway_family = family == 2U;
		context.cache = &context.caches[family];
		context.random = family_random[family];
		for (size_t index = 0; index < routes.count && !expired(&context); ++index) {
			if (family == 2U && !routes.routes[index].dualway_count) {
				continue;
			}
			context.corkscrew_cut_available = 0;
			for (size_t point = 0; point < routes.routes[index].point_count; ++point) {
				if (!options->owoot && !routes.routes[index].points[point].roll_ramp &&
					routes.routes[index].points[point].physical_model ==
						PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT) {
					context.corkscrew_cut_available = 1;
					break;
				}
			}
			size_t search_index = family * routes.count + index;
			struct route_search *search = &searches[search_index];
			if (context.corkscrew_cut_available && !result->race.finished &&
				search->trials == POPULATION) {
				/* Failed cut parents may reach farther than ordinary profiles and
				 * crowd them out forever. Restart discovery with an independent
				 * ordinary lineage after funding all initial cut profiles. */
				legacy_u64 revision = search->revision + 1U;
				free(search->best_inputs);
				memset(search, 0, sizeof(*search));
				search->trials = POPULATION;
				search->revision = revision;
				search->fitness = -DBL_MAX;
				search->ordinary_discovery = 1;
				context.cache->checkpoint_count = 0U;
				context.cache->boundary_count = 0U;
				context.cache->guide_count = 0U;
			}
			if (search->ordinary_discovery && !result->race.finished) {
				context.corkscrew_cut_available = 0;
			}
			legacy_uint attempts = search->race.finished && search->trials >= POPULATION ? 3U : 1U;
			for (legacy_uint attempt = 0; attempt < attempts && !expired(&context); ++attempt) {
				struct GENGHOST_POLICY policy;
				struct GENGHOST_DRIVER driver;
				struct GENGHOST_RESULT race;
				context.guided_candidate = 0;
				legacy_uint catalog_first_segment = 0U;
				legacy_int barrel_seed =
					search->trials < genghost_barrel_seed_count(&routes.routes[index]);
				legacy_int catalog =
					barrel_seed
						? 0
						: choose_catalog_policy(&context, &routes, index, search, &policy,
												&catalog_first_segment, error, sizeof(error));
				if (catalog < 0) {
					fprintf(stderr, "genghost: %s\n", error);
					status = -1;
					goto done;
				}
				context.catalog_mutations_available = !options->owoot && result->race.finished &&
													  search->catalog_exhausted &&
													  search->catalog_cursor != 0U;
				legacy_int mutation =
					!barrel_seed && !catalog && search->best_inputs && search->race.finished &&
					search->trials >= POPULATION &&
					random_word(&context) % (search->demonstration ? 16U : 3U) != 0U;
				legacy_int initial_fallback = search->ordinary_discovery &&
											  !result->race.finished &&
											  search->trials < POPULATION * 2U;
				legacy_int repair = !barrel_seed && !catalog && search->best_inputs &&
									!search->race.finished && !initial_fallback &&
									search->trials >= POPULATION &&
									random_word(&context) % 3U != 0U;
				context.dualway_available = routes.routes[index].dualway_count != 0U;
				if (!catalog && !mutation && !repair) {
					choose_policy(&context, search, &routes.routes[index], &policy);
					if (context.dualway_family && search->trials < POPULATION) {
						genghost_dualway_seed_policy(&routes, index, &policy,
													 (legacy_uint)search->trials);
					}
				}
				struct GENGHOST_ROUTE effective;
				const struct GENGHOST_ROUTE *route = &routes.routes[index];
				const struct GENGHOST_POLICY *route_policy =
					mutation || repair ? &search->best_policy : &policy;
				legacy_int changed_route = genghost_route_policy(&routes, index, route_policy,
																 &effective, error, sizeof(error));
				if (changed_route < 0) {
					fprintf(stderr, "genghost: %s\n", error);
					status = -1;
					goto done;
				}
				if (changed_route) {
					route = &effective;
				}
				context.dualway_candidate = changed_route > 0;
				if (context.dualway_candidate) {
					++result->dualway_evaluations;
				}
				legacy_uint length =
					catalog	   ? run_catalog_policy(&context, search_index, route, search, &policy,
													catalog_first_segment, inputs, &driver, &race)
					: mutation ? run_mutation(&context, search_index, route, search, inputs,
											  &driver, &race)
					: repair ? repair_suffix(&context, search_index, route, search, &policy, inputs,
											 &driver, &race)
							 : run_policy(&context, route, &policy, inputs, &driver, &race);
				if (!search->trials) {
					legacy_int attempted = 0;
					for (legacy_uint other = 0; other < family_count; ++other) {
						attempted |= searches[other * routes.count + index].trials != 0U;
					}
					if (!attempted) {
						++result->routes_attempted;
					}
				}
				if (catalog && !driver.tactic_ticks && driver.segment < catalog_first_segment &&
					(!search->catalog_deferred ||
					 search->catalog_cursor - 1U < search->catalog_deferred_cursor)) {
					search->catalog_deferred = 1;
					search->catalog_deferred_cursor = search->catalog_cursor - 1U;
					search->catalog_deferred_segment = catalog_first_segment;
				}
				context.tactic_candidate = !mutation && driver.tactic_ticks != 0U;
				if (context.tactic_candidate) {
					++result->tactic_evaluations;
				}
				/* Catalog probes consume the public budget without displacing an
				 * ordinary trial or advancing its random schedule. */
				if (!catalog) {
					++search->trials;
				}
				++result->evaluations;
				if (family == 1U) {
					++result->tile_evaluations;
				}
				if (mutation) {
					++result->input_evaluations;
				} else {
					++result->policy_evaluations;
				}
				if (race.finished && !search->completed_attempt) {
					legacy_int completed = 0;
					for (legacy_uint other = 0; other < family_count; ++other) {
						completed |= searches[other * routes.count + index].completed_attempt;
					}
					search->completed_attempt = 1;
					if (!completed) {
						++result->routes_finished;
					}
				}
				legacy_f64 fitness = candidate_fitness(route, &driver, &race);
				if (changed_route) {
					genghost_route_release(&effective);
				}
				if (!mutation && (!catalog || race.finished)) {
					insert_policy(search, &policy, fitness);
				}
				/* Failed localized probes keep both the ordinary policy population
				 * and replay parent intact. A verified finish has an unambiguous
				 * score and may replace that parent. */
				if ((!catalog || race.finished) && fitness > search->fitness && length) {
					if (!search->best_inputs) {
						search->best_inputs = malloc(genghost_sim_frame_limit());
					}
					if (!search->best_inputs) {
						status = -1;
						goto done;
					}
					search->fitness = fitness;
					search->race = race;
					search->catalog_parent_frontier =
						race.finished ? GENGHOST_POLICY_SEGMENTS : driver.segment;
					if (!mutation) {
						search->best_policy = policy;
						search->demonstration = 0;
					}
					search->length = length;
					++search->revision;
					memcpy(search->best_inputs, inputs, length);
					context.cache->checkpoint_count = 0;
				}
				if (save_candidate(&context, inputs, length, &race)) {
					status = -1;
					goto done;
				}
				legacy_u64 now = SDL_GetTicksNS();
				if (now >= context.next_progress) {
					fprintf(stderr,
							"progress: %" LEGACY_PRIu64
							" candidates, %zu/%zu routes attempted, %zu completed, "
							"%.1fs\n",
							(legacy_u64)result->evaluations, result->routes_attempted,
							result->routes, result->routes_finished, elapsed(&context));
					context.next_progress = now + 5000000000ULL;
				}
			}
		}
		family_random[family] = context.random;
	}
done:
	result->elapsed_seconds = elapsed(&context);
	if (searches) {
		for (size_t i = 0; i < routes.count * family_count; ++i) {
			free(searches[i].best_inputs);
		}
	}
	free(searches);
	free(inputs);
	for (legacy_uint family = 0; family < MAX_SEARCH_FAMILIES; ++family) {
		free_cache(&context.caches[family]);
	}
	genghost_routes_free(&routes);
	if (family_count > 1U) {
		fprintf(stderr,
				"tile family: %" LEGACY_PRIu64 " candidates, %" LEGACY_PRIu64
				" verified improvements\n",
				(legacy_u64)result->tile_evaluations, (legacy_u64)result->tile_improvements);
	}
	fprintf(stderr,
			"stunt tactics: %" LEGACY_PRIu64 " candidates, %" LEGACY_PRIu64
			" verified improvements\n",
			(legacy_u64)result->tactic_evaluations, (legacy_u64)result->tactic_improvements);
	if (status) {
		return status;
	}
	return result->race.finished ? 0 : 2;
}
