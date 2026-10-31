#define SDL_MAIN_HANDLED
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include <signal.h>
#include <unistd.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/stat.h>
#endif
#include "search.h"
#include "parallel.h"
#include "state_search.h"
#include "../platform/sdl3/sdl3.h"
#include "../c/fileio.h"
#include "../c/cstrk.h"
#include "../c/csrpl.h"

#ifndef RESTUNTS_GIT_HASH
#define RESTUNTS_GIT_HASH "source-archive"
#endif

#define GENGHOST_DEFAULT_SECONDS 1800.0
#define GENGHOST_OPTIONAL_PHYSICS_ARGUMENTS 2
#define GENGHOST_DEFAULT_CHICANE_FAST_GRASS 1
#define GENGHOST_FORMAT_SIGNATURE_SIZE 8U
#define GENGHOST_MAX_TRACK_SIZE                                                                    \
	(CSTRK_HEADER_SIZE + (CSTRK_MAX_DIMENSION + 1U) * (CSTRK_MAX_DIMENSION + 1U) +                 \
	 2U * CSTRK_MAX_DIMENSION * CSTRK_MAX_DIMENSION + CSTRK_OBJECT_SIZE * CSTRK_MAX_OBJECTS +      \
	 CSTRK_MAX_NOTES)
#define GENGHOST_MAX_SOURCE_SIZE (CSRPL_HEADER_SIZE + GENGHOST_MAX_TRACK_SIZE + GENGHOST_MAX_FRAMES)
#define GENGHOST_SOURCE_ERROR_SIZE 256U

static const legacy_u8 cstrk_signature[GENGHOST_FORMAT_SIGNATURE_SIZE] = {'C', 'S', 'T',  'R',
																		  'K', 26U, '\r', '\n'};
static const legacy_u8 csrpl_signature[GENGHOST_FORMAT_SIGNATURE_SIZE] = {'C', 'S',	 'R',  'P',
																		  'L', '\r', '\n', 26U};

struct output_context {
	const legacy_char *path, *track_name, *car;
	const struct GENGHOST_OPTIONS *options;
	legacy_int transmission;
	legacy_u16 frame_rate;
	const struct GENGHOST_PARALLEL_REPORT *parallel;
	legacy_uint baseline_total_frames;
};

static const legacy_char help[] =
	"genghost - offline, headless Stunts ghost generator\n"
	"Usage: genghost [--data-dir DIR] TRACK[.TRK|.RPL|.CSTRK|.CSRPL] CAR [options]\n"
	"       genghost [--data-dir DIR] REPLAY[.RPL|.CSRPL] [CAR] --fine-tune [options]\n"
	"       genghost [--data-dir DIR] REPLAY[.RPL|.CSRPL] [CAR] --verify [options]\n"
	"       genghost [--data-dir DIR] REPLAY[.RPL|.CSRPL] [CAR] --fine-tune-gears-only "
	"[options]\n\n"
	"  --output FILE          Replay destination (.rpl for TRK, .csrpl for CSTRK).\n"
	"  --seconds N            Search budget; default 1800 seconds, 0 until stopped.\n"
	"  --evaluations N        Deterministic candidate budget (disables default timer).\n"
	"  --seed N               Search random seed; default 1.\n"
	"  --strategy MODE        legacy (default) or state (cooperative state planner).\n"
	"  --cores N              Parallel search workers (1 default, 0 available cores).\n"
	"  --stop-file FILE       Stop gracefully when FILE is a nonempty regular file.\n"
	"  --pg:on|off            Original power gear bug (default on).\n"
	"  --lc:on|off            Original collision behavior (default on).\n"
	"  --lcb:on|off           Original left corner bias (default on).\n"
	"  --dws:on|off           Dual-Way Switching (original behavior; default on).\n"
	"  --cfg:on|off           Chicane fast-grass (default on).\n"
	"  --amigagrass           Original Amiga grass grip for stock cars (default off).\n"
	"  --owoot[:on|off]       Enforce One Wheel On/Over Track.\n"
	"  --fine-tune            Refine driving and gear inputs of a completed replay.\n"
	"  --fine-tune-gears-only Refine gear timing, preserving every driving input.\n"
	"  --verify               Validate the input replay and print its score; no search.\n"
	"  --help, -h             Show this help without loading game resources.\n"
	"  --version, -v          Show version.\n\n"
	"CAR is a four-character resource code, such as COUN, PMIN, or DIA3.\n"
	"Fine-tuning reads CAR from the replay and ignores any supplied CAR argument.\n"
	"All generated replays use manual transmission; --verify honors recorded gears.\n"
	"Relative input paths resolve from the current directory, then --data-dir.\n"
	"Output paths always resolve from the original current directory.\n"
	"Track searches use 20 Hz; replay searches preserve recorded 10/20 Hz timing.\n"
	"The evaluation budget is shared across workers; seconds are wall-clock time.\n"
	"Ctrl+C keeps the best fully validated completed replay found so far.\n"
	"The accompanying .json records the physics flags required for playback.\n"
	"Fine-tuning requires a solo replay that finishes under the selected rules.\n"
	"Automatic seed shifts become manual inputs in either fine-tuning mode.\n"
	"--fine-tune and --fine-tune-gears-only cannot be combined.\n"
	"It writes a separate *-FT.rpl/.csrpl and reports whether the time improved.\n"
	"CSRPL recordings supply driving rules unless explicitly overridden.\n"
	"CSTRK ghosts embed the complete track and rule flags in CSRPL.\n"
	"A bounded heuristic search cannot guarantee a finish or global optimum.\n";

static const legacy_char *strategy_name(const struct GENGHOST_OPTIONS *options)
{
	return options->strategy == GENGHOST_STRATEGY_STATE ? "state" : "legacy";
}

struct cooperative_context {
	struct GENGHOST_COOPERATIVE_POOL *pool;
	struct GENGHOST_PARALLEL_REPORT *report;
};

static legacy_int cooperative_batch(void *context, const struct GENGHOST_STATE_TASK *tasks,
									struct GENGHOST_STATE_REPLY *replies, size_t count)
{
	struct cooperative_context *shared = context;
	legacy_int status = genghost_cooperative_batch(shared->pool, tasks, replies, count);
	if (!status) {
		for (size_t i = 0; i < count; ++i) {
			shared->report->workers[i % shared->report->cores_used].simulated_frames +=
				replies[i].simulated_frames;
		}
	}
	return status;
}

static void interrupt_search(legacy_int signal_number)
{
	(void)signal_number;
	genghost_interrupted = 1;
}

static legacy_char *join_path(const legacy_char *directory, const legacy_char *name)
{
	if (name[0] == '/' || name[0] == '\\' || (isalpha((legacy_u8)name[0]) && name[1] == ':')) {
		return SDL_strdup(name);
	}
	size_t a = strlen(directory), b = strlen(name);
	legacy_char *result = malloc(a + b + 2U);
	if (result) {
		snprintf(result, a + b + 2U, "%s/%s", directory, name);
	}
	return result;
}

