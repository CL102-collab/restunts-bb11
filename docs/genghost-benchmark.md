# Golden replay comparison

Tested on 2026-10-08 using the supplied `tools/scripts/rpls_golden/replays.zip`.
The archive contains 6,907 recordings and 555 distinct embedded tracks. These
results cover 16 non-default tracks, not the entire archive.

For each selected track/car, the benchmark re-simulated eligible solo 20 Hz
replays with identical track bytes and chose the lowest verified total. Across
16 cases, 121 of 122 candidate recordings completed under default physics.
This human reference is the best compatible run in this archive, not a claim
about current world records. Automatic/manual transmission matched the reference.

All times below are seconds including the engine's penalties. Replay padding
is excluded. **TRK** searches receive track bytes only. **RPL** searches start
from the selected expert's actual recorded controls and can retain them.
RPL results demonstrate refinement of human driving, not independent discovery.

## Changes informed by the recordings

Telemetry from the first 12 tracks showed substantial throttle use through slides
and stunts, carefully timed shifts, and useful grass sections. The driver now
searches a per-segment slide-braking choice, keeps normal acceleration until
power-gear takeoff, and avoids premature airborne downshifts in suitable cars.
Broader changes such as universal airborne throttle and countersteering were
rejected after separate controller comparisons showed regressions.

The search uses 64 route segments and edits pedal, steering, and shift boundaries
in small coordinated steps. Some continuations align to the successful parent's
measured trajectory after an edit. Completed autonomous laps retain more policy
exploration; human demonstrations receive more timing refinement. The engine
validates every saved improvement from a fresh state.

## Paired 20-second searches

The previous executable came from commit `3f33fe4d3cc6f7f3273f87874614b9aef6b9e5bb`.
Old and updated executables each ran two workers concurrently under a four-core
CPU quota, with seed 1 and 20 seconds per track/mode. All 63 completed outputs
passed fresh genghost verification and independent `repldump` playback.

| Track | Car | Human | Old TRK | New TRK | Old RPL | New RPL |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| ZCT1 | LM02 | 113.60 | 136.85 | 145.00 | 113.50 | 113.35 |
| ZCT2 | ANSX | 80.40 | 289.75 | 110.40 | 79.95 | 79.90 |
| ZCT3 | FGTO | 105.30 | 153.20 | 161.35 | 105.10 | 104.80 |
| ZCT4 | JAGU | 72.30 | 98.30 | 97.70 | 72.25 | 72.00 |
| ZCT5 | COUN | 84.35 | 147.70 | 170.35 | 84.35 | 83.60 |
| ZCT6 | AUDI | 68.25 | 145.50 | 100.10 | 68.20 | 68.20 |
| ZCT7 | P962 | 69.20 | 98.00 | 90.10 | 69.10 | 69.10 |
| ZCT8 | LANC | 106.90 | 120.20 | 131.15 | 106.80 | 106.75 |
| ZCT9 | PC04 | 76.20 | 91.20 | 114.10 | 76.15 | 76.00 |
| ZCT10 | VETT | 56.40 | 164.70 | 100.45 | 56.35 | 56.35 |
| ZCT20 | PMIN | 96.55 | DNF | 237.95 | 96.55 | 96.55 |
| ZCT68 | PMIN | 73.90 | 188.05 | 187.10 | 73.90 | 73.90 |
| ZCT276 | PC04 | 87.15 | 111.90 | 115.85 | 87.10 | 87.10 |
| ZCT284 | AUDI | 72.15 | 124.55 | 127.30 | 72.15 | 72.15 |
| ZCT297 | JAGU | 64.05 | 94.40 | 101.80 | 63.60 | 64.05 |
| ZCT300 | AUDI | 76.05 | 168.15 | 163.90 | 76.05 | 76.05 |

Track-only completion rose from 15/16 to 16/16. On the 15 tracks completed by
both versions, mean total fell from 142.16 to 127.78 seconds (10.1%), with seven
faster and eight slower results. The matched-track median gap to humans worsened
from 47.39% to 49.74%; including newly completed ZCT20, the updated median gap
was 51.48%. The average improvement is concentrated in a few difficult cases.
Track-only driving is therefore still substantially below expert quality.

