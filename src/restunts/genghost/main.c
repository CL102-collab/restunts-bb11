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

#ifndef RESTUNTS_GIT_HASH
#define RESTUNTS_GIT_HASH "source-archive"
#endif

struct output_context {
	const char *path, *track_name, *car;
	const struct GENGHOST_OPTIONS *options;
	int transmission;
	const struct GENGHOST_PARALLEL_REPORT *parallel;
	unsigned int baseline_total_frames;
};

static const char help[] =
	"genghost - offline, headless Stunts ghost generator\n"
	"Usage: genghost [--data-dir DIR] TRACK[.TRK|.RPL] CAR [options]\n"
	"       genghost [--data-dir DIR] REPLAY.RPL [CAR] --fine-tune [options]\n"
	"       genghost [--data-dir DIR] REPLAY.RPL [CAR] --fine-tune-gears-only [options]\n\n"
	"  --output FILE          Replay destination (default TRK-CAR.rpl, DOS 8.3).\n"
	"  --seconds N            Search budget; default 1800 seconds, 0 until stopped.\n"
	"  --evaluations N        Deterministic candidate budget (disables default timer).\n"
	"  --seed N               Search random seed; default 1.\n"
	"  --strategy MODE        legacy (default) or state (cooperative state planner).\n"
	"  --cores N              Parallel search workers (1 default, 0 available cores).\n"
	"  --pg:on|off            Original power gear bug (default on).\n"
	"  --lc:on|off            Original collision behavior (default on).\n"
	"  --lcb:on|off           Original left corner bias (default on).\n"
	"  --dws:on|off           Dual-Way Switching (original behavior; default on).\n"
	"  --owoot                Enforce One Wheel On/Over Track.\n"
	"  /amigagrass            Original Amiga grass grip for stock cars (default off).\n"
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
	"Search uses the original 20 Hz physics, including finish penalties.\n"
	"The evaluation budget is shared across workers; seconds are wall-clock time.\n"
	"Ctrl+C keeps the best fully validated completed replay found so far.\n"
	"The accompanying .json records the physics flags required for playback.\n"
	"Fine-tuning requires a solo 20 Hz replay that finishes under the selected rules.\n"
	"Automatic seed shifts become manual inputs in either fine-tuning mode.\n"
	"--fine-tune and --fine-tune-gears-only cannot be combined.\n"
	"It writes a separate *-FT.rpl by default and reports whether the time improved.\n"
	"A bounded heuristic search cannot guarantee a finish or global optimum.\n";

static const char *strategy_name(const struct GENGHOST_OPTIONS *options)
{
	return options->strategy == GENGHOST_STRATEGY_STATE ? "state" : "legacy";
}

struct cooperative_context {
	struct GENGHOST_COOPERATIVE_POOL *pool;
	struct GENGHOST_PARALLEL_REPORT *report;
};

static int cooperative_batch(void *context, const struct GENGHOST_STATE_TASK *tasks,
							 struct GENGHOST_STATE_REPLY *replies, size_t count)
{
	struct cooperative_context *shared = context;
	int status = genghost_cooperative_batch(shared->pool, tasks, replies, count);
	if (!status) {
		for (size_t i = 0; i < count; ++i) {
			shared->report->workers[i % shared->report->cores_used].simulated_frames +=
				replies[i].simulated_frames;
		}
	}
	return status;
}

static void interrupt_search(int signal_number)
{
	(void)signal_number;
	genghost_interrupted = 1;
}

static char *join_path(const char *directory, const char *name)
{
	if (name[0] == '/' || name[0] == '\\' || (isalpha((unsigned char)name[0]) && name[1] == ':')) {
		return SDL_strdup(name);
	}
	size_t a = strlen(directory), b = strlen(name);
	char *result = malloc(a + b + 2U);
	if (result) {
		snprintf(result, a + b + 2U, "%s/%s", directory, name);
	}
	return result;
}

static const char *base_name(const char *path)
{
	const char *name = path;
	for (; *path; ++path) {
		if (*path == '/' || *path == '\\') {
			name = path + 1;
		}
	}
	return name;
}

static char *with_suffix(const char *path, const char *suffix)
{
	size_t size = strlen(path) + strlen(suffix) + 1U;
	char *result = malloc(size);
	if (result) {
		snprintf(result, size, "%s%s", path, suffix);
	}
	return result;
}