static const legacy_char *base_name(const legacy_char *path)
{
	const legacy_char *name = path;
	for (; *path; ++path) {
		if (*path == '/' || *path == '\\') {
			name = path + 1;
		}
	}
	return name;
}

static legacy_char *with_suffix(const legacy_char *path, const legacy_char *suffix)
{
	size_t size = strlen(path) + strlen(suffix) + 1U;
	legacy_char *result = malloc(size);
	if (result) {
		snprintf(result, size, "%s%s", path, suffix);
	}
	return result;
}

static legacy_int read_file(const legacy_char *path, legacy_u8 **data, size_t *length)
{
	FILE *file = fopen(path, "rb");
	if (!file) {
		return -1;
	}
	if (fseek(file, 0, SEEK_END)) {
		fclose(file);
		return -1;
	}
	legacy_s64 file_size = ftell(file);
	if (file_size < 0 || (legacy_u64)file_size > GENGHOST_MAX_SOURCE_SIZE ||
		fseek(file, 0, SEEK_SET)) {
		fclose(file);
		return -1;
	}
	*length = (size_t)file_size;
	*data = malloc(*length + 1U);
	if (!*data) {
		fclose(file);
		return -1;
	}
	legacy_int failed = fread(*data, 1, *length, file) != *length;
	if (fclose(file)) {
		failed = 1;
	}
	if (failed) {
		free(*data);
		*data = NULL;
		return -1;
	}
	return 0;
}

static legacy_int read_source(const legacy_char *source, const legacy_char *cwd,
							  const legacy_char *data_dir, legacy_int replay_only, legacy_u8 **data,
							  size_t *length, legacy_char **resolved_path)
{
	static const legacy_char *track_suffixes[] = {"",		".CSTRK", ".cstrk", ".TRK", ".trk",
												  ".CSRPL", ".csrpl", ".RPL",	".rpl"};
	static const legacy_char *replay_suffixes[] = {"", ".CSRPL", ".csrpl", ".RPL", ".rpl"};
	const legacy_char **suffixes = replay_only ? replay_suffixes : track_suffixes;
	legacy_uint suffix_count = replay_only ? sizeof(replay_suffixes) / sizeof(replay_suffixes[0])
										   : sizeof(track_suffixes) / sizeof(track_suffixes[0]);
	for (legacy_uint directory = 0; directory < 2U; ++directory) {
		for (legacy_uint i = 0; i < suffix_count; ++i) {
			legacy_char *name = with_suffix(source, suffixes[i]);
			legacy_char *path = name ? join_path(directory ? data_dir : cwd, name) : NULL;
			free(name);
			if (!path) {
				return -1;
			}
			legacy_int status = read_file(path, data, length);
			if (!status) {
				*resolved_path = path;
				return 0;
			}
			free(path);
		}
	}
	return -1;
}

/* Compare existing file identities so relative paths, case differences on
 * Windows, symlinks, and hard links cannot overwrite the fine-tuning source. */
static legacy_int same_file(const legacy_char *source, const legacy_char *destination)
{
#ifdef _WIN32
	WCHAR *a = (WCHAR *)SDL_iconv_string("UTF-16LE", "UTF-8", source, strlen(source) + 1U);
	WCHAR *b =
		(WCHAR *)SDL_iconv_string("UTF-16LE", "UTF-8", destination, strlen(destination) + 1U);
	if (!a || !b) {
		SDL_free(a);
		SDL_free(b);
		return -1;
	}
	DWORD sharing = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
	HANDLE first = CreateFileW(a, 0, sharing, NULL, OPEN_EXISTING, 0, NULL);
	HANDLE second = CreateFileW(b, 0, sharing, NULL, OPEN_EXISTING, 0, NULL);
	DWORD error = GetLastError();
	SDL_free(a);
	SDL_free(b);
	legacy_int result = -1;
	if (first != INVALID_HANDLE_VALUE && second != INVALID_HANDLE_VALUE) {
		BY_HANDLE_FILE_INFORMATION left, right;
		if (GetFileInformationByHandle(first, &left) &&
			GetFileInformationByHandle(second, &right)) {
			result = left.dwVolumeSerialNumber == right.dwVolumeSerialNumber &&
					 left.nFileIndexHigh == right.nFileIndexHigh &&
					 left.nFileIndexLow == right.nFileIndexLow;
		}
	} else if (first != INVALID_HANDLE_VALUE &&
			   (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)) {
		result = 0;
	}
	if (first != INVALID_HANDLE_VALUE) {
		CloseHandle(first);
	}
	if (second != INVALID_HANDLE_VALUE) {
		CloseHandle(second);
	}
	return result;
#else
	struct stat left, right;
	if (stat(source, &left)) {
		return -1;
	}
	if (stat(destination, &right)) {
		return errno == ENOENT || errno == ENOTDIR ? 0 : -1;
	}
	return left.st_dev == right.st_dev && left.st_ino == right.st_ino;
#endif
}

static legacy_int parse_integer(const legacy_char *text, legacy_u64 maximum, legacy_u64 *value)
{
	errno = 0;
	if (!*text || *text == '-') {
		return -1;
	}
	legacy_char *end;
	legacy_u64 number = strtoull(text, &end, 10);
	if (errno || *end || number > maximum) {
		return -1;
	}
	*value = number;
	return 0;
}

static legacy_int physics_option(const legacy_char *argument, const legacy_char *name,
								 legacy_int *value)
{
	size_t count = strlen(name);
	if (SDL_strncasecmp(argument, name, count) != 0) {
		return 0;
	}
	if (SDL_strcasecmp(argument + count, "on") == 0) {
		*value = 1;
	} else if (SDL_strcasecmp(argument + count, "off") == 0) {
		*value = 0;
	} else {
		return -1;
	}
	return 1;
}

/* A recording is self-contained: unspecified switches inherit its rules,
 * while an explicit command-line choice keeps its existing meaning. */
static void replay_driving_rules(struct GENGHOST_OPTIONS *options, legacy_u16 recorded,
								 legacy_u16 explicit_rules)
{
	if (!(explicit_rules & DRIVING_RULE_POWER_GEAR)) {
		options->powergear = (recorded & DRIVING_RULE_POWER_GEAR) != 0;
	}
	if (!(explicit_rules & DRIVING_RULE_LEGACY_COLLISION)) {
		options->collision = (recorded & DRIVING_RULE_LEGACY_COLLISION) != 0;
	}
	if (!(explicit_rules & DRIVING_RULE_LEFT_CORNER_BIAS)) {
		options->left_bias = (recorded & DRIVING_RULE_LEFT_CORNER_BIAS) != 0;
	}
	if (!(explicit_rules & DRIVING_RULE_DUAL_WAY_SWITCHING)) {
		options->dual_way_switching = (recorded & DRIVING_RULE_DUAL_WAY_SWITCHING) != 0;
	}
	if (!(explicit_rules & DRIVING_RULE_CHICANE_FAST_GRASS)) {
		options->chicane_fast_grass = (recorded & DRIVING_RULE_CHICANE_FAST_GRASS) != 0;
	}
	if (!(explicit_rules & DRIVING_RULE_AMIGA_GRASS)) {
		options->amiga_grass = (recorded & DRIVING_RULE_AMIGA_GRASS) != 0;
	}
	if (!(explicit_rules & DRIVING_RULE_OWOOT)) {
		options->owoot = (recorded & DRIVING_RULE_OWOOT) != 0;
	}
}

