# Expanded replay analysis and tile shortcuts

This follow-up compares the tile-aware search with `4e8d398`, the previous
expert-trajectory optimizer. The golden replay archive is unchanged.
Track-only searches receive track bytes, car resources, and the game rules;
replay-assisted searches additionally receive a compatible human recording.
Those results are reported separately.

## Human evidence

The analyzer re-simulated 6,841 complete solo 20 Hz recordings from the 6,907
recordings in the archive. Of these, 6,802 finished under the current default
physics, spanning 555 exact track hashes and 58 car codes. The other 39 were
excluded from reference selection. Scores use the simulated finish and penalty
frames, not recording length.

Before examining driving traces, a deterministic hash split reserved 16 new
tracks for validation. Tracks from the earlier 16-map benchmark were excluded
from both groups. Detailed analysis used 207 completed best, median, and slow
recordings across 80 other tracks and 54 cars. All 207 independent `repldump`
traces agreed with the generator's score, covering 414,647 driving frames.
The optimizer's tuning experiments used 23 of those 80 tracks.

- Faster humans drove shorter horizontal paths in 52 of 66 comparable
  track/car pairs. The median reduction across all 66 pairs was 1.085%.
- In 151 of 206 zero-penalty recordings, all four wheels were on grass at
  some point. Fully grass time had a median of 2.175 seconds and a maximum
  of 27.50 seconds. Grass excursions can also be mistakes.
- The sample contained 1,708 zero-penalty road-contact tile visits whose car
  center stayed outside the tile's inscribed circle. These corner grazes
  lasted a median of two frames; their deepest tile inset had a median of
  27.83 world units in a 1,024-unit tile. Such visits demonstrate that brief
  corner touches can coexist with valid laps; they do not establish that
  every visited tile was mandatory.
- ZCT130/ZLET's faster recording saved 48.60 seconds with a 39.87% shorter
  path and 63 rather than 103 visited tiles; both recordings had zero penalty.
- Distance alone does not decide the winner. CCCT002/VETT's best recording
  took 80.85 rather than 155.45 seconds despite a 3.23% longer path. It first
  exceeded 220 mph at 26.80 seconds and spent 25.40 seconds above that speed
  with all four wheels on grass, without a penalty.

These are observational comparisons. Players, transmission settings, routes,
and skill differ. Numeric changes in confirmed route indices cannot count
skipped pieces reliably because indices also encode graph branches. Exceeding
220 mph alone does not prove power gear, particularly for custom cars.

## Algorithm change

The game's ordinary penalty tracker recognizes the car center within the
whole tile footprint, including multi-tile pieces. It does not require contact
with the painted road. On an ordinary unbranched sequence, reaching the third
successor can confirm progress without charging for the two intervening pieces.
Branches, reconfirmation, and the finish have additional rules. The optimizer
therefore proposes shortcuts and measures their actual completed score using
the engine; it does not assign a geometric shortcut a presumed zero penalty.

For eligible flat road pieces, the new planner minimizes the local path through
an inset tile rectangle toward a later tile. An exact rectangle crossing
calculation replaces iterative edge sampling. The inset increases with speed
to allow the discrete 20 Hz simulation to register brief contact. Braking uses
the proposed line's curvature and an evolvable speed scale. The controller
allows grass crossings within the planned tile corridor and restores ordinary
recovery outside it. Stunts, elevated geometry, and the finish retain their
existing guidance. Road-following and power-gear entry remain alternatives.
The native simulator still supplies grass grip, collision outcomes, and penalties.

Road and tile policies use separate populations, random streams, and replay
parents. They share one time/evaluation budget and the best saved replay. The
scheduler runs two road rounds followed by one tile round. Completed routes
receive small refinement batches, so this does not imply an exact two-thirds
candidate split. A verified human seed and OWOOT retain the original single
search schedule and random sequence.

OWOOT keeps the road-constrained controller. Saved candidates are independently
re-simulated from the initial state before they replace the current best replay.
This remains a local geometric heuristic with input refinement, not an exact
global shortest-path solver or a guarantee of human-level performance.

## Tuning results and rejected approaches

At 5,000 evaluations and seed 1, the final numerical algorithm finished 22 of
23 tuning tracks, matching the baseline. It improved seven completed laps,
regressed 14, and tied one. Combined completed times fell from 3,422.55 to
3,370.50 seconds (1.52%), so several larger gains outweighed many small losses.
The unfinished case was ZCT106/ANSX for both versions. All 22 new finishes had
zero penalty; the baseline's 10DRAIN/50CI included 18 seconds of penalty.

| Track | Car | Baseline total | New total | Interpretation |
| --- | --- | ---: | ---: | --- |
| ZCT24 | AUDI | 97.05 | 91.25 | Zero penalty; path 0.41% shorter. |
| CCCT002 | VETT | 208.65 | 198.75 | Zero penalty; almost identical distance. |
| 10DRAIN | 50CI | 171.55 | 141.70 | Removes 18 s penalty and saves 11.85 s driving. |
| CCCT04R3 | BA70 | 140.55 | 122.70 | Zero penalty; path 0.95% longer. |
| ZCT262 | DC70 | 174.35 | 164.25 | Zero penalty; path 8.07% shorter. |

These selected tuning gains are not the reserved-track validation. They also
show why shorter distance, faster driving, and penalty avoidance must be
measured separately. The remaining track-only gap to expert recordings is
substantial; this is not evidence of near-perfect autonomous driving.

Several alternatives were rejected: replacing the existing shortcut genes with
tile semantics, mixing all policies into one small population, and extra
completed-sector policy repair. They helped selected cases but regressed the
broader tuning sample. Separate corner speed control prevents the fast tile
seed profiles from also demanding excessive speed through their turns.

## Reproducing the analysis

```sh
python3 tools/scripts/analyze-genghost-replays.py \
  --binary build/genghost-linux/genghost \
  --repldump build/genghost-linux/repldump --data-dir stunts \
  --car-assets tools/scripts/cars --output build/expanded-human

python3 tools/scripts/benchmark-genghost.py \
  --binary build/genghost-linux/genghost \
  --repldump build/genghost-linux/repldump --data-dir stunts \
  --suite build/expanded-human/validation-manifest.json \
  --evaluations 5000 --seed 1 --jobs 2 \
  --output build/expanded-validation --label current
```

The analyzer selects cases by exact track bytes and car, preserves existing
installed resources, and records engine, replay-driver, archive, and resource
hashes. Its cached rankings are reusable only with matching provenance.
The benchmark re-ranks compatible human references under the selected physics
and independently verifies every completed generated replay. A different
resource set or physics configuration can change which recordings finish.