static int read_file(const char *path, unsigned char **data, size_t *length)
{
	FILE *file = fopen(path, "rb");
	if (!file) {
		return -1;
	}
	if (fseek(file, 0, SEEK_END) || (*length = (size_t)ftell(file)) > 65536U ||
		fseek(file, 0, SEEK_SET)) {
		fclose(file);
		return -1;
	}
	*data = malloc(*length + 1U);
	if (!*data) {
		fclose(file);
		return -1;
	}
	int failed = fread(*data, 1, *length, file) != *length;
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

static int read_source(const char *source, const char *cwd, const char *data_dir, int replay_only,
					   unsigned char **data, size_t *length, char **resolved_path)
{
	static const char *track_suffixes[] = {"", ".TRK", ".trk", ".RPL", ".rpl"};
	static const char *replay_suffixes[] = {"", ".RPL", ".rpl"};
	const char **suffixes = replay_only ? replay_suffixes : track_suffixes;
	unsigned int suffix_count = replay_only ? sizeof(replay_suffixes) / sizeof(replay_suffixes[0])
											: sizeof(track_suffixes) / sizeof(track_suffixes[0]);
	for (unsigned int directory = 0; directory < 2U; ++directory) {
		for (unsigned int i = 0; i < suffix_count; ++i) {
			char *name = with_suffix(source, suffixes[i]);
			char *path = name ? join_path(directory ? data_dir : cwd, name) : NULL;
			free(name);
			if (!path) {
				return -1;
			}
			int status = read_file(path, data, length);
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
static int same_file(const char *source, const char *destination)
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
	int result = -1;
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

static int parse_integer(const char *text, uint64_t maximum, uint64_t *value)
{
	char *end;
	errno = 0;
	if (!*text || *text == '-') {
		return -1;
	}
	unsigned long long number = strtoull(text, &end, 10);
	if (errno || *end || number > maximum) {
		return -1;
	}
	*value = number;
	return 0;
}

static int physics_option(const char *argument, const char *name, int *value)
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

static void json_string(FILE *file, const char *text)
{
	fputc('"', file);
	for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
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

static int write_report(const char *path, const struct GENGHOST_SEARCH_RESULT *result,
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
		unsigned int saved = context->baseline_total_frames - result->race.total_frames;
		fprintf(file,
				",\n  \"baseline_total_frames\": %u,\n  \"baseline_total_seconds\": %.2f,\n"
				"  \"improved\": %s,\n  \"saved_frames\": %u,\n  \"saved_seconds\": %.2f",
				context->baseline_total_frames, context->baseline_total_frames / 20.0,
				saved ? "true" : "false", saved, saved / 20.0);
	} else {
		fputs(",\n  \"baseline_total_frames\": null,\n  \"baseline_total_seconds\": null,\n"
			  "  \"improved\": null,\n  \"saved_frames\": null,\n  \"saved_seconds\": null",
			  file);
	}
	const struct GENGHOST_PARALLEL_REPORT *parallel = context->parallel;
	int shared = context->options->strategy == GENGHOST_STRATEGY_STATE;
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
	unsigned int worker_count = parallel ? parallel->cores_used : 1U;
	for (unsigned int i = 0; i < worker_count; ++i) {
		const struct GENGHOST_WORKER_STATS *worker = parallel ? &parallel->workers[i] : NULL;
		if (shared && worker) {
			fprintf(file,
					"%s\n    {\"index\": %u, \"seed\": %u, \"budget_evaluations\": null, "
					"\"evaluations\": %llu, \"simulated_frames\": %llu, \"finished\": null, "
					"\"total_frames\": null, \"status\": %d}",
					i ? "," : "", i, worker->seed, (unsigned long long)worker->evaluations,
					(unsigned long long)worker->simulated_frames, worker->status);
			continue;
		}

		fprintf(file,
				"%s\n    {\"index\": %u, \"seed\": %u, \"budget_evaluations\": %llu, "
				"\"evaluations\": %llu, \"simulated_frames\": %llu, \"finished\": %s, "
				"\"total_frames\": %u, \"status\": %d}",
				i ? "," : "", i, worker ? worker->seed : context->options->seed,
				(unsigned long long)(worker ? worker->budget_evaluations
											: context->options->evaluations),
				(unsigned long long)(worker ? worker->evaluations : result->evaluations),
				(unsigned long long)(worker ? worker->simulated_frames : result->simulated_frames),
				(worker ? worker->finished : result->race.finished) ? "true" : "false",
				worker ? worker->total_frames : result->race.total_frames,
				worker ? worker->status : 0);
	}
	fputs("\n  ]", file);
	fprintf(file,
			",\n  \"seed_replay_finished\": %s,\n  \"seed_total_frames\": %u,\n"
			"  \"policy_evaluations\": %llu,\n  \"input_evaluations\": %llu,\n"
			"  \"guided_evaluations\": %llu,\n  \"guided_improvements\": %llu,\n"
			"  \"tile_evaluations\": %llu,\n  \"tile_improvements\": %llu",
			result->seed_finished ? "true" : "false", result->seed_total_frames,
			(unsigned long long)result->policy_evaluations,
			(unsigned long long)result->input_evaluations,
			(unsigned long long)result->guided_evaluations,
			(unsigned long long)result->guided_improvements,
			(unsigned long long)result->tile_evaluations,
			(unsigned long long)result->tile_improvements);
	fprintf(file, ",\n  \"tactic_evaluations\": %llu,\n  \"tactic_improvements\": %llu",
			(unsigned long long)result->tactic_evaluations,
			(unsigned long long)result->tactic_improvements);
	fprintf(file,
			",\n  \"dualway_pairs\": %llu,\n  \"dualway_evaluations\": %llu,\n  "
			"\"dualway_improvements\": %llu",
			(unsigned long long)result->dualway_pairs,
			(unsigned long long)result->dualway_evaluations,
			(unsigned long long)result->dualway_improvements);
	fprintf(file,
			",\n  \"state_evaluations\": %llu,\n  \"state_frontier_peak\": %llu,\n"
			"  \"state_duplicates\": %llu,\n  \"state_pg_evaluations\": %llu,\n"
			"  \"state_pg_opportunities\": %llu,\n  \"state_macro_evaluations\": %llu,\n"
			"  \"state_input_evaluations\": %llu,\n  \"state_chicane_evaluations\": %llu,\n"
			"  \"state_task_hash\": \"%016llx\"",
			(unsigned long long)result->state_evaluations,
			(unsigned long long)result->state_frontier_peak,
			(unsigned long long)result->state_duplicates,
			(unsigned long long)result->state_pg_evaluations,
			(unsigned long long)result->state_pg_states,
			(unsigned long long)result->state_macro_evaluations,
			(unsigned long long)result->state_input_evaluations,
			(unsigned long long)result->state_chicane_evaluations,
			(unsigned long long)result->state_task_hash);
	fprintf(file, ",\n  \"state_section_starts\": %llu,\n  \"state_section_evaluations\": %llu",
			(unsigned long long)result->state_section_starts,
			(unsigned long long)result->state_section_evaluations);
	fprintf(file,
			",\n  \"state_rescue_starts\": %llu,\n  \"state_diversity_preserved\": %llu,\n"
			"  \"state_transition_stops\": %llu",
			(unsigned long long)result->state_rescue_starts,
			(unsigned long long)result->state_diversity_preserved,
			(unsigned long long)result->state_transition_stops);
	fprintf(
		file,
		",\n  \"transmission\": \"%s\",\n  \"physics\": {\"pg\": %s, \"lc\": %s, \"lcb\": %s, "
		"\"dws\": %s, \"owoot\": %s, \"amiga_grass\": %s},\n"
		"  \"finished\": true,\n  \"frames_per_second\": 20,\n  \"replay_frames\": %u,\n"
		"  \"finish_frame\": %u,\n  \"penalty_frames\": %u,\n  \"total_frames\": %u,\n"
		"  \"total_seconds\": %.2f,\n  \"seed\": %u,\n  \"evaluations\": %llu,\n"
		"  \"simulated_frames\": %llu,\n  \"route_combinations\": %zu,\n"
		"  \"routes_attempted\": %zu,\n  \"routes_finished\": %zu,\n"
		"  \"search_seconds\": %.6f,\n  \"globally_optimal\": false\n}\n",
		context->transmission ? "auto" : "manual", context->options->powergear ? "true" : "false",
		context->options->collision ? "true" : "false",
		context->options->left_bias ? "true" : "false",
		context->options->dual_way_switching ? "true" : "false",
		context->options->owoot ? "true" : "false",
		context->options->amiga_grass ? "true" : "false", result->length, result->race.finish_frame,
		result->race.penalty_frames, result->race.total_frames, result->race.total_frames / 20.0,
		context->options->seed, (unsigned long long)result->evaluations,
		(unsigned long long)result->simulated_frames, result->routes, result->routes_attempted,
		result->routes_finished, result->elapsed_seconds);
	int failed = ferror(file);
	if (fclose(file)) {
		failed = 1;
	}
	return failed ? -1 : 0;
}

static int save_result(const struct GENGHOST_SEARCH_RESULT *result, void *opaque)
{
	struct output_context *context = opaque;
	if (context->options->fine_tune &&
		(!result->race.finished || result->race.total_frames > context->baseline_total_frames)) {
		fputs("genghost: refusing a fine-tuning result worse than its verified baseline\n", stderr);
		return -1;
	}
	char suffix[64];
	snprintf(suffix, sizeof(suffix), ".tmp-%llu", (unsigned long long)SDL_GetTicksNS());
	char *temporary = with_suffix(context->path, suffix);
	char *report = with_suffix(context->path, ".json");
	char *temporary_report = report ? with_suffix(report, suffix) : NULL;
	int failed = !temporary || !report || !temporary_report;
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

int main(int argc, char **argv)
{
	const legacy_char *asset_directory = getenv("RESTUNTS_ASSETS_DIR");
	file_car_set_base(
		(const legacy_s8 *)(asset_directory != NULL ? asset_directory : SDL_GetBasePath()));
	struct GENGHOST_OPTIONS options = {1800.0, 0, 1U, 1, 1, 1, 1, 0, 0};
	const char *source = NULL, *car_argument = NULL, *data_argument = ".", *output_argument = NULL;
	char car[5] = {0}, track_name[256];
	int transmission = TRANSMISSION_MANUAL, verify = 0, explicit_seconds = 0, status = 1;
	unsigned int cores = 1U;
	const char *cooperative_request = NULL;
	struct GENGHOST_WORKER_CONTROL worker_control = {0};
	char *cwd = NULL, *data_dir = NULL, *output = NULL, *executable = NULL, *source_path = NULL;
	struct GENGHOST_RESULT baseline = {0};
	unsigned char *file = NULL;
	size_t file_length = 0;
	for (int i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
			fputs(help, stdout);
			return 0;
		}
		if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-v")) {
			printf("genghost %.7s (%s)\n", RESTUNTS_GIT_HASH, __DATE__);
			return 0;
		}
	}
	for (int i = 1; i < argc; ++i) {
		const char *argument = argv[i];
		int flag = physics_option(argument, "--pg:", &options.powergear);
		if (!flag) {
			flag = physics_option(argument, "--lc:", &options.collision);
		}
		if (!flag) {
			flag = physics_option(argument, "--lcb:", &options.left_bias);
		}
		if (!flag) {
			flag = physics_option(argument, "--dws:", &options.dual_way_switching);
		}
		if (flag < 0) {
			fprintf(stderr, "Invalid physics option: %s\n", argument);
			goto done;
		}
		if (flag) {
			continue;
		}
		if (!SDL_strcasecmp(argument, "--owoot")) {
			options.owoot = 1;
			continue;
		}
		if (!SDL_strcasecmp(argument, "/amigagrass")) {
			options.amiga_grass = 1;
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
			!strcmp(argument, "--cooperative-worker")) {
			if (++i == argc) {
				fprintf(stderr, "Missing value for %s\n", argument);
				goto done;
			}
			if (!strcmp(argument, "--data-dir")) {
				data_argument = argv[i];
			} else if (!strcmp(argument, "--output")) {
				output_argument = argv[i];
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
				uint64_t value;
				uint64_t maximum = !strcmp(argument, "--seed")	  ? UINT32_MAX
								   : !strcmp(argument, "--cores") ? GENGHOST_MAX_CORES
																  : UINT64_MAX;
				if (parse_integer(argv[i], maximum, &value)) {
					fprintf(stderr, "Invalid numeric value for %s: %s\n", argument, argv[i]);
					goto done;
				}
				if (!strcmp(argument, "--cores")) {
					cores = (unsigned int)value;
				} else if (!strcmp(argument, "--worker-parent")) {
					worker_control.parent_pid = value;
				} else if (!strcmp(argument, "--seconds")) {
					options.seconds = (double)value;
					explicit_seconds = 1;
				} else if (!strcmp(argument, "--evaluations")) {
					if (!value) {
						fputs("--evaluations must be positive\n", stderr);
						goto done;
					}
					options.evaluations = value;
				} else {
					options.seed = (uint32_t)value;
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
	const char *fine_tune_option =
		options.fine_tune_gears_only ? "--fine-tune-gears-only" : "--fine-tune";
	if (options.fine_tune && verify) {
		fprintf(stderr, "%s cannot be combined with --verify\n", fine_tune_option);
		goto done;
	}
	if (!source) {
		if (options.fine_tune) {
			fprintf(stderr, "%s requires an input replay (.RPL)\n", fine_tune_option);
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
	} else {
		if (!car_argument) {
			fputs("CAR is required for generation and --verify\n", stderr);
			goto done;
		}
		if (strlen(car_argument) != 4U) {
			fputs("CAR must have exactly four letters, digits or underscores\n", stderr);
			goto done;
		}
		for (unsigned int i = 0; i < 4U; ++i) {
			if (!isalnum((unsigned char)car_argument[i]) && car_argument[i] != '_') {
				fputs("Invalid CAR code\n", stderr);
				goto done;
			}
			car[i] = (char)toupper((unsigned char)car_argument[i]);
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
	if (read_source(source, cwd, data_dir, options.fine_tune, &file, &file_length, &source_path)) {
		fprintf(stderr, "Cannot read %s: %s\n",
				options.fine_tune ? "input replay (.RPL)" : "track/replay", source);
		goto done;
	}
	unsigned char *track = file, *seed_inputs = NULL;
	unsigned int seed_length = 0;
	struct GAMEINFO replay = {0};
	int is_replay = file_length >= REPLAY_INPUT_OFFSET && file_length != REPLAY_TRACK_SIZE;
	if (options.fine_tune && !is_replay) {
		fprintf(stderr, "%s requires a complete replay (.RPL), not a track-only or invalid file\n",
				fine_tune_option);
		goto done;
	}
	if (is_replay) {
		replay_gameinfo_decode(&replay, file);
		if ((replay.game_framespersec != 10U && replay.game_framespersec != 20U) ||
			replay.game_recordedframes > GENGHOST_MAX_FRAMES ||
			replay_file_size(replay.game_recordedframes) != file_length ||
			replay.game_playertransmission < 0 || replay.game_playertransmission > 1) {
			fputs("Invalid or unsupported replay; expected a complete Stunts 1.1 RPL\n", stderr);
			goto done;
		}
		track = file + REPLAY_GAMEINFO_SIZE;
		if (options.fine_tune) {
			for (unsigned int i = 0; i < REPLAY_CAR_ID_SIZE; ++i) {
				if (!isalnum((unsigned char)replay.game_playercarid[i]) &&
					replay.game_playercarid[i] != '_') {
					fputs("Invalid CAR code in input replay header\n", stderr);
					goto done;
				}
				car[i] = (char)toupper((unsigned char)replay.game_playercarid[i]);
			}
		}
		if ((options.fine_tune || memcmp(replay.game_playercarid, car, 4U) == 0) &&
			replay.game_framespersec == 20U) {
			if (options.fine_tune || verify ||
				replay.game_playertransmission == TRANSMISSION_MANUAL) {
				seed_inputs = file + REPLAY_INPUT_OFFSET;
				seed_length = replay.game_recordedframes;
			}
		}
	} else if (file_length != REPLAY_TRACK_SIZE) {
		fputs("Invalid track; expected 1802 bytes or a complete Stunts 1.1 replay\n", stderr);
		goto done;
	}
	if (options.fine_tune && (!seed_inputs || !seed_length)) {
		fprintf(stderr, "%s requires a nonempty 20 Hz replay\n", fine_tune_option);
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
		fputs("--verify requires a 20 Hz replay with matching car\n", stderr);
		goto done;
	}
	if (verify && replay.game_opponenttype != 0) {
		fputs("--verify requires a solo replay; opponent collisions change the simulation\n",
			  stderr);
		goto done;
	}
	snprintf(track_name, sizeof(track_name), "%s", base_name(source));
	char *extension = strrchr(track_name, '.');
	if (extension) {
		*extension = 0;
	}
	if (!*track_name) {
		strcpy(track_name, "TRACK");
	}
	if (output_argument) {
		output = join_path(cwd, output_argument);
	} else if (options.fine_tune) {
		char prefix[6] = {0}, default_name[13];
		for (unsigned int i = 0; i < 5U && track_name[i]; ++i) {
			unsigned char letter = (unsigned char)toupper((unsigned char)track_name[i]);
			prefix[i] = (letter >= 'A' && letter <= 'Z') || (letter >= '0' && letter <= '9')
							? (char)letter
							: '_';
		}
		snprintf(default_name, sizeof(default_name), "%s-FT.rpl", prefix);
		output = join_path(cwd, default_name);
	} else {
		/* The game's replay picker only enumerates DOS 8.3 filenames. */
		char prefix[4] = {0}, default_name[13];
		for (unsigned int i = 0; i < 3U && track_name[i]; ++i) {
			unsigned char letter = (unsigned char)toupper((unsigned char)track_name[i]);
			prefix[i] = '_';
			if ((letter >= 'A' && letter <= 'Z') || (letter >= '0' && letter <= '9')) {
				prefix[i] = (char)letter;
			}
		}
		snprintf(default_name, sizeof(default_name), "%s-%s.rpl", prefix, car);
		output = join_path(cwd, default_name);
	}
	if (!output) {
		goto done;
	}
	if (options.fine_tune) {
		char *report_path = with_suffix(output, ".json");
		int source_output = same_file(source_path, output);
		int source_report = report_path ? same_file(source_path, report_path) : -1;
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
	char *physics_arguments[] = {"genghost",
								 options.powergear ? "--pg:on" : "--pg:off",
								 options.collision ? "--lc:on" : "--lc:off",
								 options.left_bias ? "--lcb:on" : "--lcb:off",
								 options.dual_way_switching ? "--dws:on" : "--dws:off",
								 NULL,
								 NULL};
	int physics_count = 5;
	if (options.owoot) {
		physics_arguments[physics_count++] = "--owoot";
	}
	if (options.amiga_grass) {
		physics_arguments[physics_count++] = "/amigagrass";
	}
	int source_transmission =
		options.fine_tune || verify ? replay.game_playertransmission : TRANSMISSION_MANUAL;
	if (genghost_sim_init(track, car, source_transmission, physics_count, physics_arguments)) {
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
		replay_gameinfo_encode(file, &replay);
		fprintf(stderr, "fine-tune baseline: %.2fs (driving %.2fs + penalty %.2fs)\n",
				baseline.total_frames / 20.0, baseline.finish_frame / 20.0,
				baseline.penalty_frames / 20.0);
	}
	if (verify) {
		struct GENGHOST_RESULT race;
		genghost_sim_evaluate(seed_inputs, seed_length, &race);
		printf("{\"finished\":%s,\"frames\":%u,\"finish_frame\":%u,\"penalty_frames\":%u,\"total_"
			   "frames\":%u}\n",
			   race.finished ? "true" : "false", race.frames, race.finish_frame,
			   race.penalty_frames, race.total_frames);
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
	struct output_context context = {output, track_name,		   car, &options, transmission,
									 NULL,	 baseline.total_frames};
	struct GENGHOST_PARALLEL_REPORT parallel = {0};
	fprintf(stderr, "genghost %s / %s, %s, --pg:%s --lc:%s --lcb:%s --dws:%s%s%s\n", track_name,
			car, transmission ? "automatic" : "manual", options.powergear ? "on" : "off",
			options.collision ? "on" : "off", options.left_bias ? "on" : "off",
			options.dual_way_switching ? "on" : "off", options.owoot ? " --owoot" : "",
			options.amiga_grass ? " /amigagrass" : "");
	fprintf(stderr, "search strategy: %s\n", strategy_name(&options));
	int search_status;
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
	fprintf(stderr, "%llu candidates, %llu simulated ticks, %.2fs; %zu/%zu routes attempted\n",
			(unsigned long long)result.evaluations, (unsigned long long)result.simulated_frames,
			result.elapsed_seconds, result.routes_attempted, result.routes);
	if (!search_status) {
		if (save_result(&result, &context)) {
			goto done;
		}
		printf("Saved %s (%.2fs including penalties)\n", output, result.race.total_frames / 20.0);
		if (options.fine_tune) {
			unsigned int saved = baseline.total_frames - result.race.total_frames;
			if (saved) {
				printf("Fine-tuned %.2fs -> %.2fs; saved %.2fs including penalties\n",
					   baseline.total_frames / 20.0, result.race.total_frames / 20.0, saved / 20.0);
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
	free(file);
	free(source_path);
	free(data_dir);
	free(output);
	SDL_free(cwd);
	SDL_free(executable);
	return status;
}