static legacy_u16 selected_driving_rules(const struct GENGHOST_OPTIONS *options)
{
	return (options->powergear ? DRIVING_RULE_POWER_GEAR : 0U) |
		   (options->collision ? DRIVING_RULE_LEGACY_COLLISION : 0U) |
		   (options->left_bias ? DRIVING_RULE_LEFT_CORNER_BIAS : 0U) |
		   (options->dual_way_switching ? DRIVING_RULE_DUAL_WAY_SWITCHING : 0U) |
		   (options->chicane_fast_grass ? DRIVING_RULE_CHICANE_FAST_GRASS : 0U) |
		   (options->amiga_grass ? DRIVING_RULE_AMIGA_GRASS : 0U) |
		   (options->owoot ? DRIVING_RULE_OWOOT : 0U);
}

static void json_string(FILE *file, const legacy_char *text)
{
	fputc('"', file);
	for (const legacy_u8 *p = (const legacy_u8 *)text; *p; ++p) {
		if (*p == '"' || *p == '\\') {
			fputc('\\', file);
			fputc(*p, file);
		} else if (*p < 32U) {
			fprintf(file, "\\u%04x", *p);
		} else {
			fputc(*p, file);
		}
	}
	fputc('"', file);
}

static legacy_int write_report(const legacy_char *path, const struct GENGHOST_SEARCH_RESULT *result,
							   const struct output_context *context)
{
	FILE *file = fopen(path, "wb");
	if (!file) {
		perror(path);
		return -1;
	}
	fputs("{\n  \"tool\": \"genghost\",\n  \"source_revision\": ", file);
	json_string(file, RESTUNTS_GIT_HASH);
	fputs(",\n  \"track\": ", file);
	json_string(file, context->track_name);
	fputs(",\n  \"car\": ", file);
	json_string(file, context->car);
	fputs(",\n  \"strategy\": ", file);
	json_string(file, strategy_name(context->options));
	fprintf(file, ",\n  \"fine_tune\": %s", context->options->fine_tune ? "true" : "false");
	fprintf(file, ",\n  \"fine_tune_gears_only\": %s",
			context->options->fine_tune_gears_only ? "true" : "false");
	if (context->options->fine_tune) {
		legacy_uint saved = context->baseline_total_frames - result->race.total_frames;
		fprintf(file,
				",\n  \"baseline_total_frames\": %u,\n  \"baseline_total_seconds\": %.2f,\n"
				"  \"improved\": %s,\n  \"saved_frames\": %u,\n  \"saved_seconds\": %.2f",
				context->baseline_total_frames,
				context->baseline_total_frames / (legacy_f64)context->frame_rate,
				saved ? "true" : "false", saved, saved / (legacy_f64)context->frame_rate);
	} else {
		fputs(",\n  \"baseline_total_frames\": null,\n  \"baseline_total_seconds\": null,\n"
			  "  \"improved\": null,\n  \"saved_frames\": null,\n  \"saved_seconds\": null",
			  file);
	}
	const struct GENGHOST_PARALLEL_REPORT *parallel = context->parallel;
	legacy_int shared = context->options->strategy == GENGHOST_STRATEGY_STATE;
	fprintf(file, ",\n  \"cores_requested\": %u,\n  \"cores_used\": %u,\n  \"winner_worker\": ",
			parallel ? parallel->cores_requested : 1U, parallel ? parallel->cores_used : 1U);
	if (shared || (parallel && parallel->winner_worker == UINT_MAX)) {
		fputs("null", file);
	} else {
		fprintf(file, "%u", parallel ? parallel->winner_worker : 0U);
	}
	fprintf(
		file,
		",\n  \"winner_seed\": %u,\n  \"route_coverage\": \"%s\",\n  \"worker_metrics\": \"%s\",\n"
		"  \"worker_results\": [",
		parallel ? parallel->winner_seed : context->options->seed,
		shared	   ? "shared_frontier"
		: parallel ? "maximum_from_one_worker"
				   : "single_worker",
		shared && parallel ? "completed_dispatches_excluding_parent_validation"
						   : "search_candidates");
	legacy_uint worker_count = parallel ? parallel->cores_used : 1U;
	for (legacy_uint i = 0; i < worker_count; ++i) {
		const struct GENGHOST_WORKER_STATS *worker = parallel ? &parallel->workers[i] : NULL;
		if (shared && worker) {
			fprintf(file,
					"%s\n    {\"index\": %u, \"seed\": %u, \"budget_evaluations\": null, "
					"\"evaluations\": %" LEGACY_PRIu64 ", \"simulated_frames\": %" LEGACY_PRIu64
					", \"finished\": null, "
					"\"total_frames\": null, \"status\": %d}",
					i ? "," : "", i, worker->seed, (legacy_u64)worker->evaluations,
					(legacy_u64)worker->simulated_frames, worker->status);
			continue;
		}

		fprintf(file,
				"%s\n    {\"index\": %u, \"seed\": %u, \"budget_evaluations\": %" LEGACY_PRIu64 ", "
				"\"evaluations\": %" LEGACY_PRIu64 ", \"simulated_frames\": %" LEGACY_PRIu64
				", \"finished\": %s, "
				"\"total_frames\": %u, \"status\": %d}",
				i ? "," : "", i, worker ? worker->seed : context->options->seed,
				(legacy_u64)(worker ? worker->budget_evaluations : context->options->evaluations),
				(legacy_u64)(worker ? worker->evaluations : result->evaluations),
				(legacy_u64)(worker ? worker->simulated_frames : result->simulated_frames),
				(worker ? worker->finished : result->race.finished) ? "true" : "false",
				worker ? worker->total_frames : result->race.total_frames,
				worker ? worker->status : 0);
	}
	fputs("\n  ]", file);
	fprintf(file,
			",\n  \"seed_replay_finished\": %s,\n  \"seed_total_frames\": %u,\n"
			"  \"policy_evaluations\": %" LEGACY_PRIu64
			",\n  \"input_evaluations\": %" LEGACY_PRIu64 ",\n"
			"  \"guided_evaluations\": %" LEGACY_PRIu64
			",\n  \"guided_improvements\": %" LEGACY_PRIu64 ",\n"
			"  \"tile_evaluations\": %" LEGACY_PRIu64 ",\n  \"tile_improvements\": %" LEGACY_PRIu64
			"",
			result->seed_finished ? "true" : "false", result->seed_total_frames,
			(legacy_u64)result->policy_evaluations, (legacy_u64)result->input_evaluations,
			(legacy_u64)result->guided_evaluations, (legacy_u64)result->guided_improvements,
			(legacy_u64)result->tile_evaluations, (legacy_u64)result->tile_improvements);
	fprintf(file,
			",\n  \"tactic_evaluations\": %" LEGACY_PRIu64
			",\n  \"tactic_improvements\": %" LEGACY_PRIu64,
			(legacy_u64)result->tactic_evaluations, (legacy_u64)result->tactic_improvements);
	fprintf(file,
			",\n  \"dualway_pairs\": %" LEGACY_PRIu64
			",\n  \"dualway_evaluations\": %" LEGACY_PRIu64 ",\n  "
			"\"dualway_improvements\": %" LEGACY_PRIu64,
			(legacy_u64)result->dualway_pairs, (legacy_u64)result->dualway_evaluations,
			(legacy_u64)result->dualway_improvements);
	fprintf(file,
			",\n  \"state_evaluations\": %" LEGACY_PRIu64
			",\n  \"state_frontier_peak\": %" LEGACY_PRIu64 ",\n"
			"  \"state_duplicates\": %" LEGACY_PRIu64
			",\n  \"state_pg_evaluations\": %" LEGACY_PRIu64 ",\n"
			"  \"state_pg_opportunities\": %" LEGACY_PRIu64
			",\n  \"state_macro_evaluations\": %" LEGACY_PRIu64 ",\n"
			"  \"state_input_evaluations\": %" LEGACY_PRIu64
			",\n  \"state_chicane_evaluations\": %" LEGACY_PRIu64 ",\n"
			"  \"state_task_hash\": \"%016" LEGACY_PRIx64 "\"",
			(legacy_u64)result->state_evaluations, (legacy_u64)result->state_frontier_peak,
			(legacy_u64)result->state_duplicates, (legacy_u64)result->state_pg_evaluations,
			(legacy_u64)result->state_pg_states, (legacy_u64)result->state_macro_evaluations,
			(legacy_u64)result->state_input_evaluations,
			(legacy_u64)result->state_chicane_evaluations, (legacy_u64)result->state_task_hash);
	fprintf(file,
			",\n  \"state_section_starts\": %" LEGACY_PRIu64
			",\n  \"state_section_evaluations\": %" LEGACY_PRIu64,
			(legacy_u64)result->state_section_starts,
			(legacy_u64)result->state_section_evaluations);
	fprintf(file,
			",\n  \"state_rescue_starts\": %" LEGACY_PRIu64
			",\n  \"state_diversity_preserved\": %" LEGACY_PRIu64 ",\n"
			"  \"state_transition_stops\": %" LEGACY_PRIu64,
			(legacy_u64)result->state_rescue_starts, (legacy_u64)result->state_diversity_preserved,
			(legacy_u64)result->state_transition_stops);
	fprintf(file,
			",\n  \"transmission\": \"%s\",\n  \"physics\": {\"pg\": %s, \"lc\": %s, \"lcb\": %s, "
			"\"dws\": %s, \"chicane_fast_grass\": %s, \"amiga_grass\": %s, \"owoot\": %s},\n"
			"  \"finished\": true,\n  \"frames_per_second\": %u,\n  \"replay_frames\": %u,\n"
			"  \"finish_frame\": %u,\n  \"penalty_frames\": %u,\n  \"total_frames\": %u,\n"
			"  \"total_seconds\": %.2f,\n  \"seed\": %u,\n  \"evaluations\": %" LEGACY_PRIu64 ",\n"
			"  \"simulated_frames\": %" LEGACY_PRIu64 ",\n  \"route_combinations\": %zu,\n"
			"  \"routes_attempted\": %zu,\n  \"routes_finished\": %zu,\n"
			"  \"search_seconds\": %.6f,\n  \"globally_optimal\": false\n}\n",
			context->transmission ? "auto" : "manual",
			context->options->powergear ? "true" : "false",
			context->options->collision ? "true" : "false",
			context->options->left_bias ? "true" : "false",
			context->options->dual_way_switching ? "true" : "false",
			context->options->chicane_fast_grass ? "true" : "false",
			context->options->amiga_grass ? "true" : "false",
			context->options->owoot ? "true" : "false", context->frame_rate, result->length,
			result->race.finish_frame, result->race.penalty_frames, result->race.total_frames,
			result->race.total_frames / (legacy_f64)context->frame_rate, context->options->seed,
			(legacy_u64)result->evaluations, (legacy_u64)result->simulated_frames, result->routes,
			result->routes_attempted, result->routes_finished, result->elapsed_seconds);
	legacy_int failed = ferror(file);
	if (fclose(file)) {
		failed = 1;
	}
	return failed ? -1 : 0;
}

