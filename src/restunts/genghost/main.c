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
#include "search.h"
#include "../platform/sdl3/sdl3.h"

#ifndef RESTUNTS_GIT_HASH
#define RESTUNTS_GIT_HASH "source-archive"
#endif

struct output_context {
	const char *path, *track_name, *car;
	const struct GENGHOST_OPTIONS *options;
	int transmission;
};

static const char help[] =
	"genghost - offline, headless Stunts ghost generator\n"
	"Usage: genghost [--data-dir DIR] TRACK[.TRK|.RPL] CAR [options]\n\n"
	"  --output FILE          Replay destination (default TRK-CAR.rpl, DOS 8.3).\n"
	"  --seconds N            Search budget; default 1800 seconds, 0 until stopped.\n"
	"  --evaluations N        Deterministic candidate budget (disables default timer).\n"
	"  --seed N               Search random seed; default 1.\n"
	"  --transmission MODE    manual or auto; defaults to compatible replay, else manual.\n"
	"  --pg:on|off            Original power gear bug (default on).\n"
	"  --lc:on|off            Original collision behavior (default on).\n"
	"  --lcb:on|off           Original left corner bias (default on).\n"
	"  --owoot                Enforce One Wheel On/Over Track.\n"
	"  --verify               Validate the input replay and print its score; no search.\n"
	"  --help, -h             Show this help without loading game resources.\n"
	"  --version, -v          Show version.\n\n"
	"CAR is a four-character resource code, such as COUN, PMIN, or DIA3.\n"
	"Relative input paths resolve from the current directory, then --data-dir.\n"
	"Output paths always resolve from the original current directory.\n"
	"Search uses the original 20 Hz physics, including finish penalties.\n"
	"Ctrl+C keeps the best fully validated completed replay found so far.\n"
	"The accompanying .json records the physics flags required for playback.\n"
	"A bounded heuristic search cannot guarantee a finish or global optimum.\n";

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

static int read_source(const char *source, const char *cwd, const char *data_dir,
					   unsigned char **data, size_t *length)
{
	static const char *suffixes[] = {"", ".TRK", ".trk", ".RPL", ".rpl"};
	for (unsigned int directory = 0; directory < 2U; ++directory) {
		for (unsigned int i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i) {
			char *name = with_suffix(source, suffixes[i]);
			char *path = name ? join_path(directory ? data_dir : cwd, name) : NULL;
			free(name);
			if (!path) {
				return -1;
			}
			int status = read_file(path, data, length);
			free(path);
			if (!status) {
				return 0;
			}
		}
	}
	return -1;
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
	fprintf(file,
			",\n  \"seed_replay_finished\": %s,\n  \"seed_total_frames\": %u,\n"
			"  \"policy_evaluations\": %llu,\n  \"input_evaluations\": %llu,\n"
			"  \"guided_evaluations\": %llu,\n  \"guided_improvements\": %llu",
			result->seed_finished ? "true" : "false", result->seed_total_frames,
			(unsigned long long)result->policy_evaluations,
			(unsigned long long)result->input_evaluations,
			(unsigned long long)result->guided_evaluations,
			(unsigned long long)result->guided_improvements);
	fprintf(
		file,
		",\n  \"transmission\": \"%s\",\n  \"physics\": {\"pg\": %s, \"lc\": %s, \"lcb\": %s, "
		"\"owoot\": %s},\n"
		"  \"finished\": true,\n  \"frames_per_second\": 20,\n  \"replay_frames\": %u,\n"
		"  \"finish_frame\": %u,\n  \"penalty_frames\": %u,\n  \"total_frames\": %u,\n"
		"  \"total_seconds\": %.2f,\n  \"seed\": %u,\n  \"evaluations\": %llu,\n"
		"  \"simulated_frames\": %llu,\n  \"route_combinations\": %zu,\n"
		"  \"routes_attempted\": %zu,\n  \"routes_finished\": %zu,\n"
		"  \"search_seconds\": %.6f,\n  \"globally_optimal\": false\n}\n",
		context->transmission ? "auto" : "manual", context->options->powergear ? "true" : "false",
		context->options->collision ? "true" : "false",
		context->options->left_bias ? "true" : "false", context->options->owoot ? "true" : "false",
		result->length, result->race.finish_frame, result->race.penalty_frames,
		result->race.total_frames, result->race.total_frames / 20.0, context->options->seed,
		(unsigned long long)result->evaluations, (unsigned long long)result->simulated_frames,
		result->routes, result->routes_attempted, result->routes_finished, result->elapsed_seconds);
	int failed = ferror(file);
	if (fclose(file)) {
		failed = 1;
	}
	return failed ? -1 : 0;
}

