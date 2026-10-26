#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <SDL3/SDL.h>
#include "parallel.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#if defined(__linux__) || defined(__ANDROID__)
#include <sched.h>
#include <sys/prctl.h>
#endif
#ifdef __ANDROID__
#include <fcntl.h>
#endif
#endif

#define WORKER_RUNNING 3
#define POLL_NS 100000000ULL
#define PUBLISH_NS 1000000000ULL
#define STOP_GRACE_NS 2000000000ULL

/* Private IPC between identical executables on one machine. No native struct
 * is persisted as a public replay format or accepted from an input replay. */
struct worker_snapshot {
	char magic[8];
	uint64_t sequence;
	int status;
	struct GENGHOST_SEARCH_RESULT result;
};

struct worker {
#ifdef __ANDROID__
	pid_t process;
#else
	SDL_Process *process;
#endif
	char *status_path, *log_path;
	uint64_t sequence;
	int started, done, exit_status, failed;
	struct GENGHOST_SEARCH_RESULT latest;
};

static uint64_t current_pid(void)
{
#ifdef _WIN32
	return GetCurrentProcessId();
#else
	return (uint64_t)getpid();
#endif
}

char *genghost_executable_path(const char *argument)
{
#ifdef _WIN32
	WCHAR name[32768];
	DWORD length = GetModuleFileNameW(NULL, name, (DWORD)SDL_arraysize(name));
	if (length && length < SDL_arraysize(name)) {
		return SDL_iconv_string("UTF-8", "UTF-16LE", (const char *)name,
								(length + 1U) * sizeof(*name));
	}
#else
	char name[4096];
	ssize_t length = readlink("/proc/self/exe", name, sizeof(name) - 1U);
	if (length > 0 && (size_t)length < sizeof(name) - 1U) {
		name[length] = 0;
		return SDL_strdup(name);
	}
#endif
	/* A fallback for a platform without /proc; resolve before main changes cwd. */
	if (argument[0] == '/' || argument[0] == '\\' || strchr(argument, ':')) {
		return SDL_strdup(argument);
	}
	char *directory = SDL_GetCurrentDirectory();
	char *path = NULL;
	if (directory) {
		SDL_asprintf(&path, "%s/%s", directory, argument);
	}
	SDL_free(directory);
	return path;
}

unsigned int genghost_available_cores(void)
{
	unsigned int count = (unsigned int)SDL_GetNumLogicalCPUCores();
#ifdef _WIN32
	DWORD_PTR process_mask, system_mask;
	if (GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask)) {
		count = 0;
		while (process_mask) {
			count += (unsigned int)(process_mask & 1U);
			process_mask >>= 1U;
		}
	}
#elif defined(__linux__) || defined(__ANDROID__)
	cpu_set_t mask;
	if (!sched_getaffinity(0, sizeof(mask), &mask)) {
		count = (unsigned int)CPU_COUNT(&mask);
	}
	/* Honor common container CPU quotas as well as the process affinity mask.
	 * Round fractional quotas up so the last partial CPU can still be used. */
	FILE *quota_file = fopen("/sys/fs/cgroup/cpu.max", "r");
	unsigned long long quota = 0, period = 0;
	if (quota_file) {
		if (fscanf(quota_file, "%llu %llu", &quota, &period) != 2) {
			quota = period = 0;
		}
		fclose(quota_file);
	} else {
		long long old_quota = -1;
		quota_file = fopen("/sys/fs/cgroup/cpu/cpu.cfs_quota_us", "r");
		if (quota_file) {
			if (fscanf(quota_file, "%lld", &old_quota) != 1) {
				old_quota = -1;
			}
			fclose(quota_file);
		}
		quota_file = fopen("/sys/fs/cgroup/cpu/cpu.cfs_period_us", "r");
		if (quota_file) {
			if (fscanf(quota_file, "%llu", &period) != 1) {
				period = 0;
			}
			fclose(quota_file);
		}
		if (old_quota > 0) {
			quota = (unsigned long long)old_quota;
		}
	}
	if (quota && period) {
		uint64_t limited = quota / period + (quota % period != 0U);
		if (limited < count) {
			count = (unsigned int)limited;
		}
	}
#endif
	if (!count) {
		count = 1U;
	}
	return count > GENGHOST_MAX_CORES ? GENGHOST_MAX_CORES : count;
}

#ifdef _WIN32
static BOOL WINAPI console_interrupt(DWORD event)
{
	if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT || event == CTRL_CLOSE_EVENT) {
		genghost_interrupted = 1;
		return TRUE;
	}
	return FALSE;
}
#endif

void genghost_parallel_install_interrupt_handler(void)
{
#ifdef _WIN32
	SetConsoleCtrlHandler(console_interrupt, TRUE);
#endif
}

int genghost_worker_attach(struct GENGHOST_WORKER_CONTROL *control)
{
	if (!control->status_path || !control->stop_path || control->parent_pid <= 1U) {
		return -1;
	}
#ifdef _WIN32
	control->parent_handle = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)control->parent_pid);
	if (!control->parent_handle ||
		WaitForSingleObject((HANDLE)control->parent_handle, 0) != WAIT_TIMEOUT) {
		return -1;
	}
#else
#if defined(__linux__) || defined(__ANDROID__)
	if (prctl(PR_SET_PDEATHSIG, SIGKILL)) {
		return -1;
	}
#endif
	/* Recheck after installing PDEATHSIG: the parent can die before prctl. */
	if ((uint64_t)getppid() != control->parent_pid) {
		return -1;
	}
#endif
	return 0;
}

void genghost_worker_close(struct GENGHOST_WORKER_CONTROL *control)
{
#ifdef _WIN32
	if (control->parent_handle) {
		CloseHandle((HANDLE)control->parent_handle);
	}
#else
	(void)control;
#endif
}

static int publish_worker(struct GENGHOST_WORKER_CONTROL *control,
						  const struct GENGHOST_SEARCH_RESULT *result, int status)
{
	struct worker_snapshot snapshot = {"GGWORK1", ++control->sequence, status, {0}};
	snapshot.result = *result;
	char *temporary = NULL;
	SDL_asprintf(&temporary, "%s.tmp", control->status_path);
	if (!temporary) {
		control->failed = 1;
		return -1;
	}
	FILE *file = fopen(temporary, "wb");
	int failed = !file;
	if (file) {
		failed = fwrite(&snapshot, 1U, sizeof(snapshot), file) != sizeof(snapshot);
		if (fclose(file)) {
			failed = 1;
		}
	}
	if (!failed) {
		/* A reader can momentarily hold the old file open on Windows. */
		failed = 1;
		for (unsigned int attempt = 0; attempt < 20U; ++attempt) {
			if (SDL_RenamePath(temporary, control->status_path)) {
				failed = 0;
				break;
			}
			SDL_Delay(5U);
		}
	}
	if (failed) {
		remove(temporary);
		control->failed = 1;
	}
	SDL_free(temporary);
	control->next_publish = SDL_GetTicksNS() + PUBLISH_NS;
	return failed ? -1 : 0;
}

