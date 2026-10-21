#ifndef RESTUNTS_GENGHOST_PARALLEL_H
#define RESTUNTS_GENGHOST_PARALLEL_H

#include "search.h"

#define GENGHOST_MAX_CORES 256U

struct GENGHOST_WORKER_STATS {
	uint32_t seed;
	uint64_t budget_evaluations, evaluations, simulated_frames;
	unsigned int total_frames;
	int finished, status;
	double elapsed_seconds;
};

struct GENGHOST_PARALLEL_REPORT {
	unsigned int cores_requested, cores_used, winner_worker;
	uint32_t winner_seed;
	struct GENGHOST_WORKER_STATS workers[GENGHOST_MAX_CORES];
};

struct GENGHOST_WORKER_CONTROL {
	const char *status_path, *stop_path;
	uint64_t parent_pid, sequence, next_poll, next_publish;
	void *parent_handle;
	int failed;
};

/* A persistent pool for cooperative search. Requests and replies are private
 * pointer-free records shared only between the exact same executable. The
 * coordinator owns search priorities, deduplication and accepted checkpoints;
 * workers only evaluate one deterministic expansion at a time. The transmission
 * argument is retained for native callers; CLI workers always use manual. */
struct GENGHOST_COOPERATIVE_POOL;
typedef int (*genghost_cooperative_execute)(const void *request, void *reply, void *context);

struct GENGHOST_COOPERATIVE_POOL *
genghost_cooperative_open(const char *executable, const char *output, const char *data_dir,
						  const unsigned char *source, size_t source_length, const char *car,
						  int transmission, unsigned int requested_cores,
						  const struct GENGHOST_OPTIONS *options, size_t request_size,
						  size_t reply_size, genghost_cooperative_execute execute,
						  void *execute_context, struct GENGHOST_PARALLEL_REPORT *report);
/* Workers receive bounded packets of their fixed request subsequence, avoiding
 * a coordinator round trip for every short macro. Results correspond to request
 * order independently of worker scheduling.
 * Returns 0 for a complete batch, 1 on failure, 2 on cancellation/deadline.
 * Callers must not consume an incomplete batch. */
int genghost_cooperative_batch(struct GENGHOST_COOPERATIVE_POOL *pool, const void *requests,
							   void *replies, size_t count);
void genghost_cooperative_close(struct GENGHOST_COOPERATIVE_POOL *pool);
/* Main calls this after initializing physics and the planner's route context;
 * worker_control has already been attached to the launching parent. */
int genghost_cooperative_worker(struct GENGHOST_WORKER_CONTROL *worker_control,
								const char *request_path, size_t request_size, size_t reply_size,
								genghost_cooperative_execute execute, void *execute_context);

char *genghost_executable_path(const char *argument);
unsigned int genghost_available_cores(void);
void genghost_parallel_install_interrupt_handler(void);
int genghost_worker_attach(struct GENGHOST_WORKER_CONTROL *control);
void genghost_worker_close(struct GENGHOST_WORKER_CONTROL *control);
int genghost_worker_progress(const struct GENGHOST_SEARCH_RESULT *result, void *opaque);
int genghost_worker_save(const struct GENGHOST_SEARCH_RESULT *result, void *opaque);
int genghost_worker_finish(struct GENGHOST_WORKER_CONTROL *control,
						   const struct GENGHOST_SEARCH_RESULT *result, int status);

/* Separate seeded search islands: the simulation is deliberately process-global.
 * Only the coordinator writes the public output, after canonical resimulation.
 * The transmission argument is retained for native callers; CLI workers always
 * use manual. */
int genghost_parallel_search(const char *executable, const char *output, const char *data_dir,
							 const unsigned char *source, size_t source_length, const char *car,
							 int transmission, unsigned int requested_cores,
							 const struct GENGHOST_OPTIONS *options,
							 struct GENGHOST_SEARCH_RESULT *result,
							 struct GENGHOST_PARALLEL_REPORT *report, genghost_save_function save,
							 void *save_context);

#endif
