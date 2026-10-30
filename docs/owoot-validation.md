# OWOOT validation notes

The road query uses the high-detail GAME1/GAME2 road polygons, independent of
camera, clipping, detail settings, and the original collision approximation.
It retains polygon height, clips the complete tire volume against each road
plane, and tests their horizontal overlap. The twelve-unit vertical allowance
applies only to tires with a current paved, dirt, or ice contact, matching the
physics contact tolerance. Airborne tires
have no vertical allowance. Large-turn curb materials 127 and 128 are
excluded; road materials 19, 22, and 23 include their dirt/ice paint variants.

`tools/scripts/generate-owoot-road-geometry.py` regenerates the far-data triangle
table from the original compressed resources. The table records their SHA256
hashes. Its Python decompression output was compared byte-for-byte with the
production C decompressor for both banks.

## Golden replay corpus

The audit generated original physics dumps for all 1,591 `r*` replays in
`tools/scripts/rpls_golden/replays.zip`: 3,761,045 frame states across 90
embedded tracks. The archive SHA256 is
`1424322864468584210cda8dbba034b5a68157758b55a2db6e7b889e8938c888`.

The final audit found **1,194 replays with no additional violation** and
**397 rejection candidates**, with no process errors. The first rejection is
loss of road coverage in 239 replays and a stunt-route violation in 158:
103 slalom, 41 tunnel, seven corkscrew up/down, six loop, and one corkscrew
left/right. Independent checks over the remaining original trajectory flag
238 replays for route violations and 288 for road coverage; these overlap.

The [per-replay CSV](owoot-replay-audit.csv) records the first rejection frame,
original outcome, car, and track hash. A blank first-frame field means no
additional violation was detected. The [audit metadata](owoot-replay-audit.json)
records source/resource hashes, category counts, and the actual DOS results.

Without OWOOT, 1,587 replays finish, R0933 and R1095 end in collisions, and
R0367 and R1091 end without a finish or crash. A normal finish uses event 3;
it must not be counted as a crash.