int genghost_worker_progress(const struct GENGHOST_SEARCH_RESULT *result, void *opaque)
{
	struct GENGHOST_WORKER_CONTROL *control = opaque;
	uint64_t now = SDL_GetTicksNS();
	if (now < control->next_poll) {
		return control->failed;
	}
	control->next_poll = now + POLL_NS;
#ifdef _WIN32
	if (WaitForSingleObject((HANDLE)control->parent_handle, 0) != WAIT_TIMEOUT) {
		genghost_interrupted = 1;
	}
#else
	if ((uint64_t)getppid() != control->parent_pid) {
		genghost_interrupted = 1;
	}
#endif
	/* SDL_GetPathInfo falls back to Android's Java asset manager for a missing
	 * file, even an absolute path. Workers have no JVM; use native file I/O. */
	FILE *stop = fopen(control->stop_path, "rb");
	if (stop) {
		fclose(stop);
		genghost_interrupted = 1;
	}
	if (now >= control->next_publish && publish_worker(control, result, WORKER_RUNNING)) {
		return 1;
	}
	return genghost_stop_requested() || control->failed;
}

int genghost_worker_save(const struct GENGHOST_SEARCH_RESULT *result, void *opaque)
{
	return publish_worker(opaque, result, WORKER_RUNNING);
}

int genghost_worker_finish(struct GENGHOST_WORKER_CONTROL *control,
						   const struct GENGHOST_SEARCH_RESULT *result, int status)
{
	if (control->failed || (status != 0 && status != 2)) {
		status = 1;
	}
	return publish_worker(control, result, status) ? 1 : status;
}

static uint32_t worker_seed(uint32_t base, unsigned int index)
{
	/* Index zero is exactly the existing search. A bijective integer mix gives
	 * the other islands distinct nonzero xorshift states, including seed zero. */
	uint32_t normalized = base ? base : 1U;
	if (!index) {
		return base;
	}
	uint32_t value = normalized + 0x9e3779b9U * index;
	value ^= value >> 16U;
	value *= 0x7feb352dU;
	value ^= value >> 15U;
	value *= 0x846ca68bU;
	value ^= value >> 16U;
	if (!value || value == normalized) {
		value = normalized ^ (0xa5a5a5a5U + index);
		if (!value) {
			value = index + 1U;
		}
	}
	return value;
}

static int write_bytes(const char *path, const void *bytes, size_t length)
{
	FILE *file = fopen(path, "wb");
	if (!file) {
		return -1;
	}
	int failed = fwrite(bytes, 1, length, file) != length;
	if (fclose(file)) {
		failed = 1;
	}
	return failed ? -1 : 0;
}

static int start_worker(struct worker *worker, const char *const *arguments, void *job)
{
#ifdef __ANDROID__
	/* SDL uses its dummy process backend on Android. This standalone native
	 * executable has no Java VM or SDL threads; exec immediately after fork. */
	int descriptor = open(worker->log_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (descriptor < 0) {
		return -1;
	}
	pid_t parent = getpid();
	pid_t process = fork();
	if (process == 0) {
		if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent ||
			dup2(descriptor, STDOUT_FILENO) < 0 || dup2(descriptor, STDERR_FILENO) < 0) {
			_exit(1);
		}
		close(descriptor);
		execv(arguments[0], (char *const *)arguments);
		_exit(1);
	}
	close(descriptor);
	(void)job;
	if (process < 0) {
		return -1;
	}
	worker->process = process;
#else
	SDL_IOStream *log = SDL_IOFromFile(worker->log_path, "wb");
	SDL_PropertiesID properties = SDL_CreateProperties();
	if (!log || !properties) {
		if (log) {
			SDL_CloseIO(log);
		}
		SDL_DestroyProperties(properties);
		return -1;
	}
	SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)arguments);
	SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
						  SDL_PROCESS_STDIO_REDIRECT);
	SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_POINTER, log);
	SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
	worker->process = SDL_CreateProcessWithProperties(properties);
	SDL_DestroyProperties(properties);
	SDL_CloseIO(log);
	if (!worker->process) {
		return -1;
	}
#ifdef _WIN32
	DWORD pid = (DWORD)SDL_GetNumberProperty(SDL_GetProcessProperties(worker->process),
											 SDL_PROP_PROCESS_PID_NUMBER, 0);
	HANDLE process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, pid);
	int assigned = process && AssignProcessToJobObject((HANDLE)job, process);
	if (process) {
		CloseHandle(process);
	}
	if (!assigned) {
		SDL_KillProcess(worker->process, true);
		SDL_WaitProcess(worker->process, true, NULL);
		SDL_DestroyProcess(worker->process);
		worker->process = NULL;
		return -1;
	}
#else
	(void)job;
#endif
#endif
	worker->started = 1;
	return 0;
}

static int poll_worker(struct worker *worker)
{
	if (!worker->started || worker->done) {
		return worker->done;
	}
#ifdef __ANDROID__
	int status;
	pid_t got = waitpid(worker->process, &status, WNOHANG);
	if (got == 0 || (got < 0 && errno == EINTR)) {
		return 0;
	}
	worker->exit_status = got < 0 ? 1 : WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#else
	if (!SDL_WaitProcess(worker->process, false, &worker->exit_status)) {
		return 0;
	}
#endif
	worker->done = 1;
	return 1;
}

static void force_worker(struct worker *worker)
{
	if (!worker->started || worker->done) {
		return;
	}
#ifdef __ANDROID__
	kill(worker->process, SIGKILL);
	int status;
	while (waitpid(worker->process, &status, 0) < 0 && errno == EINTR) {
	}
	worker->exit_status = 1;
#else
	SDL_KillProcess(worker->process, true);
	SDL_WaitProcess(worker->process, true, &worker->exit_status);
#endif
	worker->done = 1;
}

