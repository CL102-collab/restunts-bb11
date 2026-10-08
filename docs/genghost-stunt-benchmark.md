# Winning replay study and stunt search

This study extends the [earlier tile-shortcut work](genghost-expanded-benchmark.md).
Driving evidence now comes only from the fastest verified recording for each
exact track layout, across all cars. The score includes the game's penalties;
equal scores use archive filename order. Slower recordings are used only to
establish the ranking, not to derive driving strategies.

## Human evidence

The analyzer re-simulated 1,048 eligible solo 20 Hz recordings on 60 tuning and
20 reserved tracks; 1,044 completed. It inspected only the 60 tuning winners:
27 cars and 107,639 driving frames. All winner scores agree with independent
`repldump` playback. Fifty-nine winners have no penalty; ZCT61/CDOR wins with
56.30 seconds driving and 3 seconds penalty.

The 20 validation track hashes were reserved before inspecting their geometry
or driving traces. The split excludes 112 previously studied or reserved hashes.
Track identity uses all 1,802 embedded track bytes, not a potentially reused
track name. Car-specific physics still apply when testing a strategy.

| Observation in winning recordings | Evidence | Search implication |
| --- | --- | --- |
| Nearly straight slalom crossings | Seven swept center and wheel intersections with stone volumes; median entry 193 mph. | Try selected lateral offsets and refine the approach under legacy collisions. |
| Ground-level loop traversal | ZCT145/ZGT3 passes near local x=25–35 at about 237 mph. | Explore the returning side as well as normal loop traversal. |
| Clear corkscrew bypass | ZCT141/ZMP4's downward cork cut produces identical checkpoint playback with either collision setting. | Keep clear geometric cuts available with collision fixes. |
| Long stunt launches | 22 flights of at least two tiles, on 19 tracks; median displacement 2,483 world units. | Aim takeoff position and heading toward later tiles; evaluate landing and the complete lap. |
| Gear preservation in flight | One gear change across 882 long-flight frames; throttle held for 848 frames. | Preserve a useful gear through flight while retaining occasional shift mutations. |
| Brief highway median contact | SZOMBATH/P962 has one supported frame with two wheels on the median and two in the lane. | Test offsets near the median boundary; sustained median riding is not established by this sample. |

No winning replay sustains the measured median straddle for four frames. The
five longer examples from an earlier 160-recording analysis were slower
recordings and are excluded from this study. That earlier analysis is superseded.

Controlled native comparisons distinguish collision bugs from clear shortcuts:

- RIJAD/VETT's winning slalom crossing has identical states with `--lc:on` and
  `--lc:off` until frame 472, when fixed collisions stop it at a stone.
- ZCT145/ZGT3's highway entrance first diverges at frame 547. Its loop passage
  crashes only with fixed collisions in a checkpoint comparison at frame 849.
- RIJAD/VETT's later loop passage similarly diverges at frame 791. Checkpoint
  comparisons isolate these events from earlier collision-dependent maneuvers.
- ZCT141/ZMP4's left/right corkscrew depends on legacy collisions; its separate
  downward-corkscrew ground bypass does not.

Checkpoint diagnostics change a rule only to identify its mechanism. Generated
replays always run with one requested rule set from start to finish. High speed
alone is not proof of power gear, particularly for custom cars. Grass contact,
collision outcomes, OWOOT validity, and penalties remain native simulation results.

## Refinement and budget allocation

Dual-way alternatives join a prefix on one branch to a suffix on another before
the branches merge. Eligible departure and arrival tiles exclude footprints
shared by both branches. Ordered tile targets protect the unique departure
touch and the subsequent confirmation elements, including confirmation on the
common suffix. The actual finish plane is retained.

All eligible departure/arrival pairs are represented by implicit selectors;
the search does not allocate a population for each pair or permanently keep
only a short geometric shortlist. Shortest estimated and nearest crossings
provide initial proposals, followed by pair mutations. Switches in separate
branch sections can combine. A finite budget need not visit every selector.
The independent switching population activates after the first canonically
validated finish, retaining the ordinary road and tile populations and their
saved parents. OWOOT omits cross-country switching; the three bug switches
do not disable this penalty-tracker behavior.

A native test covers all 6,164 DEFAULT departure/arrival selectors. It also
checks that a valid branch touch and re-entry differ from skipping the unique
touch or jumping straight to the merge. Some selectors still earn penalties;
the optimizer never assigns them an assumed zero penalty. In a seven-case
component screen at 5,000 evaluations, three improved, three tied, and one
regressed by 0.70 seconds. All 14 outputs completed and passed independent
playback; combined time fell from 742.85 to 726.35 seconds (2.22%). DEFAULT/COUN
improved from 116.40 to 99.70 seconds. The comparator already used adaptive
allocation, so these are separate component results rather than a final combined
or unseen-track result.

