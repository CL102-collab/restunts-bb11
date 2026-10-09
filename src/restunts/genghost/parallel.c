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
	return genghost_interrupted || control->failed;
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

static void show_worker_error(unsigned int index, const struct worker *worker)
{
	fprintf(stderr, "genghost: worker %u failed (exit %d); retained any verified best replay.\n",
			index, worker->exit_status);
	FILE *file = fopen(worker->log_path, "rb");
	if (file) {
		char log[2049];
		size_t size = fread(log, 1, sizeof(log) - 1U, file);
		log[size] = 0;
		fputs(log, stderr);
		fclose(file);
	}
}

int genghost_parallel_search(const char *executable, const char *output, const char *data_dir,
							 const unsigned char *source, size_t source_length, const char *car,
							 int transmission, unsigned int requested_cores,
							 const struct GENGHOST_OPTIONS *options,
							 struct GENGHOST_SEARCH_RESULT *result,
							 struct GENGHOST_PARALLEL_REPORT *report, genghost_save_function save,
							 void *save_context)
{
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
		if (genghost_interrupted ||
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
		const char *arguments[32];
		unsigned int n = 0;
		arguments[n++] = executable;
		arguments[n++] = "--data-dir";
		arguments[n++] = data_dir;
		arguments[n++] = input_path;
		arguments[n++] = car;
		arguments[n++] = "--cores";
		arguments[n++] = "1";
		arguments[n++] = "--seconds";
		arguments[n++] = "0";
		arguments[n++] = "--seed";
		arguments[n++] = seed;
		arguments[n++] = "--transmission";
		arguments[n++] = transmission ? "auto" : "manual";
		arguments[n++] = options->powergear ? "--pg:on" : "--pg:off";
		arguments[n++] = options->collision ? "--lc:on" : "--lc:off";
		arguments[n++] = options->left_bias ? "--lcb:on" : "--lcb:off";
		if (options->owoot) {
			arguments[n++] = "--owoot";
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
			(failed || genghost_interrupted ||
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