static legacy_int save_result(const struct GENGHOST_SEARCH_RESULT *result, void *opaque)
{
	struct output_context *context = opaque;
	if (context->options->fine_tune &&
		(!result->race.finished || result->race.total_frames > context->baseline_total_frames)) {
		fputs("genghost: refusing a fine-tuning result worse than its verified baseline\n", stderr);
		return -1;
	}
	legacy_char suffix[64];
	snprintf(suffix, sizeof(suffix), ".tmp-%" LEGACY_PRIu64, (legacy_u64)SDL_GetTicksNS());
	legacy_char *temporary = with_suffix(context->path, suffix);
	legacy_char *report = with_suffix(context->path, ".json");
	legacy_char *temporary_report = report ? with_suffix(report, suffix) : NULL;
	legacy_int failed = !temporary || !report || !temporary_report;
	if (!failed) {
		failed = genghost_sim_write_replay(temporary, context->track_name, result->inputs,
										   result->length) != 0;
	}
	if (!failed) {
		failed = write_report(temporary_report, result, context) != 0;
	}
	if (!failed && !SDL_RenamePath(temporary, context->path)) {
		failed = 1;
	}
	if (!failed && !SDL_RenamePath(temporary_report, report)) {
		failed = 1;
	}
	if (failed) {
		fprintf(stderr, "Cannot publish replay/report at %s: %s\n", context->path, SDL_GetError());
		if (temporary) {
			remove(temporary);
		}
		if (temporary_report) {
			remove(temporary_report);
		}
	}
	free(temporary);
	free(report);
	free(temporary_report);
	return failed;
}

