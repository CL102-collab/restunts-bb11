# State planner and human maneuver study

The new `--strategy state` search runs alongside the established
`--strategy legacy` optimizer. It uses the same headless native physics and
ordinary replay inputs. Android calls it **State search (preview)**. The
comparison below determines whether it is ready to replace the default.

## What changed

The coordinator maintains a bounded beam of complete driving histories, rather
than keeping only one fast arrival at a road segment. Its pruning buckets
include location, heading, pitch, roll, speed, gear, airborne state, predicted
power gear opportunity, native branch confirmations, and penalties. Each history
reconstructs the complete native state, including hidden collision, random and
route-tracking state. Similar buckets are deliberately approximate pruning, not
proof that two native states are equivalent.

Each expansion chooses a parameterized maneuver: approach, traverse or bypass,
then exit or land. Normal road driving remains available. A separate section
beam starts before stunts, gear changes, climbs and takeoffs in the current
verified best lap. It explores three successive macro actions, retains diverse
exits, and completes each promising continuation before comparing total time.
The existing continuation can be retimed to the changed pose using native guide
points; fresh controllers remain alternatives. Before a first finish, safe
prefixes from failed policies receive recovery sections. Periodic new profiles
continue exploring. Macro actions can stop at takeoff, landing, or a newly
available PG opportunity. The bounded beam reserves representatives for useful
gear/contact/PG categories. Parameter and input refinements share the search budget. The grid-start beam
continues exploring alternatives independently of those incumbent prefixes.

Persistent workers evaluate one shared deterministic task schedule. Each worker
owns its native physics globals and caches exact replay-prefix checkpoints.
Only pointer-free inputs and results cross process boundaries. Each process
receives bounded request packets rather than paying a handshake for every short
macro; the planner batches independent parents and rotates action roles to
reduce systematic load imbalance. Batches are
consumed in submission order, so fixed-budget searches can be compared exactly
across core counts. Timed searches remain machine-dependent. Available batch
width limits useful parallelism; requesting more workers does not guarantee that
all of them stay busy. Duplicate resulting prefixes are pruned after simulation.

Only a completed replay's native finish time plus penalties selects a saved
winner. Every accepted candidate is simulated again from the start. The beam's
estimated remaining time is a ranking heuristic, not an admissible lower bound
or a global-optimality proof.

## Human evidence

The study reused the fastest verified replay per exact track layout in the
forty-track tuning set. It extracted 24 complete stunt windows and 109 flights
covering at least two tiles, then ran seven new power-gear-disabled
counterfactuals. The twelve reserved tracks were excluded from maneuver learning.
Runtime search does not need the archive or an external AI service.

| Evidence | Generalized proposal |
| --- | --- |
| ZCT110 / PMIN crosses chicane grass almost straight at 241.13–245 mph. | Test straight and diagonal chords inside the native chicane footprint, preserving speed. |
| R4K29 / LOLA crosses the opposite chicane diagonally at 192.05–197.18 mph. | Retain both orientations and alternative exit headings; do not assume one universal chord. |
| ZCT166 / PMIN first boosts in fourth gear; ZCT267 / PMIN begins a loop boost at 92.86 mph. | Evaluate gearing, uphill contact and native acceleration arithmetic, rather than requiring high speed or top gear. |
| ZCT110 / PMIN stays at 245 mph across a 3.56-tile flight. | Keep useful launch/landing outcomes even when the immediate section is not the shortest. |

The native chicane collision code assigns road material before testing its
visible arc. The visually grassy interior therefore receives paved grip when
that object is selected. This holds with collision fixes too; it does not turn
neighboring ordinary grass into road. OWOOT's wheel-overlap geometry remains a
separate constraint. Both behaviors are exercised by native tests.

Local chicane probes started from human checkpoints, with the same car and
physics. Of thirty proposed continuations, twenty-four exited without a crash
or added penalty. The ZCT110 straight proposal held 244.90–245 mph with no braking
through the interior. Failed proposals are retained in the evidence. These are
local feasibility measurements, not autonomous whole-lap victories.