static int save_result(const struct GENGHOST_SEARCH_RESULT *result, void *opaque)
{
	struct output_context *context = opaque;
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
	struct GENGHOST_OPTIONS options = {1800.0, 0, 1U, 1, 1, 1, 0};
	const char *source = NULL, *car_argument = NULL, *data_argument = ".", *output_argument = NULL;
	char car[5] = {0}, track_name[256];
	int transmission = -1, verify = 0, explicit_seconds = 0, status = 1;
	char *cwd = NULL, *data_dir = NULL, *output = NULL;
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
		if (!strcmp(argument, "--verify")) {
			verify = 1;
			continue;
		}
		if (!strcmp(argument, "--data-dir") || !strcmp(argument, "--output") ||
			!strcmp(argument, "--seconds") || !strcmp(argument, "--evaluations") ||
			!strcmp(argument, "--seed") || !strcmp(argument, "--transmission")) {
			if (++i == argc) {
				fprintf(stderr, "Missing value for %s\n", argument);
				goto done;
			}
			if (!strcmp(argument, "--data-dir")) {
				data_argument = argv[i];
			} else if (!strcmp(argument, "--output")) {
				output_argument = argv[i];
			} else if (!strcmp(argument, "--transmission")) {
				if (!strcmp(argv[i], "manual")) {
					transmission = 0;
				} else if (!strcmp(argv[i], "auto")) {
					transmission = 1;
				} else {
					fputs("Transmission must be manual or auto\n", stderr);
					goto done;
				}
			} else {
				uint64_t value;
				if (parse_integer(argv[i], !strcmp(argument, "--seed") ? UINT32_MAX : UINT64_MAX,
								  &value)) {
					fprintf(stderr, "Invalid numeric value for %s: %s\n", argument, argv[i]);
					goto done;
				}
				if (!strcmp(argument, "--seconds")) {
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
	if (!source || !car_argument) {
		fputs(help, stderr);
		goto done;
	}
	if (strlen(car_argument) != 4U) {
		fputs("CAR must have exactly four alphanumeric characters\n", stderr);
		goto done;
	}
	for (unsigned int i = 0; i < 4U; ++i) {
		if (!isalnum((unsigned char)car_argument[i])) {
			fputs("Invalid CAR code\n", stderr);
			goto done;
		}
		car[i] = (char)toupper((unsigned char)car_argument[i]);
	}
	if (options.evaluations && !explicit_seconds) {
		options.seconds = 0.0;
	}
	cwd = SDL_GetCurrentDirectory();
	if (!cwd || !(data_dir = join_path(cwd, data_argument))) {
		goto done;
	}
	if (read_source(source, cwd, data_dir, &file, &file_length)) {
		fprintf(stderr, "Cannot read track/replay: %s\n", source);
		goto done;
	}
	unsigned char *track = file, *seed_inputs = NULL;
	unsigned int seed_length = 0;
	struct GAMEINFO replay = {0};
	int is_replay = file_length >= REPLAY_INPUT_OFFSET && file_length != REPLAY_TRACK_SIZE;
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
		if (memcmp(replay.game_playercarid, car, 4U) == 0 && replay.game_framespersec == 20U) {
			if (transmission < 0) {
				transmission = replay.game_playertransmission;
			}
			if (transmission == replay.game_playertransmission) {
				seed_inputs = file + REPLAY_INPUT_OFFSET;
				seed_length = replay.game_recordedframes;
			}
		}
	} else if (file_length != REPLAY_TRACK_SIZE) {
		fputs("Invalid track; expected 1802 bytes or a complete Stunts 1.1 replay\n", stderr);
		goto done;
	}
	if (transmission < 0) {
		transmission = 0;
	}
	if (verify && (!seed_inputs || !seed_length)) {
		fputs("--verify requires a 20 Hz replay with matching car and transmission\n", stderr);
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
	char *physics_arguments[] = {"genghost", options.powergear ? "--pg:on" : "--pg:off",
								 options.collision ? "--lc:on" : "--lc:off",
								 options.left_bias ? "--lcb:on" : "--lcb:off", "--owoot"};
	if (genghost_sim_init(track, car, transmission, options.owoot ? 5 : 4, physics_arguments)) {
		goto done;
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
	struct GENGHOST_SEARCH_RESULT result;
	struct output_context context = {output, track_name, car, &options, transmission};
	fprintf(stderr, "genghost %s / %s, %s, --pg:%s --lc:%s --lcb:%s%s\n", track_name, car,
			transmission ? "automatic" : "manual", options.powergear ? "on" : "off",
			options.collision ? "on" : "off", options.left_bias ? "on" : "off",
			options.owoot ? " --owoot" : "");
	int search_status =
		genghost_search(&options, seed_inputs, seed_length, &result, save_result, &context);
	fprintf(stderr, "%llu candidates, %llu simulated ticks, %.2fs; %zu/%zu routes attempted\n",
			(unsigned long long)result.evaluations, (unsigned long long)result.simulated_frames,
			result.elapsed_seconds, result.routes_attempted, result.routes);
	if (!search_status) {
		if (save_result(&result, &context)) {
			goto done;
		}
		printf("Saved %s (%.2fs including penalties)\n", output, result.race.total_frames / 20.0);
		status = 0;
	} else if (search_status == 2) {
		fputs("No completed replay found within the budget; no ghost was written.\n", stderr);
		status = 2;
	}
done:
	free(file);
	free(data_dir);
	free(output);
	SDL_free(cwd);
	return status;
}