legacy_int main(legacy_int argc, legacy_char **argv)
{
	const legacy_char *asset_directory = getenv("RESTUNTS_ASSETS_DIR");
	file_car_set_base(
		(const legacy_s8 *)(asset_directory != NULL ? asset_directory : SDL_GetBasePath()));
	struct GENGHOST_OPTIONS options = {GENGHOST_DEFAULT_SECONDS,		   0, 1U, 1, 1, 1, 1, 0, 0,
									   GENGHOST_DEFAULT_CHICANE_FAST_GRASS};
	const legacy_char *source = NULL, *car_argument = NULL, *data_argument = ".",
					  *output_argument = NULL;
	const legacy_char *stop_argument = NULL;
	legacy_char *stop_file = NULL;
	legacy_char car[REPLAY_CAR_ID_SIZE + 1U] = {0}, track_name[256];
	legacy_int transmission = TRANSMISSION_MANUAL, verify = 0, explicit_seconds = 0, status = 1;
	legacy_uint cores = 1U;
	legacy_u16 simulation_rate = GENGHOST_FRAME_RATE;
	legacy_u16 explicit_rules = 0;
	const legacy_char *cooperative_request = NULL;
	struct GENGHOST_WORKER_CONTROL worker_control = {0};
	legacy_char *cwd = NULL, *data_dir = NULL, *output = NULL, *executable = NULL,
				*source_path = NULL;
	struct GENGHOST_RESULT baseline = {0};
	legacy_u8 *file = NULL;
	size_t file_length = 0;
	for (legacy_int i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
			fputs(help, stdout);
			return 0;
		}
		if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-v")) {
			printf("genghost %.7s (%s)\n", RESTUNTS_GIT_HASH, __DATE__);
			return 0;
		}
	}
	for (legacy_int i = 1; i < argc; ++i) {
		const legacy_char *argument = argv[i];
		legacy_int flag = physics_option(argument, "--pg:", &options.powergear);
		legacy_u16 selected_rule = DRIVING_RULE_POWER_GEAR;
		if (!flag) {
			flag = physics_option(argument, "--lc:", &options.collision);
			selected_rule = DRIVING_RULE_LEGACY_COLLISION;
		}
		if (!flag) {
			flag = physics_option(argument, "--lcb:", &options.left_bias);
			selected_rule = DRIVING_RULE_LEFT_CORNER_BIAS;
		}
		if (!flag) {
			flag = physics_option(argument, "--dws:", &options.dual_way_switching);
			selected_rule = DRIVING_RULE_DUAL_WAY_SWITCHING;
		}
		if (!flag) {
			flag = physics_option(argument, "--cfg:", &options.chicane_fast_grass);
			selected_rule = DRIVING_RULE_CHICANE_FAST_GRASS;
		}
		if (!flag) {
			flag = physics_option(argument, "--owoot:", &options.owoot);
			selected_rule = DRIVING_RULE_OWOOT;
		}
		if (!flag) {
			flag = physics_option(argument, "--amigagrass:", &options.amiga_grass);
			selected_rule = DRIVING_RULE_AMIGA_GRASS;
		}
		if (flag < 0) {
			fprintf(stderr, "Invalid physics option: %s\n", argument);
			goto done;
		}
		if (flag) {
			explicit_rules |= selected_rule;
			continue;
		}
		if (!SDL_strcasecmp(argument, "--owoot")) {
			options.owoot = 1;
			explicit_rules |= DRIVING_RULE_OWOOT;
			continue;
		}
		if (!SDL_strcasecmp(argument, "--amigagrass")) {
			options.amiga_grass = 1;
			explicit_rules |= DRIVING_RULE_AMIGA_GRASS;
			continue;
		}
		if (!strcmp(argument, "--fine-tune")) {
			options.fine_tune = 1;
			continue;
		}
		if (!strcmp(argument, "--fine-tune-gears-only")) {
			options.fine_tune_gears_only = 1;
			continue;
		}
		if (!strcmp(argument, "--verify")) {
			verify = 1;
			continue;
		}
		if (!strcmp(argument, "--data-dir") || !strcmp(argument, "--output") ||
			!strcmp(argument, "--seconds") || !strcmp(argument, "--evaluations") ||
			!strcmp(argument, "--seed") || !strcmp(argument, "--cores") ||
			!strcmp(argument, "--worker-status") || !strcmp(argument, "--worker-stop") ||
			!strcmp(argument, "--worker-parent") || !strcmp(argument, "--strategy") ||
			!strcmp(argument, "--cooperative-worker") || !strcmp(argument, "--stop-file")) {
			if (++i == argc) {
				fprintf(stderr, "Missing value for %s\n", argument);
				goto done;
			}
			if (!strcmp(argument, "--data-dir")) {
				data_argument = argv[i];
			} else if (!strcmp(argument, "--output")) {
				output_argument = argv[i];
			} else if (!strcmp(argument, "--stop-file")) {
				stop_argument = argv[i];
			} else if (!strcmp(argument, "--worker-status")) {
				worker_control.status_path = argv[i];
			} else if (!strcmp(argument, "--worker-stop")) {
				worker_control.stop_path = argv[i];
			} else if (!strcmp(argument, "--cooperative-worker")) {
				cooperative_request = argv[i];
			} else if (!strcmp(argument, "--strategy")) {
				if (!strcmp(argv[i], "legacy")) {
					options.strategy = GENGHOST_STRATEGY_LEGACY;
				} else if (!strcmp(argv[i], "state")) {
					options.strategy = GENGHOST_STRATEGY_STATE;
				} else {
					fputs("Strategy must be legacy or state\n", stderr);
					goto done;
				}
			} else {
				legacy_u64 value;
				legacy_u64 maximum = !strcmp(argument, "--seed")	? UINT32_MAX
									 : !strcmp(argument, "--cores") ? GENGHOST_MAX_CORES
																	: UINT64_MAX;
				if (parse_integer(argv[i], maximum, &value)) {
					fprintf(stderr, "Invalid numeric value for %s: %s\n", argument, argv[i]);
					goto done;
				}
				if (!strcmp(argument, "--cores")) {
					cores = (legacy_uint)value;
				} else if (!strcmp(argument, "--worker-parent")) {
					worker_control.parent_pid = value;
				} else if (!strcmp(argument, "--seconds")) {
					options.seconds = (legacy_f64)value;
					explicit_seconds = 1;
				} else if (!strcmp(argument, "--evaluations")) {
					if (!value) {
						fputs("--evaluations must be positive\n", stderr);
						goto done;
					}
					options.evaluations = value;
				} else {
					options.seed = (legacy_u32)value;
				}
			}
		} else if (argument[0] == '-') {
			fprintf(stderr, "Unknown option: %s\n", argument);
			goto done;
		} else if (!source) {
			source = argument;
		} else if (!car_argument) {
			car_argument = argument;
		} else {
			fprintf(stderr, "Unexpected argument: %s\n", argument);
			goto done;
		}
	}
	if (options.fine_tune && options.fine_tune_gears_only) {
		fputs("--fine-tune and --fine-tune-gears-only cannot be combined\n", stderr);
		goto done;
	}
	options.fine_tune = options.fine_tune || options.fine_tune_gears_only;
	const legacy_char *fine_tune_option =
		options.fine_tune_gears_only ? "--fine-tune-gears-only" : "--fine-tune";
	if (options.fine_tune && verify) {
		fprintf(stderr, "%s cannot be combined with --verify\n", fine_tune_option);
		goto done;
	}
	if (!source) {
		if (options.fine_tune) {
			fprintf(stderr, "%s requires an input replay (.RPL or .CSRPL)\n", fine_tune_option);
		} else {
			fputs(help, stderr);
		}
		goto done;
	}
	if (options.fine_tune) {
		if (car_argument) {
			fprintf(stderr, "Warning: ignoring CAR argument '%s'; %s uses the replay's car\n",
					car_argument, fine_tune_option);
		}
	} else if (car_argument || !verify) {
		if (!car_argument) {
			fputs("CAR is required for generation\n", stderr);
			goto done;
		}
		if (strlen(car_argument) != REPLAY_CAR_ID_SIZE) {
			fputs("CAR must have exactly four letters, digits or underscores\n", stderr);
			goto done;
		}
		for (legacy_uint i = 0; i < REPLAY_CAR_ID_SIZE; ++i) {
			if (!isalnum((legacy_u8)car_argument[i]) && car_argument[i] != '_') {
				fputs("Invalid CAR code\n", stderr);
				goto done;
			}
			car[i] = (legacy_char)toupper((legacy_u8)car_argument[i]);
		}
	}
	if (options.evaluations && !explicit_seconds) {
		options.seconds = 0.0;
	}
	if (cooperative_request || worker_control.status_path || worker_control.stop_path ||
		worker_control.parent_pid) {
		if (cores != 1U || verify ||
			(cooperative_request && options.strategy != GENGHOST_STRATEGY_STATE) ||
			(!cooperative_request && options.strategy != GENGHOST_STRATEGY_LEGACY) ||
			genghost_worker_attach(&worker_control)) {
			fputs("Invalid or orphaned internal search worker\n", stderr);
			goto done;
		}
		if (!cooperative_request) {
			options.progress = genghost_worker_progress;
			options.progress_context = &worker_control;
		}
	}
	if (cores != 1U && !(executable = genghost_executable_path(argv[0]))) {
		fputs("Cannot resolve generator executable for parallel search\n", stderr);
		goto done;
	}
	cwd = SDL_GetCurrentDirectory();
	if (!cwd || !(data_dir = join_path(cwd, data_argument))) {
		goto done;
	}
	if (stop_argument) {
		stop_file = join_path(cwd, stop_argument);
		if (!stop_file) {
			goto done;
		}
		genghost_set_stop_file(stop_file);
	}
	if (read_source(source, cwd, data_dir, options.fine_tune, &file, &file_length, &source_path)) {
		fprintf(stderr, "Cannot read %s: %s\n",
				options.fine_tune ? "input replay (.RPL or .CSRPL)" : "track/replay", source);
		goto done;
	}
	legacy_u8 *track = file, *source_inputs = NULL, *seed_inputs = NULL;
	legacy_uint track_length = (legacy_uint)file_length, seed_length = 0, source_frames = 0;
	struct GAMEINFO replay = {0};
	struct CSRPL_VIEW extended_replay = {0};
	legacy_int extended = 0, is_replay = 0;
	legacy_char source_error[GENGHOST_SOURCE_ERROR_SIZE];
	if (file_length >= sizeof(csrpl_signature) &&
		memcmp(file, csrpl_signature, sizeof(csrpl_signature)) == 0) {
		if (!csrpl_decode(file, file_length, &extended_replay, source_error,
						  sizeof(source_error))) {
			fprintf(stderr, "Invalid CSRPL replay: %s\n", source_error);
			goto done;
		}
		const struct CSRPL_METADATA *metadata = &extended_replay.metadata;
		if (metadata->frame_count > GENGHOST_MAX_FRAMES) {
			fprintf(stderr, "Replay exceeds the generator's %u-frame limit\n", GENGHOST_MAX_FRAMES);
			goto done;
		}
		extended = is_replay = 1;
		track = file + (extended_replay.track_data - file);
		track_length = (legacy_uint)extended_replay.track_size;
		source_inputs = file + (extended_replay.inputs - file);
		source_frames = metadata->frame_count;
		memcpy(replay.game_playercarid, metadata->player_car_id, REPLAY_CAR_ID_SIZE);
		replay.game_playermaterial = metadata->player_material;
		replay.game_playertransmission = metadata->player_transmission;
		replay.game_opponenttype = metadata->opponent_type;
		replay.game_framespersec = metadata->frames_per_second;
		memcpy(replay.game_trackname, metadata->track_name, REPLAY_TRACK_NAME_SIZE);
		replay_driving_rules(&options, metadata->driving_rules, explicit_rules);
		if (metadata->driving_rules != selected_driving_rules(&options)) {
			fputs("Using explicitly overridden CSRPL driving rules for this simulation\n", stderr);
		}
		if (metadata->version > CSRPL_VERSION) {
			fprintf(stderr,
					"CSRPL version %u is newer than supported version %u; using known layout\n",
					metadata->version, CSRPL_VERSION);
		}
	} else if (file_length >= sizeof(cstrk_signature) &&
			   memcmp(file, cstrk_signature, sizeof(cstrk_signature)) == 0) {
		if (!cstrk_validate_bytes(file, file_length, source_error, sizeof(source_error))) {
			fprintf(stderr, "Invalid CSTRK track: %s\n", source_error);
			goto done;
		}
		extended = 1;
	} else if (file_length != REPLAY_TRACK_SIZE && file_length >= REPLAY_INPUT_OFFSET) {
		replay_gameinfo_decode(&replay, file);
		if ((replay.game_framespersec != GAME_FRAME_RATE_LOW &&
			 replay.game_framespersec != GENGHOST_FRAME_RATE) ||
			replay.game_recordedframes > GENGHOST_LEGACY_MAX_FRAMES ||
			replay_file_size(replay.game_recordedframes) != file_length ||
			replay.game_playertransmission < 0 || replay.game_playertransmission > 1) {
			fputs("Invalid or unsupported replay; expected a complete Stunts 1.1 RPL\n", stderr);
			goto done;
		}
		is_replay = 1;
		track = file + REPLAY_GAMEINFO_SIZE;
		track_length = REPLAY_TRACK_SIZE;
		source_inputs = file + REPLAY_INPUT_OFFSET;
		source_frames = replay.game_recordedframes;
	} else if (file_length != REPLAY_TRACK_SIZE) {
		fputs("Invalid track/replay; expected TRK, RPL, CSTRK or CSRPL data\n", stderr);
		goto done;
	}
	if (options.fine_tune && !is_replay) {
		fprintf(stderr, "%s requires a complete replay (.RPL or .CSRPL), not a track-only file\n",
				fine_tune_option);
		goto done;
	}
	if (is_replay) {
		if (extended) {
			/* Preserve the source timing even when an automatic/opponent replay
			 * supplies only its track rather than compatible driving inputs. */
			simulation_rate = replay.game_framespersec;
		}
		if (options.fine_tune && !extended && replay.game_framespersec != GENGHOST_FRAME_RATE) {
			fputs("Legacy fine-tuning requires a 20 Hz RPL replay\n", stderr);
			goto done;
		}
		if (options.fine_tune || (verify && !car_argument)) {
			for (legacy_uint i = 0; i < REPLAY_CAR_ID_SIZE; ++i) {
				if (!isalnum((legacy_u8)replay.game_playercarid[i]) &&
					replay.game_playercarid[i] != '_') {
					fputs("Invalid CAR code in input replay header\n", stderr);
					goto done;
				}
				car[i] = (legacy_char)toupper((legacy_u8)replay.game_playercarid[i]);
			}
		}
		if ((options.fine_tune || verify || replay.game_opponenttype == 0) &&
			(options.fine_tune || memcmp(replay.game_playercarid, car, REPLAY_CAR_ID_SIZE) == 0)) {
			if (options.fine_tune || verify ||
				replay.game_playertransmission == TRANSMISSION_MANUAL) {
				seed_inputs = source_inputs;
				seed_length = source_frames;
				simulation_rate = replay.game_framespersec;
			}
		}
	}
	if (options.fine_tune && (!seed_inputs || !seed_length)) {
		fprintf(stderr, "%s requires a nonempty replay\n", fine_tune_option);
		goto done;
	}
	if (options.fine_tune && replay.game_playertransmission == TRANSMISSION_AUTOMATIC) {
		fprintf(stderr, "%s preserves automatic seed shifts as manual inputs\n", fine_tune_option);
	}
	if (options.fine_tune && replay.game_opponenttype != 0) {
		fprintf(stderr, "%s requires a solo replay; opponent collisions change the simulation\n",
				fine_tune_option);
		goto done;
	}
	if (verify && (!seed_inputs || !seed_length)) {
		fputs("--verify requires a nonempty replay with matching car\n", stderr);
		goto done;
	}
	if (verify && replay.game_opponenttype != 0 && !extended) {
		fputs("--verify requires a solo legacy replay; opponent metadata is supported in CSRPL\n",
			  stderr);
		goto done;
	}
	snprintf(track_name, sizeof(track_name), "%s", base_name(source));
	legacy_char *extension = strrchr(track_name, '.');
	if (extension) {
		*extension = 0;
	}
	if (!*track_name) {
		strcpy(track_name, "TRACK");
	}
	if (output_argument) {
		output = join_path(cwd, output_argument);
	} else if (extended) {
		legacy_char default_name[sizeof(track_name) + REPLAY_CAR_ID_SIZE + sizeof("-FT.csrpl")];
		if (options.fine_tune) {
			snprintf(default_name, sizeof(default_name), "%s-FT.csrpl", track_name);
		} else {
			snprintf(default_name, sizeof(default_name), "%s-%s.csrpl", track_name, car);
		}
		output = join_path(cwd, default_name);
	} else if (options.fine_tune) {
		legacy_char prefix[6] = {0}, default_name[13];
		for (legacy_uint i = 0; i < 5U && track_name[i]; ++i) {
			legacy_u8 letter = (legacy_u8)toupper((legacy_u8)track_name[i]);
			prefix[i] = (letter >= 'A' && letter <= 'Z') || (letter >= '0' && letter <= '9')
							? (legacy_char)letter
							: '_';
		}
		snprintf(default_name, sizeof(default_name), "%s-FT.rpl", prefix);
		output = join_path(cwd, default_name);
	} else {
		/* The game's replay picker only enumerates DOS 8.3 filenames. */
		legacy_char prefix[4] = {0}, default_name[13];
		for (legacy_uint i = 0; i < 3U && track_name[i]; ++i) {
			legacy_u8 letter = (legacy_u8)toupper((legacy_u8)track_name[i]);
			prefix[i] = '_';
			if ((letter >= 'A' && letter <= 'Z') || (letter >= '0' && letter <= '9')) {
				prefix[i] = (legacy_char)letter;
			}
		}
		snprintf(default_name, sizeof(default_name), "%s-%s.rpl", prefix, car);
		output = join_path(cwd, default_name);
	}
	if (!output) {
		goto done;
	}
	const legacy_char *output_extension = strrchr(base_name(output), '.');
	if (extended && (!output_extension || SDL_strcasecmp(output_extension, ".csrpl") != 0)) {
		fputs("CSTRK ghosts require an output filename ending in .csrpl\n", stderr);
		goto done;
	}
	if (options.fine_tune) {
		legacy_char *report_path = with_suffix(output, ".json");
		legacy_int source_output = same_file(source_path, output);
		legacy_int source_report = report_path ? same_file(source_path, report_path) : -1;
		free(report_path);
		if (source_output != 0 || source_report != 0) {
			fprintf(stderr, "%s must preserve its input replay; choose a different --output path\n",
					fine_tune_option);
			goto done;
		}
		memcpy(track_name, replay.game_trackname, sizeof(replay.game_trackname));
		track_name[sizeof(replay.game_trackname)] = 0;
	}
	if (chdir(data_dir)) {
		perror(data_dir);
		goto done;
	}
	SDL_SetMainReady();
