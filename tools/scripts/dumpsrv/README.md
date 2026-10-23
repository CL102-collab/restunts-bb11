# Replay regression service and runner

`tools/scripts/dumpsrv` is one C# application for Linux, Windows, and GitHub Actions.
Its HTTP service and direct runner share replay discovery, sampling, task
scheduling, DOSBox and native SDL3 execution, oracle caching, comparisons, and
reporting.
The shell client continues to use the same HTTP endpoint and report format.

## Build and deployment

Install the .NET 10 SDK to build and test the application. From the repository
root, publish the portable application:

```sh
dotnet publish tools/scripts/dumpsrv/dumpsrv/dumpsrv.csproj --configuration Release --output out/dumpsrv
```

Copy the complete publish directory to the service machine. The same published
files work on Linux and Windows with the .NET 10 ASP.NET Core Runtime installed.
The publish directory includes `dosbox.proc.conf`; install DOSBox-X separately.
Set `DUMPSRV_DOSBOX_PATH` to an explicit DOSBox-X executable path when needed;
otherwise it uses `C:\DOSBox-x\dosbox-X.exe` on Windows if present, then
`dosbox-x` from `PATH`.

Create a `stunts` directory alongside `dumpsrv.dll` and put the processing inputs
there:

- `repldumo.exe` and `pixldumo.exe`, the original physics and renderer tools.
- The `.rpl` replay corpus, all required custom-car files, and other Stunts data.

When preparing a Linux game directory from this repository, copy the whole car
pack with `cp -R tools/scripts/cars/. assets/cars/. /path/to/stunts/`.
The original oracle executables require loose car files. Do not filter by
uppercase extensions: the pack includes `.3sh`, `.vsh`, and `.res` files as well.
Omitting these files leaves the original renderer waiting in its resource-error
dialog and is reported as a timeout. The replays using PACK, PFCR, LANI, LRDE,
and FUNO require these mixed-case files. Updating the executables alone does
not repair an already prepared game directory.

Some custom tracks have unsupported skybox selectors. ZCT77's `0xFF` skips
skybox initialization in the original engine, causing blank or corrupted
horizons and renderer hangs. For renderer runs, prepare a separate copy of the
game directory and normalize it before starting the service or direct runner:

```sh
python3 tools/scripts/normalize-track-skybox.py --in-place /path/to/renderer-stunts
```

