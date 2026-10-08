# DEFAULT: Ayrton's winning replay and horizontal corkscrew cuts

This follow-up studies the fastest successfully simulated replay for the exact
DEFAULT track and tests generalized changes to the offline search. It follows
the [earlier 60-track study](genghost-stunt-benchmark.md); results from that
older release are not measurements of the updated executable.

## Reference and method

[ZakStunts ZCT079](https://zak.stunts.hu/tracks/ZCT079) used the byte-identical
DEFAULT layout and lists Ayrton's winning PMIN time as **31.25 seconds**. The
user supplied `01ayrton.rpl`, which is byte-identical to `0722.rpl` in the golden
archive (SHA-256 `4fc941ba9d1fcce38d8567557c4c8aacffc5e7e9e33fbd6528c09ecf13adbe05`).
The native simulator and independent `repldump` playback both confirm 625 driving
frames, zero penalty, and a 31.25-second total.

All 18 golden recordings containing these exact track bytes were freshly scored.
Only the fastest successful recording was analyzed for driving tactics. The
track's SHA-256 is
`4111e30379c39020d10f30eef15b7e46aca87a7716e499cde2e89c7c545388fd`.

## What the winner does

Ayrton does not enter the horizontal corkscrew, slalom, or vertical loop. He
builds speed on the opening straight, triggers power gear on the first terrain
hill, and crosses the grass between branches. The opening near-full left circle
is observed, but this study does not establish that it is necessary; no
track-specific opening script was added.

The important power-gear setup happens **before takeoff**. At input 261 the car
starts shifting from third gear, reaching fourth at 262 and fifth at 266 while
all four wheels still touch the hill. The first divergence with the power-gear
fix occurs at 272; the car does not become airborne until 278. Its speed grows
from about 148 mph at frame 271 to 205 mph on takeoff, then roughly 245 mph after
landing. Delaying the upshift until airborne misses this boost and breaks the
rest of the unchanged replay. The launch is on a terrain slope with an ordinary
road model, so limiting early shifts to stunt ramps misses it.

On the alternate branch the car confirms private pieces 60, 61, and 62. It then
crosses the map and briefly touches pieces 55 and 56 on the other branch before
confirming the common merge at 57. These piece numbers describe the reference
only; the algorithm uses branch metadata. The native confirmed-piece tracker
stays at 62 until the merge, so those brief geometric touches do not justify
relaxing the existing rules for shared split/merge tiles.

With collision fixes enabled, the winning replay has exactly the same state
trajectory and score. Disabling power gear or left-corner bias breaks it;
OWOOT rejects it early. These counterfactuals distinguish a clear geometric
shortcut and a power-gear launch from an assumed collision exploit.

## Changes derived from the replay

The controller now predicts whether a grounded uphill upshift can trigger power
gear. It uses each car's gear-knob travel to estimate clutch time and coasting
loss, then evaluates its top-gear torque, drag, gravity, and native mass division.
Checking the force at the current speed alone shifted too early in the reference
and lost the boost; forecasting clutch loss fixes that timing. Replacing only
Ayrton's shift decisions during frames 240–299 with the new controller reproduces
all 645 serialized replay states exactly. This is a prefix/controller test,
not an independently discovered 31.25-second lap. `--pg:off` disables this cue.

Dual-way initialization retains the shortest and nearest crossings and adds
pairs selected near the beginning of the departure branch and near the end of
the arrival branch. The private endpoint ranks come from each branch's own
metadata, and all eligible pairs remain reachable through mutation. The
winning reference's long cross-country connector is consequently represented
among initial proposals without hard-coded track identifiers.

The horizontal corkscrew also has a default right-side cut when OWOOT is off.
A centered entry moves right gradually to clear the red wall using the car's
width. If six initial cut profiles cannot finish, the search starts an
independent ordinary-traversal lineage, preventing failed cut parents from
crowding out every viable fallback. Both lines remain eligible for full-lap
refinement. OWOOT retains normal traversal; collision fixes do not prohibit a
clear path around the wall.

## Controlled DEFAULT comparisons

Each row uses seed 1, manual PMIN, native default physics, and the same fixed
candidate budget. Track-only runs receive no human inputs. The starting point
here already contains the corkscrew cut and ordinary-lineage recovery.

| Search variant | 5,000 candidates | 10,000 candidates |
| --- | ---: | ---: |
| Corkscrew/recovery starting point | 77.90 s | 76.45 s |
| Grounded uphill shift only | 74.70 s | 72.10 s |
| Early/late branch endpoints only | 72.40 s | 70.90 s |
| Both changes | 72.40 s | 70.90 s |

All listed results finish with zero penalty. The combined result is faster,
but the two gains are not additive, and autonomous discovery remains far from
Ayrton's 31.25 seconds. Matching a demonstrated gear change is not evidence that
the search has discovered the full winning strategy.

The existing input-refinement algorithm separately improved the supplied human
replay from **31.25 to 31.05 seconds**, verified by both playback paths. Its first
changed input is 329, after the inherited hill launch; it shortens the later
line by about 374 world units and reaches the merge five frames earlier. This
result predates the new controller changes and must not be attributed to them
or described as track-only performance.

## Other tracks and physics settings

This comparison uses the previous distributed numerical build `5bb5f874` as
the baseline and includes all changes described above. Every case uses 5,000
candidates; DEFAULT tests three seeds plus a second car and physics variants.
The remaining tracks are the first four with a horizontal corkscrew in the
previous 60-winner manifest and the first two without one, selected without
looking at candidate performance. Cached baseline results use identical input
tracks, car resources, seeds, and flags. Both native playback paths independently
verify every output.

| Case | Baseline seconds | Current seconds | Change |
| --- | ---: | ---: | ---: |
| DEFAULT-PMIN-s1 | 79.2 | 72.4 | -6.80 |
| DEFAULT-PMIN-s2 | 81.95 | 73.1 | -8.85 |
| DEFAULT-PMIN-s3 | 69.75 | 73.5 | +3.75 |
| DEFAULT-COUN-s1 | 102.5 | 108.45 | +5.95 |
| DEFAULT-PMIN-fixed-s1 | 81.65 | 77.9 | -3.75 |
| DEFAULT-PMIN-owoot-s1 | 79.35 | 78.75 | -0.60 |
| ZCT206-FGTO | 355.0 | 341.9 | -13.10 |
| FTT0111-PMIN | 7.45 | 8.35 | +0.90 |
| ZCT75-ANSX | 72.35 | 71.15 | -1.20 |
| ZCT141-ZMP4 | 273.95 | 272.05 | -1.90 |
| ZCT261-VETT | 102.95 | 118.5 | +15.55 |
| R4K8-ZPFT | 112.75 | 112.75 | +0.00 |

Completed 12/12 baseline and 12/12 candidate; 7 faster, 1 tied, 4 slower. Comparable totals: 1418.85→1408.80 seconds.


“Fixed” means `--pg:off --lc:off --lcb:off`, without OWOOT. The OWOOT row
retains the default bug switches. The three unrestricted DEFAULT/PMIN outputs
avoid the corkscrew branch completely. COUN and fixed-physics PMIN use the low
right-side passage, while OWOOT traverses the full corkscrew and independently
passes the game's OWOOT check. The grounded hill shift can also be legal with
OWOOT, so its resulting lap need not remain byte-identical to the older driver.

The small aggregate gain does not establish a general improvement: four cases
regress, including ZCT261/VETT by 15.55 seconds. All 12 still finish. These
fixed-candidate runs do not measure the default 30-minute search budget or
prove optimality.

A longer autonomous run with 100,000 candidates reaches **64.95 seconds**, zero
penalty, independently verified. It skips the entire corkscrew branch but peaks
at only 173.56 mph: it has not discovered Ayrton's sustained 240–245 mph strategy.
A fresh 10,000-candidate refinement of the attached replay also reaches
**31.05 seconds**, zero penalty, with the combined search. These two outputs
are distributed separately as `DEFPMIN.RPL` (track-only) and `AYR31.RPL`
(human-seeded). Their JSON reports identify the input mode and candidate budget.

During refinement, a post-finish reporting error was also found: native playback
can accumulate additional live penalties while the car brakes after the score
has been latched. Completed reports now derive the counted penalty from that
latched finish score. Candidate ranking already used the correct total; the
correction prevents a misleading log such as 31.10 seconds total plus an apparent
171-second penalty accrued after finishing. A regression preserves the actual
native tail behavior and checks that the reported counted penalty remains zero.
