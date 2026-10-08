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

#undef memcpy

#define POPULATION 6U
#define MAX_SEARCH_FAMILIES 3U
#define CHECKPOINT_INTERVAL 64U
#define CHECKPOINT_COUNT (GENGHOST_MAX_FRAMES / CHECKPOINT_INTERVAL + 1U)

volatile sig_atomic_t genghost_interrupted;

struct individual {
	struct GENGHOST_POLICY policy;
	double fitness;
};

struct route_search {
	struct individual population[POPULATION];
	unsigned int population_count;
	unsigned char *best_inputs;
	unsigned int length;
	double fitness;
	struct GENGHOST_RESULT race;
	struct GENGHOST_POLICY best_policy;
	uint64_t trials;
	uint64_t revision;
	int completed_attempt;
	int demonstration;
	int ordinary_discovery;
};

struct checkpoint_cache {
	struct GENGHOST_SNAPSHOT *checkpoints;
	struct GENGHOST_DRIVER *driver_checkpoints;
	unsigned int checkpoint_count;
	unsigned int boundary_count;
	unsigned short *boundaries;
	struct GENGHOST_GUIDE_POINT *guide;
	unsigned int guide_count;
	size_t checkpoint_route;
	uint64_t checkpoint_trial;
};

struct search_context {
	struct checkpoint_cache *cache;
	struct checkpoint_cache caches[MAX_SEARCH_FAMILIES];
	const struct GENGHOST_OPTIONS *options;
	struct GENGHOST_SEARCH_RESULT *result;
	uint32_t random;
	uint64_t started;
	uint64_t next_progress;
	genghost_save_function save;
	void *save_context;
	int guided_candidate;
	int portal_family;
	int tactic_candidate;
	int corkscrew_cut_available;
	int dualway_family;
	int dualway_available;
	int dualway_candidate;
};

static int allocate_cache(struct checkpoint_cache *cache)
{
	cache->checkpoints = calloc(CHECKPOINT_COUNT, sizeof(*cache->checkpoints));
	cache->driver_checkpoints = calloc(CHECKPOINT_COUNT, sizeof(*cache->driver_checkpoints));
	cache->guide = calloc(GENGHOST_MAX_FRAMES, sizeof(*cache->guide));
	cache->boundaries = malloc(GENGHOST_MAX_FRAMES * sizeof(*cache->boundaries));
	return !cache->checkpoints || !cache->driver_checkpoints || !cache->guide || !cache->boundaries;
}

static void free_cache(struct checkpoint_cache *cache)
{
	free(cache->checkpoints);
	free(cache->driver_checkpoints);
	free(cache->boundaries);
	free(cache->guide);
}

static double elapsed(const struct search_context *context)
{
	return (double)(SDL_GetTicksNS() - context->started) / 1000000000.0;
}

static int expired(const struct search_context *context)
{
	return genghost_interrupted ||
		   (context->options->seconds > 0.0 && elapsed(context) >= context->options->seconds) ||
		   (context->options->evaluations &&
			context->result->evaluations >= context->options->evaluations);
}

static uint32_t random_word(struct search_context *context)
{
	uint32_t value = context->random;
	value ^= value << 13;
	value ^= value >> 17;
	value ^= value << 5;
	return context->random = value;
}

static double random_signed(struct search_context *context)
{
	return (double)(random_word(context) & 0xffffffU) / 8388607.5 - 1.0;
}

static double clamp(double value, double low, double high)
{
	return value < low ? low : value > high ? high : value;
}

