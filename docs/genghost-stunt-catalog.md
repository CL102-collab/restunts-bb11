# Stunt tactics from a new set of track winners

The default search now includes a collision-aware catalog for **24 physical
stunt variants**, with an explicit preferred recipe for each collision setting
and another geometric line to refine. No runtime switch enables this catalog:
it is part of ordinary offline search. Complete native playback, including
penalties and the selected physics fixes, decides whether a proposal is useful.
OWOOT retains constrained traversal and disables off-road cuts. A physically
clear slalom center passage remains available because it satisfies the road rule.

## Human evidence

All 777 eligible golden recordings on 52 previously unused exact track layouts
were scored afresh. Only the fastest successfully verified recording per tuning
track, across cars, supplied driving evidence. The study analyzed **40 new
winners, 68,678 driving frames, and 22 cars**. Twelve tracks were reserved before
telemetry analysis. Previous tuning/validation layouts and DEFAULT were excluded.
The reference binary is numerical commit `7ec6e9fd`; game resources were hashed
and kept unchanged. Every analyzed winner has zero penalty.

The study found several useful patterns:

- ZCT110/PMIN bypasses ramps, a solid ramp, elevated road, a pipe entrance, and
  a half-pipe on the ground, and flattens a chicane at roughly 241–245 mph.
  Its full replay is byte-identical with collision bugs disabled.
- R4K29/LOLA uses a diagonal across the other chicane variant at 192–197 mph,
  avoiding the painted S-shaped line. Collision fixes leave that passage unchanged.
- ZCT227/ANSX passes outside the horizontal corkscrew without rolling and only
  touches a tunnel tile's outside corner for five frames at roughly 192–196 mph.
  The complete replay remains identical with collision fixes.
- Useful ramps can instead produce long launches: ZCT110/PMIN travels 3.56 tiles
  during 43 airborne frames after a ramp. The surrounding track determines whether
  a launch or a ground bypass is better.
- Local checkpoint counterfactuals confirm collision-dependent passages through
  slalom stones, a loop's returning side, and vertical corkscrew walls. Highway
  median riding can also work unchanged with fixes; it is not automatically a bug.

A whole replay failing after a physics change does not identify every later stunt
as an exploit. The study therefore also compared identical native checkpoints
with collision bugs enabled and disabled. Clear human examples exist for every
catalog model. There is no evidence of a distinct advantageous collision bug for
every model: where a clear shortcut works in both regimes, both entries may use
it instead of inventing a wall-crossing trick.

## Built-in recipes

These are starting proposals, not assertions that one path is fastest for every
track and car. They use native object orientation, current height and contact,
car dimensions, and route entry/exit geometry. An outside route can skip the
intended stunt entirely; native route confirmation decides whether the tile
visit or skipped piece earns a penalty.

| Stunt variant | Collision bugs enabled (`--lc:on`) | Collision bugs disabled (`--lc:off`) | Additional line to refine |
| --- | --- | --- | --- |
| Chicane right/left | chicane chord | chicane chord | chord variation |
| Chicane left/right | chicane chord | chicane chord | chord variation |
| Highway entrance | median approach | clear inner lane | outside bypass |
| Highway | two-wheel median ride | two-wheel median ride | outside bypass |
| Ramp | outside ramp bypass | outside ramp bypass | launch toward later tiles |
| Solid ramp | outside solid ramp bypass | outside solid ramp bypass | launch toward later tiles |
| Elevated road | low underpass or deck exit | low underpass or deck exit | launch toward later tiles |
| Elevated span | low underpass or deck exit | low underpass or deck exit | launch toward later tiles |
| Solid road | solid deck edge or outside bypass | solid deck edge or outside bypass | outside bypass |
| Elevated corner | low chord or inside deck line | low chord or inside deck line | launch toward later tiles |
| Overpass | low crossing or deck exit | low crossing or deck exit | outside bypass |
| Bank entrance B | low bank entry | low bank entry | outside bypass |
| Bank entrance A | low bank entry | low bank entry | outside bypass |
| Banked road | low bank edge | low bank edge | outside bypass |
| Banked corner | inside bank chord | inside bank chord | lower edge |
| Loop | loop ground crossing | outside loop bypass | launch toward later tiles |
| Tunnel | straight tunnel passage | straight tunnel passage | outside bypass |
| Pipe entrance | pipe floor entry | pipe floor entry | outside bypass |
| Pipe | pipe floor chord | pipe floor chord | outside bypass |
| Half pipe | outside half-pipe bypass | outside half-pipe bypass | straight floor passage |
| Vertical corkscrew up | corkscrew wall crossing | outside vertical corkscrew | launch toward later tiles |
| Vertical corkscrew down | corkscrew wall crossing | outside vertical corkscrew | launch toward later tiles |
| Slalom | precise center passage | precise center passage if the car fits | outside bypass; real airborne continuation in state search |
| Horizontal corkscrew | right of red wall | right of red wall | outside bypass |

