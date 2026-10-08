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

[Dual-way switching](https://wiki.stunts.hu/wiki/Dual-way_switching)
alternatives join a prefix on one branch to a suffix on another before the
branches merge. Eligible departure and arrival tiles exclude footprints
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

## Combined search on 60 tuning tracks

The final combined search was compared with the previous release on all 60
winning-track references, using track bytes only, seed 1, and 5,000 evaluations
per case. Both versions finished all 60. The new search improved 41 and regressed
19; combined total time fell from 9,843.25 to 9,649.60 seconds (1.97%). Median
gap to the fastest human reference fell from 81.06% to 74.44%. This remains far
from consistently matching expert times. All 120 outputs passed independent
replay verification.

| Selected combined tuning case | Previous total | New total | New penalty |
| --- | ---: | ---: | ---: |
| ZCT251/FGTO | 285.10 | 222.25 | 138.00 |
| ZCT164/XCFX | 191.30 | 149.90 | 0.00 |
| 4K2020-3/DAUD | 98.45 | 58.65 | 0.00 |
| ZCT101/PMIN | 130.35 | 143.75 | 0.00 |
| ZCT279/LANC | 155.70 | 176.40 | 0.00 |
| R4K43/ZCS7 | 402.85 | 464.30 | 18.00 |

The component gains above do not simply add together: strategies compete for
a finite budget. ZCT101 illustrates this distinction; its isolated stunt change
helped, while the final combined 5,000-candidate search regressed against the
previous release. Some unrestricted native finishes also beat the supplied
human recordings through accepted shortcuts; external competition rules are
not encoded by the replay archive.

The numerical sources were frozen before reserved-track evaluation. The release
build embeds source commit `5bb5f8748d8544641178e9e11c93dbc0acc7a4a8`.
Machine-code comparison confirms that the tuning and release builds differ only
in reporting their source revision; their numerical code is identical.

## Reserved tracks: 5,000 evaluations

The frozen release was compared with the previous release on all 20 reserved
tracks, using the overall track winner as the car and replay reference. Both
versions finished every track-only and replay-assisted case. All 80 outputs
passed independent playback. No parameters were retuned from these results.

Track-only searches improved 12, tied one, and regressed seven. Combined total
time fell from 2,943.20 to 2,922.00 seconds (0.72%); median gap to human references
fell from 98.56% to 92.70%. Thus the new search improves more reserved cases than
it regresses, but autonomous performance remains well behind the expert sample.

Replay-assisted searches improved two, tied 14, and regressed four against the
previous tool. Combined time rose by 0.40 seconds, from 1,559.35 to 1,559.75.
This does not establish an overall replay-refinement gain on the reserved sample.

Times below are seconds including penalties. The seeded column uses the new
tool with the matching winning human inputs; it is not autonomous driving.

| Track | Car | Human | Previous scratch | New scratch | New seeded | New scratch penalty |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| VALDIVIA | LM02 | 100.75 | 151.65 | 151.95 | 100.30 | 0.00 |
| ZCT195 | ANSX | 88.90 | 167.40 | 175.20 | 88.90 | 0.00 |
| J | FGTO | 57.50 | 138.60 | 132.05 | 57.50 | 0.00 |
| ZCT98 | JAGU | 66.85 | 113.10 | 111.75 | 66.85 | 0.00 |
| ZCT202 | COUN | 55.85 | 126.95 | 123.70 | 55.65 | 0.00 |
| ZCT105 | LANC | 69.30 | 111.85 | 105.70 | 69.20 | 0.00 |
| ZCT132 | PC04 | 104.30 | 191.80 | 189.50 | 104.25 | 0.00 |
| FONYOD | VETT | 65.20 | 174.55 | 167.55 | 65.10 | 0.00 |
| ZCT32 | PMIN | 43.50 | 94.15 | 94.15 | 43.45 | 0.00 |
| ZCT28 | CDOR | 82.80 | 157.05 | 150.40 | 82.80 | 0.00 |
| 4K2019-8 | XCFX | 131.75 | 165.45 | 160.40 | 131.75 | 0.00 |
| MILLGAME | ZF40 | 84.25 | 118.00 | 118.50 | 84.05 | 0.00 |
| ZCT275 | ZGT3 | 83.90 | 145.30 | 141.25 | 83.70 | 0.00 |
| ZCT157 | ZLET | 81.20 | 175.10 | 174.90 | 81.20 | 0.00 |
| SZERENCS | JAGU | 73.85 | 168.60 | 178.35 | 73.85 | 0.00 |
| SHANGHAI | COUN | 65.30 | 102.15 | 107.30 | 65.30 | 0.00 |
| TORINO | LANC | 94.65 | 199.35 | 202.45 | 94.60 | 0.00 |
| ZCT80 | PMIN | 60.50 | 125.50 | 124.15 | 60.45 | 0.00 |
| ZCT42 | COUN | 69.30 | 146.70 | 158.70 | 69.15 | 0.00 |
| ZCT33 | COUN | 81.80 | 169.95 | 154.05 | 81.70 | 0.00 |

The new seeded outputs beat 12 supplied human references and match 8;
the already validated human replay remains a fallback. This does not mean
the generator independently discovered those expert routes.

## Reserved tracks: equal search time

The first four reserved cases, selected in advance, also received 60 seconds
per binary at seed 1. Paired batches ran four workers, two per binary, on the
same four-core machine with other builds and searches idle. Both versions
finished all four; all eight outputs passed independent playback. Three
improved and one regressed. Combined total fell from 543.65 to 538.25 seconds
(0.99%), while median gap to the human references rose from 68.68% to 72.06%.
The aggregate gain therefore does not imply improvement in every summary metric.

| Track | Car | Previous total | New total |
| --- | --- | ---: | ---: |
| VALDIVIA | LM02 | 143.70 | 138.30 |
| ZCT195 | ANSX | 165.60 | 172.00 |
| J | FGTO | 133.35 | 127.25 |
| ZCT98 | JAGU | 101.00 | 100.70 |

These comparisons use one fixed random seed. The default 30-minute budget was
not benchmarked in this study, and no global optimum or consistent expert-level
autonomous performance is established.

## Release validation

All seven `genghost` CTest checks passed. Native replay/checkpoint equivalence
covered 30 replay and physics combinations. Tests also cover 1,000 tile-region
cases, every DEFAULT switching selector, ordered branch touches, collision
restrictions, roll wrapping, deterministic scheduling, and analyzer selection.
Native penalty counterfactuals ran under six physics configurations: touching
the unique departure tile and confirming the other branch can finish without
penalty; omitting that touch or jumping directly to the merge earns penalties.

The Linux x64 and Windows x64 release binaries produced identical replay bytes
and deterministic report fields in six cases, with independent playback checks.
These include positive stunt and switching evaluations, plus an OWOOT search
with all three bug fixes enabled. The ARMv7 debug APK's exact packaged native
payload matched Linux on five cases under Android API 21 Bionic in QEMU.
The installable APK includes track/car controls; its physical-device UI was
not tested. No original game or car-resource files are bundled in the releases.

The downloadable validation archive contains the winner-only evidence archive,
final tuning and reserved comparisons, generated replays, validation logs,
source and binary hashes, and reproduction scripts. Rejected variants and
superseded nonwinning driving traces are excluded.

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