static int read_worker(struct worker *worker, struct GENGHOST_WORKER_STATS *stats)
{
	FILE *file = fopen(worker->status_path, "rb");
	if (!file) {
		return 0;
	}
	struct worker_snapshot snapshot;
	int valid = fread(&snapshot, 1, sizeof(snapshot), file) == sizeof(snapshot) &&
				fgetc(file) == EOF && memcmp(snapshot.magic, "GGWORK1", 8U) == 0 &&
				snapshot.status >= 0 && snapshot.status <= WORKER_RUNNING &&
				snapshot.result.length <= GENGHOST_MAX_FRAMES;
	fclose(file);
	if (!valid) {
		return -1;
	}
	if (snapshot.sequence <= worker->sequence) {
		return 0;
	}
	worker->sequence = snapshot.sequence;
	worker->latest = snapshot.result;
	stats->evaluations = snapshot.result.evaluations;
	stats->simulated_frames = snapshot.result.simulated_frames;
	stats->finished = snapshot.result.race.finished;
	stats->total_frames = snapshot.result.race.total_frames;
	stats->elapsed_seconds = snapshot.result.elapsed_seconds;
	stats->status = snapshot.status;
	return 1;
}

static void aggregate(struct GENGHOST_SEARCH_RESULT *result, const struct worker *workers,
					  unsigned int count, uint64_t started)
{
	result->evaluations = result->simulated_frames = 0;
	result->policy_evaluations = result->input_evaluations = 0;
	result->guided_evaluations = result->guided_improvements = 0;
	result->tile_evaluations = result->tile_improvements = 0;
	result->tactic_evaluations = result->tactic_improvements = 0;
	result->dualway_evaluations = result->dualway_improvements = result->dualway_pairs = 0;
	result->routes = result->routes_attempted = result->routes_finished = 0;
	result->seed_finished = 0;
	result->seed_total_frames = 0;
	for (unsigned int i = 0; i < count; ++i) {
		const struct GENGHOST_SEARCH_RESULT *part = &workers[i].latest;
#define SUM_FIELD(field) result->field += part->field
		SUM_FIELD(evaluations);
		SUM_FIELD(simulated_frames);
		SUM_FIELD(policy_evaluations);
		SUM_FIELD(input_evaluations);
		SUM_FIELD(guided_evaluations);
		SUM_FIELD(guided_improvements);
		SUM_FIELD(tile_evaluations);
		SUM_FIELD(tile_improvements);
		SUM_FIELD(tactic_evaluations);
		SUM_FIELD(tactic_improvements);
		SUM_FIELD(dualway_evaluations);
		SUM_FIELD(dualway_improvements);
#undef SUM_FIELD
#define MAX_FIELD(field) result->field = SDL_max(result->field, part->field)
		MAX_FIELD(dualway_pairs);
		MAX_FIELD(routes);
		MAX_FIELD(routes_attempted);
		MAX_FIELD(routes_finished);
		MAX_FIELD(seed_finished);
		MAX_FIELD(seed_total_frames);
#undef MAX_FIELD
	}
	result->elapsed_seconds = (SDL_GetTicksNS() - started) / 1000000000.0;
}

static void show_worker_log(const struct worker *worker)
{
	FILE *file = fopen(worker->log_path, "rb");
	if (file) {
		char log[2049];
		size_t size = fread(log, 1, sizeof(log) - 1U, file);
		log[size] = 0;
		fputs(log, stderr);
		fclose(file);
	}
}

static void show_worker_error(unsigned int index, const struct worker *worker)
{
	fprintf(stderr, "genghost: worker %u failed (exit %d); retained any verified best replay.\n",
			index, worker->exit_status);
	show_worker_log(worker);
}

