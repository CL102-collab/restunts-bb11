# Offline ghost generation

`genghost` searches for driving inputs and writes a standard Stunts replay that
Restunts can load for playback or use as a ghost. It is an SDL3 tool built
alongside `repldump` and `pixldump`: `genghost` on Linux and `genghost.exe`
on Windows. An [Android app](genghost-android.md) packages the same headless
generator with track and car controls. The search runs locally without network access,
language models, or an external AI service. Original game resources and the
selected car's resources must be available in the game data directory.

## Usage

```sh
genghost --data-dir /path/to/game TRACK.TRK COUN --output ghost.rpl
genghost --data-dir /path/to/game DEFAULT.TRK PMIN --cores 4 --output parallel.rpl
genghost --data-dir /path/to/game DEFAULT.TRK PMIN --cores 0 --output allcores.rpl
genghost --data-dir /path/to/game DEFAULT.TRK PMIN --strategy state --cores 4 --output state.rpl
genghost --data-dir /path/to/game race.RPL DIA3 --seconds 1800 --output ghost.rpl
genghost --data-dir /path/to/game TRACK.TRK COUN --owoot --pg:off --lc:off --lcb:off --output clean.rpl
```

On Windows, use the same arguments with `genghost.exe`; quote paths containing
spaces. `--data-dir` can appear anywhere. Relative input paths are searched in
the original working directory, then the data directory. Relative output
paths always refer to the original working directory.

The default output uses a DOS 8.3 filename so the game's replay picker can
list it: `DEFAULT.TRK COUN` produces `DEF-COUN.rpl`. The prefix takes the first
three track-name characters (or fewer for a shorter name), uppercases letters,
and replaces non-alphanumeric characters with `_`. With `--output`, use a
basename of at most eight characters followed by `.rpl` for in-game selection.

The two positional arguments are a track source and a four-character car code.
The source may be a `.TRK` file or a `.RPL` file containing an embedded track.
The car code selects the installed car resources, for example `COUN` or `DIA3`.
An input replay can therefore supply a track even when it was driven with a
different car.

| Option | Purpose |
| --- | --- |
| `--data-dir DIR` | Game resource directory; defaults to the working directory. |
| `--output FILE` | Destination replay; defaults to `TRK-CAR.rpl` using the shortened track prefix above. |
| `--seconds N` | Search budget in whole seconds; defaults to 1800 (30 minutes). `0` disables the timer. |
| `--strategy MODE` | `legacy` (default route optimizer) or `state` (new cooperative state planner). |
| `--seed N` | Pseudorandom search seed; defaults to `1`. |
| `--cores N` | Number of parallel search workers, from `1` to `256`: `1` by default, `0` for automatic CPU selection. |
| `--evaluations N` | Positive candidate budget; disables the default timer unless `--seconds` is also specified. |
| `--transmission MODE` | `manual` or `auto`; defaults to a matching 20 Hz replay's setting, otherwise manual. |
| `--pg:on`, `--pg:off` | Enable or disable the original power gear arithmetic bug. |
| `--lc:on`, `--lc:off` | Enable or disable the original collision behavior. |
| `--lcb:on`, `--lcb:off` | Enable or disable the original left corner bias. |
| `--owoot` | Enforce the game's One Wheel On/Over Track rules. |
| `--verify` | Re-simulate a compatible input replay and print its score, without searching or writing a ghost. |

Physics defaults match Restunts: `--pg:on --lc:on --lcb:on`, with OWOOT off.
Physics option spelling is case-insensitive, and the last value for an on/off
option wins. OWOOT is independent of the three bug switches: enabling it does
not implicitly disable power gear or change collision arithmetic.

A fixed seed, core count, and evaluation budget make the search repeatable with
the same resources and physics. Adding `--seconds` also imposes a time limit,
which can stop at different candidates on different computers. Ctrl+C stops
the search while keeping the best fully validated completed replay found so
far; improvements are saved during the run.

For an independent check of a generated replay:

```sh
genghost --data-dir /path/to/game ghost.rpl COUN --verify
```

`--verify` requires a solo 20 Hz replay with the requested car and matching
transmission. Supply the physics options used to generate it. The command
prints finish and penalty counts in frames and returns a nonzero exit status
if the replay does not finish.

## Parallel search