This replaces selectors outside 0 through 4 with desert, preserving all other
input bytes, and marks affected PDO caches for regeneration. Use the original
corpus for physics regressions. CI performs this step only in its temporary
renderer directory. See [the skybox diagnosis](../../../docs/renderer-parity.md#unsupported-track-skybox-selectors)
for details and instructions for creating repaired interactive track/replay copies.

The service writes uploads to `stunts/repldump.exe` and `stunts/pixldump.exe`.
The `repldump` upload must be named exactly `repldump.exe`, and the `pixldump`
upload exactly `pixldump.exe`, all lowercase. Missing or different filenames,
including uppercase, mixed case, paths, or swapped names, return HTTP `400` before either
executable is saved. Both `filename` and `filename*` are checked when supplied.
DOSBox mounts only this game directory as drive `C:`, keeping service binaries,
configuration, and reports outside the mount.
The direct runner accepts an explicit game directory and requires all four
physics and renderer executables there when both phases are enabled.

## Start the service

Set a long, random API key and choose the number of concurrent workers. These
examples start the same application on either platform.

Linux:

```sh
export DUMPSRV_API_KEY='replace-with-a-long-random-secret'
dotnet out/dumpsrv/dumpsrv.dll serve \
    -PartitionCount 12 -Port 8080 \
    -DosBoxTimeoutSeconds 60 -RendererTestPercentage 5 \
    -ResponseProcessingTimeoutSeconds 1800
```

Windows PowerShell:

```powershell
$env:DUMPSRV_API_KEY = 'replace-with-a-long-random-secret'
dotnet out/dumpsrv/dumpsrv.dll serve `
    -PartitionCount 12 -Port 8080 `
    -DosBoxTimeoutSeconds 60 -RendererTestPercentage 5 `
    -ResponseProcessingTimeoutSeconds 1800
```

| Parameter | Default | Meaning |
| --- | --- | --- |
| `ApiKey` | `DUMPSRV_API_KEY` | Secret required in the `X-API-Key` request header. |
| `PartitionCount` | Required | Number of concurrent workers, from 1 through 64. |
| `Port` | `8080` | HTTP port, from 1 through 65535. |
| `DosBoxTimeoutSeconds` | `60` | Positive time limit for each DOSBox execution. |
| `RendererTestPercentage` | `100` | Whole-number renderer coverage, from 1 through 100. |
| `Camera` | `2` | Renderer camera, from 1 through 4 (F1 through F4). |
| `Target` | `0` | Renderer target: player (`0`) or opponent (`1`). |
| `ResponseProcessingTimeoutSeconds` | `1800` | Positive overall processing limit, in seconds. |

The `serve` command is optional; named service parameters can follow
`dumpsrv.dll` directly.

`Camera`, `Target`, and `ResponseProcessingTimeoutSeconds` are accepted only
at service startup. HTTP requests cannot set or override them. Opponent
rendering selects only replays containing an opponent.

The processing timeout starts after validated uploads are saved and covers the
enabled test phases and report generation together. It is separate from the
timeout for an individual DOSBox execution. A processing timeout
cancels the workers, forcibly terminates their DOSBox processes, and returns
HTTP `504`. Configure the client's timeout slightly above this limit to allow
for the upload and response transfer.

Service console lines begin with the request's elapsed processing time in
`[HH:mm:ss]` format. Each request starts its own timer after validated uploads
are saved. Immediately before sending the response, the service prints the
individual durations of the requested physics and renderer phases, followed by
its total processing time including discovery and report generation. Phase
durations measure elapsed time across all parallel workers:

```text
[00:00:00] Processing requested phases: physics and renderer.
[00:00:12] Processing physics replay: race.rpl
[00:02:08] Physics phase took 00:00:45.
[00:02:08] Renderer phase took 00:01:22.
[00:02:08] Processing requested phases (physics and renderer) took 00:02:08.
```

Disabled phases are omitted. If processing is interrupted, the summary includes
time spent in any started phase and identifies requested phases that did not start.
The final duration excludes response transmission. Startup messages and requests
that have not started processing show `[00:00:00]`. Elapsed prefixes appear only
in the console; the returned report retains its existing format.

The service listens on all local interfaces using ASP.NET Core Kestrel. Windows
HTTP URL reservations are not required. Allow inbound traffic on the selected
port in the host firewall. For example, in an elevated Windows PowerShell
session, allow the private local subnet:

```powershell
New-NetFirewallRule `
    -DisplayName 'Replay regression service' `
    -Direction Inbound -Action Allow -Protocol TCP -LocalPort 8080 `
    -Profile Private -RemoteAddress LocalSubnet
```

## Replay selection and scheduling

Every top-level file with a case-insensitive `.rpl` extension is discovered.
Filenames do not need a numeric suffix. The existing DOS executables still
require DOS-compatible basenames of 1 through 8 characters. Unsupported names,
reserved DOS device names, and case-insensitive name collisions are reported
as errors instead of being skipped. The application sorts the corpus once
using ordinal ordering to make repeated runs deterministic. Physics tests use
the entire corpus. With `Target 1`, renderer selection filters for a nonzero
opponent type at byte offset 6 in the 26-byte replay header. With `Target 0`,
all replays are eligible. Renderer tests select
`ceiling(eligible replay count * percentage / 100)` evenly spaced entries from
that ordered list before balancing or assigning work. A corpus with no
opponents produces an empty renderer phase for `Target 1`; physics still
uses every replay. The HTTP service uses the same selection rules.

For distributed runs, `tools/scripts/plan-replay-shards.py` reads the recorded
tick count from each replay's header and assigns longer replays to the shard
with the lowest total first. It then improves the totals with replay moves
and swaps until every shard is within ±2% of the average or no improving move
or swap remains. Small samples or indivisible replay lengths can prevent that
target; the planner reports the remaining deviation. Physics and renderer
lists are balanced independently, after renderer sampling. Shards can contain
different numbers of replays, and repeated planning is deterministic.

The planner writes a JSON object with `version`, `rendererTestPercentage`,
`target`, and a `shards` array indexed by `ShardIndex`. Each entry contains `physics` and
`renderer` filename arrays, plus `physicsTicks` and `rendererTicks` totals.
The C# application validates the plan's target, percentage, full physics
coverage, and eligible renderer sample, then uses the lists for its shard ID
without computing assignments. Oracle extraction and report merging use the
same JSON plan and opponent filter. Plans missing the target must be regenerated.

Within each shard, lists are distributed round robin to workers, whose replay
counts differ by at most one. Each worker processes its list in order, and
workers run as asynchronous C# tasks. Selection is independent of the number
of shards or workers.

The service uses one shard. GitHub Actions assigns one shard to each matrix job;
all jobs use the same complete replay corpus and renderer percentage. Physics
runs first, followed by rendering. A comparison error does not stop the other
replays or the remaining enabled phase.

## Call the service

The endpoint is `POST /process`, authenticated with `X-API-Key`. Its
`multipart/form-data` fields are unchanged:

| Field | Required | Description |
| --- | --- | --- |
| `repldump` | Yes | Upload filename `repldump.exe`; maximum 1 MiB. |
| `pixldump` | Yes | Upload filename `pixldump.exe`; maximum 1 MiB. |
| `physics_tests` | No | `true` or `false`; defaults to `true`. |
| `renderer_tests` | No | `true` or `false`; defaults to `true`. |

Both executables must be uploaded even if one test type is disabled. At least
one test type must be enabled. A completed run returns HTTP `200` and a UTF-8
`partitions_all.txt` attachment. Comparison errors are recorded in the report;
an empty report means all enabled comparisons passed.

Copy `dumpsrv-client.sh`, `repldump.exe`, and `pixldump.exe` into the same client
directory, using these lowercase filenames. Set the API key, then pass the
endpoint URL:

```sh
export DUMPSRV_API_KEY='replace-with-the-server-secret'
./dumpsrv-client.sh http://server-name:8080/process
```

Alternatively, set `DUMPSRV_URL` and omit the URL argument. Client options may
appear before or after the URL:

```sh
./dumpsrv-client.sh --physics-tests false http://server-name:8080/process
./dumpsrv-client.sh --renderer-tests=false http://server-name:8080/process
./dumpsrv-client.sh --timeout-seconds 3660 http://server-name:8080/process
./dumpsrv-client.sh http://server-name:8080/process --timeout-seconds=3660
```

Both `--physics-tests` and `--renderer-tests` accept `true` or `false`, either
as the next argument or after `=`. `--timeout-seconds` accepts a positive
integer in either form and defaults to `1860` seconds. It controls only how
long the client waits and is never sent as a request field. The connection timeout
remains 10 seconds. The client saves a successful response beside the script as
`partitions_all.txt`; a failed request leaves an existing result unchanged.

Only one authenticated request runs at a time. Other authenticated requests
receive `503 Service Unavailable` with `Retry-After: 60`.

Other response codes are:

- `400`: empty or malformed multipart body, missing executable, invalid test
  setting, or both tests disabled.
- `401`: missing or incorrect API key.
- `404`: another path.
- `405`: a non-POST request to `/process`.
- `413`: an executable exceeds 1 MiB, or the total body exceeds 2 MiB plus 64 KiB.
- `500`: processing could not complete or its report could not be produced.
- `504`: the configured overall processing timeout expired.

## Direct execution and CI reports

The `run` command uses the same engine without HTTP. For example, from the
repository root:

```sh
dotnet out/dumpsrv/dumpsrv.dll run \
    -GameDirectory stunts -OutputDirectory results \
    -DosBoxConfigPath tools/scripts/dosbox.proc.conf \
    -PartitionCount 12 -DosBoxTimeoutSeconds 30 \
    -RendererTimeoutSeconds 120 -RendererTestPercentage 5 -Camera 2 -Target 0
```

To test the SDL3 port, point the same runner at an installed native `bin`
directory (or a native build directory). The game directory still contains the
assets, replays, and source-built `repldumo.exe` / `pixldumo.exe` DOS oracles
(`make -C src/restunts repldump-original pixldump-original`):

```sh
dotnet out/dumpsrv/dumpsrv.dll run \
    -GameDirectory stunts -OutputDirectory out/sdl3-results -PartitionCount 5 \
    -CandidatePlatform sdl3 -NativeDirectory out/package-linux-x64/bin \
    -DosBoxConfigPath tools/scripts/dosbox.proc.conf \
    -DosBoxTimeoutSeconds 120 -RendererTimeoutSeconds 980 \
    -RendererTestPercentage 5 -Camera 2 -Target 0
```

`CandidatePlatform` is `dos` by default; `sdl3` requires `NativeDirectory` and
executes its `repldump` and `pixldump` directly, with dummy SDL video and audio
drivers. Oracles always run in DOSBox-X when the cache is missing or invalid.
Native tools must exist; there is no fallback to DOS candidates. The same
selection, sampling, sharding, output validation, comparisons, and timeouts apply
to both platforms. `DosBoxTimeoutSeconds` also limits native physics executions.
Use `-CandidatePlatform sdl3` with `merge` too: coverage checks reject results
from another candidate platform.

The native renderer uses logical DOS PSP segment 654 (`0x028e`) and program
path `C:\PIXLDUMP.EXE`, matching the archived renderer ABI and the supplied
DOSBox-X configuration. For references generated in another DOS environment,
`run -OraclePspSegment N` overrides the SDL candidate's logical PSP segment;
use references generated under that same environment.

`Camera` accepts integers from `1` through `4` and defaults to `2`. `Target`
accepts `0` (player, the default) or `1` (opponent). These settings are passed
to both renderer executables; physics output is independent of them. Each
service or direct-runner invocation uses one camera and target. CI supports
multiple targets by starting separate renderer jobs, so these parameters and
the HTTP interface are unchanged.

`ShardIndex` defaults to `0` and `ShardCount` to `1`. Single-shard runs, including
the HTTP service, work without a plan. For a distributed run, generate a plan
once from the complete replay directory or ZIP:

```sh
python3 tools/scripts/plan-replay-shards.py \
    --replays stunts --shards 20 \
    --renderer-test-percentage 5 --target 0 --output shard-plan.json
```

Pass `-ShardPlan shard-plan.json -ShardCount 20 -ShardIndex 0` to `run`, changing
the index for each job, and use `-RendererTestPercentage 5 -Target 0` to match
this plan. For an opponent run, generate the plan with `--target 1` and use
`-Target 1` for `run`, `extract-oracles`, and `merge`.
Retain the complete replay corpus on every shard. `ShardPlan` is required when
`ShardCount` exceeds one. `PhysicsTests` and `RendererTests` default to `true`;
use `-PhysicsTests false` or
`-RendererTests false` to disable a phase. `DosBoxConfigPath` defaults to the
configuration alongside the application. Progress is written to the console.

Each run writes `shard-<index>.json` with shard identity, camera and target,
completed replay names, and diagnostics, plus a local `partitions_all.txt`
report. `run` returns zero only when the assigned work finishes without errors. Copy every shard's JSON
result into one directory, then merge against the complete replay corpus:

```sh
dotnet out/dumpsrv/dumpsrv.dll merge \
    -ReplayDirectory stunts -ResultsDirectory results \
    -ShardCount 1 -RendererTestPercentage 5 -Camera 2 -Target 0 \
    -OutputFile partitions_all.txt
```

Pass the same shard count, percentage, camera, target, and enabled test phases
used by `run`. Shards with missing or inconsistent camera/target settings
are rejected.
For distributed runs, also pass the same `-ShardPlan shard-plan.json` to `merge`.
`merge` reads shard identity from the JSON content, checks actual completed
replay names against expected coverage, and rejects missing or duplicate shards.
It writes the text report even when validation fails and returns nonzero for
errors or incomplete coverage. Optional `-SummaryFile PATH` writes the Markdown
summary used by GitHub Actions.

CI first runs formatting, service and host tests, and shard planning in
parallel, then builds the selected platforms after all four jobs pass. The
`platforms` workflow input is a nonempty JSON array of unique names from `dos`
and `sdl3`. **Build and validate** and **Release** default to
`["sdl3"]`; **PR validation** defaults to `["sdl3"]`.
Use `platforms: '["dos"]'` for DOS only, `platforms: '["sdl3"]'` for Linux x64
SDL3 only, or `platforms: '["dos","sdl3"]'` for both. Like `cameras` and
`targets`, it is available in the manual **PR validation** and **Release**
workflows and the reusable **Build and validate** workflow. Unselected replay
candidate builds are skipped. The original-assembly physics and renderer
references are always built, including for SDL3-only replay tests.
**PR validation** and **Release** always build every
distribution package, including DOS, even for SDL3-only replay tests.

The planning job publishes one plan per selected target as `target0.json` or
`target1.json` in the `replay-shard-plan` artifact and reports each phase's tick
balance in its job summary. Replay jobs and coverage checks download that
artifact and select the matching plan. Each plan retains the single-target
JSON format and uses the same physics assignments; renderer eligibility and
sampling are computed separately for each target. Physics runs once per
selected platform after the builds. A separate renderer job runs for each
selected platform, camera, and target after every physics shard and its
coverage check pass. Both platforms and all cameras share the same plan and
renderer sample for a given target. Renderer percentage, shard, worker, and
timeout inputs configure both platforms. Tests and reports run only for
selected platforms.
Each shard's JSON is uploaded as
`<phase>-partitions-cam<camera>-target<target>-<index>`. Renderer oracle files
are uploaded as `renderer-pdo-cam<camera>-target<target>-<index>` and combined
into `renderer-pdo-cam<camera>-target<target>`. Phase reports are uploaded as
`<phase>-cam<camera>-target<target>-report`, including diagnostics when
validation fails. The physics report uses the first requested camera and
target in its metadata; physics results do not depend on either setting. The
final Replay report jobs run once per selected platform, camera, and target
after all renderer validation passes. Each job combines that platform's shared
physics diagnostics with its camera and target's renderer diagnostics and
publishes `partitions_all-cam<camera>-target<target>`, containing
`partitions_all.txt`. Each camera and target combination has its own report.
SDL3 artifacts use the same names with an `sdl3-` prefix. Native executables
and their shared OPL library travel together in `restunts-sdl3-replay-exes`;
the tar archive preserves executable permissions and the installed layout.
The summaries show physics and renderer coverage separately, errors grouped by
type, and up to the first 200 diagnostic lines. The DOS executable build and
`restunts-exes` artifact remain separate from the C# regression runner.

## Precomputed oracle archives

The `extract-oracles` command imports precomputed outputs into an existing
game directory containing the full replay corpus. It uses the same sampling
and shard assignment as `run`, extracting only the selected top-level `.BIN`
or `.PDO` entries. For example:

```sh
dotnet out/dumpsrv/dumpsrv.dll extract-oracles \
    -GameDirectory stunts -Archive BINs.zip -ShardIndex 0 -ShardCount 20 \
    -ShardPlan shard-plan.json -RendererTestPercentage 5
dotnet out/dumpsrv/dumpsrv.dll extract-oracles \
    -GameDirectory stunts -Archive PDOs-cam2-target0.zip -Renderer true -Camera 2 -Target 0 \
    -RendererTestPercentage 5 -ShardIndex 0 -ShardCount 20 \
    -ShardPlan shard-plan.json
```

`Renderer` defaults to `false`, `RendererTestPercentage` to `100`,
`ShardIndex` to `0`, `ShardCount` to `1`, `Camera` to `2`, and `Target` to `0`.
Match these settings to the subsequent `run` command and supply the same
`ShardPlan` for distributed runs. Renderer imports record the selected camera
and target beside each `.PDO`; the archive must contain outputs for that view.
Missing entries are reported and left for the runner to generate. Invalid
archives fail preparation. Imported outputs
still undergo the runner's completeness checks before reuse.

**Build and validate** and the manual **PR validation** and **Release**
workflows accept `cameras` and `targets` as nonempty JSON arrays of unique
integers. Cameras range from 1 through 4; **Build and validate** and **Release**
default to `[1,2,3,4]`, while **PR validation** defaults to `[1,2,3,4]`. Targets are
player (`0`) and opponent (`1`), defaulting to `[0,1]`. Use `targets: '[0,1]'` to
test both or `targets: '[1]'` to test the opponent alone. Each renderer job
passes one `camera` and one `target` to **Replay tests**. For target `1`, the
renderer percentage applies only to replays containing an opponent.
CI downloads `PDOs-cam<camera>-target<target>.zip` for rendering and `BINs.zip`
for physics from oracle release `v1.0.4`.
The oracle download/extraction step runs for both targets. If a cache download
fails (including HTTP 404, server errors, or network failures), CI logs a warning
and continues. Missing references are generated locally during replay testing.
Partial downloads are discarded. A successfully downloaded archive must still
pass extraction and shard-plan validation.

## Cached outputs and diagnostics

Physics compares original `.BIN` output against fresh `.BNI` output. Rendering
uses the selected camera and target, comparing original `.PDO` output against
fresh `.PDD` output. Both renderer executables must use incremental redraws and
hashes on every frame and the recording's simulation rate. CI uses the
source-built original-assembly `repldumo.exe` and `pixldumo.exe`; for local runs,
build `repldump-original` and `pixldump-original` alongside the candidates.
See the [oracle guide](../../oracles/borland/README.md) for the historical
Borland references. Their wrappers force 20 Hz, so existing 10 Hz `.BIN` and
`.PDO` caches need replacement. Comparisons are byte for byte.

Renderer caches also require a matching `<replay>.PDO.settings` file containing
the camera and target, separated by one space. The runner and oracle extractor
write this metadata; missing metadata or a changed view forces regeneration.
`pixelcheck.sh` clears this metadata when replacing a hash dump.

Completed `.BIN` and `.PDO` files are reused only after checking their contents
against the replay's recorded frame count. Physics dumps must contain the
matching two-byte frame count and exactly 1,120 bytes per frame. Renderer dumps
must start with `PIXLDUMP 2` and CRLF, followed by every CRLF-terminated
MurmurHash3_x86_32 sample (seed 0, eight lowercase hexadecimal digits) at
frames 0, 1, 2, ... through the replay's final frame. Empty, truncated, or
malformed caches are regenerated, including five-frame sampled dumps and files
left by older runners without a pending marker. The header
also invalidates full-redraw caches from the old renderer wrapper. Regeneration
uses `pixldumo.exe` from the game directory: CI supplies the current
`pixldump-original` source build. Cache completeness does not distinguish the
archived wrapper's fixed 20 Hz from the recording's rate; replace affected
published caches when correcting that rate.

A `.BIN.pending` or `.PDO.pending` marker is written before oracle generation
and removed only after successful execution produces a complete output.
An interrupted oracle dump is regenerated on the next run. Fresh oracle and
candidate outputs undergo the same completeness checks; invalid output is
reported as `type=invalid_output` instead of a replay desync or a successful
comparison of two partial files. Candidate outputs are removed before execution
so stale files cannot hide failures. DOSBox uses `core=dynamic` and `cycles=max`;
cancellation and execution timeouts kill the process tree forcibly.

Diagnostics retain the existing one-line format. Duplicate lines are removed
and results are ordered by the `input` field:

```text
ERROR|type=timeout|exe=pixldump.exe|input=0000.rpl|timeout_seconds=60
ERROR|type=missing_output|input=0000.rpl|output=0000.PDD
ERROR|type=file_mismatch|input=0000.rpl|pdo=0000.PDO|pdd=0000.PDD
```

After successfully sending a report, the service removes generated candidate
outputs, incomplete oracle outputs, and report files owned by that request.
Completed oracle caches are retained. If processing or response delivery fails,
diagnostics and generated outputs are retained for investigation.

## Development checks

```sh
dotnet test tools/scripts/dumpsrv/dumpsrv.slnx --configuration Release
dotnet format tools/scripts/dumpsrv/dumpsrv.slnx --verify-no-changes
```

CI runs both commands on Linux and Windows. The HTTP and engine tests use
controlled process fixtures so they can exercise failures, cancellation, and
coverage without requiring DOSBox for every test. On Linux, also run the POSIX
client tests; CI runs them on its Linux job:

```sh
python3 tools/scripts/dumpsrv/dumpsrv.tests/client-tests.py
```
