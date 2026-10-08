#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <SDL3/SDL.h>
#include "search.h"
#include "../c/externs.h"
#include "../c/crash_state.h"

#undef memcpy

#define POPULATION 6U
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
};

struct search_context {
	const struct GENGHOST_OPTIONS *options;
	struct GENGHOST_SEARCH_RESULT *result;
	uint32_t random;
	uint64_t started;
	uint64_t next_progress;
	genghost_save_function save;
	void *save_context;
	struct GENGHOST_SNAPSHOT *checkpoints;
	struct GENGHOST_DRIVER *driver_checkpoints;
	unsigned int checkpoint_count;
	size_t checkpoint_route;
	uint64_t checkpoint_trial;
};

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
		gene->speed_scale = clamp(gene->speed_scale + random_signed(context) * 0.28, 0.3, 3.0);
		gene->lookahead_scale =
			clamp(gene->lookahead_scale + random_signed(context) * 0.28, 0.3, 2.8);
		gene->lateral_offset =
			clamp(gene->lateral_offset + random_signed(context) * 0.4,
				  context->options->owoot ? -0.85 : -2.0, context->options->owoot ? 0.85 : 2.0);
		gene->shift_rpm_scale =
			clamp(gene->shift_rpm_scale + random_signed(context) * 0.12, 0.45, 1.05);
		if (!context->options->owoot && random_word(context) % 4U == 0U) {
			gene->shortcut_skip = random_word(context) % 5U;
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
	if (search->trials < POPULATION) {
		static const double speeds[POPULATION] = {1.0, 0.75, 0.55, 1.2, 0.9, 1.5};
		genghost_policy_default(policy);
		for (unsigned int i = 0; i < GENGHOST_POLICY_SEGMENTS; ++i) {
			policy->segment[i].speed_scale *= speeds[search->trials];
			if (search->trials == 4U) {
				policy->segment[i].lateral_offset = -0.35;
			}
			if (search->trials == 5U) {
				policy->segment[i].powergear_bias = 1.0;
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
		unsigned char input = genghost_driver_step(
			route, policy, driver, context->options->powergear, context->options->owoot);
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
	if (context->checkpoint_count && context->checkpoint_route == route_index &&
		context->checkpoint_trial == search->revision) {
		return;
	}
	genghost_sim_reset();
	struct GENGHOST_DRIVER driver;
	genghost_driver_reset(&driver);
	context->checkpoint_count = 0;
	for (unsigned int i = 0; i < search->length; ++i) {
		if (i % CHECKPOINT_INTERVAL == 0U) {
			context->driver_checkpoints[context->checkpoint_count] = driver;
			genghost_sim_capture(&context->checkpoints[context->checkpoint_count++]);
		}
		int event = genghost_sim_step(search->best_inputs[i]);
		genghost_driver_observe(route, &driver);
		++context->result->simulated_frames;
		if (event) {
			break;
		}
	}
	context->checkpoint_route = route_index;
	context->checkpoint_trial = search->revision;
}

static unsigned int mutate_inputs(struct search_context *context, const struct route_search *search,
								  unsigned char *inputs, unsigned int *start)
{
	unsigned int length = search->length;
	memcpy(inputs, search->best_inputs, length);
	*start = random_word(context) % length;
	unsigned int width = 1U << (random_word(context) % 7U);
	if (width > length - *start) {
		width = length - *start;
	}
	switch (random_word(context) % 5U) {
		case 0: {
			static const unsigned char steering[] = {0U, 4U, 8U};
			unsigned char mask = steering[random_word(context) % 3U];
			for (unsigned int i = *start; i < *start + width; ++i) {
				inputs[i] = (unsigned char)((inputs[i] & ~12U) | mask);
			}
			break;
		}
		case 1: {
			static const unsigned char pedal[] = {0U, 1U, 2U};
			unsigned char mask = pedal[random_word(context) % 3U];
			for (unsigned int i = *start; i < *start + width; ++i) {
				inputs[i] = (unsigned char)((inputs[i] & ~3U) | mask);
			}
			break;
		}
		case 2:
			inputs[*start] =
				(unsigned char)((inputs[*start] & ~48U) | (random_word(context) % 2U ? 16U : 32U));
			break;
		case 3:
			width = 1U + random_word(context) % (width < 12U ? width : 12U);
			memmove(inputs + *start, inputs + *start + width, length - *start - width);
			length -= width;
			break;
		default:
			if (length + width < GENGHOST_MAX_FRAMES - GENGHOST_FRAME_RATE) {
				memmove(inputs + *start + width, inputs + *start, length - *start);
				memset(inputs + *start, inputs[*start + width], width);
				length += width;
			}
			break;
	}
	return length;
}

static unsigned int run_mutation(struct search_context *context, size_t route_index,
								 const struct GENGHOST_ROUTE *route,
								 const struct route_search *search, unsigned char *inputs,
								 struct GENGHOST_DRIVER *driver, struct GENGHOST_RESULT *race)
{
	unsigned int start, length = mutate_inputs(context, search, inputs, &start);
	cache_checkpoints(context, route_index, route, search);
	unsigned int checkpoint = start / CHECKPOINT_INTERVAL;
	if (checkpoint >= context->checkpoint_count) {
		checkpoint = 0;
	}
	genghost_sim_restore(&context->checkpoints[checkpoint]);
	*driver = context->driver_checkpoints[checkpoint];
	unsigned int i = checkpoint * CHECKPOINT_INTERVAL;
	for (; i < length; ++i) {
		if (i % 64U == 0U && expired(context)) {
			break;
		}
		int event = genghost_sim_step(inputs[i]);
		++context->result->simulated_frames;
		genghost_driver_observe(route, driver);
		if (event) {
			++i;
			break;
		}
	}
	genghost_driver_observe(route, driver);
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
	if (checkpoint >= context->checkpoint_count) {
		checkpoint = 0;
	}
	unsigned int length = checkpoint * CHECKPOINT_INTERVAL;
	memcpy(inputs, search->best_inputs, length);
	genghost_sim_restore(&context->checkpoints[checkpoint]);
	*driver = context->driver_checkpoints[checkpoint];
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
		if (!context->options->owoot && random_word(context) % 3U == 0U) {
			gene->shortcut_skip = random_word(context) % 5U;
		}
	}
	unsigned int last_progress_tick = length;
	double furthest = driver->progress;
	while (length < GENGHOST_MAX_FRAMES - GENGHOST_FRAME_RATE) {
		if (length % 64U == 0U && expired(context)) {
			break;
		}
		unsigned char input = genghost_driver_step(
			route, policy, driver, context->options->powergear, context->options->owoot);
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

int genghost_search(const struct GENGHOST_OPTIONS *options, const unsigned char *seed_inputs,
					unsigned int seed_length, struct GENGHOST_SEARCH_RESULT *result,
					genghost_save_function save, void *save_context)
{
	struct GENGHOST_ROUTES routes = {0};
	struct search_context context = {0};
	char error[256];
	int status = 0;
	unsigned char *inputs = malloc(GENGHOST_MAX_FRAMES);
	struct route_search *searches = NULL;
	memset(result, 0, sizeof(*result));
	context.options = options;
	context.result = result;
	context.random = options->seed ? options->seed : 1U;
	context.started = SDL_GetTicksNS();
	context.next_progress = context.started + 5000000000ULL;
	context.save = save;
	context.save_context = save_context;
	context.checkpoints = calloc(CHECKPOINT_COUNT, sizeof(*context.checkpoints));
	context.driver_checkpoints = calloc(CHECKPOINT_COUNT, sizeof(*context.driver_checkpoints));
	if (!inputs || !context.checkpoints || !context.driver_checkpoints) {
		status = -1;
		goto done;
	}
	if (genghost_routes_build(&routes, error, sizeof(error))) {
		fprintf(stderr, "genghost: %s\n", error);
		status = -1;
		goto done;
	}
	result->routes = routes.count;
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
		}
	}
	/* Fair round-robin scheduling: every enumerated combination gets the same
	 * number of opportunities before any is revisited. Both route policy and
	 * short input windows are refined; actual finish+penalty is authoritative. */
	while (!expired(&context)) {
		for (size_t index = 0; index < routes.count && !expired(&context); ++index) {
			struct route_search *search = &searches[index];
			struct GENGHOST_POLICY policy;
			struct GENGHOST_DRIVER driver;
			struct GENGHOST_RESULT race;
			choose_policy(&context, search, &policy);
			int mutation = search->best_inputs && search->race.finished &&
						   search->trials >= POPULATION && random_word(&context) % 3U != 0U;
			int repair = search->best_inputs && !search->race.finished &&
						 search->trials >= POPULATION && random_word(&context) % 3U != 0U;
			unsigned int length = mutation ? run_mutation(&context, index, &routes.routes[index],
														  search, inputs, &driver, &race)
								  : repair ? repair_suffix(&context, index, &routes.routes[index],
														   search, &policy, inputs, &driver, &race)
										   : run_policy(&context, &routes.routes[index], &policy,
														inputs, &driver, &race);
			if (!search->trials) {
				++result->routes_attempted;
			}
			++search->trials;
			++result->evaluations;
			if (race.finished && !search->completed_attempt) {
				search->completed_attempt = 1;
				++result->routes_finished;
			}
			double fitness = candidate_fitness(&routes.routes[index], &driver, &race);
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
				}
				search->length = length;
				++search->revision;
				memcpy(search->best_inputs, inputs, length);
				context.checkpoint_count = 0;
			}
			if (save_candidate(&context, inputs, length, &race)) {
				status = -1;
				goto done;
			}
			uint64_t now = SDL_GetTicksNS();
			if (now >= context.next_progress) {
				fprintf(
					stderr,
					"progress: %llu candidates, %zu/%zu routes attempted, %zu completed, %.1fs\n",
					(unsigned long long)result->evaluations, result->routes_attempted,
					result->routes, result->routes_finished, elapsed(&context));
				context.next_progress = now + 5000000000ULL;
			}
		}
	}
done:
	result->elapsed_seconds = elapsed(&context);
	if (searches) {
		for (size_t i = 0; i < routes.count; ++i) {
			free(searches[i].best_inputs);
		}
	}
	free(searches);
	free(inputs);
	free(context.checkpoints);
	free(context.driver_checkpoints);
	genghost_routes_free(&routes);
	if (status) {
		return status;
	}
	return result->race.finished ? 0 : 2;
}