The requested rule uses actual tires in world coordinates. Historical OWOOT
competitions have used different tests, including contact by any part of the
car in a prescribed camera view. Consequently an archived OWOOT replay is
useful evidence, but not an unconditional passing fixture for this stricter
tire check. See the [OWOOT variants](https://wiki.stunts.hu/wiki/OWOOT) and
[Race For Kicks rules](https://wiki.stunts.hu/wiki/Race_For_Kicks#Rules).

The track metadata contains 212 directed gaps between connected elevated road
ends, including elevated corkscrew ends. All span exactly one tile; no longer
aligned gap was found. The full replay scan found 2,131 airborne intervals
crossing non-road tiles. Only R0539, R0541, R0547, and R0548 visit multiple
non-road tiles in one flight, but all four land on grass after leaving the
connected road corridor. R0539 leaves a continuous bridge; the other three
miss the receiving ramp. None demonstrates a completed longer ramp-to-ramp
jump. The implemented exception therefore covers the whole single intervening
tile between paired raised road ends, as the wiki
[grass-square exception](https://wiki.stunts.hu/wiki/OWOOT) describes. It does
not impose an imaginary road-width limit inside that exempt tile.

The audit checks the production OWOOT rules against the original frame states.
After a first violation, further diagnostics describe the original replay
trajectory; actual `--owoot` playback has already crashed and will diverge.
The final DOS checks below independently verify first-violation behavior.
Uninvestigated rejections remain review candidates; this audit does not certify
that every flagged replay is invalid.

## Investigated replay boundaries

These are observations from the archived physics states, not a presumption
that every saved replay satisfies the requested rules. Frame numbers below are
one-based dump records.

- **R0011, frame 258:** the car is between ramp centres x=19968 and x=22016,
  crossing x=20528 at z=16920. An unrelated pipe entrance on the intervening
  tile has no road beneath its tires. This is a ramp-gap exemption case; the
  road query correctly has no overlap and the jump query must allow the gap.
- **R0019, frame 474:** the car is on the half-pipe tile centred at
  (12800, 6656), orientation 256. After rotation into tile coordinates all tire
  vertices are inside x=41..73, z=-47..23, y=109..148. The half-pipe floor has
  an opening where |x|<84 and |z|<75. Its roof above these x values lies between
  y=204 and y=235; even the highest tire vertex plus the twelve-unit allowance
  is below it. Thus there is no road at or below any part of any tire. The car
  came from the pipe entrance and remains inside the half-pipe tile, rather
  than crossing a gap between ramps or bridges. Under the requested literal
  containment rule this frame is outside the road. A regression fixture also
  tests valid coverage just beyond the opening and at roof height.
- **R1561, frame 1950:** all four recorded wheel contact surfaces are grass on
  sharp-corner tile 07 centred at (6656, 3584). Its inner middle edge runs
  between local vertices (-172, -317) and (-315, -173), so the road-side
  half-plane is `144*x + 143*z + 70099 >= 0`. The maximum expression among the
  four tire hulls is respectively -6563, -680, -218, and -6101. Every vertex
  lies strictly on the grass side; the closest tire is about 1.07 world units
  away. This is an actual complete-wheel departure at the sharp corner,
  rather than a curb classification or tolerance failure.
- **R0546, frame 153:** the car is banked beyond the right edge of a bridge
  ramp. Its tires have world x >= 12929 while the road ends at x=12920. The
  original collision approximation still reports one paved contact; the
  rendered tire geometry is entirely outside the road.
- **R1362, frame 911:** at the corkscrew left/right exit, transform to local
  coordinates with `x=2560-world_z`, `z=world_x-24576`. The diagonal road edge
  from (84,-931) to (115,-838) has road-side equation `z-3*x+1183 >= 0`.
  The maximum values over the four tires are -100, -47, -3, and -56. The
  nearest tire is approximately 0.95 world units outside the polygon, despite
  the physics approximation reporting a paved contact.

- **R0768, frame 1017:** the car passes straight through a slalom at
  z=19969, close to the road centre z=19968. Its tire reaches z=19993
  (local lateral coordinate 25), while the barrier begins at local 23.
  This two-unit overlap is missed by the original wheel-centre collision
  approximation. All tires are on asphalt, but the slalom clearance check
  rejects the barrier crossing.

## Stunt checks

Progress follows ordered, swept gates through the intended stunt route. Loops
and corkscrews require their complete sequence; pipe and tunnel crossings must
intersect the portal interior with the tire actually crossing the shared
tile-boundary plane. This uses the logical entrance, rather than the inward
AI guidance points or the two-unit overlap between rendered shells. Touching
only an exterior wall or roof is insufficient. Slaloms require the
intended alternating sides, with the swept tire/body envelope clear of the
barrier footprints even when airborne.

The Barrel roll ramp requires genuine tire contact with its twisting grille,
forward travel through its bounded flight corridor, and ordered airborne roll
milestones: sideways, inverted, the opposite side and back upright. The car must
complete these before touchdown and land wheels down; reaching the receiving
road cannot excuse an incomplete roll. Reversing cannot collect
roll progress, and supported rotation cannot finish the stunt. Progress is part
of the serialized car state, so replay seeking restores the same requirement.

Route tests cover reversed traversal, backtracking, checkpoint restoration,
skipped stunts, exact portal boundaries, and invalid cross-wheel/angled-wheel
portal shortcuts. Jump tests cover connected approaches, reverse direction,
multi-tile receiving elements, unrelated lower roads, and contact at both
ends of a gap.

## Geometry checks

`src/restunts/tests/test-owoot-road.c` covers partial tire overlap, nearby
non-overlap, airborne coverage, grass inside chicanes, excluded large-turn
curbs, isolated bridges approached from underneath, ramp and hill heights,
raised terrain, rotated roads, both levels of an overpass, half-pipe floor
openings, 3D clipping, and coverage of every generated road triangle. Native
builds with warnings as errors and AddressSanitizer/UndefinedBehaviorSanitizer
pass. Regenerating the road table to a separate output file reproduces the
checked-in header byte for byte. Pipe portal tests reject roof riding and
exterior corner paths. Slalom tests cover swept collisions between frames,
rotated and angled passes, clear-side travel, and airborne obstacle skipping.

## DOS integration checks

The DOS game, physics dump tool, and renderer dump tool build with Open Watcom.
Seventeen physics dumps with OWOOT disabled match the independent original
Borland executable byte for byte. With the switch, R0000, R0187, R0249, R0262,
R0377, R1105, R1250, and R1509 finish without a crash. The nine selected
violations crash at exactly the frames predicted by the native audit:

| Replay | First crash frame |
| --- | ---: |
| R0019 | 474 |
| R0153 | 1872 |
| R0224 | 352 |
| R0448 | 664 |
| R0546 | 153 |
| R0768 | 1017 |
| R1362 | 911 |
| R1411 | 915 |
| R1561 | 1950 |

Before each crash, all state bytes other than the reserved OWOOT progress
fields match the original. The default renderer snapshot for R0019, camera 3,
frame 474 also matches the original BMP byte for byte. DOSBox-X used dynamic
core and maximum cycles and was terminated with SIGKILL after each batch.

All 66 host regression tests pass. An additional simulation setup
check verifies that race initialization clears OWOOT progress and that the
real checkpoint restore path recovers saved stunt and jump progress.