Use `--cores 4` to run four search workers, or `--cores 0` to select the available
CPU count automatically. The default is one worker, preserving the previous
single-core search. The Android app exposes the same setting as **CPU cores**.
Automatic selection respects process CPU affinity and common Linux container
quotas, up to the 256-worker limit. A candidate budget smaller than the requested
worker count reduces the number started. An explicit count can exceed the
available CPU count, but does not create additional physical processing capacity.

With the default `--strategy legacy`, each worker runs the optimizer in its own process, with independent simulator
state and a distinct, reproducible random seed. This isolation is necessary
because the original game physics uses global state. Workers explore and refine
their own populations concurrently. A coordinator checks their completed laps
with fresh native playback and saves the best overall score, including penalties,
as the search continues. All workers use the selected car, transmission, physics
flags, and compatible input replay. Their populations remain independent; this
is a parallel search across several seeds, rather than a thread-safe rewrite of
the game engine.

`--seconds` sets the overall wall-clock search budget. `--evaluations` sets the
combined candidate budget, divided across the workers; it is not multiplied by
the core count. Changing the core count changes the search trajectories and can
change the resulting replay. More workers provide more simultaneous exploration,
but a fixed candidate budget gives each worker less refinement time. Neither a
faster lap nor a linear speedup is guaranteed.

Ctrl+C and Android's Stop control stop all workers while retaining the best
completed replay already validated. The JSON report records the requested and
used worker counts, per-worker results, and the seed of the winning worker.
`--verify` remains a single replay simulation and does not launch search workers.

The report's evaluation counters sum worker activity; simulated ticks exclude
the coordinator's independent validation passes. Route coverage counts are the
maximum reported by an individual worker, identified by
`route_coverage: "maximum_from_one_worker"`; they are not a union of route visits.
Worker status `0` means a completed replay was found, `2` means none was found,
`1` indicates an error, and `3` means the worker is still running in a progress
report. A worker error stops the pool and returns a failing command status while
retaining any already validated output. Normal exit cleans up private worker
files. Forcibly killing the coordinator stops its workers, but can leave its
temporary `.work-...` directory behind.
See the [parallel validation report](genghost-parallel.md) for measured throughput,
replay comparisons, and lifecycle checks.

## State planner

`--strategy state` enables the new planner; Android exposes it as **State search
(preview)** under **Search method**. The route optimizer remains available as
`--strategy legacy`. Both use the same native physics, resources, replay format,
penalties, and restrictions. A matching input replay is a verified starting
candidate, not a required runtime training archive.

The state planner keeps a bounded frontier of alternative driving states. It
preserves differences in position, heading, speed, gear, airborne state, power
gear opportunity, native route confirmations, and penalties. These buckets are
heuristic pruning: two similar states are not assumed to be interchangeable.
Exact duplicate removal requires identical replay prefixes and route geometry.
Every saved finish is replayed from the start before publication.

Parameterized maneuvers cover acceleration, braking, tile touches, stunt
traversals and bypasses, launches, power gear setup, and landing recovery.
Each stunt maneuver has an approach, an action, and a useful exit or landing.
The existing two-physics stunt catalog supplies collision-dependent geometry.
Normal traversal remains an alternative. Macro parameters and short input
intervals are refined using whole-lap native scores, including penalties. Short
macros can stop at takeoff, landing, or a newly available power gear opportunity
so the next decision can preserve that state. Useful gear/contact categories
receive reserved representatives within the bounded beam.

A separate section frontier refines approach and exit states from the best
completed lap, then retimes its demonstrated continuation to the changed pose.
Fresh controllers remain fallback continuations. Before a first finish, safe
prefixes from failed driving policies also supply recovery sections; the native
simulator reconstructs them from inputs. Periodic new driving profiles continue
to explore instead of repeatedly restarting only at the grid.

**Chicane fast grass is built in.** Both chicane models assign paved grip to
visually grassy areas inside their selected native object footprint. The new
planner proposes diagonal and near-straight crossings without an ordinary-grass
speed limit. It still simulates the actual wheel contacts; ordinary grass beyond
the footprint keeps its normal physics. OWOOT uses its separate geometric road
coverage rule and disables these crossings. Collision fixes alone do not remove
fast grass.

Power gear proposals use native torque, gearing, drag, gravity, rear-wheel
contact, and the signed/unsigned acceleration arithmetic. They can preserve a
useful fourth-gear uphill setup rather than always shifting straight to top gear.
A positive readiness prediction is an opportunity to test, not a persistent
power gear flag or proof of a successful lap. `--pg:off` disables these proposals
and their ranking reward.