Ground crossings through loop/corkscrew walls or slalom stones are selected only
with original collision behavior. The slalom center passage goes between the
solids and is distinct from stone penetration; fixed collisions permit it when
the actual car envelope fits. Other fixed-collision alternatives stay outside the
relevant solids. Tunnel/pipe recipes prepare their entrance before reaching the
wall and decline a late sideways crossing. A grounded car uses an outside line
around a solid deck; an elevated car can keep a lower edge or launch from a real
exit. Curvature limits remain active for banked and elevated turns. Lateral and
speed genes refine clearance and approach, while ordinary traversal remains
available throughout.

## Default search integration

After ordinary initial profiles, each population funds one localized catalog
probe between groups of four ordinary attempts. It changes the approach and
one stunt's policy segments, preserving the parent's recorded input prefix with
native checkpoints. It does not need a human seed or a completed lap first.
Failed probes retain the ordinary replay parent; a verified finish can replace
it. Regions that were not reached can be retried when the ordinary parent makes
progress, with a bounded retry count. Broader local mutations then refine the
new lines alongside the previous tactics.

The full-lap objective remains driving time plus the game's counted penalty.
A shorter sector that crashes later, violates OWOOT, or produces a worse final
score does not become the saved ghost. Airborne targets prepare the landing
line; they do not create midair steering or change game physics.

## Validation

Detailed native and whole-lap results are recorded with frozen binaries, source
snapshots, track/resource hashes, and independently replayed outputs in the
stunt-catalog validation archive. Local element exits and whole-lap finishes
are separate measurements; neither proves global optimality.

Native tests cover every catalog model at winning replay checkpoints, both
collision settings, extreme lateral offsets, full planner state immutability,
and OWOOT restrictions, including the legal slalom center exception. Tunnel/pipe entry and low solid-deck guards are tested
under four rotations and both traversal directions. Analyzer tests compare all
24 accepted hill substitutions with the native tables so a ramp converted by
hill terrain is not misclassified as the raw track element.

In separate native continuation probes, **163 of 228 attempts** crossed the
selected element's exit plane without a crash or additional penalty, with that
specific element's tactic active. Every one of the 24 models has a successful
primary-recipe exit under both collision settings. These runs start from human
checkpoints, retain failed attempts in the evidence, and measure local feasibility;
they do not establish autonomous whole-lap performance or the fastest possible
maneuver. One fixed-collision downward-corkscrew bypass fails from a late approach
but works from an earlier checkpoint, illustrating why approach space matters.

The checkpoint-backed search was also compared with fresh full playback for
226 localized probes: every result field and the full native state matched.
A whole-track regression exposed failed probes contaminating ordinary policy
selection. Failed probes now leave both its population and replay parent intact,
and random catalog mutations require a verified finish. Deterministic probes
still run before a first finish. A map-only R4K9/GATE regression with collision
fixes verifies that ordinary completion remains discoverable within 100 attempts.

### Fixed-budget whole-lap comparison

The final numerical change is commit `ed498d8ca100a93a44b6a09af42841b7302359c9`.
The benchmark plan was frozen before inspecting candidate performance. It uses
six reserved tracks and six tuning tracks, each with collision bugs enabled and
disabled, at 3,000 candidates and seed 1. Three DEFAULT/PMIN cases use 5,000
candidates. All searches start from track files without human replay inputs.
The baseline is `7ec6e9fd`. Every finish was checked with fresh native verification
and independent frozen `repldump` playback using the same physics settings.

