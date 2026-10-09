# Parallel ghost search

`genghost --cores N` runs independent search populations concurrently in separate
processes. `--cores 1` is the default and preserves the previous algorithm.
`--cores 0` selects available CPUs automatically, respecting affinity and common
Linux container quotas. The Android 1.0.4 app exposes this as **CPU cores**.
See [usage and budget semantics](genghost.md#parallel-search).

Each worker has a reproducible seed and isolated game globals. The coordinator
independently plays proposed winners through the original physics before saving
the best replay. Restrictions, source replay inputs, transmission, penalties,
and previously implemented stunt tactics are preserved. Populations remain
independent; workers do not import one another's live candidates.

## Measured throughput

The following are one 20-second run per configuration, seed 1, track files only,
with original physics. Other build and test jobs were paused. This Linux
environment permits five CPUs in its affinity mask and has a four-CPU quota.
All six outputs finished with zero penalty and passed independent `repldump`
playback. These timings are illustrative measurements, not reproducible
candidate-budget benchmarks or general performance guarantees.

| Track / car | Workers | Simulation ticks / second | Speedup | Final lap |
| --- | ---: | ---: | ---: | ---: |
| DEFAULT / PMIN | 1 | 1,010,277 | 1.00x | 67.40 s |
| DEFAULT / PMIN | 2 | 2,036,390 | 2.02x | 64.90 s |
| DEFAULT / PMIN | 4 | 4,040,681 | 4.00x | 64.90 s |
| ZCT181 / LM02 | 1 | 716,055 | 1.00x | 122.10 s |
| ZCT181 / LM02 | 2 | 1,377,701 | 1.92x | 122.55 s |
| ZCT181 / LM02 | 4 | 2,808,835 | 3.92x | 119.00 s |

Measured CPU consumption was approximately 1.00, 2.01, and 3.99 CPU-seconds per
wall-clock second for DEFAULT, confirming simultaneous use of multiple CPUs.
Simulation counts sum worker activity and exclude the coordinator's verification
passes. Faster simulation does not guarantee a faster final lap: each worker
explores a different trajectory, and a fixed total candidate budget gives each
population fewer attempts. ZCT181 with two workers illustrates this distinction.

## Correctness and lifecycle checks

All seven existing genghost test suites passed, along with the new parallel
integration suite. Tests cover:

- Odd candidate budgets split exactly across workers; the combined budget is
  not multiplied by the worker count.
- Each worker's result matches a standalone run with its assigned seed and
  budget; the output is the best score, with worker-index tie breaking.
- Repeated fixed-budget searches produce identical replay bytes and numerical
  reports at the same worker count.
- Source replays, manual/automatic transmission, OWOOT, and all physics fixes
  survive worker launch and independent replay verification.
- Paths containing spaces, invalid counts, output failures, no-finish cases,
  automatic CPU selection under affinity restrictions, and short deadlines.
- Real CPU-time advancement in simultaneous child processes, graceful
  cancellation, worker failure, and workers exiting after a killed coordinator.

Three one-worker compatibility cases also matched the prior release byte for
byte, with identical pre-existing numerical metrics: DEFAULT/PMIN at 5,000
candidates, DEFAULT/COUN with all three fixes at 500, and a compatible human
LM02 replay seed at 100. They scored 74.30, 112.20, and 113.60 seconds respectively.

Linux and Windows under Wine produced identical two-worker replays and
deterministic metrics on DEFAULT/PMIN at 5,000 total candidates (68.65 seconds)
and DEFAULT/COUN with OWOOT and all fixes at 2,000 (125.85 seconds). Both have
zero penalty. Linux cancellation preserved a verified replay and stopped both
workers; ordinary shutdown also removed private working files. Under Wine, a
native Windows helper forcibly terminated the coordinator and confirmed that
its Job Object stopped both workers while preserving an independently verified
replay. Graceful Windows console signaling could not be exercised in this
noninteractive Wine environment and remains untested here.

The packaged Android ARMv7 executable passed exact Linux replay and numerical
report comparisons for one worker, two workers sharing an odd 201-candidate
budget, and a seeded two-worker search with OWOOT and all fixes. The APK was
extracted and executed with QEMU ARM, Android API 21 Bionic, and a PRoot exec
bridge; these are functional checks, not Android throughput measurements.
Graceful Stop retained an independently verified replay. Forced coordinator
termination stopped both workers; the container's init left their exited zombie
process entries unreaped. No live worker remained. Android lint passed with
zero errors and three existing warnings. The software drawing and opaque
background settings used for the Galaxy S5 display fix remain intact.

Android testing also caught an SDL file-status fallback that tried to access a
Java asset manager from the standalone native worker. Stop-file polling now uses
plain native file I/O, so parallel searches do not require a Java VM.

The downloadable validation archive contains source/binary hashes, commands,
reports, generated replays, independent playback results, lifecycle checks, and
the throughput measurements. Native Windows hardware and the physical Galaxy
S5 Android UI are not exercised by these environment checks.