int genghost_parallel_search(const char *executable, const char *output, const char *data_dir,
							 const unsigned char *source, size_t source_length, const char *car,
							 int transmission, unsigned int requested_cores,
							 const struct GENGHOST_OPTIONS *options,
							 struct GENGHOST_SEARCH_RESULT *result,
							 struct GENGHOST_PARALLEL_REPORT *report, genghost_save_function save,
							 void *save_context)
{
	/* Kept for native callers; generated workers always use manual transmission. */
	(void)transmission;
	unsigned int count = requested_cores ? requested_cores : genghost_available_cores();
	if (options->evaluations && options->evaluations < count) {
		count = (unsigned int)options->evaluations;
	}
	unsigned int allocated = count;
	memset(result, 0, sizeof(*result));
	memset(report, 0, sizeof(*report));
	report->cores_requested = requested_cores;
	report->cores_used = count;
	report->winner_worker = UINT_MAX;
	report->winner_seed = options->seed;
	unsigned int baseline_total_frames = 0U;
	if (options->fine_tune) {
		/* Retain a canonical parent baseline even if cancellation or startup
		 * overhead prevents every child from publishing its first result. */
		if (!source || source_length <= REPLAY_INPUT_OFFSET ||
			source_length - REPLAY_INPUT_OFFSET > GENGHOST_MAX_FRAMES) {
			return 1;
		}
		result->length = (unsigned int)(source_length - REPLAY_INPUT_OFFSET);
		memcpy(result->inputs, source + REPLAY_INPUT_OFFSET, result->length);
		if (genghost_sim_evaluate(result->inputs, result->length, &result->race) ||
			!result->race.finished) {
			return 1;
		}
		result->seed_finished = 1;
		baseline_total_frames = result->seed_total_frames = result->race.total_frames;
	}
	struct worker *workers = calloc(count, sizeof(*workers));
	char *directory = NULL, *input_path = NULL, *stop_path = NULL;
	void *job = NULL;
	uint64_t started = SDL_GetTicksNS(), stop_started = 0, next_progress = 0;
	int failed = !workers, directory_created = 0;
	if (!workers) {
		return 1;
	}
	SDL_asprintf(&directory, "%s.work-%llu-%llu", output, (unsigned long long)current_pid(),
				 (unsigned long long)started);
	if (!directory || !SDL_CreateDirectory(directory)) {
		failed = 1;
		goto cleanup;
	}
	directory_created = 1;
	SDL_asprintf(&input_path, "%s/source.%s", directory,
				 source_length == REPLAY_TRACK_SIZE ? "trk" : "rpl");
	SDL_asprintf(&stop_path, "%s/stop", directory);
	if (!input_path || !stop_path || write_bytes(input_path, source, source_length)) {
		failed = 1;
		goto cleanup;
	}
#ifdef _WIN32
	job = CreateJobObjectW(NULL, NULL);
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	if (!job || !SetInformationJobObject((HANDLE)job, JobObjectExtendedLimitInformation, &limits,
										 sizeof(limits))) {
		failed = 1;
		goto cleanup;
	}
#endif
	fprintf(stderr, "parallel search: %u workers, %s budget; independent seeded islands\n", count,
			options->evaluations ? "shared candidate" : "wall-clock");
	for (unsigned int i = 0; i < count; ++i) {
		/* Starting a large pool is part of the wall-clock budget. Do not launch
		 * more children after the deadline or a cancellation during startup. */
		if (genghost_stop_requested() ||
			(options->seconds > 0.0 &&
			 (SDL_GetTicksNS() - started) / 1000000000.0 >= options->seconds)) {
			count = i;
			report->cores_used = count;
			break;
		}
		struct worker *worker = &workers[i];
		struct GENGHOST_WORKER_STATS *stats = &report->workers[i];
		stats->seed = worker_seed(options->seed, i);
		/* Extremely rare hash collisions are resolved without changing earlier
		 * streams. Repeatedly checking all prior states makes uniqueness exact. */
		for (unsigned int j = 0; j < i; ++j) {
			uint32_t previous = report->workers[j].seed ? report->workers[j].seed : 1U;
			if (!stats->seed || stats->seed == previous) {
				++stats->seed;
				j = UINT_MAX;
			}
		}
		stats->budget_evaluations =
			options->evaluations / count + (i < options->evaluations % count);
		stats->status = WORKER_RUNNING;
		SDL_asprintf(&worker->status_path, "%s/w%03u.status", directory, i);
		SDL_asprintf(&worker->log_path, "%s/w%03u.log", directory, i);
		if (!worker->status_path || !worker->log_path) {
			failed = 1;
			count = i;
			report->cores_used = count;
			break;
		}
		char seed[16], budget[32], parent[32];
		snprintf(seed, sizeof(seed), "%u", stats->seed);
		snprintf(budget, sizeof(budget), "%llu", (unsigned long long)stats->budget_evaluations);
		snprintf(parent, sizeof(parent), "%llu", (unsigned long long)current_pid());
		const char *arguments[36];
		unsigned int n = 0;
		arguments[n++] = executable;
		arguments[n++] = "--strategy";
		arguments[n++] = "legacy";
		arguments[n++] = "--data-dir";
		arguments[n++] = data_dir;
		arguments[n++] = input_path;
		if (!options->fine_tune) {
			arguments[n++] = car;
		}
		arguments[n++] = "--cores";
		arguments[n++] = "1";
		arguments[n++] = "--seconds";
		arguments[n++] = "0";
		arguments[n++] = "--seed";
		arguments[n++] = seed;
		arguments[n++] = options->powergear ? "--pg:on" : "--pg:off";
		arguments[n++] = options->collision ? "--lc:on" : "--lc:off";
		arguments[n++] = options->left_bias ? "--lcb:on" : "--lcb:off";
		arguments[n++] = options->dual_way_switching ? "--dws:on" : "--dws:off";
		arguments[n++] = options->chicane_fast_grass ? "--cfg:on" : "--cfg:off";
		if (options->owoot) {
			arguments[n++] = "--owoot";
		}
		if (options->amiga_grass) {
			arguments[n++] = "--amigagrass";
		}
		if (options->fine_tune) {
			arguments[n++] =
				options->fine_tune_gears_only ? "--fine-tune-gears-only" : "--fine-tune";
		}
		if (options->evaluations) {
			arguments[n++] = "--evaluations";
			arguments[n++] = budget;
		}
		arguments[n++] = "--worker-status";
		arguments[n++] = worker->status_path;
		arguments[n++] = "--worker-stop";
		arguments[n++] = stop_path;
		arguments[n++] = "--worker-parent";
		arguments[n++] = parent;
		arguments[n] = NULL;
		if (start_worker(worker, arguments, job)) {
			fprintf(stderr, "genghost: cannot start worker %u: %s\n", i, SDL_GetError());
			stats->status = 1;
			failed = 1;
			count = i;
			report->cores_used = count;
			break;
		}
	}
	for (;;) {
		unsigned int active = 0;
		int improved = 0;
		uint64_t now = SDL_GetTicksNS();
		if (!stop_started &&
			(failed || genghost_stop_requested() ||
			 (options->seconds > 0.0 && (now - started) / 1000000000.0 >= options->seconds))) {
			stop_started = now;
			if (write_bytes(stop_path, "stop\n", 5U)) {
				failed = 1;
			}
		}
		for (unsigned int i = 0; i < count; ++i) {
			struct worker *worker = &workers[i];
			struct GENGHOST_WORKER_STATS *stats = &report->workers[i];
			if (!worker->started || worker->done) {
				continue;
			}
			/* Reap first, then read: a final publication always precedes exit. */
			int done = poll_worker(worker);
			int updated = read_worker(worker, stats);
			if (updated < 0) {
				fprintf(stderr, "genghost: invalid status snapshot from worker %u\n", i);
				failed = 1;
				worker->failed = 1;
			}
			if (updated > 0 && worker->latest.race.finished &&
				(!result->race.finished ||
				 worker->latest.race.total_frames < result->race.total_frames ||
				 (worker->latest.race.total_frames == result->race.total_frames &&
				  i < report->winner_worker))) {
				struct GENGHOST_RESULT verified;
				if (!worker->latest.length ||
					genghost_sim_evaluate(worker->latest.inputs, worker->latest.length,
										  &verified) ||
					!verified.finished ||
					verified.total_frames != worker->latest.race.total_frames ||
					verified.finish_frame != worker->latest.race.finish_frame ||
					verified.penalty_frames != worker->latest.race.penalty_frames) {
					fprintf(stderr, "genghost: worker %u replay failed canonical validation\n", i);
					failed = 1;
					worker->failed = 1;
				} else {
					memcpy(result->inputs, worker->latest.inputs, worker->latest.length);
					result->length = worker->latest.length;
					result->race = verified;
					report->winner_worker = i;
					report->winner_seed = stats->seed;
					improved = 1;
				}
			}
			if (done && (worker->failed || (worker->exit_status != 0 && worker->exit_status != 2) ||
						 !worker->sequence || stats->status != worker->exit_status)) {
				stats->status = 1;
				show_worker_error(i, worker);
				failed = 1;
			}
			if (!done && stop_started && now - stop_started >= STOP_GRACE_NS) {
				force_worker(worker);
				stats->status = 1;
				fprintf(stderr, "genghost: worker %u exceeded shutdown grace period\n", i);
				failed = 1;
			}
			active += !worker->done;
		}
		aggregate(result, workers, count, started);
		if (options->fine_tune) {
			result->seed_finished = 1;
			result->seed_total_frames = baseline_total_frames;
		}
		if (result->race.finished && (improved || !active || now >= next_progress)) {
			if (save(result, save_context)) {
				failed = 1;
			}
		}
		if (now >= next_progress) {
			fprintf(stderr, "parallel progress: %llu candidates, %u/%u workers active, %.2fs\n",
					(unsigned long long)result->evaluations, active, count,
					result->elapsed_seconds);
			next_progress = now + 5000000000ULL;
		}
		if (!active) {
			break;
		}
		SDL_Delay(20U);
	}
cleanup:
	for (unsigned int i = 0; i < allocated; ++i) {
		struct worker *worker = &workers[i];
		force_worker(worker);
#ifndef __ANDROID__
		if (worker->process) {
			SDL_DestroyProcess(worker->process);
		}
#endif
		if (worker->status_path) {
			remove(worker->status_path);
			char *temporary = NULL;
			SDL_asprintf(&temporary, "%s.tmp", worker->status_path);
			if (temporary) {
				remove(temporary);
			}
			SDL_free(temporary);
		}
		if (worker->log_path) {
			remove(worker->log_path);
		}
		SDL_free(worker->status_path);
		SDL_free(worker->log_path);
	}
#ifdef _WIN32
	if (job) {
		CloseHandle((HANDLE)job);
	}
#endif
	if (input_path) {
		remove(input_path);
	}
	if (stop_path) {
		remove(stop_path);
	}
	if (directory_created) {
		SDL_RemovePath(directory);
	}
	SDL_free(input_path);
	SDL_free(stop_path);
	SDL_free(directory);
	free(workers);
	return failed ? 1 : result->race.finished ? 0 : 2;
}

