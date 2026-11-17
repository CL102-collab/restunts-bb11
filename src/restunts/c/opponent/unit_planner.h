#ifndef RESTUNTS_UNIT_PLANNER_H
#define RESTUNTS_UNIT_PLANNER_H

#include "../legacy.h"

/* Planner coordinates are full-width world units (car_position / 64), speed
 * is mph, yaw is radians, and accelerations are mph per second. All geometry
 * and car parameters are copied: worker threads never read engine globals. */
enum UNIT_ROUTE_STUNT {
	UNIT_STUNT_NONE = 0,
	UNIT_STUNT_PROTECTED = 1,
	UNIT_STUNT_BARREL = 2,
	UNIT_STUNT_RAMP = 3,
	UNIT_STUNT_MAX = UNIT_STUNT_RAMP
};

enum UNIT_ROUTE_ALIGNMENT {
	UNIT_ALIGN_NONE = 0,
	/* Keep the entire pursuit horizon centered while approaching a maneuver. */
	UNIT_ALIGN_MANEUVER = 1,
	/* Fix this gate's samples while leaving neighboring passing lines open. */
	UNIT_ALIGN_GATE = 2,
	/* Conservative centering for unrestricted live guidance. */
	UNIT_ALIGN_FALLBACK = 4,
	/* Center clearance checks unless a forecast obstruction requires passing. */
	UNIT_ALIGN_CLEARANCE = 8
};

struct UNIT_ROUTE_POINT {
	legacy_f64 x, y, z, half_width, speed_limit, speed_floor;
	legacy_s16 route_index;
	legacy_u16 route_point;
	legacy_u8 surface, shortcut_safe, height_unspecified, stunt;
	legacy_u8 banked_corner; /* Native thirty-degree bank steering assistance. */
	/* Ordinary placed bank: hold cached approach until native exit support. */
	legacy_u8 bank_support;
	/* Certify support and obstacles while retaining ordinary planning. */
	legacy_u8 clearance_only;
	legacy_u8 align_center; /* UNIT_ROUTE_ALIGNMENT flags. */
	/* Optional unrestricted-live limit; native previews and OWOOT ignore it. */
	legacy_f64 fallback_speed_limit;
};

struct UNIT_VEHICLE {
	legacy_f64 x, y, z, yaw, speed, vx, vz, half_width, half_length;
	legacy_f64 steering, steering_rate, vy;
	legacy_u8 recovering, shortcuts_forbidden, airborne;
};

struct UNIT_CAR_LIMITS {
	legacy_f64 max_speed, acceleration, braking, lateral_acceleration;
	legacy_f64 wheelbase;
	legacy_f64 half_width, half_length;
	/* Optional ratios to paved grip, indexed by CAR_SURFACE_TYPE. Zero uses
	 * conservative built-in ratios. */
	legacy_f64 surface_grip[5];
};

enum UNIT_PLAN_BRAKE {
	UNIT_PLAN_BRAKE_NONE = 0,
	UNIT_PLAN_BRAKE_COLLISION = 1,
	UNIT_PLAN_BRAKE_STOP = 2
};

struct UNIT_PLAN {
	legacy_f64 target_x, target_y, target_z, target_speed;
	legacy_u32 route_cursor, generation;
	legacy_u8 brake, valid, from_worker, overtaking;
};

struct UNIT_PLANNER_STATS {
	legacy_u64 opening_candidates, route_candidates, live_candidates, worker_accepted;
	legacy_u32 workers;
	legacy_u8 opening_ready, route_ready;
};

/* Portable learning values. Serialize fields explicitly, never raw struct bytes.
 * The transient clearance-passing permission is deliberately excluded. */
struct UNIT_PLANNER_PROFILE {
	legacy_f64 lane, apex, lookahead;
};

struct UNIT_PLANNER_LEARNING {
	legacy_f64 lane, apex, lookahead, score;
	legacy_u64 next_sample, completed_samples;
	legacy_u32 seed;
	legacy_u8 ready;
};

struct UNIT_PLANNER;

/* Snapshot the best geometric proposal and resumable deterministic search.
 * next_sample counts issued random candidates, including canceled partial work;
 * completed_samples counts those whose geometric evaluation completed.
 * With ready=0 no proposal exists; finite proposal fields are ignored on import. */
legacy_int unit_planner_export_learning(struct UNIT_PLANNER *planner,
										struct UNIT_PLANNER_LEARNING *state);
/* Main-thread only, during startup before any live request or native preview.
 * Stops bounded worker jobs, restores search/proposal, then resumes workers.
 * The caller must match geometry, car, rules and learning-format generation. */
legacy_int unit_planner_import_learning(struct UNIT_PLANNER *planner,
										const struct UNIT_PLANNER_LEARNING *state);