For this strategy, `--cores` selects persistent workers sharing one coordinator's
frontier and task schedule. Each worker reconstructs replay prefixes and keeps
complete simulator checkpoints locally; native pointers never cross process
boundaries. Requests are batched to reduce process handshakes. Results are consumed in deterministic task order. With fixed
resources, physics, seed, and evaluation budget, changing worker count preserves
the task sequence and resulting replay. Cache work and wall-clock throughput can
still differ. A time budget may stop at different batches on different machines.

The JSON report records `strategy`, `state_task_hash`, frontier size, duplicate
prefixes, macro/input counts, section/recovery work, guided continuations, transition stops,
fast-grass proposals, and power gear opportunities.
`state_pg_opportunities` is explicitly a readiness count, not detected boosts.
For cooperative runs, `winner_worker` is null because the coordinator combines
work from all workers; dynamic worker budgets and individual winning laps are
also null. Worker tick counts cover completed batches and exclude parent
validation; total simulated ticks include validation. Route coverage is the
shared frontier's union. A candidate budget counts valid evaluated proposals;
unavailable maneuver slots still appear in the deterministic task hash.

See [state planner validation](genghost-state-planner.md) for human evidence,
benchmarks, and the default-strategy decision. The planner is a bounded heuristic
and does not promise every route combination, every exploit, or human-best times.

## Search and scoring

The evaluator runs the game's player physics without rendering. It measures
completed laps using the game's finish time, including its actual penalty
calculation. Shortcuts can therefore be useful even if they earn a penalty,
provided their final score is better. Grass, slopes, collisions, gearing, and
the selected restrictions all act through the same simulation used for replay
playback.

The legacy route planner enumerates simple directed start-to-finish paths through the
track's branch connections. The search explores alternatives and refines
driving parameters for 64 route segments, then refines recorded steering,
pedal, and shift inputs. Segment genes control speed, cornering, racing line,
shifts, shortcut attempts, power gear entry, and whether to brake during a
slide. Local refinements replay the affected continuation from cached
simulation checkpoints. Failed runs
also supply useful prefixes: the search changes the driver near the failing
sector and regenerates the suffix. A faster sector is useful only when the
resulting lap completes with a better total score. Branch combinations share
the budget; large graphs and short budgets can prevent exhaustive refinement.

Unrestricted track-only searches also explore tile-area shortcuts. Ordinary route
recognition uses the car center within the tile footprint, so the planner can
cut across grass and barely enter a corner without following the painted road.
It proposes skipping up to two intervening flat road pieces, with branch and
finish exceptions decided by the real penalty tracker. A local shortest crossing
of an inset tile rectangle supplies a steering target; the inset grows with speed
so the 20 Hz simulation can register contact. Braking follows the proposed line's
curvature, while elevated pieces, stunts, and the finish retain their guidance.
OWOOT keeps the road-constrained controller. The objective is still total time:
a longer line that reaches power gear can beat the shortest physical route.

Without OWOOT, initial driving profiles cut the horizontal corkscrew to the
right of its red central wall. The approach starts near the road center and
moves sideways gradually, using the car's width to propose clearance. This
short line is tried from the first candidates, so later sectors can be refined
without first discarding the cut. If six initial cut profiles fail to finish,
the search starts an independent ordinary-traversal lineage so that failed cut
parents cannot crowd it out. Both paths remain available for later refinement
and are scored using complete native playback and penalties. Collision
fixes do not disable this geometric bypass. OWOOT retains full corkscrew traversal.

The built-in [stunt catalog](genghost-stunt-catalog.md) covers both chicanes,
highway pieces, ramps, elevated and banked pieces, loops, tunnels, pipes,
half-pipes, both vertical corkscrews, slalom stones, and the horizontal corkscrew.
Every model has a preferred recipe for each collision setting plus another line
to refine. The search funds local probes after its ordinary initial profiles,
including before a first finish. Probes preserve the known input prefix and
change one stunt's approach and traversal; unreachable sectors can be retried
when the ordinary parent advances. Normal traversal remains available.

Recipes include straight chords, ground or outside bypasses, median riding,
low edges, wall crossings where the original collision behavior permits them,
and launches toward later tiles. Fixed-collision paths respect solid clearance
and height. Clear geometric cuts can work with either collision setting; a
special collision exploit is not assumed for every stunt. OWOOT disables these
proposals. Native full replay completion and penalties decide which line wins.
Airborne targets prepare landing; they do not introduce midair steering or
change the physics.