After a fully validated lap exists, the alternative driving population can
mutate stunt tactics over a few route segments. Initial policies retain their
ordinary behavior. Proposals include slalom offsets, loop backside lines,
corkscrew ground cuts, left/right edges, highway median offsets, and launches
from rising geometry toward later tiles. Each car and requested rule set is
evaluated by the real simulation; these are attempts, not assumed successes.
OWOOT retains its road-constrained controller. Intentional ground-level solid
crossings through loops or slalom stones require legacy collisions, while
clear corkscrew cuts remain candidates with fixed collisions.

Keeping changes local and waiting for the first validated finish preserves a
usable fallback. It also means these stunt mutations do not help initial lap
discovery on a track the ordinary search cannot finish. Broader profiles that
changed every stunt together were rejected after losing completed laps.

In a component comparison on 12 selected winning-track references, at 5,000
evaluations, five improved, five tied, and two slightly regressed. Both versions
finished all 12; combined total time fell from 2,176.55 to 2,044.05 seconds
(6.09%). The comparator already included the roll and allocation changes below.
All generated outputs passed independent playback.

| Selected tuning case | Before total | Local stunt total | Interpretation |
| --- | ---: | ---: | --- |
| ZCT251/FGTO | 285.80 | 222.25 | Driving falls from 261.80 to 84.25 s, while penalty rises from 24 to 138 s. |
| ZCT101/PMIN | 143.75 | 126.55 | Zero penalty; path falls from 88,383 to 85,809 world units and a loop traversal peaks below its crown. |
| RIJAD/VETT | 194.10 | 182.30 | Both have zero penalty. |
| ZCT280/PMIN | 128.90 | 125.55 | Both have zero penalty. |

These outcomes do not establish successful autonomous execution of every
proposed bug. In particular, a geometry proposal or a tactic-use counter alone
does not prove slalom penetration, wall tunneling, or sustained median riding.

Trajectory matching now includes wrapped roll error alongside position, yaw,
pitch, and speed. This distinguishes nearby banked or inverted samples during
stunts. It preserves demonstrated airborne controls rather than assuming that
steering can redirect an airborne car.

Both road-following and shortcut policies remain part of the default search.
After every route has received its initial policies in both families, three of
four full route sweeps favor the family with the faster completed score. The
remaining sweep rotates through the other active families. Before any completed
score exists, the original two-road-to-one-shortcut schedule continues. Separate
random streams and full route sweeps preserve each family's candidate order.
Human-seeded and OWOOT searches retain their existing single-family allocation.

The allocation change was tested separately on 23 earlier tuning cases with
two random seeds and 5,000 evaluations each. It produced 34 faster, eight tied,
and three slower matched finishes, with no lost completions. Combined time fell
from 6,996.40 to 6,934.80 seconds (0.88%). All 90 completed comparison replays
passed independent playback. This is tuning evidence, not unseen-track validation.

Roll-aware matching was separately tested on the first 12 newly selected
overall track winners at seed 2 and 5,000 evaluations. Two improved, ten tied,
and none regressed; combined time fell from 924.15 to 921.50 seconds. All 24
baseline and candidate outputs passed independent playback.

An earlier roll experiment used car-balanced references before the winner-only
instruction. Its apparent 28.05-second gain was dominated by a 26.90-second
native start/finish shortcut. It is not evidence that roll matching alone
improved ordinary driving by that amount. Event-weighted mutation and global
stunt profiles were rejected after regressions in broader tuning samples.

## Reproduction

`tools/scripts/analyze-genghost-stunts.py` groups candidates by exact track,
freshly verifies their native scores, and analyzes one overall winner per track.
Its `--prepare-only` mode establishes references without reading driving traces.
Engine, archive, and resource hashes protect cached rankings; analyzer hashes
protect cached telemetry. `analyze-genghost-replays.py` also selects and traces
only overall track winners.

Coordinate tests cover opposite object/terrain row order, multi-tile origins,
rotation, swept stone intersections, and winner selection including penalties,
cars, unfinished recordings, and ties. All 6,546 initialized route pieces from
the 60 tuning tracks agree with the static geometry extractor. The roll test
also covers angular wrapping at 1023/0 and distinguishes banked samples.