#ifdef __ANDROID__
	/* This executable runs without SDLActivity or a Java VM. Do not ask SDL to
	 * discover its application name through Android's Java context. */
	SDL_SetAppMetadata("genghost", RESTUNTS_GIT_HASH, "org.restunts.genghost");
#endif
	sdl3_batch_mode = 1;
	if (!SDL_Init(0)) {
		fprintf(stderr, "SDL initialization: %s\n", SDL_GetError());
		goto done;
	}
	atexit(SDL_Quit);
	legacy_char *physics_arguments[] = {"genghost",
										options.powergear ? "--pg:on" : "--pg:off",
										options.collision ? "--lc:on" : "--lc:off",
										options.left_bias ? "--lcb:on" : "--lcb:off",
										options.dual_way_switching ? "--dws:on" : "--dws:off",
										options.chicane_fast_grass ? "--cfg:on" : "--cfg:off",
										NULL,
										NULL};
	legacy_int physics_count =
		(legacy_int)(sizeof(physics_arguments) / sizeof(*physics_arguments)) -
		GENGHOST_OPTIONAL_PHYSICS_ARGUMENTS;
	if (options.owoot) {
		physics_arguments[physics_count++] = "--owoot";
	}
	if (options.amiga_grass) {
		physics_arguments[physics_count++] = "--amigagrass";
	}
	legacy_int source_transmission =
		options.fine_tune || verify ? replay.game_playertransmission : TRANSMISSION_MANUAL;
	genghost_sim_set_frame_rate(simulation_rate);
	if (extended && is_replay) {
		struct CSRPL_METADATA metadata = extended_replay.metadata;
		metadata.frames_per_second = simulation_rate;
		if (!verify && !options.fine_tune) {
			/* Generation uses the embedded track for a solo ghost; existing
			 * opponent-dependent inputs are not a compatible search seed. */
			metadata.opponent_type = 0;
		}
		genghost_sim_set_replay_metadata(&metadata);
	}
	if (genghost_sim_init_track(track, track_length, car, source_transmission, physics_count,
								physics_arguments)) {
		goto done;
	}
	if (options.fine_tune) {
		if (genghost_sim_manual_inputs(seed_inputs, seed_length, &baseline)) {
			fprintf(stderr,
					"%s requires an input replay that finishes under the selected physics "
					"and OWOOT settings, with an equivalent manual lap\n",
					fine_tune_option);
			goto done;
		}
		/* Parallel workers must receive the same converted manual seed as the
		 * parent, including its header, rather than the untouched source file. */
		replay.game_playertransmission = TRANSMISSION_MANUAL;
		memcpy(replay.game_playercarid, car, REPLAY_CAR_ID_SIZE);
		if (extended) {
			struct CSRPL_METADATA metadata = extended_replay.metadata;
			metadata.player_transmission = TRANSMISSION_MANUAL;
			metadata.driving_rules = selected_driving_rules(&options);
			memcpy(metadata.player_car_id, car, REPLAY_CAR_ID_SIZE);
			legacy_u8 *manual_file = NULL;
			size_t manual_length = 0;
			if (!csrpl_encode(&metadata, track, track_length, seed_inputs, seed_length,
							  &manual_file, &manual_length, source_error, sizeof(source_error))) {
				fprintf(stderr, "Cannot materialize manual CSRPL seed: %s\n", source_error);
				goto done;
			}
			free(file);
			file = manual_file;
			file_length = manual_length;
			if (!csrpl_decode(file, file_length, &extended_replay, source_error,
							  sizeof(source_error))) {
				goto done;
			}
			seed_inputs = file + (extended_replay.inputs - file);
		} else {
			replay_gameinfo_encode(file, &replay);
		}
		fprintf(stderr, "fine-tune baseline: %.2fs (driving %.2fs + penalty %.2fs)\n",
				baseline.total_frames / (legacy_f64)simulation_rate,
				baseline.finish_frame / (legacy_f64)simulation_rate,
				baseline.penalty_frames / (legacy_f64)simulation_rate);
	}
	if (verify) {
		struct GENGHOST_RESULT race;
		genghost_sim_evaluate(seed_inputs, seed_length, &race);
		printf("{\"finished\":%s,\"frames\":%u,\"finish_frame\":%u,\"penalty_frames\":%u,\"total_"
			   "frames\":%u,\"frames_per_second\":%u}\n",
			   race.finished ? "true" : "false", race.frames, race.finish_frame,
			   race.penalty_frames, race.total_frames, simulation_rate);
		status = race.finished ? 0 : 2;
		goto done;
	}
	signal(SIGINT, interrupt_search);
	signal(SIGTERM, interrupt_search);
	genghost_parallel_install_interrupt_handler();
	if (cooperative_request) {
		struct GENGHOST_STATE_EXECUTOR *executor = genghost_state_executor_create(&options);
		if (!executor) {
			goto done;
		}
		status = genghost_cooperative_worker(
			&worker_control, cooperative_request, sizeof(struct GENGHOST_STATE_TASK),
			sizeof(struct GENGHOST_STATE_REPLY), genghost_state_execute, executor);
		genghost_state_executor_destroy(executor);
		goto done;
	}
	struct GENGHOST_SEARCH_RESULT result = {0};
	struct output_context context = {output,	   track_name,		car,  &options,
									 transmission, simulation_rate, NULL, baseline.total_frames};
	struct GENGHOST_PARALLEL_REPORT parallel = {0};
	fprintf(stderr, "genghost %s / %s, %s, --pg:%s --lc:%s --lcb:%s --dws:%s --cfg:%s%s%s\n",
			track_name, car, transmission ? "automatic" : "manual",
			options.powergear ? "on" : "off", options.collision ? "on" : "off",
			options.left_bias ? "on" : "off", options.dual_way_switching ? "on" : "off",
			options.chicane_fast_grass ? "on" : "off", options.amiga_grass ? " --amigagrass" : "",
			options.owoot ? " --owoot" : "");
	fprintf(stderr, "search strategy: %s\n", strategy_name(&options));
	legacy_int search_status;
	if (options.strategy == GENGHOST_STRATEGY_STATE && cores != 1U) {
		struct GENGHOST_STATE_EXECUTOR *executor = genghost_state_executor_create(&options);
		if (!executor) {
			goto done;
		}
		context.parallel = &parallel;
		struct GENGHOST_COOPERATIVE_POOL *pool = genghost_cooperative_open(
			executable, output, data_dir, file, file_length, car, transmission, cores, &options,
			sizeof(struct GENGHOST_STATE_TASK), sizeof(struct GENGHOST_STATE_REPLY),
			genghost_state_execute, executor, &parallel);
		if (!pool) {
			genghost_state_executor_destroy(executor);
			goto done;
		}
		struct cooperative_context shared = {pool, &parallel};
		search_status = genghost_state_search(&options, seed_inputs, seed_length, &result,
											  save_result, &context, cooperative_batch, &shared);
		genghost_cooperative_close(pool);
		genghost_state_executor_destroy(executor);
	} else if (options.strategy == GENGHOST_STRATEGY_STATE) {
		search_status = genghost_state_search(&options, seed_inputs, seed_length, &result,
											  save_result, &context, NULL, NULL);
	} else if (cores != 1U) {
		context.parallel = &parallel;
		search_status = genghost_parallel_search(executable, output, data_dir, file, file_length,
												 car, transmission, cores, &options, &result,
												 &parallel, save_result, &context);
	} else {
		search_status = genghost_search(
			&options, seed_inputs, seed_length, &result,
			worker_control.status_path ? genghost_worker_save : save_result,
			worker_control.status_path ? (void *)&worker_control : (void *)&context);
	}
	if (worker_control.status_path) {
		status = genghost_worker_finish(&worker_control, &result, search_status);
		goto done;
	}
	fprintf(stderr,
			"%" LEGACY_PRIu64 " candidates, %" LEGACY_PRIu64
			" simulated ticks, %.2fs; %zu/%zu routes attempted\n",
			(legacy_u64)result.evaluations, (legacy_u64)result.simulated_frames,
			result.elapsed_seconds, result.routes_attempted, result.routes);
	if (!search_status) {
		if (save_result(&result, &context)) {
			goto done;
		}
		printf("Saved %s (%.2fs including penalties)\n", output,
			   result.race.total_frames / (legacy_f64)simulation_rate);
		if (options.fine_tune) {
			legacy_uint saved = baseline.total_frames - result.race.total_frames;
			if (saved) {
				printf("Fine-tuned %.2fs -> %.2fs; saved %.2fs including penalties\n",
					   baseline.total_frames / (legacy_f64)simulation_rate,
					   result.race.total_frames / (legacy_f64)simulation_rate,
					   saved / (legacy_f64)simulation_rate);
			} else {
				puts("No faster replay found within the budget; saved the verified baseline.");
			}
		}
		status = 0;
	} else if (search_status == 2) {
		fputs("No completed replay found within the budget; no ghost was written.\n", stderr);
		status = 2;
	} else if (result.race.finished) {
		/* Preserve the verified incumbent and expose worker errors in its report. */
		if (save_result(&result, &context)) {
			goto done;
		}
	}
done:
	genghost_worker_close(&worker_control);
	genghost_set_stop_file(NULL);
	free(stop_file);
	free(file);
	free(source_path);
	free(data_dir);
	free(output);
	SDL_free(cwd);
	SDL_free(executable);
	return status;
}