#define COOPERATIVE_MAX_RECORD (1024U * 1024U)
#define COOPERATIVE_PACKET_RECORDS 32U

struct cooperative_header {
	char magic[8];
	uint64_t sequence;
	uint64_t size;
	int status;
};

struct cooperative_worker {
	struct worker process;
	char *request_path;
	uint64_t sequence;
	size_t task_index, task_count, capacity;
	void *requests, *replies;
	int busy;
};

struct GENGHOST_COOPERATIVE_POOL {
	struct cooperative_worker *workers;
	unsigned int count;
	char *directory, *input_path, *stop_path;
	void *job;
	uint64_t started, sequence;
	size_t request_size, reply_size;
	const struct GENGHOST_OPTIONS *options;
	genghost_cooperative_execute execute;
	void *execute_context;
	struct GENGHOST_PARALLEL_REPORT *report;
	int failed, directory_created;
};

static int cooperative_publish(const char *path, uint64_t sequence, const void *bytes, size_t size,
							   int status)
{
	struct cooperative_header header = {"GGCOOP2", sequence, size, status};
	char *temporary = NULL;
	SDL_asprintf(&temporary, "%s.tmp", path);
	if (!temporary) {
		return -1;
	}
	FILE *file = fopen(temporary, "wb");
	int failed = !file;
	const char *operation = "open temporary record";
	if (file) {
		operation = "write temporary record";
		failed = fwrite(&header, 1, sizeof(header), file) != sizeof(header) ||
				 fwrite(bytes, 1, size, file) != size;
		if (fclose(file)) {
			failed = 1;
			operation = "close temporary record";
		}
	}
	if (!failed) {
		operation = "replace record";
		failed = 1;
		for (unsigned int attempt = 0; attempt < 20U; ++attempt) {
			if (SDL_RenamePath(temporary, path)) {
				failed = 0;
				break;
			}
			SDL_Delay(2U);
		}
	}
	if (failed) {
		fprintf(
			stderr, "genghost: cooperative IPC cannot %s %s (sequence %llu, %zu bytes): %s; %s\n",
			operation, path, (unsigned long long)sequence, size, strerror(errno), SDL_GetError());
		remove(temporary);
	}
	SDL_free(temporary);
	return failed ? -1 : 0;
}

static FILE *cooperative_open_read(const char *path)
{
#ifdef _WIN32
	/* CRT fopen does not share deletion. A polling reader can therefore block
	 * MoveFileEx from replacing an old packet indefinitely, even though every
	 * individual read is short. Allow replacement while our handle continues
	 * reading the complete old file, matching POSIX rename semantics. */
	WCHAR *wide = (WCHAR *)SDL_iconv_string("UTF-16LE", "UTF-8", path, strlen(path) + 1U);
	if (!wide) {
		errno = ENOMEM;
		return NULL;
	}
	HANDLE handle =
		CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
					NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	DWORD error = GetLastError();
	SDL_free(wide);
	if (handle == INVALID_HANDLE_VALUE) {
		errno = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND	   ? ENOENT
				: error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION ? EACCES
																				   : EIO;
		return NULL;
	}
	int descriptor = _open_osfhandle((intptr_t)handle, _O_RDONLY | _O_BINARY);
	if (descriptor < 0) {
		CloseHandle(handle);
		return NULL;
	}
	FILE *file = _fdopen(descriptor, "rb");
	if (!file) {
		_close(descriptor);
	}
	return file;
#else
	return fopen(path, "rb");
#endif
}

static int cooperative_consume(const char *path)
{
	/* Each direction has exactly one packet in flight. The consumer removes
	 * it before acknowledging it through the other direction, so the next
	 * publication targets an empty inbox instead of replacing a polled file.
	 * This also avoids Windows/Wine replacement denial on busy filesystems. */
	for (unsigned int attempt = 0U; attempt < 20U; ++attempt) {
		if (!remove(path) || errno == ENOENT) {
			return 0;
		}
		SDL_Delay(2U);
	}
	fprintf(stderr, "genghost: cooperative IPC cannot consume record %s: %s\n", path,
			strerror(errno));
	return -1;
}

/* Consume only the expected response. Stale sequences are ignored; malformed
 * current records and unexpected future records are errors. */