Replay-assisted results beat 11 human references and matched five. Compared
with the previous optimizer, seven improved, eight tied, and one regressed
(ZCT297), for a combined 1.25-second reduction across 16 laps. A valid seed is
preserved, so matching it is expected; only a lower verified score is an
optimization gain.

ZCT276, ZCT284, ZCT297, and ZCT300 were reserved from the controller/segment
experiments. Three of their four track-only results regressed at 20 seconds.
This limits claims about generalization beyond the tuning maps.

## Paired 120-second follow-up

The longer comparison uses the same concurrency and seed. Scratch cases
cover the previous failure, a large penalty-heavy gap, an already stronger
baseline, and a reserved modern track. Replay cases cover four distinct
demonstrations, including modern ZCT297. These runs start fresh from the
same source track or human replay, not from the 20-second outputs.

### Track only

| Track | Car | Human | Old 120 s | New 120 s | Old penalty | New penalty |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| ZCT20 | PMIN | 96.55 | DNF | 237.30 | — | 0.00 |
| ZCT2 | ANSX | 80.40 | 157.85 | 109.25 | 153.00 | 0.00 |
| ZCT9 | PC04 | 76.20 | 90.70 | 113.25 | 0.00 | 0.00 |
| ZCT284 | AUDI | 72.15 | 123.40 | 119.45 | 0.00 | 0.00 |

### Replay assisted

| Track | Car | Human | Old 120 s | New 120 s | Old penalty | New penalty |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| ZCT1 | LM02 | 113.60 | 113.30 | 113.35 | 0.00 | 0.00 |
| ZCT4 | JAGU | 72.30 | 72.10 | 72.00 | 0.00 | 0.00 |
| ZCT8 | LANC | 106.90 | 106.80 | 106.75 | 0.00 | 0.00 |
| ZCT297 | JAGU | 64.05 | 63.35 | 63.90 | 0.00 | 0.00 |

The longer replay-assisted subset improved two cases and regressed two relative
to the previous optimizer, for a combined 0.45-second loss. All four remained
faster than their human input. All 15 completed outputs from the longer comparison
passed independent replay verification, bringing the paired comparison total to 78.

A subsequent diagnostic traced the older optimizer's ZCT297 advantage to sustained
throttle lifts during jumps. A targeted airborne-coasting operator was tested on
four human seeds with 20,000 evaluations each. It gained 0.10 seconds on ZCT297,
lost 0.10 seconds on ZCT4, and tied on ZCT8 and ZCT10. All eight outputs verified,
but the operator was rejected because it did not improve the combined score.
The production implementation and the paired tables above remain unchanged.

Regression checks also passed for deterministic searches, both branches, compatible
seed preservation, output naming, malformed inputs, and the 12,000-frame limit.
Simulation/checkpoint and exact-guide identity tests matched independent replay
states on five fixtures (including VETT, PMIN, and modern JAGU) across six physics
configurations.

## Reproduction and limits

Use `tools/scripts/benchmark-genghost.py` as described in [genghost.md](genghost.md).
Pass all 16 names using repeated `--case` arguments to reproduce the wider suite.
The downloadable results include complete JSON commands, binary/archive hashes,
finish and penalty frames, and separate-driver verification for successful laps.
Wall-clock limits are hardware-dependent; use a fixed `--evaluations` budget
for deterministic searches on a given binary and resource set.

All performance comparisons here use default legacy physics. Regression checks
also cover OWOOT and each physics switch, but these performance numbers do not
establish expert pace under those restrictions. There is no proof of a global
optimum, exhaustive stunt-exploit discovery, or near-human autonomous driving
on arbitrary tracks. The default search budget remains 30 minutes.

Archive SHA-256:
`1424322864468584210cda8dbba034b5a68157758b55a2db6e7b889e8938c888`

Baseline benchmark binary SHA-256:
`e20bbcfe304d0343bd59c901b01bbf3f4f15f22da0ebb3cbb240b756d26dbc26`

Updated benchmark binary SHA-256:
`b0094dab05648d6902958dc3704342868c2dc4d67bb0c5dd4384b123a6b85d12`