Dual-way switching is also part of the default search. A separate population
tries touching a unique tile on one branch, crossing to the other branch before
their merge, and confirming progress there. Shared split/merge tiles do not
count as the required unique touch. Short and nearby crossings seed the search,
alongside early-departure/late-arrival pairs drawn from each branch’s private
tiles. This gives long grass crossings a starting candidate even when the
nearest crossing saves little distance. Mutations can select every eligible
departure/arrival pair and combine switches
in separate branch sections. The simulator supplies the actual penalties.
These proposals start after the first validated lap and are disabled for OWOOT.

Input refinement targets control transitions: brake release, turn-in, steering
corrections, and shifts. It moves individual transitions or entire short pulses
by a few frames while preserving the other controls. Some candidates realign
the remaining controls to the parent lap's measured position, orientation, and
speed, with optional small steering corrections. This allows an improved sector
to rejoin the demonstrated line despite arriving earlier. Airborne and heavily
banked sections retain their demonstrated steering. Every such continuation
still consists of ordinary keyboard inputs and must pass full replay validation.

Road, tile, and dual-way policies retain separate populations and replay parents. They
share one search budget and saved best result. Initially two road rounds
alternate with one tile round. The dual-way population joins after a validated
finish when the track has compatible branches. After the active populations
receive their initial policies, three of four complete route rounds favor the
family with the faster completed lap; the fourth rotates through the others. Completed
routes also receive short input-refinement batches.
Independent random streams preserve the road search's sequence. A compatible
verified replay and OWOOT use the original single search schedule.

The search spends more attempts refining a verified human demonstration. A lap
found from track geometry receives more fresh driving-policy exploration, so
finding an initially slow finish does not stop the search for a better line.
Successful parents supply cached checkpoints and control boundaries; no replay
archive is required at runtime.

An input replay is also a possible starting candidate. Its inputs are useful
only when its car and transmission match and its recording rate is 20 Hz.
They are re-simulated as a solo run under the requested restrictions; a
previously successful replay may fail with different physics or without its
original opponent. A 10 Hz replay or one using a different car still supplies
an embedded track, but its inputs are not reused.

Power gear is considered only for cars whose parameters support it and when
`--pg:on` is active. The driver accelerates normally on the approach and tries early
upshifts when the rear wheels become unloaded. It also considers an earlier
upshift while climbing with the rear wheels still touching: the car’s gear-knob
travel predicts clutch time and coasting loss, then its torque curve, top-gear
ratio, drag, gravity, and native mass division predict whether the shift can
trigger positive power-gear acceleration. This uses car and physics parameters,
not track names or recorded frame numbers. It avoids premature airborne
downshifts in useful power gear cars. Suitable slopes can make reaching power
gear valuable, but the completed lap score determines whether that approach wins.
Power gear is not available to every car: the repository's `DIA3` has a mass of
32, which avoids the bug, while the stock `COUN` suffers from anti-power gear.
`--pg:off` fixes that shared arithmetic defect, removing both effects.

The result is a heuristic search, not a proof of the globally fastest lap.
More time can improve a result, but does not guarantee a finish on every track
or discovery of every collision exploit. The program validates a completed
replay from the beginning before reporting success. If no valid completed lap
is found, it exits unsuccessfully and does not present an unfinished run as a
completed ghost. Replays are limited to 12,000 input frames at 20 Hz. The
normal one-second post-finish tail is shortened when necessary to keep a valid
finish within that limit.

## Playback and reports

The output uses the standard `.RPL` format, which does **not** store the power
gear, collision, left corner bias, or OWOOT switches. A companion report,
`FILE.rpl.json`, records the selected physics options and search result. Launch
Restunts with the same physics switches before playing the replay or selecting it as a
ghost. For example, a replay generated with `--owoot --pg:off --lc:off --lcb:off`
must be played with those same options. Changing the car resources can also
change the replay's outcome.