static int cooperative_read(const char *path, uint64_t sequence, void *bytes, size_t size,
							int *status)
{
	FILE *file = cooperative_open_read(path);
	if (!file) {
#ifdef _WIN32
		/* Windows can transiently report a sharing violation while another
		 * process replaces this private record with MoveFileEx. */
		if (errno == EACCES) {
			return 0;
		}
#endif
		if (errno == ENOENT) {
			return 0;
		}
		fprintf(stderr, "genghost: cooperative IPC cannot read reply %s: %s\n", path,
				strerror(errno));
		return -1;
	}
	struct cooperative_header header = {0};
	size_t header_bytes = fread(&header, 1, sizeof(header), file);
	int valid = header_bytes == sizeof(header) && !memcmp(header.magic, "GGCOOP2", 8U) &&
				header.size <= COOPERATIVE_MAX_RECORD && header.status >= 0 && header.status <= 2;
	int result = -1;
	if (valid && header.sequence < sequence) {
		result = 0;
	} else if (valid && header.sequence == sequence && header.size == size &&
			   fread(bytes, 1, size, file) == size && fgetc(file) == EOF) {
		*status = header.status;
		result = 1;
	}
	fclose(file);
	if (result > 0 && cooperative_consume(path)) {
		return -1;
	}
	if (result < 0) {
		fprintf(stderr,
				"genghost: cooperative IPC invalid reply %s (header %zu/%zu, sequence %llu/%llu, "
				"size %llu/%zu, status %d): %s\n",
				path, header_bytes, sizeof(header), (unsigned long long)header.sequence,
				(unsigned long long)sequence, (unsigned long long)header.size, size, header.status,
				strerror(errno));
	}
	return result;
}

static int cooperative_parent_stopped(struct GENGHOST_WORKER_CONTROL *control)
{
#ifdef _WIN32
	if (WaitForSingleObject((HANDLE)control->parent_handle, 0) != WAIT_TIMEOUT) {
		genghost_interrupted = 1;
	}
#else
	if ((uint64_t)getppid() != control->parent_pid) {
		genghost_interrupted = 1;
	}
#endif
	FILE *stop = fopen(control->stop_path, "rb");
	if (stop) {
		fclose(stop);
		genghost_interrupted = 1;
	}
	return genghost_stop_requested();
}

static size_t cooperative_packet_limit(size_t request_size, size_t reply_size)
{
	size_t record_size = request_size > reply_size ? request_size : reply_size;
	size_t limit = COOPERATIVE_MAX_RECORD / record_size;
	return limit < COOPERATIVE_PACKET_RECORDS ? limit : COOPERATIVE_PACKET_RECORDS;
}

static int cooperative_reserve(void **requests, void **replies, size_t *capacity, size_t count,
							   size_t request_size, size_t reply_size)
{
	if (count <= *capacity) {
		return 0;
	}
	void *buffer = realloc(*requests, count * request_size);
	if (!buffer) {
		return -1;
	}
	*requests = buffer;
	buffer = realloc(*replies, count * reply_size);
	if (!buffer) {
		return -1;
	}
	*replies = buffer;
	*capacity = count;
	return 0;
}

int genghost_cooperative_worker(struct GENGHOST_WORKER_CONTROL *control, const char *request_path,
								size_t request_size, size_t reply_size,
								genghost_cooperative_execute execute, void *execute_context)
{
	if (!control || !request_path || !execute || !request_size || !reply_size ||
		request_size > COOPERATIVE_MAX_RECORD || reply_size > COOPERATIVE_MAX_RECORD) {
		return 1;
	}
	void *requests = NULL, *replies = NULL;
	size_t capacity = 0U;
	int failed = 0;
	uint64_t sequence = 0;
	while (!failed && !cooperative_parent_stopped(control)) {
		/* One packet contains this worker's deterministic subsequence of a
		 * complete frontier batch. Cached short macros no longer wait for a
		 * filesystem round trip between every individual expansion. */
		FILE *file = cooperative_open_read(request_path);
		if (!file) {
			if (errno != ENOENT
#ifdef _WIN32
				&& errno != EACCES
#endif
			) {
				fprintf(stderr, "genghost: cooperative IPC cannot read request %s: %s\n",
						request_path, strerror(errno));
				failed = 1;
				break;
			}
			SDL_Delay(1U);
			continue;
		}
		struct cooperative_header header = {0};
		size_t header_bytes = fread(&header, 1, sizeof(header), file);
		int valid =
			header_bytes == sizeof(header) && !memcmp(header.magic, "GGCOOP2", 8U) && header.size &&
			header.size <= COOPERATIVE_MAX_RECORD && !(header.size % request_size) &&
			header.size / request_size <= cooperative_packet_limit(request_size, reply_size) &&
			!header.status;
		size_t count = valid ? (size_t)(header.size / request_size) : 0U;
		size_t records_read = 0U;
		int extra = EOF;
		if (valid && header.sequence > sequence) {
			valid = !cooperative_reserve(&requests, &replies, &capacity, count, request_size,
										 reply_size);
			if (valid) {
				records_read = fread(requests, request_size, count, file);
				extra = fgetc(file);
				valid = records_read == count && extra == EOF;
			}
		}
		fclose(file);
		if (!valid) {
			fprintf(stderr,
					"genghost: cooperative IPC invalid request %s (header %zu/%zu, sequence %llu, "
					"size %llu, status %d, records %zu/%zu of %zu, extra %d): %s\n",
					request_path, header_bytes, sizeof(header), (unsigned long long)header.sequence,
					(unsigned long long)header.size, header.status, records_read, count,
					request_size, extra, strerror(errno));
			failed = 1;
			break;
		}
		if (header.sequence <= sequence) {
			SDL_Delay(1U);
			continue;
		}
		if (cooperative_consume(request_path)) {
			failed = 1;
			break;
		}
		sequence = header.sequence;
		memset(replies, 0, count * reply_size);
		int status = 0;
		for (size_t i = 0; i < count; ++i) {
			if (cooperative_parent_stopped(control)) {
				status = 2;
				break;
			}
			status = execute((unsigned char *)requests + i * request_size,
							 (unsigned char *)replies + i * reply_size, execute_context);
			if (status < 0 || status > 2) {
				status = 1;
			}
			if (status) {
				fprintf(stderr,
						"genghost: cooperative executor returned %d for record %zu/%zu, sequence "
						"%llu\n",
						status, i + 1U, count, (unsigned long long)sequence);
				break;
			}
		}
		if (cooperative_publish(control->status_path, sequence, replies, count * reply_size,
								status)) {
			failed = 1;
			break;
		}
		if (status) {
			failed = status == 1;
			break;
		}
	}
	free(requests);
	free(replies);
	return failed ? 1 : 0;
}

static int cooperative_stopped(const struct GENGHOST_COOPERATIVE_POOL *pool)
{
	return genghost_stop_requested() ||
		   (pool->options->seconds > 0.0 &&
			(SDL_GetTicksNS() - pool->started) / 1000000000.0 >= pool->options->seconds);
}