The power gear readiness predicate predicts a beneficial unsigned negative-force
calculation on a grounded throttle tick. Its telemetry counts opportunities,
including those that may be consumed by a gear change or speed limit; it does
not claim to detect an active power gear latch. Native counterfactual tests use
cloned car states and the real speed update under both settings.

## Comparison protocol

Ten track/car/rule cases were selected and hashed before measuring either
engine: two DEFAULT settings, four tuning layouts, collision-fixed ZCT110, and
three reserved layouts. Each search starts from a `.TRK`, uses the same installed
resource hashes and manual transmission, and receives twenty wall-clock seconds
with two workers. Runs are sequential without competing builds or searches.

The frozen baseline is algorithm commit `8c4934e177b22b08873fdf129d26488fa62fa509`,
Linux binary SHA256
`738060d2a3b3abb4fb7eae434c721abf322eec1345a1c4729e8ef69360bc85fe`.
The immutable plan SHA256 is
`adb4e24f739bc5caaec9db2dbc1a9b30f5f908841df7fe4dcef248f0067db1cd`.

Human times use the exact matching car, transmission and installed resources.
An unrestricted human time is not used as a restricted-physics reference unless
separately verified under those rules. DEFAULT's comparison is the original
Ayrton replay at 31.25 seconds; the previously optimized 31.05-second derivative
is excluded. Human-seeded refinement is a separate experiment.

Evaluation counts are not equalized because a short macro expansion and a
whole-lap policy have different costs. Reports include actual wall time,
aggregate child CPU time and simulated ticks. A single timed run is noisy;
results do not establish general superiority. Completed outputs are checked by
both the native generator and independent `repldump` playback.

## Correctness checks

The state test suite compares complete native simulator snapshots after cached,
fresh-worker and from-start playback, including hidden physics/route state.
Guided continuations are tested with changed reference bytes of identical length
to catch stale-cache reuse, and unchanged references preserve the original input
stream. Transition stops are checked by independently replaying the ticks before
and after each reported contact or PG-readiness transition.

One, two and four workers produce identical fixed-budget task hashes, replay
inputs and independently serialized game states. A 2,501-candidate comparison
also exercises section beams and guided continuations. Native tests cover the
actual chicane material/OWOOT difference, uphill PG arithmetic, all three landing
proposals surviving a full action list, and recovery from failed map-only pilots
on tuning track ZCT166. Explicit legacy selection matches the frozen release's
replay inputs. Cancellation and a killed worker preserve the last verified lap
and leave no running children.

The process transport also passes variable-size packet and multi-wave tests on
Linux, Windows under Wine, and ARMv7 Android under userspace emulation. A
controlled test with only transport changed preserves the prior planner's fixed-
budget task hashes and replay bytes. On DEFAULT/PMIN, its twenty-second search
increased from 10,977 to 43,508 evaluated candidates and from 1.70 to 13.75 million
simulated ticks. This is transport evidence, not a claim that extra candidates
alone make a competitive racing strategy.

## Measured results and default decision

| Track / car / rules | Human seconds | Legacy seconds | State seconds | State penalty seconds |
| --- | ---: | ---: | ---: | ---: |
| DEFAULT-PMIN-original | 31.25 | 64.90 | 77.45 | 0.00 |
| DEFAULT-COUN-owoot-allfix | — | 123.50 | 120.10 | 0.00 |
| ZCT110-PMIN-original | 46.60 | 92.00 | 87.90 | 0.00 |
| R4K29-LOLA-original | 57.70 | 68.25 | 69.15 | 0.00 |
| ZCT227-ANSX-original | 81.00 | 154.80 | 179.15 | 0.00 |
| ZCT166-PMIN-original | 89.40 | 230.45 | 181.60 | 18.00 |
| ZCT110-PMIN-lc-off | 46.60 | 95.25 | 90.65 | 0.00 |
| ZCT181-LM02-original | 79.15 | 122.35 | 116.70 | 0.00 |
| SIOFOK-ANSX-original | 59.95 | 112.35 | No finish | — |
| ZCT180-P962-original | 65.70 | 115.45 | 97.60 | 0.00 |