/* The active profile may be a separately native-validated incumbent rather than
 * the best geometric proposal. Reading it never changes either search. */
legacy_int unit_planner_export_profile(struct UNIT_PLANNER *planner,
									   struct UNIT_PLANNER_PROFILE *profile);
/* Independent native-preview branches leave the active/validated source intact.
 * An explicit profile reproduces an incumbent; an indexed candidate explores a
 * fresh deterministic profile even when the geometric best no longer improves. */
struct UNIT_PLANNER *unit_planner_clone_profile(struct UNIT_PLANNER *planner,
												const struct UNIT_PLANNER_PROFILE *profile);
struct UNIT_PLANNER *unit_planner_clone_learning(struct UNIT_PLANNER *planner,
												 const struct UNIT_PLANNER_LEARNING *state);
struct UNIT_PLANNER *unit_planner_clone_candidate(struct UNIT_PLANNER *planner, legacy_u64 sample);

/* Creation establishes an immediately usable conservative guide. Every startup
 * candidate evaluates the complete route before its profile can be published.
 * A requested worker count of zero selects all available CPUs; negative forces
 * bounded synchronous operation for tests or platforms without threads. */
struct UNIT_PLANNER *unit_planner_create(const struct UNIT_ROUTE_POINT *points, legacy_u32 count,
										 const struct UNIT_CAR_LIMITS *limits, legacy_u8 owoot,
										 legacy_s32 requested_workers);
/* Independent deterministic native pilot snapshot: copies the current profiles
 * and geometry, ignores fallback limits/alignment, resets request/candidate state,
 * and starts live mode without workers.
 * It does not stop or take ownership of the source planner. */
struct UNIT_PLANNER *unit_planner_clone_sync(struct UNIT_PLANNER *planner);
/* Branch a paused synchronous pilot at the same native snapshot. Unlike the
 * fresh clone, this preserves candidate order and recent request/publication. */
struct UNIT_PLANNER *unit_planner_clone_continuation(struct UNIT_PLANNER *planner);
/* Reuse an allocated pilot snapshot in constant work. Both planners must be
 * paused/live and descend from the same geometry clone generation. */
legacy_int unit_planner_restore_continuation(struct UNIT_PLANNER *destination,
											 struct UNIT_PLANNER *source);
/* Freeze native-validated pilot profiles before the first live request. This
 * finishes startup, retains its single live worker, and rejects other geometry. */
legacy_int unit_planner_adopt_profiles(struct UNIT_PLANNER *planner,
									   struct UNIT_PLANNER *validated);
void unit_planner_destroy(struct UNIT_PLANNER *planner);
/* Replace the complete route geometry. The old planner is destroyed only on
 * success; NULL leaves it usable. Search is seeded from its best profile, but
 * a changed route must be evaluated again before it is considered ready. */
struct UNIT_PLANNER *unit_planner_rebuild(struct UNIT_PLANNER *planner,
										  const struct UNIT_ROUTE_POINT *points, legacy_u32 count);
/* Stop startup workers and retain exactly one background planner when possible.
 * This is idempotent. No unbounded whole-route evaluation is joined. */
void unit_planner_finish_startup(struct UNIT_PLANNER *planner);
/* Idempotent handoff for bounded native previews on the race thread. Pause
 * joins at most the current small rollout; resume starts one worker if allowed. */
legacy_int unit_planner_pause_live(struct UNIT_PLANNER *planner);
legacy_int unit_planner_resume_live(struct UNIT_PLANNER *planner);
/* Called by the progress-screen event loop. With no threads this does at most
 * work_units small resumable chunks; threaded builds return immediately. */
void unit_planner_work(struct UNIT_PLANNER *planner, legacy_u32 work_units);
/* Publish a new immutable driving snapshot and return guidance immediately.
 * Recent worker results are accepted only for this route neighborhood and a
 * nearby pose. Older work cannot overwrite a newer request. Single-threaded
 * platforms evaluate a fixed small number of predictive rollouts per call. */
legacy_int unit_planner_plan(struct UNIT_PLANNER *planner, const struct UNIT_VEHICLE *unit,
							 const struct UNIT_VEHICLE *player, legacy_u32 route_cursor,
							 legacy_u32 generation, struct UNIT_PLAN *plan);
/* Main-thread intent includes the gradual merge after clearing the human. */
legacy_int unit_planner_overtaking(const struct UNIT_PLANNER *planner);
/* A native cached line can already provide a clear passage past traffic. */
void unit_planner_cancel_overtake(struct UNIT_PLANNER *planner);
void unit_planner_stats(struct UNIT_PLANNER *planner, struct UNIT_PLANNER_STATS *stats);

#endif