| Group | Finishes before / after | Faster / tied / slower | Total scored time before / after |
| --- | --- | --- | --- |
| Reserved tracks, 12 cases | 10 / 10 | 6 / 0 / 4 | 1,429.60 / 1,414.60 s |
| Tuning tracks, 12 cases | 11 / 11 | 8 / 0 / 3 | 2,462.55 / 2,438.20 s |
| DEFAULT, 3 cases | 3 / 3 | 0 / 1 / 2 | 229.05 / 234.20 s |
| All 27 cases | 24 / 24 | 14 / 1 / 9 | 4,121.20 / 4,087.00 s |

Totals compare completed pairs and include penalty time. No previously completed
case was lost. SIOFOK/ANSX under both collision settings and R4K9/GATE with
original collisions still do not finish within this budget. Of the 24 accepted
new laps, 21 have zero penalty. ZCT45/AUDI with original collisions has 18 seconds
of penalty; 4K2020-5/ZMP4 has 18 seconds with original collisions and 36 seconds
with fixed collisions. The full case table and driving/penalty decomposition are
in `study/benchmark-release/findings.md` and `penalties.md` in the evidence archive.

The largest improvement is ZCT180/P962 with fixed collisions, 152.10 to 131.20
seconds, including removal of its previous 12-second penalty. The largest
regression is ZCT45/AUDI with fixed collisions, 217.45 to 229.00 seconds.
DEFAULT at 5,000 candidates changes from 72.40 to 74.30 seconds with original
physics and from 77.90 to 81.15 seconds with all fixes. Its OWOOT result remains
78.75 seconds, with identical replay bytes. New proposals consume search budget
and change subsequent refinement, so the catalog does not improve every run.

A separate final-release DEFAULT/PMIN run at 100,000 candidates, seed 1,
produced **63.55 seconds with zero penalty**, compared with the previous
release's 64.95-second result at that budget. It starts from `DEFAULT.TRK`, without
a human seed. Its replay and report are distributed as `DFSTUNT.RPL` and
`DFSTUNT.RPL.json`; the earlier `DEFPMIN.RPL` and human-assisted `AYR31.RPL`
remain separate. These fixed-candidate results do not establish 30-minute search
performance, human-best performance, or optimality.

Independent playback confirms the 63.55-second lap and zero penalty. This sample
skips the horizontal corkscrew's footprint entirely and does not reach power
gear: its peak speed is about 153.95 mph, and disabling power gear produces
identical native states. Its improvement is a route/refinement result, not
evidence of successful power-gear exploitation.

### Release checks

All seven relevant CTest suites passed after the final search change. The actual
Linux release reproduced the frozen benchmark candidate's replay bytes, full
native state streams, scores, and deterministic counters on ZCT181/LM02 under
both collision settings and R4K9/GATE with collision fixes.

Linux x64 and Windows x64 under Wine produced identical replay bytes and
deterministic metrics for DEFAULT/PMIN at 5,000 candidates and DEFAULT/COUN
at 2,000 candidates with OWOOT and all fixes. Both outputs passed independent
playback; the constrained COUN lap also passed OWOOT validation.

Android ARMv7 debug APK **1.0.3, version code 4**, contains the same source revision.
The actual packaged native executable ran under QEMU ARM with Android Bionic
and matched Linux byte-for-byte in three searches: DEFAULT/PMIN with original
collisions, with collision fixes, and a two-candidate horizontal-corkscrew cut.
Each replay passed native and independent playback verification. APK signature,
alignment, API 21 compatibility, debug symbols, and existing software-rendered
opaque UI configuration were checked. Android lint reported no errors and three
existing warnings. Scrolling on a physical Galaxy S5 running Android 9 has not
been tested here.

The evidence archive keeps unsuccessful local probes, slower full laps, and
rejected prototype comparisons. Use `study/benchmark-release/` for the final
27-case comparison; `study/benchmark-final/` is an earlier prototype, despite
its directory name. Artifact manifests distinguish the algorithm commit from
the subsequent documentation-only commit.