static void mutate_policy(struct search_context *context, struct GENGHOST_POLICY *policy, int broad)
{
	unsigned int first = random_word(context) % GENGHOST_POLICY_SEGMENTS;
	unsigned int count = broad ? GENGHOST_POLICY_SEGMENTS : 1U + random_word(context) % 4U;
	for (unsigned int n = 0; n < count; ++n) {
		struct GENGHOST_GENE *gene = &policy->segment[(first + n) % GENGHOST_POLICY_SEGMENTS];
		if (context->dualway_family && context->dualway_available) {
			uint32_t gate = random_word(context);
			if (gate % 4U == 0U) {
				uint32_t high = random_word(context);
				uint32_t low = random_word(context);
				uint64_t word = ((uint64_t)high << 32U) | low;
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
			unsigned int choice = random_word(context);
			gene->shortcut_skip = choice % 5U;
			if (context->portal_family) {
				double delta = ((choice >> 8U) & 255U) / 127.5 - 1.0;
				gene->corner_scale = clamp(gene->corner_scale + delta * 0.2, 0.5, 1.8);
			}
			if (context->portal_family && (choice >> 4U) % 4U == 0U) {
				gene->line_mode = !gene->line_mode;
			}
			/* Before the first finish, only alternate the clear cork cut with
			 * ordinary traversal. Other stunt proposals need a validated lap;
			 * broad mutations must not force every obstacle at once. */
			if ((context->result->race.finished || context->corkscrew_cut_available) &&
				context->portal_family && !broad && (choice >> 6U) % 4U == 0U) {
				if (!context->result->race.finished) {
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
						  struct GENGHOST_POLICY *policy)
{
	int ordinary_fallback = search->ordinary_discovery && !context->result->race.finished &&
							search->trials >= POPULATION && search->trials < POPULATION * 2U;
	if (search->trials < POPULATION || ordinary_fallback) {
		unsigned int initial = (unsigned int)(search->trials % POPULATION);
		static const double speeds[POPULATION] = {1.0, 0.75, 0.55, 1.2, 0.9, 1.5};
		genghost_policy_default(policy);
		for (unsigned int i = 0; i < GENGHOST_POLICY_SEGMENTS; ++i) {
			policy->segment[i].speed_scale *= speeds[initial];
			/* Seed the short horizontal-cork line across driving profiles so
			 * refinement does not discard it because of a slower later sector.
			 * If these fail to finish, explicitly fund the ordinary profiles
			 * before attempting repairs; local mutations also retain both paths. */
			if (context->corkscrew_cut_available && !context->options->owoot &&
				!ordinary_fallback) {
				policy->segment[i].stunt_mode = GENGHOST_STUNT_CORKSCREW_CUT;
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
				static const unsigned int skips[POPULATION] = {1U, 2U, 2U, 1U, 1U, 2U};
				policy->segment[i].speed_scale = initial % 2U ? 1.5 : 1.2;
				policy->segment[i].shortcut_skip = skips[initial];
				policy->segment[i].slide_brake = initial >= 4U ? 1.0 : 0.0;
				policy->segment[i].powergear_bias = initial % 2U ? 1.0 : 0.5;
				policy->segment[i].lateral_offset = 0.0;
			}
		}
		return;
	}
	unsigned int first = random_word(context) % search->population_count;
	unsigned int second = random_word(context) % search->population_count;
	if (search->population[second].fitness > search->population[first].fitness) {
		first = second;
	}
	*policy = search->population[first].policy;
	if (random_word(context) % 4U == 0U) {
		second = random_word(context) % search->population_count;
		unsigned int cut = random_word(context) % GENGHOST_POLICY_SEGMENTS;
		for (unsigned int i = cut; i < GENGHOST_POLICY_SEGMENTS; ++i) {
			policy->segment[i] = search->population[second].policy.segment[i];
		}
	}
	mutate_policy(context, policy, random_word(context) % 8U == 0U);
}

static void insert_policy(struct route_search *search, const struct GENGHOST_POLICY *policy,
						  double fitness)
{
	unsigned int slot = search->population_count;
	if (slot == POPULATION) {
		slot = 0;
		for (unsigned int i = 1; i < POPULATION; ++i) {
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

static double candidate_fitness(const struct GENGHOST_ROUTE *route,
								const struct GENGHOST_DRIVER *driver,
								const struct GENGHOST_RESULT *race)
{
	if (race->finished) {
		return 1000000000.0 - (double)race->total_frames * 1000.0 - race->finish_frame * 0.01;
	}
	return 1000000.0 * clamp(driver->progress / route->length, 0.0, 1.0) - driver->off_route * 2.0 -
		   race->frames * 0.03 - race->penalty_frames * 10.0;
}

static int save_candidate(struct search_context *context, unsigned char *inputs,
						  unsigned int length, const struct GENGHOST_RESULT *race)
{
	struct GENGHOST_RESULT verified;
	if (!race->finished || (context->result->race.finished &&
							race->total_frames >= context->result->race.total_frames)) {
		return 0;
	}
	/* Store the normal one-second finish tail, never truncate or claim a finish
	 * based on route geometry. Replay it again from a clean canonical state. */
	length = race->finish_frame;
	if (length > GENGHOST_MAX_FRAMES) {
		return -1;
	}
	unsigned int tail = GENGHOST_MAX_FRAMES - length;
	if (tail > GENGHOST_FRAME_RATE) {
		tail = GENGHOST_FRAME_RATE;
	}
	memset(inputs + length, 0, tail);
	length += tail;
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
	fprintf(stderr, "best %.2fs (driving %.2fs + penalty %.2fs), %llu candidates\n",
			verified.total_frames / 20.0, verified.finish_frame / 20.0,
			verified.penalty_frames / 20.0, (unsigned long long)context->result->evaluations);
	return context->save(context->result, context->save_context) ? -1 : 0;
}

static unsigned int run_policy(struct search_context *context, const struct GENGHOST_ROUTE *route,
							   const struct GENGHOST_POLICY *policy, unsigned char *inputs,
							   struct GENGHOST_DRIVER *driver, struct GENGHOST_RESULT *race)
{
	unsigned int length = 0, last_progress_tick = 0;
	double furthest = 0.0;
	genghost_sim_reset();
	genghost_driver_reset(driver);
	while (length < GENGHOST_MAX_FRAMES - GENGHOST_FRAME_RATE) {
		if (length % 64U == 0U && expired(context)) {
			break;
		}
		unsigned char input =
			genghost_driver_step(route, policy, driver, context->options->powergear,
								 context->options->collision, context->options->owoot);
		inputs[length++] = input;
		int event = genghost_sim_step(input);
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
							  const struct GENGHOST_ROUTE *route, const struct route_search *search)
{
	if (context->cache->checkpoint_count && context->cache->checkpoint_route == route_index &&
		context->cache->checkpoint_trial == search->revision) {
		return;
	}
	genghost_sim_reset();
	struct GENGHOST_DRIVER driver;
	genghost_driver_reset(&driver);
	context->cache->checkpoint_count = 0;
	context->cache->boundary_count = 0;
	context->cache->guide_count = 0;
	for (unsigned int i = 0; i < search->length; ++i) {
		if (i && search->best_inputs[i] != search->best_inputs[i - 1U]) {
			context->cache->boundaries[context->cache->boundary_count++] = (unsigned short)i;
		}
		if (i % CHECKPOINT_INTERVAL == 0U) {
			context->cache->driver_checkpoints[context->cache->checkpoint_count] = driver;
			genghost_sim_capture(&context->cache->checkpoints[context->cache->checkpoint_count++]);
		}
		if (search->race.finished && i < search->race.finish_frame) {
			genghost_guide_capture(&context->cache->guide[context->cache->guide_count++],
								   search->best_inputs[i]);
		}
		int event = genghost_sim_step(search->best_inputs[i]);
		if (!search->race.finished) {
			genghost_driver_observe(route, &driver);
		}
		++context->result->simulated_frames;
		if (event) {
			break;
		}
	}
	context->cache->checkpoint_route = route_index;
	context->cache->checkpoint_trial = search->revision;
}

/* Human laps encode useful control boundaries: brake entry/release, steering
 * corrections and gear changes. Move those boundaries without overwriting the
 * other controls. Small coordinated timing edits preserve far more of an expert
 * line than replacing a random long block of all the inputs. */
static unsigned int mutate_inputs(struct search_context *context, const struct route_search *search,
								  unsigned char *inputs, unsigned int *start)
{
	unsigned int length = search->race.finished ? search->race.finish_frame : search->length;
	if (length > search->length) {
		length = search->length;
	}
	memcpy(inputs, search->best_inputs, length);
	*start = random_word(context) % length;
	unsigned int operation = random_word(context) % 12U;
	unsigned int boundary =
		context->cache->boundary_count
			? context->cache->boundaries[random_word(context) % context->cache->boundary_count]
			: *start;
	if (boundary >= length) {
		boundary = length - 1U;
	}
	if ((operation < 6U || operation == 8U) && boundary > 0U) {
		static const unsigned int masks[] = {3U, 12U, 48U};
		static const unsigned int widths[] = {1U, 1U, 2U, 2U, 3U, 4U, 6U, 8U};
		unsigned int changed = inputs[boundary] ^ inputs[boundary - 1U];
		unsigned int channel = random_word(context) % 3U;
		for (unsigned int n = 0; n < 3U && !(changed & masks[channel]); ++n) {
			channel = (channel + 1U) % 3U;
		}
		unsigned int mask = masks[channel];
		unsigned char before = inputs[boundary - 1U] & mask;
		unsigned char after = inputs[boundary] & mask;
		unsigned int width = widths[random_word(context) % 8U];
		int later = random_word(context) % 2U;
		if (mask == 3U && random_word(context) % 4U != 0U) {
			/* Usually test less braking/coasting, while retaining exploratory
			 * earlier braking for tighter and faster corner exits. */
			later = after != 1U;
		}
		unsigned int run_end = boundary + 1U;
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
			unsigned char following = inputs[run_end] & mask;
			unsigned int first = later ? boundary : boundary - width;
			unsigned int second = later ? run_end : run_end - width;
			for (unsigned int i = 0; i < width; ++i) {
				inputs[first + i] =
					(unsigned char)((inputs[first + i] & ~mask) | (later ? before : after));
				inputs[second + i] =
					(unsigned char)((inputs[second + i] & ~mask) | (later ? after : following));
			}
			*start = first;
		} else if (later) {
			if (width > run_end - boundary) {
				width = run_end - boundary;
			}
			for (unsigned int i = boundary; i < boundary + width; ++i) {
				inputs[i] = (unsigned char)((inputs[i] & ~mask) | before);
			}
			*start = boundary;
		} else {
			unsigned int first = boundary;
			while (first && boundary - first < width && (inputs[first - 1U] & mask) == before) {
				--first;
			}
			for (unsigned int i = first; i < boundary; ++i) {
				inputs[i] = (unsigned char)((inputs[i] & ~mask) | after);
			}
			*start = first;
		}
		return length;
	}
	unsigned int width = 1U << (random_word(context) % 6U);
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
			if (length + width < GENGHOST_MAX_FRAMES - GENGHOST_FRAME_RATE) {
				memmove(inputs + *start + width, inputs + *start, length - *start);
				memset(inputs + *start, inputs[*start + width], width);
				length += width;
			}
			break;
		case 9: {
			static const unsigned char steering[] = {0U, 4U, 8U};
			unsigned char mask = steering[random_word(context) % 3U];
			for (unsigned int i = *start; i < *start + width; ++i) {
				inputs[i] = (unsigned char)((inputs[i] & ~12U) | mask);
			}
			break;
		}
		default: {
			static const unsigned char pedal[] = {1U, 1U, 0U, 2U};
			unsigned char mask = pedal[random_word(context) % 4U];
			for (unsigned int i = *start; i < *start + width; ++i) {
				inputs[i] = (unsigned char)((inputs[i] & ~3U) | mask);
			}
			break;
		}
	}
	return length;
}

static unsigned int run_mutation(struct search_context *context, size_t route_index,
								 const struct GENGHOST_ROUTE *route,
								 const struct route_search *search, unsigned char *inputs,
								 struct GENGHOST_DRIVER *driver, struct GENGHOST_RESULT *race)
{
	cache_checkpoints(context, route_index, route, search);
	unsigned int start, length = mutate_inputs(context, search, inputs, &start);
	unsigned int common = 0, parent_length = context->cache->guide_count;
	while (common < length && common < parent_length &&
		   inputs[length - common - 1U] == search->best_inputs[parent_length - common - 1U]) {
		++common;
	}
	unsigned int changed_end = length - common;
	unsigned int guide_cursor = parent_length - common;
	/* Most attempts retain the exact suffix. A minority re-time its demonstrated
	 * controls by position, so a faster sector can rejoin the line instead of
	 * applying every later corner input at the old wall-clock timestamp. */
	int guided = search->trials % 16U < 2U && common > 16U && changed_end > start;
	unsigned int limit = guided ? length + 120U : length;
	if (limit > GENGHOST_MAX_FRAMES) {
		limit = GENGHOST_MAX_FRAMES;
	}
	context->guided_candidate = guided;
	if (guided) {
		++context->result->guided_evaluations;
	}
	unsigned int checkpoint = start / CHECKPOINT_INTERVAL;
	if (checkpoint >= context->cache->checkpoint_count) {
		checkpoint = 0;
	}
	genghost_sim_restore(&context->cache->checkpoints[checkpoint]);
	*driver = context->cache->driver_checkpoints[checkpoint];
	unsigned int i = checkpoint * CHECKPOINT_INTERVAL;
	for (; i < limit; ++i) {
		if (i % 64U == 0U && expired(context)) {
			break;
		}
		if (guided && i >= changed_end) {
			inputs[i] = genghost_guide_step(context->cache->guide, context->cache->guide_count,
											&guide_cursor, search->trials % 16U ? 0.35 : 0.0);
		}
		int event = genghost_sim_step(inputs[i]);
		++context->result->simulated_frames;
		if (event) {
			++i;
			break;
		}
	}
	genghost_sim_result(race);
	return i;
}

static unsigned int repair_suffix(struct search_context *context, size_t route_index,
								  const struct GENGHOST_ROUTE *route,
								  const struct route_search *search, struct GENGHOST_POLICY *policy,
								  unsigned char *inputs, struct GENGHOST_DRIVER *driver,
								  struct GENGHOST_RESULT *race)
{
	cache_checkpoints(context, route_index, route, search);
	unsigned int rollback = 32U << (random_word(context) % 5U);
	unsigned int start = search->length > rollback ? search->length - rollback : 0U;
	unsigned int checkpoint = start / CHECKPOINT_INTERVAL;
	if (checkpoint >= context->cache->checkpoint_count) {
		checkpoint = 0;
	}
	unsigned int length = checkpoint * CHECKPOINT_INTERVAL;
	memcpy(inputs, search->best_inputs, length);
	genghost_sim_restore(&context->cache->checkpoints[checkpoint]);
	*driver = context->cache->driver_checkpoints[checkpoint];
	driver->tactic_ticks = 0U;
	*policy = search->best_policy;
	/* Retain the successful prefix and search the failing sector with a fresh
	 * speed/line/shift choice. This builds a lap incrementally instead of
	 * repeatedly losing earlier hard-won stunt entries. */
	for (unsigned int n = driver->segment; n < GENGHOST_POLICY_SEGMENTS && n < driver->segment + 3U;
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
			unsigned int choice = random_word(context);
			gene->shortcut_skip = choice % 5U;
			if (context->portal_family) {
				double delta = ((choice >> 8U) & 255U) / 127.5 - 1.0;
				gene->corner_scale = clamp(gene->corner_scale + delta * 0.2, 0.5, 1.8);
			}
			if (context->portal_family && (choice >> 4U) % 4U == 0U) {
				gene->line_mode = !gene->line_mode;
			}
			if ((context->result->race.finished || context->corkscrew_cut_available) &&
				context->portal_family && (choice >> 6U) % 2U == 0U) {
				if (!context->result->race.finished) {
					gene->stunt_mode = (choice >> 12U) % 2U ? GENGHOST_STUNT_CORKSCREW_CUT : 0U;
				} else {
					gene->stunt_mode =
						(choice >> 12U) %
						(context->corkscrew_cut_available ? GENGHOST_STUNT_CORKSCREW_CUT + 1U : 5U);
				}
			}
		}
	}
	unsigned int last_progress_tick = length;
	double furthest = driver->progress;
	while (length < GENGHOST_MAX_FRAMES - GENGHOST_FRAME_RATE) {
		if (length % 64U == 0U && expired(context)) {
			break;
		}
		unsigned char input =
			genghost_driver_step(route, policy, driver, context->options->powergear,
								 context->options->collision, context->options->owoot);
		inputs[length++] = input;
		int event = genghost_sim_step(input);
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

/* Keep whole route sweeps together so each family preserves its own candidate
 * sequence. A new family receives its initial policies before score selection;
 * routes without compatible switching geometry do not delay that warmup. */
static unsigned int choose_family(const struct GENGHOST_ROUTES *routes,
								  const struct route_search *searches, unsigned int family_count,
								  uint64_t round)
{
	if (family_count == 1U) {
		return 0U;
	}
	unsigned int family = round % 3U == 2U;
	unsigned int pending[MAX_SEARCH_FAMILIES] = {0};
	int initialized = 1;
	double best_score = DBL_MAX;
	unsigned int winner = 0U;
	for (unsigned int candidate = 0; candidate < family_count; ++candidate) {
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
		for (unsigned int offset = 0; offset < family_count; ++offset) {
			unsigned int candidate = (unsigned int)((round + offset) % family_count);
			if (pending[candidate]) {
				return candidate;
			}
		}
	}
	if (initialized && best_score < DBL_MAX) {
		family = winner;
		if (round % 4U == 3U) {
			unsigned int offset = 1U + (unsigned int)((round / 4U) % (family_count - 1U));
			family = (winner + offset) % family_count;
		}
	}
	return family;
}

int genghost_search(const struct GENGHOST_OPTIONS *options, const unsigned char *seed_inputs,
					unsigned int seed_length, struct GENGHOST_SEARCH_RESULT *result,
					genghost_save_function save, void *save_context)
{
	struct GENGHOST_ROUTES routes = {0};
	struct search_context context = {0};
	char error[256];
	int status = 0;
	unsigned int family_count = 1U;
	uint64_t family_round = 0U;
	uint32_t family_random[MAX_SEARCH_FAMILIES];
	unsigned char *inputs = malloc(GENGHOST_MAX_FRAMES);
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
		if (save_candidate(&context, inputs, seed_length, &seed_result)) {
			status = -1;
			goto done;
		}
		if (result->race.finished && result->length) {
			/* Keep the source as one input refinement parent, independently
			 * of the autonomous driving attempts on every route combination. */
			struct route_search *seed_parent = &searches[0];
			seed_parent->best_inputs = malloc(GENGHOST_MAX_FRAMES);
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
		}
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
		unsigned int family = choose_family(&routes, searches, family_count, family_round++);
		context.portal_family = (int)family;
		context.dualway_family = family == 2U;
		context.cache = &context.caches[family];
		context.random = family_random[family];
		for (size_t index = 0; index < routes.count && !expired(&context); ++index) {
			if (family == 2U && !routes.routes[index].dualway_count) {
				continue;
			}
			context.corkscrew_cut_available = 0;
			for (size_t point = 0; point < routes.routes[index].point_count; ++point) {
				if (!options->owoot && routes.routes[index].points[point].physical_model ==
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
				uint64_t revision = search->revision + 1U;
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
			unsigned int attempts = search->race.finished && search->trials >= POPULATION ? 3U : 1U;
			for (unsigned int attempt = 0; attempt < attempts && !expired(&context); ++attempt) {
				struct GENGHOST_POLICY policy;
				struct GENGHOST_DRIVER driver;
				struct GENGHOST_RESULT race;
				context.guided_candidate = 0;
				int mutation = search->best_inputs && search->race.finished &&
							   search->trials >= POPULATION &&
							   random_word(&context) % (search->demonstration ? 16U : 3U) != 0U;
				int initial_fallback = search->ordinary_discovery && !result->race.finished &&
									   search->trials < POPULATION * 2U;
				int repair = search->best_inputs && !search->race.finished && !initial_fallback &&
							 search->trials >= POPULATION && random_word(&context) % 3U != 0U;
				context.dualway_available = routes.routes[index].dualway_count != 0U;
				if (!mutation && !repair) {
					choose_policy(&context, search, &policy);
					if (context.dualway_family && search->trials < POPULATION) {
						genghost_dualway_seed_policy(&routes, index, &policy,
													 (unsigned int)search->trials);
					}
				}
				struct GENGHOST_ROUTE effective;
				const struct GENGHOST_ROUTE *route = &routes.routes[index];
				const struct GENGHOST_POLICY *route_policy =
					mutation || repair ? &search->best_policy : &policy;
				int changed_route = genghost_route_policy(&routes, index, route_policy, &effective,
														  error, sizeof(error));
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
				unsigned int length =
					mutation ? run_mutation(&context, search_index, route, search, inputs, &driver,
											&race)
					: repair ? repair_suffix(&context, search_index, route, search, &policy, inputs,
											 &driver, &race)
							 : run_policy(&context, route, &policy, inputs, &driver, &race);
				if (!search->trials) {
					int attempted = 0;
					for (unsigned int other = 0; other < family_count; ++other) {
						attempted |= searches[other * routes.count + index].trials != 0U;
					}
					if (!attempted) {
						++result->routes_attempted;
					}
				}
				context.tactic_candidate = !mutation && driver.tactic_ticks != 0U;
				if (context.tactic_candidate) {
					++result->tactic_evaluations;
				}
				++search->trials;
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
					int completed = 0;
					for (unsigned int other = 0; other < family_count; ++other) {
						completed |= searches[other * routes.count + index].completed_attempt;
					}
					search->completed_attempt = 1;
					if (!completed) {
						++result->routes_finished;
					}
				}
				double fitness = candidate_fitness(route, &driver, &race);
				if (changed_route) {
					genghost_route_release(&effective);
				}
				if (!mutation) {
					insert_policy(search, &policy, fitness);
				}
				if (fitness > search->fitness && length) {
					if (!search->best_inputs) {
						search->best_inputs = malloc(GENGHOST_MAX_FRAMES);
					}
					if (!search->best_inputs) {
						status = -1;
						goto done;
					}
					search->fitness = fitness;
					search->race = race;
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
				uint64_t now = SDL_GetTicksNS();
				if (now >= context.next_progress) {
					fprintf(stderr,
							"progress: %llu candidates, %zu/%zu routes attempted, %zu completed, "
							"%.1fs\n",
							(unsigned long long)result->evaluations, result->routes_attempted,
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
	for (unsigned int family = 0; family < MAX_SEARCH_FAMILIES; ++family) {
		free_cache(&context.caches[family]);
	}
	genghost_routes_free(&routes);
	if (family_count > 1U) {
		fprintf(stderr, "tile family: %llu candidates, %llu verified improvements\n",
				(unsigned long long)result->tile_evaluations,
				(unsigned long long)result->tile_improvements);
	}
	fprintf(stderr, "stunt tactics: %llu candidates, %llu verified improvements\n",
			(unsigned long long)result->tactic_evaluations,
			(unsigned long long)result->tactic_improvements);
	if (status) {
		return status;
	}
	return result->race.finished ? 0 : 2;
}