The JSON report also records whether the input replay finished under the
selected rules (`seed_replay_finished`), its verified score (`seed_total_frames`),
and counts of policy, input, and trajectory-guided evaluations. Guided evaluations
are a subset of input evaluations. `guided_improvements` counts independently
validated improvements to the saved best lap from those guided candidates.
`tile_evaluations` counts candidates from the tile-policy search, including input
refinements of its parents; `tile_improvements` counts its verified improvements
to the shared best lap. Both are zero for OWOOT and a verified human seed.
`dualway_pairs` reports the implicit departure/arrival choices across route
alternatives. `dualway_evaluations` counts candidates with a switched route,
including refinements of those parents; `dualway_improvements` counts their
validated improvements. `tactic_evaluations` counts policy or repair candidates
that actually use a stunt target, and `tactic_improvements` counts their validated
improvements. These counters identify search proposals, not proof of a particular
collision bug in the resulting trajectory.
A replay-assisted result may preserve most of an expert's original driving;
it should not be interpreted as an independently discovered racing line.

## Golden replay benchmark

See the [complete stunt catalog and new winner study](genghost-stunt-catalog.md),
[Ayrton DEFAULT study and corkscrew follow-up](genghost-default-study.md),
[winning replay and stunt study](genghost-stunt-benchmark.md),
[initial 16-track comparison](genghost-benchmark.md), and
[expanded replay analysis](genghost-expanded-benchmark.md) for measured results
and the remaining gap between track-only driving and human replay refinement.

[`benchmark-genghost.py`](../tools/scripts/benchmark-genghost.py) compares track-only
searches and expert-replay refinement separately. It selects the fastest freshly
simulated solo 20 Hz recording for the exact track bytes and car in the provided
archive. This is a corpus reference, not a claim about the current world record.
The total score includes penalties and excludes replay padding. Successful outputs
are replayed again; supplying `--repldump` also checks them with the independent
replay driver.

```sh
python3 tools/scripts/benchmark-genghost.py \
  --binary build/genghost-linux/genghost \
  --repldump build/genghost-linux/repldump --data-dir stunts \
  --seconds 120 --jobs 2 --output build/golden-benchmark --label local
```

Use `analyze-genghost-replays.py` to re-simulate a corpus, reserve disjoint track
hashes, and measure only the fastest verified recording per exact track, across
all cars. `analyze-genghost-stunts.py` adds geometry and stunt telemetry for those
winners. Their JSON manifests can be
passed to the benchmark with `--suite PATH` to select exact track bytes and cars,
even when embedded track names were reused. It copies only missing resources
when `--car-assets` is supplied and preserves installed packed/unpacked variants.

Use repeated `--case ZCT1` arguments to select tracks, `--evaluations 2000` for a
repeatable candidate budget, or `--physics=--pg:off` (and other physics options)
to re-rank humans and search under those restrictions. The script writes raw
reports, commands, logs, generated replays, and independent state dumps beneath
the output directory. It does not change the source archive.

## Driving rules behind the search

The implementation evaluates these effects through the engine, rather than
using the wiki descriptions as a substitute for simulation:

- [Penalty time](https://wiki.stunts.hu/wiki/Penalty_time) explains why touching
  an element's full tile area can count even when the car is off the road.
  The finish and accumulated route history have special cases, so a simple
  distance-to-road cost cannot reproduce penalties.
- [Dual-way switching](https://wiki.stunts.hu/wiki/Dual-way_switching) describes
  shortcuts between branches. The exact approach and re-entry sequence affect
  whether the game adds time.
- [Power gear](https://wiki.stunts.hu/wiki/Power_gear) and the
  [power gear bug](https://wiki.stunts.hu/wiki/Power_gear_bug) distinguish
  flexible PG, rigid PG, unaffected, and anti-PG cars. Flexible PG cars such as
  the Indy and Acura can exploit a loop in top gear early; rigid PG cars such
  as the Ferrari and Corvette need sufficient speed first.
- [Driving tips](https://wiki.stunts.hu/wiki/Driving_tips) discuss manual
  shifting, airborne upshifts, racing lines, and alternative stunt paths.
  [Fast grass](https://wiki.stunts.hu/wiki/Fast_grass) explains the unusually
  permissive surface around chicanes.
- [Bugs](https://wiki.stunts.hu/wiki/Bugs),
  [loops](https://wiki.stunts.hu/wiki/Loop), and
  [slalom blocks](https://wiki.stunts.hu/wiki/Paved_slalom_road) describe
  collision tricks whose success depends on position, speed, and input timing.

With `--owoot`, the game's tire coverage and ordered stunt-route checks reject
shortcuts that violate those rules. See [OWOOT validation](owoot-validation.md)
for the implemented interpretation, including permitted ramp gaps.
