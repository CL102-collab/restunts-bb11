# UNIT opponent

UNIT is opponent 7 in SDL3 builds. It supports the selected opponent car and
transmission; PMIN is the development benchmark. Other ports retain six
opponents. The original opponents keep their existing driving policies.

The indexed 80 by 83 portrait is shared by the biography and both race outcomes.
Up and Down page through the full biography. See [artwork](opponents/README.md).

## Planning

Selecting **Let's drive** starts ten seconds of preparation. The black screen
has a centered blue bar, 30 percent of the output width and 100 output pixels
high, which fills according to elapsed time. Window and browser events remain
responsive.

Preparation builds the complete checkpoint-aware route immediately. Every startup
candidate evaluates the full lap, including later braking and maneuver constraints;
its line profile is used consistently from the start. Native builds use available
CPUs within an allocation budget. At the deadline, startup workers stop and at
most one worker remains for live planning. Single-threaded
Web builds perform bounded, resumable work in the event loop. Threaded Web
builds reuse idle rendering slots during preparation and reserve a separate slot
for the live planner when rendering resumes.

Workers use copied geometry, car limits, and vehicle snapshots. They compare
lines and speeds against the predicted player position. Stale results are
discarded or checked against the current state. The game's car physics remains
authoritative; the planner's predictive vehicle model is an approximation.

Native previews also run the shared ghost-generator physics snapshot engine in
main-thread chunks of at most 64 simulation ticks, checking a four-millisecond
budget between ticks. They test approaches and landings with the selected car,
reject crashes and new penalties, and restore all live state before returning.
Receiving-road checks also verify tire overlap with the actual road geometry,
so surface flags alone cannot validate contact below an elevated road.
Classic TRK races also certify connected ramps, hill roads, and tunnels:
their horizontal guides cannot predict a crest launch, a missed elevated corner,
or a tunnel roof strike. These maneuvers have no sustaining-speed floor.
They retain ordinary corner planning and braking; a validated entry speed is a
ceiling. Their usual centered line can yield to bounded passing candidates when
another car obstructs the forecast corridor. A parked player near a crest therefore
does not force every preview onto the same blocked centerline. Mandatory jumps,
loops, and checkpoints keep their required alignment.
Banked corners also receive native checks, with the planner accounting
for the game's roll-based steering assistance. Slaloms retain their ordered
weaving guide while native trials check faster entry speeds against the poles.
Classic sustaining maneuvers try faster native candidates before the original
recipes; CSTRK retains its established geometry and sustaining-search policy.
Clearance checks inside an existing jump approach share that validation, and
connecting road checks do not merge otherwise separate stunt searches.
An airborne crossing of a native, height-unspecified finish uses the same timing
plane as the original opponents; explicitly placed custom-track finishes retain
their height check.

Under normal rules, fresh unverified guidance uses a centered banked-road line and
conservative bank and elevated-corner speed limits, including braking before those pieces. Native
previews and verified cached commands retain their physics-validated speeds.
OWOOT keeps its existing strict-road speed and line policy.
Ramp guidance releases on genuine partial road contact so ordinary braking can
resume; grass alone does not release it.
Stunts with overlapping approaches are validated together through the final
landing. Entry and exit speeds can differ when a slow technical section leads
into a fast jump. Ramps first try a two-tile acceleration approach, then three
and four tiles if shorter approaches fail native validation. Accepted run-up
lengths are saved with the speed profile and rechecked after an approach
changes. Barrel launches can also test bounded lateral lines and a short
supported braking phase to adjust rotation before takeoff. Difficult barrels can
also reuse the ghost generator's population search for timed steering,
countersteering, and pedal inputs, jointly varying entry speed and lateral
position within the car's limits. These candidates must satisfy the existing
OWOOT roll milestones and receiving-road checks before they are accepted.

A bounded cache retains the controls and expected car states of verified route
segments. Ordinary road controls are retained as soon as their native steps pass,
even if a later maneuver fails or preparation ends. After a failed preview,
fresh guidance takes over before the cached prefix runs out. Stunt controls stay
speculative until their landing is accepted. If preparation ends mid-preview,
unfinished search continues ahead of the car with at most two native chunks per
simulation frame, checking an eight-millisecond budget between chunks. The
planner worker pauses during this continuation; only newly verified segments are appended to the cache. A changed
approach or the human player's predicted path cancels speculative work and
invalidates the affected segment. Current avoidance checks follow the cached
trajectory itself with swept car bounds; when necessary, fresh steering and
pedal commands take over.

Live rechecks reserve braking distance ahead of the required run-up. Under
normal rules they first start from the exact moving car state. A verified ordinary prefix lets UNIT keep
driving while native previews validate the upcoming stunt. If no safe prefix is
available, UNIT stops and uses a fixed future-state recheck as a fallback. OWOOT
uses settled, stopped rechecks to preserve its wheel and jump checks. These
stopped rechecks pause the planner worker, check a 30-millisecond frame budget
between chunks, and run at most eight chunks per frame. After an exact recheck
handoff, previews continue from the verified rolling exit so the following
maneuver can be planned while UNIT drives. Native
simulation never runs concurrently with live gameplay.