The state planner improved six cases, was slower on three, and did not finish
SIOFOK within the short budget. All nine completed outputs pass both replay
checks. ZCT166 includes 18 seconds of native penalty; the other eight have zero.
The new engine is therefore available as **State search (preview)** while
**legacy remains the default**. In particular, DEFAULT/PMIN is still slower
and far from Ayrton’s 31.25-second reference. These results do not justify a
claim of nearly perfect autonomous driving or universal improvement.

The first state prototype finished only four cases. After packet batching,
guided continuation, failed-policy recovery and diversity/event refinements,
the final version finished nine. Across the ten runs, its aggregate CPU use
increased from 86.81 to 258.23 seconds within about 200 wall-clock seconds;
the legacy two-worker baseline used 403.18 CPU seconds. Some workload imbalance
and serial coordination remain, so worker count is not a promise of full CPU
utilization or linear speedup.

Final numerical candidate binary SHA256:
`94170c984afbdee60ebf5c065aae703283e7a82f027c2a6802159884ac4217ec`.
The validation archive retains both prototypes, the isolated transport test,
source snapshots, commands, replay outputs, native scores, CPU measurements,
human-reference manifests and resource hashes. Release binaries are rebuilt
from the committed source and checked separately for deterministic parity.

## Seeded refinement and a discovered power gear entry

A separate frozen experiment gives each engine twenty seconds and two workers
starting from the unchanged human replay, with the same car and resources.
These are seeded refinements, not autonomous discoveries:

| Input replay | Human seconds | Legacy seconds | State seconds |
| --- | ---: | ---: | ---: |
| DEFAULT / Ayrton / PMIN | 31.25 | 31.05 | 31.25 |
| ZCT110 / winner 1152 / PMIN | 46.60 | 46.05 | 46.05 |

All four outputs have zero penalty and pass both replay checks. The new planner
ties the established engine on ZCT110 but does not improve Ayrton's replay in
this budget. Seeded plan SHA256:
`f420da6a7a0f357493878625cfa4c4ef34812dd4befc718e81b2b0f6b360c02b`.

The autonomous ZCT166 output does demonstrate a newly discovered power gear
boost. Replaying the same generated inputs with only `--pg:off` changed leaves
all serialized native states identical through frame 154. At frame 155, original
physics accelerates from 118.68 to 127.07 mph (+8.390625 mph), while the fixed
version falls to 118.54 mph (−0.140625 mph). This first state divergence directly
identifies the beneficial arithmetic bug; it is stronger evidence than peak
speed or the planner's readiness counter. The original lap subsequently reaches
245 mph and scores 181.60 seconds including 18 seconds penalty. Those unchanged
inputs crash with PG disabled. This validates discovery of a useful setup, not
a claim that the complete lap is close to the human record.

## Portable worker handoff

Final Windows testing exposed a file-replacement race in the cooperative
transport. Each consumer now closes and removes its current packet before
acknowledging it, so the next publication targets an empty inbox. Windows readers
also share deletion. Twenty repeated Windows runs at two/four cores and six
Linux runs at one/two/four cores preserve replay bytes, task hashes and state
counters. Variable-size packet, failure and deadline checks pass on Linux, Wine
and ARMv7 Android emulation.

The timed comparisons above measure the frozen numerical candidate, before this
portability fix. Fixed-budget parity validates the search decisions after the
fix; it does not establish identical wall-clock throughput. Packaged binaries
are rebuilt and validated separately, with their exact source revisions and
hashes recorded in the release manifests.

## Slalom correction

The shared stunt catalog now proposes the physical middle of a slalom under
both collision settings. The old default aimed at local x=60, toward a stone,
and the fixed-collision recipe always detoured outside. Native stones leave a
46-unit gap around x=0. COUN's actual half width is 20 units and PMIN's is 19;
their longitudinal half extents are 54 and 56. Those dimensions occupy separate
SIMD metadata fields, so the length/radius must not be mistaken for body width.

