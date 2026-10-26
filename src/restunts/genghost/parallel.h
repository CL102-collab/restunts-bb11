#ifndef RESTUNTS_GENGHOST_PARALLEL_H
#define RESTUNTS_GENGHOST_PARALLEL_H

#include "search.h"

#define GENGHOST_MAX_CORES 256U

struct GENGHOST_WORKER_STATS {
	legacy_u32 seed;
	legacy_u64 budget_evaluations, evaluations, simulated_frames;
	legacy_uint total_frames;
	legacy_int finished, status;
	legacy_f64 elapsed_seconds;
};

struct GENGHOST_PARALLEL_REPORT {
	legacy_uint cores_requested, cores_used, winner_worker;
	legacy_u32 winner_seed;
	struct GENGHOST_WORKER_STATS workers[GENGHOST_MAX_CORES];
};

struct GENGHOST_WORKER_CONTROL {
	const legacy_char *status_path, *stop_path;
	legacy_u64 parent_pid, sequence, next_poll, next_publish;
	void *parent_handle;
	legacy_int failed;
};

/* A persistent pool for cooperative search. Requests and replies are private
 * pointer-free records shared only between the exact same executable. The
 * coordinator owns search priorities, deduplication and accepted checkpoints;
 * workers only evaluate one deterministic expansion at a time. The transmission
 * argument is retained for native callers; CLI workers always use manual. */
struct GENGHOST_COOPERATIVE_POOL;
typedef legacy_int (*genghost_cooperative_execute)(const void *request, void *reply, void *context);

struct GENGHOST_COOPERATIVE_POOL *genghost_cooperative_open(
	const legacy_char *executable, const legacy_char *output, const legacy_char *data_dir,
	const legacy_u8 *source, size_t source_length, const legacy_char *car, legacy_int transmission,
	legacy_uint requested_cores, const struct GENGHOST_OPTIONS *options, size_t request_size,
	size_t reply_size, genghost_cooperative_execute execute, void *execute_context,
	struct GENGHOST_PARALLEL_REPORT *report);
/* Workers receive bounded packets of their fixed request subsequence, avoiding
 * a coordinator round trip for every short macro. Results correspond to request
 * order independently of worker scheduling.
 * Returns 0 for a complete batch, 1 on failure, 2 on cancellation/deadline.
 * Callers must not consume an incomplete batch. */
legacy_int genghost_cooperative_batch(struct GENGHOST_COOPERATIVE_POOL *pool, const void *requests,
									  void *replies, size_t count);
void genghost_cooperative_close(struct GENGHOST_COOPERATIVE_POOL *pool);
/* Main calls this after initializing physics and the planner's route context;
 * worker_control has already been attached to the launching parent. */
legacy_int genghost_cooperative_worker(struct GENGHOST_WORKER_CONTROL *worker_control,
									   const legacy_char *request_path, size_t request_size,
									   size_t reply_size, genghost_cooperative_execute execute,
									   void *execute_context);

legacy_char *genghost_executable_path(const legacy_char *argument);
legacy_uint genghost_available_cores(void);
void genghost_parallel_install_interrupt_handler(void);
legacy_int genghost_worker_attach(struct GENGHOST_WORKER_CONTROL *control);
void genghost_worker_close(struct GENGHOST_WORKER_CONTROL *control);
legacy_int genghost_worker_progress(const struct GENGHOST_SEARCH_RESULT *result, void *opaque);
legacy_int genghost_worker_save(const struct GENGHOST_SEARCH_RESULT *result, void *opaque);
legacy_int genghost_worker_finish(struct GENGHOST_WORKER_CONTROL *control,
								  const struct GENGHOST_SEARCH_RESULT *result, legacy_int status);

/* Separate seeded search islands: the simulation is deliberately process-global.
 * Only the coordinator writes the public output, after canonical resimulation.
 * The transmission argument is retained for native callers; CLI workers always
 * use manual. */
legacy_int genghost_parallel_search(const legacy_char *executable, const legacy_char *output,
									const legacy_char *data_dir, const legacy_u8 *source,
									size_t source_length, const legacy_char *car,
									legacy_int transmission, legacy_uint requested_cores,
									const struct GENGHOST_OPTIONS *options,
									struct GENGHOST_SEARCH_RESULT *result,
									struct GENGHOST_PARALLEL_REPORT *report,
									genghost_save_function save, void *save_context);

#endif