struct GENGHOST_COOPERATIVE_POOL *
genghost_cooperative_open(const char *executable, const char *output, const char *data_dir,
						  const unsigned char *source, size_t source_length, const char *car,
						  int transmission, unsigned int requested_cores,
						  const struct GENGHOST_OPTIONS *options, size_t request_size,
						  size_t reply_size, genghost_cooperative_execute execute,
						  void *execute_context, struct GENGHOST_PARALLEL_REPORT *report)
{
	/* Kept for native callers; generated workers always use manual transmission. */
	(void)transmission;
	if (!options || !execute || !report || !request_size || !reply_size ||
		request_size > COOPERATIVE_MAX_RECORD || reply_size > COOPERATIVE_MAX_RECORD ||
		requested_cores > GENGHOST_MAX_CORES) {
		return NULL;
	}
	struct GENGHOST_COOPERATIVE_POOL *pool = calloc(1U, sizeof(*pool));
	if (!pool) {
		return NULL;
	}
	pool->count = requested_cores ? requested_cores : genghost_available_cores();
	if (options->evaluations && options->evaluations < pool->count) {
		pool->count = (unsigned int)options->evaluations;
	}
	pool->started = SDL_GetTicksNS();
	pool->options = options;
	pool->request_size = request_size;
	pool->reply_size = reply_size;
	pool->execute = execute;
	pool->execute_context = execute_context;
	pool->report = report;
	memset(report, 0, sizeof(*report));
	report->cores_requested = requested_cores;
	report->cores_used = pool->count;
	report->winner_worker = UINT_MAX;
	report->winner_seed = options->seed;
	for (unsigned int i = 0; i < pool->count; ++i) {
		report->workers[i].seed = options->seed;
	}
	if (pool->count == 1U) {
		return pool;
	}
	if (!executable || !output || !data_dir || !source || !car) {
		goto fail;
	}
	pool->workers = calloc(pool->count, sizeof(*pool->workers));
	SDL_asprintf(&pool->directory, "%s.coop-%llu-%llu", output, (unsigned long long)current_pid(),
				 (unsigned long long)pool->started);
	if (!pool->workers || !pool->directory || !SDL_CreateDirectory(pool->directory)) {
		goto fail;
	}
	pool->directory_created = 1;
	SDL_asprintf(&pool->input_path, "%s/source.%s", pool->directory,
				 source_length == REPLAY_TRACK_SIZE ? "trk" : "rpl");
	SDL_asprintf(&pool->stop_path, "%s/stop", pool->directory);
	if (!pool->input_path || !pool->stop_path ||
		write_bytes(pool->input_path, source, source_length)) {
		goto fail;
	}
#ifdef _WIN32
	pool->job = CreateJobObjectW(NULL, NULL);
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	if (!pool->job || !SetInformationJobObject((HANDLE)pool->job, JobObjectExtendedLimitInformation,
											   &limits, sizeof(limits))) {
		goto fail;
	}
#endif
	fprintf(stderr, "cooperative search: %u workers; shared deterministic frontier\n", pool->count);
	for (unsigned int i = 0; i < pool->count; ++i) {
		/* Startup is charged to the same wall-clock deadline as expansion. */
		if (cooperative_stopped(pool)) {
			pool->count = i;
			report->cores_used = i;
			break;
		}
		struct cooperative_worker *worker = &pool->workers[i];
		SDL_asprintf(&worker->request_path, "%s/w%03u.task", pool->directory, i);
		SDL_asprintf(&worker->process.status_path, "%s/w%03u.reply", pool->directory, i);
		SDL_asprintf(&worker->process.log_path, "%s/w%03u.log", pool->directory, i);
		if (!worker->request_path || !worker->process.status_path || !worker->process.log_path) {
			goto fail;
		}
		char seed[16], parent[32];
		snprintf(seed, sizeof(seed), "%u", options->seed);
		snprintf(parent, sizeof(parent), "%llu", (unsigned long long)current_pid());
		const char *arguments[36];
		unsigned int n = 0;
		arguments[n++] = executable;
		arguments[n++] = "--strategy";
		arguments[n++] = "state";
		arguments[n++] = "--data-dir";
		arguments[n++] = data_dir;
		arguments[n++] = pool->input_path;
		if (!options->fine_tune) {
			arguments[n++] = car;
		}
		arguments[n++] = "--cores";
		arguments[n++] = "1";
		arguments[n++] = "--seconds";
		arguments[n++] = "0";
		arguments[n++] = "--seed";
		arguments[n++] = seed;
		arguments[n++] = options->powergear ? "--pg:on" : "--pg:off";
		arguments[n++] = options->collision ? "--lc:on" : "--lc:off";
		arguments[n++] = options->left_bias ? "--lcb:on" : "--lcb:off";
		arguments[n++] = options->dual_way_switching ? "--dws:on" : "--dws:off";
		arguments[n++] = options->chicane_fast_grass ? "--cfg:on" : "--cfg:off";
		if (options->owoot) {
			arguments[n++] = "--owoot";
		}
		if (options->amiga_grass) {
			arguments[n++] = "--amigagrass";
		}
		if (options->fine_tune) {
			arguments[n++] =
				options->fine_tune_gears_only ? "--fine-tune-gears-only" : "--fine-tune";
		}
		arguments[n++] = "--worker-status";
		arguments[n++] = worker->process.status_path;
		arguments[n++] = "--worker-stop";
		arguments[n++] = pool->stop_path;
		arguments[n++] = "--worker-parent";
		arguments[n++] = parent;
		arguments[n++] = "--cooperative-worker";
		arguments[n++] = worker->request_path;
		arguments[n] = NULL;
		if (start_worker(&worker->process, arguments, pool->job)) {
			fprintf(stderr, "genghost: cannot start cooperative worker %u: %s\n", i,
					SDL_GetError());
			goto fail;
		}
	}
	return pool;
fail:
	pool->failed = 1;
	genghost_cooperative_close(pool);
	return NULL;
}

