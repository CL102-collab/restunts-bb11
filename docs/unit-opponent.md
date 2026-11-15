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

Startup workers optimize the first four route pieces before the full lap. Native
builds use available CPUs within an allocation budget. At the deadline, startup
workers stop and at most one worker remains for live planning. Single-threaded
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
segments. If preparation ends mid-preview, unfinished search continues ahead of
the car with at most two native chunks per simulation frame, checking an
eight-millisecond budget between chunks. The planner worker pauses during this
continuation; only newly verified segments are appended to the cache. A changed
approach or the human player's predicted path cancels speculative work and
invalidates the affected segment. Current avoidance checks follow the cached
trajectory itself with swept car bounds; when necessary, fresh steering and
pedal commands take over.

Live rechecks reserve braking distance ahead of the required run-up, forecast a short waiting period, and validate the exact car
state at a fixed future start time. They pause the planner worker, check a
30-millisecond frame budget between chunks, and run at most eight chunks per
frame. After an exact recheck handoff, previews continue from the verified
rolling exit so the following maneuver can be planned while UNIT drives. Native
simulation never runs concurrently with live gameplay.

Route selection preserves a complete checkpoint color set. With OWOOT off, UNIT
may widen its line within a proven, flat, unobstructed route-piece footprint
without skipping a route gate. OWOOT disables those corner cuts and applies the
same wheel and jump checks used for the player. Independent native penalty and
checkpoint observers check UNIT's actual motion. Their state is restored with
replay snapshots; any accidental penalty is added to UNIT's finish time.

## Replays and testing

UNIT races use CSRPL on both classic and CSTRK tracks. Classic tracks retain
classic physics. Recordings contain opponent number 7, the identifier `UNIT`,
and applied commands for every simulation frame. Playback, seeking, and ghosts
consume recorded commands without rerunning the planner. See
[CSRPL](csrpl-format.md).

The native tests include complete Glacier laps with normal rules and OWOOT using
the synchronous fallback, full laps under both rule modes after a human starts
from the real grid and brakes to a stop, car and transmission variants,
checkpoint rules, and planning
isolation. The benchmark compares actual PMIN physics and checks recorded states
and random seeds against playback:

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