The new center controller targets exactly x=0 with a short alignment horizon.
It prepares the entrance before the first stone, checks projected yaw clearance
for fixed collisions, and guides beyond the second stone's rear-clearance limit.
A car that does not fit retains the outside proposal; original collision-only
stone-crossing attempts remain separate. Selected center policies exempt the
slalom's ordinary 65 mph cap during braking preview, while preserving limits for
later corners. Ordinary traversal remains available.

State search explicitly reserves center and native airborne continuation actions
before optional stunt variants can fill its action list. The flight action keeps
the observed incoming line beyond the stones and returns to ground guidance on
landing. It is offered only after real wheel-contact loss, never by inventing a
jump or treating the slalom as a ramp. A dedicated slalom-only gene prevents this
approach from accidentally selecting the outside bypass of a preceding ramp.

A clear center pass is also legal under OWOOT. The restricted driver admits only
that narrow road corridor, with fixed geometric clearance, and ignores other
stunt recipes. Legacy search funds it alongside ordinary initial profiles; state
search offers it as a road-only action. OWOOT still disables the separate jump
shortcut proposal. Native OWOOT validation decides whether each completed replay
satisfies the full wheel/route rules.

From-start DEFAULT input fixtures establish local feasibility without moving or
editing the native car state. COUN passes the stones on the ground at about
137–139 mph. PMIN crosses the first stone's longitudinal position while airborne
at 151.27 mph, then lands and clears the second on the center line. Both fixtures
also pass fixed collisions and OWOOT because their projected center corridor is
clear; this does not make arbitrary jumps over a stone's footprint legal under
OWOOT. Each partial replay matches independent `repldump` playback.

Production action tests start from those replayed prefixes. COUN clears the
section in 14 frames, accelerating from 136.74 to 138.84 mph under original/fixed
collisions and fixed collisions with OWOOT. PMIN clears it in 15 frames,
151.27 to 177.34 mph, under both collision modes. These tests require no braking,
crash, added penalty, or speed loss. Geometry checks cover rotations, reverse
traversal, early alignment, late-entry rejection, yaw, wide cars, rear clearance,
and action capacity. These are local maneuver results, not whole-lap records.

A separate before/after comparison uses DEFAULT track input only, manual
transmission, one core, seed 1 and 2,000 evaluations per search. Each case runs
against the previous release and the corrected engine with identical resources.
All twelve outputs finish with zero penalty and pass native plus independent
playback. These are fixed-budget results, not wall-clock throughput measurements.

| DEFAULT case | Before seconds | After seconds |
| --- | ---: | ---: |
| PMIN, legacy, original physics | 75.30 | 75.30 |
| PMIN, state, original physics | 83.80 | 83.80 |
| COUN, legacy, original physics | 110.55 | 106.90 |
| COUN, state, original physics | 113.45 | 113.45 |
| COUN, legacy, OWOOT and all fixes | 124.30 | 116.75 |
| COUN, state, OWOOT and all fixes | 125.70 | 126.05 |

The improved unrestricted COUN lap removes all twelve former slalom brake ticks
and accelerates from 111.35 to 139.25 mph through the tile. The restricted legacy
lap also improves overall, but still brakes in that section; the restricted
state run is 0.35 seconds slower. PMIN's saved best still brakes within the
slalom at this budget. A faster feasible local maneuver does not automatically
produce a better whole lap with its later corners and landing. Legacy remains
the default; no universal or near-record improvement is claimed.

Frozen comparison candidate SHA256:
`9c83f1af94f8dedd389e3b47825b72a49ea5a688fcea16763203f0dcaae9f103`.
The baseline is release `43a9d6fa` with binary SHA256
`a0ff96bc0f51548832097d71923f58f97e4b64de116f07d597c48bfedd31c209`.
The slalom validation archive includes the controller probe, partial fixtures,
failed alignment attempts, native action tests, complete generated replays,
resource hashes, and separate final package checks.