static int cooperative_dispatch(struct GENGHOST_COOPERATIVE_POOL *pool, unsigned int index,
								size_t task_index, size_t count, const void *requests)
{
	struct cooperative_worker *worker = &pool->workers[index];
	worker->task_index = task_index;
	worker->task_count = 1U + (count - 1U - task_index) / pool->count;
	size_t limit = cooperative_packet_limit(pool->request_size, pool->reply_size);
	if (worker->task_count > limit) {
		worker->task_count = limit;
	}
	if (cooperative_reserve(&worker->requests, &worker->replies, &worker->capacity,
							worker->task_count, pool->request_size, pool->reply_size)) {
		return -1;
	}
	worker->sequence = ++pool->sequence;
	worker->busy = 1;
	for (size_t i = 0; i < worker->task_count; ++i) {
		memcpy((unsigned char *)worker->requests + i * pool->request_size,
			   (const unsigned char *)requests +
				   (task_index + i * pool->count) * pool->request_size,
			   pool->request_size);
	}
	if (cooperative_publish(worker->request_path, worker->sequence, worker->requests,
							worker->task_count * pool->request_size, 0)) {
		return -1;
	}
	return 0;
}

int genghost_cooperative_batch(struct GENGHOST_COOPERATIVE_POOL *pool, const void *requests,
							   void *replies, size_t count)
{
	if (!pool || pool->failed || (count && (!requests || !replies)) ||
		count > SIZE_MAX / pool->request_size || count > SIZE_MAX / pool->reply_size) {
		return 1;
	}
	if (!count) {
		return 0;
	}
	if (cooperative_stopped(pool)) {
		return 2;
	}
	if (pool->count == 1U) {
		for (size_t i = 0; i < count; ++i) {
			if (cooperative_stopped(pool)) {
				return 2;
			}
			void *reply = (unsigned char *)replies + i * pool->reply_size;
			memset(reply, 0, pool->reply_size);
			int status = pool->execute((const unsigned char *)requests + i * pool->request_size,
									   reply, pool->execute_context);
			if (status) {
				pool->failed = status != 2;
				pool->report->workers[0].status = status;
				return pool->failed ? 1 : 2;
			}
		}
		pool->report->workers[0].evaluations += count;
		return 0;
	}
	for (unsigned int i = 0; i < pool->count && i < count; ++i) {
		if (cooperative_dispatch(pool, i, i, count, requests)) {
			pool->failed = 1;
			return 1;
		}
	}
	size_t completed = 0;
	while (completed < count) {
		if (cooperative_stopped(pool)) {
			return 2;
		}
		int advanced = 0;
		for (unsigned int i = 0; i < pool->count; ++i) {
			struct cooperative_worker *worker = &pool->workers[i];
			if (!worker->busy) {
				continue;
			}
			int status = 0;
			int received =
				cooperative_read(worker->process.status_path, worker->sequence, worker->replies,
								 worker->task_count * pool->reply_size, &status);
			if (received < 0 || (received > 0 && status)) {
				if (received > 0 && status == 2) {
					return 2;
				}
				fprintf(stderr, "genghost: invalid/failed cooperative reply from worker %u\n", i);
				show_worker_log(&worker->process);
				pool->report->workers[i].status = 1;
				pool->failed = 1;
				return 1;
			}
			if (!received) {
				if (poll_worker(&worker->process)) {
					show_worker_error(i, &worker->process);
					pool->report->workers[i].status = 1;
					pool->failed = 1;
					return 1;
				}
				continue;
			}
			for (size_t n = 0; n < worker->task_count; ++n) {
				memcpy((unsigned char *)replies +
						   (worker->task_index + n * pool->count) * pool->reply_size,
					   (unsigned char *)worker->replies + n * pool->reply_size, pool->reply_size);
			}
			completed += worker->task_count;
			worker->busy = 0;
			advanced = 1;
			/* Fixed striding keeps task ownership deterministic as well as the
			 * frontier, without serializing the actual simulator execution. */
			size_t remaining = 1U + (count - 1U - worker->task_index) / pool->count;
			if (worker->task_count < remaining) {
				size_t next = worker->task_index + worker->task_count * pool->count;
				if (cooperative_dispatch(pool, i, next, count, requests)) {
					pool->failed = 1;
					return 1;
				}
			}
		}
		if (!advanced) {
			SDL_Delay(1U);
		}
	}
	/* Only complete batches enter the planner or its metrics. Incomplete work
	 * at cancellation is deliberately excluded from both dispatch and tick
	 * counters, rather than presenting a partly consumed frontier. */
	for (unsigned int i = 0; i < pool->count; ++i) {
		pool->report->workers[i].evaluations += count / pool->count + (i < count % pool->count);
	}
	return 0;
}

static void cooperative_remove_record(const char *path)
{
	if (!path) {
		return;
	}
	remove(path);
	char *temporary = NULL;
	SDL_asprintf(&temporary, "%s.tmp", path);
	if (temporary) {
		remove(temporary);
	}
	SDL_free(temporary);
}

void genghost_cooperative_close(struct GENGHOST_COOPERATIVE_POOL *pool)
{
	if (!pool) {
		return;
	}
	if (pool->stop_path) {
		write_bytes(pool->stop_path, "stop\n", 5U);
	}
	uint64_t deadline = SDL_GetTicksNS() + STOP_GRACE_NS;
	if (pool->workers) {
		for (;;) {
			unsigned int active = 0;
			for (unsigned int i = 0; i < pool->count; ++i) {
				struct worker *worker = &pool->workers[i].process;
				if (worker->started && !poll_worker(worker)) {
					++active;
				}
			}
			if (!active || SDL_GetTicksNS() >= deadline) {
				break;
			}
			SDL_Delay(5U);
		}
		for (unsigned int i = 0; i < pool->count; ++i) {
			struct cooperative_worker *worker = &pool->workers[i];
			force_worker(&worker->process);
#ifndef __ANDROID__
			if (worker->process.process) {
				SDL_DestroyProcess(worker->process.process);
			}
#endif
			cooperative_remove_record(worker->request_path);
			cooperative_remove_record(worker->process.status_path);
			if (worker->process.log_path) {
				remove(worker->process.log_path);
			}
			SDL_free(worker->request_path);
			SDL_free(worker->process.status_path);
			SDL_free(worker->process.log_path);
			free(worker->requests);
			free(worker->replies);
		}
	}
#ifdef _WIN32
	if (pool->job) {
		CloseHandle((HANDLE)pool->job);
	}
#endif
	if (pool->input_path) {
		remove(pool->input_path);
	}
	if (pool->stop_path) {
		remove(pool->stop_path);
	}
	if (pool->directory_created) {
		SDL_RemovePath(pool->directory);
	}
	SDL_free(pool->input_path);
	SDL_free(pool->stop_path);
	SDL_free(pool->directory);
	free(pool->workers);
	free(pool);
}