Route selection preserves a complete checkpoint color set. Gate aperture widths
and centered crossings are included in both startup and live planning; corrective
checkpoint targets keep advancing toward the gate. With OWOOT off, UNIT
may widen its line within a proven, flat, unobstructed route-piece footprint
without skipping a route gate. OWOOT disables those corner cuts and applies the
same wheel and jump checks used for the player. Independent native penalty and
checkpoint observers check UNIT's actual motion. Their state is restored with
replay snapshots; any accidental penalty is added to UNIT's finish time.

## Learning between races

UNIT saves route-search progress when a prepared race closes or the game shuts
down normally. The next race resumes the candidate sequence instead of starting
over. Separate caches belong to each track geometry, opponent-car physics,
transmission, frame-rate, and driving-rules combination. Road, terrain, obstacle,
car-physics, or OWOOT changes produce a different identity. Track names, horizons,
and notes do not affect the identity. Planner and physics revisions invalidate
incompatible caches.

The cache retains search progress, learned maneuver recipes, and the best clean
native-preview lap. On the next start, saved commands run through fresh native
physics to rebuild expected car states. They cannot control the live car until
that validation finishes. Remaining preparation time tests new candidates;
only a complete, penalty-free, faster native lap replaces the incumbent.
Failed, slower, or unfinished candidates retain the previous validated lap.
Live rechecks cannot replace it with traffic-dependent partial controls.
An incomplete first preview can still retain accepted maneuver recipes for
validation on a later start.

Preparation remains ten seconds. Repeated starts accumulate search work and can
improve the saved best lap, but every start need not improve it. Player traffic
and avoidance can change the actual race time. The existing racing-line model
and mandatory stunt alignment still apply; caching does not add the offline
ghost generator's per-section optimization.

Learning files use `.ULC` in the writable game/save directory. Browser and
Android builds use their existing saved-file persistence. Missing, incompatible,
corrupt, or unwritable caches do not prevent racing. Delete the corresponding
`.ULC` files to reset learning. Replays use recorded commands and do not train
or rewrite the cache.

Headless regressions disable persistence by default. The benchmark flag
`--unit-cache-dir /absolute/path/to/existing/directory` opts into an isolated
cache. Its diagnostics report loading, saving, continued search, and the best
native lap. A regression runs separate processes to check warm starts, continued
exploration, best-lap retention, key isolation, and corrupt-cache recovery.

## Replays and testing

UNIT races use CSRPL on both classic and CSTRK tracks. Classic tracks retain
classic physics. Recordings contain opponent number 7, the identifier `UNIT`,
and applied commands for every simulation frame. Playback, seeking, and ghosts
consume recorded commands without rerunning the planner. See
[CSRPL](csrpl-format.md).

The native tests include complete Glacier laps with normal rules and OWOOT using
the synchronous fallback, full laps under both rule modes after a human starts
from the real grid and brakes to a stop, car and transmission variants,
checkpoint rules, and planning isolation. Full-lap regressions for Branches,
Keukenhof, Ninh Binh, and DEFAULT start the human from the real grid, then brake
it to a stop. They require clean finishes, deterministic replays, and no prolonged
low-speed waits away from the human. The preserved HELENS, JOES, and SKIDS tracks
also exercise manual PMIN driving with OWOOT off. Their regressions require clean
finishes and deterministic replays; SKIDS additionally compares UNIT's time with
Skid's native lap and requires UNIT to be faster.
Additional SKIDS regressions start the player on the real grid, accelerate for
0.1 seconds, then brake to a stop. Both planner modes must finish cleanly with
verified replays after passing the parked player near the finishing hill.

The benchmark compares actual PMIN physics and checks recorded states and random
seeds against playback:

```sh
python3 tools/scripts/test-unit-opponent.py out/sdl3-linux-x64/test-unit-race stunts src/restunts/tests/fixtures/unit/glacier.cstrk stunts/DEFAULT.TRK
```

Add `--owoot` for OWOOT, or `--planner-sync` for ten-second single-threaded
preparation and bounded live planning. `--prepare-ms 0` exercises a replay
branch that starts without preparation. `--require-faster` requires both
opponents to finish and UNIT to finish sooner. The runner reports crashes
instead of treating them as completed laps. Prepared runs pace simulation by two
milliseconds per tick so the live worker receives CPU time; `--require-worker`
verifies that its proposals were actually applied to the car. Use that assertion
for interaction scenarios that require replanning; a completely verified lap can
use its cached commands throughout. The synchronous fallback runs without that
pacing. Each run reports simulation cost separately from preparation and
intentional pacing, and checks UNIT's penalties independently against the native
route rules.
