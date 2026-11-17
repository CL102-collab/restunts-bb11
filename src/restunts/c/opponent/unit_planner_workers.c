#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "unit_planner_internal.h"

#if !defined(__DJGPP__) && (!defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)) &&       \
	!defined(RESTUNTS_UNIT_SYNC_ONLY)
#include <SDL3/SDL_cpuinfo.h>
#if defined(__linux__) || defined(__ANDROID__)
#include <sched.h>
#include <stdio.h>
#endif

legacy_u32 unit_planner_available_workers(void)
{
	legacy_s32 detected = SDL_GetNumLogicalCPUCores();
	legacy_u32 count = detected > 0 ? (legacy_u32)detected : 1U;
#if defined(__linux__) || defined(__ANDROID__)
	cpu_set_t affinity;
	if (!sched_getaffinity(0, sizeof(affinity), &affinity)) {
		legacy_u32 allowed = (legacy_u32)CPU_COUNT(&affinity);
		if (allowed && allowed < count) {
			count = allowed;
		}
	}
	FILE *file = fopen("/sys/fs/cgroup/cpu.max", "r");
	legacy_u64 quota = 0U, period = 0U;
	if (file) {
		if (fscanf(file, "%" LEGACY_PRIu64 " %" LEGACY_PRIu64, &quota, &period) != 2) {
			quota = period = 0U;
		}
		fclose(file);
	}
	if (quota && period) {
		legacy_u64 available = quota / period + (quota % period != 0U);
		if (available < count) {
			count = (legacy_u32)available;
		}
	}
#endif
	return count;
}

#else
legacy_u32 unit_planner_available_workers(void)
{
	return 1U;
}
#endif
