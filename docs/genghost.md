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
| `--seed N` | Pseudorandom search seed; defaults to `1`. |
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

A fixed seed with a fixed evaluation budget makes the search repeatable with
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

## Search and scoring

The evaluator runs the game's player physics without rendering. It measures
completed laps using the game's finish time, including its actual penalty
calculation. Shortcuts can therefore be useful even if they earn a penalty,
provided their final score is better. Grass, slopes, collisions, gearing, and
the selected restrictions all act through the same simulation used for replay
playback.

The route planner enumerates simple directed start-to-finish paths through the
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

Input refinement targets control transitions: brake release, turn-in, steering
corrections, and shifts. It moves individual transitions or entire short pulses
by a few frames while preserving the other controls. Some candidates realign
the remaining controls to the parent lap's measured position, orientation, and
speed, with optional small steering corrections. This allows an improved sector
to rejoin the demonstrated line despite arriving earlier. Airborne and heavily
banked sections retain their demonstrated steering. Every such continuation
still consists of ordinary keyboard inputs and must pass full replay validation.

Road and tile policies retain separate populations and replay parents. They
share one search budget and saved best result: two road rounds alternate with
one tile round, with short input-refinement batches for completed routes.
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
upshifts when the rear wheels become unloaded; it avoids premature airborne
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
A replay-assisted result may preserve most of an expert's original driving;
it should not be interpreted as an independently discovered racing line.

## Golden replay benchmark

See the [initial 16-track comparison](genghost-benchmark.md) and the
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
hashes, and measure a sample of human trajectories. Its JSON manifests can be
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
