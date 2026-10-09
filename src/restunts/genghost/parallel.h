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
 * Only the coordinator writes the public output, after canonical resimulation. */
int genghost_parallel_search(const char *executable, const char *output, const char *data_dir,
							 const unsigned char *source, size_t source_length, const char *car,
							 int transmission, unsigned int requested_cores,
							 const struct GENGHOST_OPTIONS *options,
							 struct GENGHOST_SEARCH_RESULT *result,
							 struct GENGHOST_PARALLEL_REPORT *report, genghost_save_function save,
							 void *save_context);

#endif
