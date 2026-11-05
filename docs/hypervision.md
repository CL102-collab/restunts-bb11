# HyperVision

HyperVision is the enhanced software renderer for SDL3 builds, including native
desktop platforms, 32-bit DOS, and WebAssembly. It replaces SDL3 SuperSight while
retaining its three internal resolutions, quality controls, visual interpolation,
skyboxes, ghosts, and shadows. The performance target is a 2 GHz Core 2 Duo.
That target is a hardware requirement to evaluate, not a measured FPS result.

F12 selects Auto or Off; Shift+F12 cycles Full, High, Medium, and Low. The startup
options are `--hv:full`, `--hv:high`, `--hv:medium`, and `--hv:low`. Off uses the
classic renderer. The Open Watcom 16-bit DOS build retains its existing SuperSight
renderer and controls.

Enhanced horizon artwork is enabled at 640x400 and 1280x800. The 320x200 mode
uses the original horizon strips and disables car shadows. Banked and inverted
views work at every HyperVision resolution. Native-resolution panorama caches
replace repeated strip lookup while rotating the horizon. Enhanced artwork stays
cached across resolution changes. Classic mode and missing enhanced assets also
use the original horizon strips.

## Visibility architecture

The enhanced path separates scene preparation from software visibility and
shading. It submits projected polygons and material information without relying
on a back-to-front scene or primitive sort. The classic path retains the
original ordering and rendering rules.

The software backend resolves visibility along screen-space spans. A planar
polygon's inverse depth varies linearly across a scanline. Overlapping spans can
therefore be compared over intervals, splitting at depth crossings to identify
the nearer surface. Planar commands cache their depth equation for reuse across
scanlines; warped polygons retain edge interpolation. Intersecting surfaces do
not need a single global painter order; the visible surface can change within
the overlap.

The pipeline is:

1. Select scene geometry using the active quality preset and visibility mask.
   Transform and clip it into projected polygon/material commands.
2. Convert opaque polygon coverage into scanline spans and resolve their
   inverse-depth intersections. Submission order does not determine which opaque
   surface is visible.
3. Resolve solid decals and generated line/point coverage against those supports.
   An attached decal can retain its supporting surface's depth even when its
   authored paint lies slightly behind that surface. The flat-ground approximation
   clips generated coverage where there is no explicit supporting surface.
4. Shade the surviving solid intervals and write their depth values once.
   Covered opaque fragments avoid repeated shading and per-fragment depth tests.
5. Draw patterned surfaces with the depth checks needed for their individual
   covered samples. A grille hole cannot discard the surface behind it.
6. Use the resulting depth information for effects that require it, including
   shadow receivers, then compose the completed frame for presentation.

This is an analytic visibility renderer, not a painter's algorithm. It still
keeps depth information and performs per-sample checks where a material or effect
needs them. Visibility bookkeeping, crossings, depth writes, composition, and
special materials all have CPU costs; eliminating opaque overdraw alone does
not establish a whole-frame speedup.

The renderer permits small differences in clipping, edge coverage, depth ties,
and material appearance compared with SuperSight. The original image is useful
for visual review, but exact SuperSight pixel hashes are not the acceptance
criterion for the new enhanced path. The classic path retains its separate
pixel-parity requirements.

## Scene and backend boundary

The command boundary describes projected geometry, material selection, and depth
independently of the code that writes software pixels. This makes visibility
rules easier to test and gives a future graphics backend a smaller integration
surface.

There is no Vulkan backend in this change. A future implementation would still
need device setup, GPU resources, presentation, and a decision about how much
transformation and clipping to move onto the GPU. The current boundary avoids
introducing those abstractions before they are needed; it does not imply that
software span resolution should be translated directly into Vulkan commands.

## Authored meshes and track previews

Native track objects can supply full and authored low-detail meshes. Classic
reduced-detail rendering can select the low alternatives; Full HyperVision keeps
full meshes on classic tracks. Extended-track driving, replays, and rotating
`.cstrk` menu previews select an available low mesh when the complete object is
farther than 20 tile lengths from the camera, including the Full preset.

The cutoff uses three-dimensional Euclidean distance to the nearest point of a
conservative axis-aligned object bounding box. The box includes the whole
footprint, full model and overlay bounds, scattered grove trees, and grade.
Camera height contributes to the distance. Equality and any part of the bounds
within 20 tiles retain full detail. Driving uses unwrapped world coordinates;
the preview uses the job's precise tile origin to preserve the same distance
across large tracks.

Models without a low-detail alternative retain their full meshes. Generated roll
ramps, tunnel slaloms and vine fields retain their existing geometry. Terrain
meshes and road cutouts retain their full geometry, and this object-distance
rule does not change vehicle detail.

The live menu preview remains capped at 25 presented frames per second. Its
reported render time excludes the pacing wait and display synchronization.

## Multi-core rendering

`--multi-core:on` is the default and enables a persistent worker pool;
`--multi-core:off` selects serial rendering. The desktop, Android, and supported
browser Graphics settings expose the same selection and preserve saved choices.
DOS32 remains serial. Browser threads require a threaded build and cross-origin
isolation, as described in [the browser guide](wasm.md).

Extended-track terrain and objects are prepared in private geometry contexts.
Jobs own their projected vertices, shape metadata, shadow receivers, origins,
and bounding rectangles. The caller joins groups in the original submission
order, remaps shape and depth-family indices, and inserts cars, start flags,
and ghost lines at the same points as serial rendering. Material overrides and
shadow receiver heights are resolved when groups join.

The extended-track menu preview uses the same private geometry contexts and
ordered joins. It builds groups of eight row-major tiles independently, then
merges them in source order before rasterization. Its 25 FPS presentation cap
continues to apply when workers are enabled.

Projected primitives then build private polygon-command buffers in parallel.
An ordered prefix layout lets jobs copy into disjoint final command ranges.
Visibility resolves in eight-row bands aligned with the logical pixel cells;
depth clearing, skybox rows, and final ARGB composition also use disjoint jobs.
All jobs join before buffers are resized, resources are retired, or SDL presents
the frame. Small workloads retain serial paths.

## Physics, interpolation, and presentation

Authoritative simulation, input sampling, and replay recording remain at their
original 10 or 20 Hz rate. HyperVision targets 60 presented frames per second by
interpolating completed physics states using the existing timing rules. Rendered
positions, rotations, suspension, cameras, and ghosts share the visual sample
time. Interpolated values must never become the next authoritative physics state
or enter recorded replay data.

Late rendering skips overdue visual samples without predicting beyond the newest
completed state. Seeking, pausing, rewinding, and camera changes reset the visual
history. Switching renderer, resolution, quality, worker count, or VSync must not
change an old replay's simulated result.

The original renderer also leaves values that later physics consumes. Those
compatibility effects remain represented explicitly by the game; replacing the
visible enhanced draw must not discard or overwrite them. See the existing
[renderer parity notes](renderer-parity.md#renderer-values-consumed-by-physics)
for the historical behavior and its regression coverage.

Desktop VSync remains enabled by default. `RESTUNTS_VSYNC=0` disables it and
`RESTUNTS_VSYNC=1` enables it explicitly on the supported desktop SDL3 platforms.
Display synchronization waits are excluded from automatic quality decisions.
The 32-bit DOS and browser builds retain their platform presentation mechanisms.
VSync does not alter physics timing or create a different replay format.

## Performance measurement

Measure the actual renderer using identical scene states, camera paths,
resolutions, build settings, and hardware. The supported internal resolutions
remain 1280x800, 640x400, and 320x200. Use locked presets during comparisons so
automatic quality changes do not make a slower run appear faster by reducing
the workload. Compare equivalent geometry and visual features at each resolution.

Useful coverage includes cockpit and external cameras, dense and sparse tracks,
crossing geometry, shadows, patterned surfaces, ghosts, and moving views. Include
both the 10 Hz and 20 Hz replay schedules in compatibility testing. Keep classic
pixel parity and authoritative replay-state comparisons as separate checks from
enhanced visual review.

For repeatable CPU measurements:

- Use Release builds with the same compiler flags and instruction-set baseline.
  Record the CPU, operating system, build revision, affinity, and worker count.
- Warm up resources and caches before collecting measurements. Alternate baseline
  and candidate batches to reduce bias from CPU frequency and host load.
- Report elapsed frame time and total process CPU time separately. Worker CPU time
  belongs in the process total, even when wall time improves.
- Disable VSync and frame pacing for uncapped rendering comparisons. Measure
  display synchronization separately during a normal interactive run.
- State which phases each harness includes: scene preparation, visibility,
  shading, shadows, composition, upload, display, physics, audio, and UI. A focused
  span benchmark cannot establish end-to-end game FPS.
- Report several runs and their variation. Inspect representative enhanced frames
  for missing objects, bad intersections, incorrect holes, shadows, and clipping.

A virtual-machine or modern-host result is useful comparative evidence but does
not demonstrate 60 FPS on a Core 2 Duo. Confirm the target on the intended machine
before making a hardware FPS claim. Record regressions and workload limitations
alongside improvements.

### Running the benchmark

In a native Linux, BSD, or macOS Release build configured with
`RESTUNTS_BUILD_TESTS=ON`, build the optional target explicitly:

```sh
cmake --build out/sdl3-linux-x64 --target benchmark-hypervision --parallel 2
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy RESTUNTS_RENDER_WORKERS=0 \
    out/sdl3-linux-x64/benchmark-hypervision --data-dir stunts DEFAULT 120
```

Replace the build directory and replay name as needed. The final argument is the
number of measured frames per case; omit it for 120. Each case warms up for eight
frames, then reports mean process CPU time and mean, median, and 95th-percentile
elapsed time in milliseconds per frame. Cases use cockpit and external cameras
at replay ticks 0, 80, and 160 (or a shorter replay's final tick), at all three
resolutions with full-track geometry held fixed. Lower resolutions here isolate
raster cost; they do not select the reduced visibility masks of the normal presets.
The output reports the actual background worker count. Repeat with
`RESTUNTS_RENDER_WORKERS=1` to assess the second Core 2 Duo core.

This batch harness includes scene rendering, framebuffer copies, and checks that
rendering preserves simulation state. Add `--compose` to include final ARGB
framebuffer composition. Display upload, VSync, audio playback, frame pacing,
and physics advancement remain outside the timed loop. It does not measure
complete interactive FPS. The target is excluded from ordinary builds and is
not a CTest timing gate.

### Measuring the rotating track preview

Use `--preview` with `--track` to time the production `draw_track_preview` path:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy RESTUNTS_RENDER_WORKERS=1 \
    out/sdl3-linux-x64/benchmark-hypervision --data-dir stunts --preview \
    --track /path/to/glacier.cstrk --compose 120
```

Each case freezes one of four quarter-turn headings in the 40-second orbit:
visual ticks 0, 1000, 2000, and 3000. The default measures all four at 1280x800.
Add `--all-views` to measure each heading at 320x200, 640x400, and 1280x800,
producing 12 cases. Each case uses the normal eight warmup frames and reports
process CPU time, mean/median/95th-percentile elapsed time, worker count, and
framebuffer hash. An additional diagnostic line reports `preview_tick`, submitted
scene `primitives`, and `detail=default` or `detail=full`.

Add `--preview-full-detail` to disable the preview's 20-tile distance reductions.
This provides a full-mesh comparison using the same orbit and parallel geometry
path. Compare worker counts with the same detail selection; compare default and
full detail separately while reviewing the resulting visual differences.

The timed loop includes preview drawing and checks that gamestate, game
configuration, and RNG seeds are unchanged. `--compose` includes final ARGB
composition. Menu text overlays, display upload, presentation, and the 25 FPS
pacing wait are excluded, so these results measure renderer throughput rather
than the menu's presented FPS. Serial/parallel pixel comparisons run before each
case when multi-core rendering is enabled.

### Workers

Render workers are enabled by default through `--multi-core:on` or the startup
checkbox. `--multi-core:off` prevents worker creation, even if
`RESTUNTS_RENDER_WORKERS` is set. Enabled rendering automatically selects the
logical CPU count minus one; the calling thread also works. For controlled
measurements, set `RESTUNTS_RENDER_WORKERS=0`, `1`, or `3` to compare one, two,
or four total rendering threads. Small workloads can remain serial. DOS renders
serially; worker creation failure retains available workers or serial execution.
Browser workers require a threaded build and an isolated page.

Compare serial execution with one background worker on the two-core target.
Retain parallel work only when its complete-frame elapsed-time benefit outweighs
scheduling, synchronization, and extra CPU cost. Results from the previous
SuperSight worker implementation are historical evidence, not a performance
claim for HyperVision.

### Glacier multi-core measurements (2026-11-03)

A Linux Release build with GCC 14.2 measured the supplied `glacier.cstrk` at the
race start, Countach cockpit view, Full HyperVision at 1280x800. Each batch used
eight warmup frames and 240 measured frames, including final ARGB composition.
CPU affinity selected separate physical cache groups. Forward and reverse
batches produced these averages:

| Physical cores used | Render ms/frame | Throughput relative to one core |
| --- | ---: | ---: |
| 1 | 24.062 | 1.000x |
| 2 | 15.695 | 1.533x |
| 3 | 11.602 | 2.074x |

The individual one-to-two-core gains were 52.2% and 54.8%, meeting the 50% target.
The VM exposes five logical CPUs but only three independent physical cache
groups, with a four-CPU quota. It cannot establish two-to-four-physical-core
scaling or a 90–95% gain per doubling. These are renderer throughput results;
display upload, physics, audio, frame pacing and VSync are excluded.

The original serial renderer averaged 28.68 ms in the same scene. Complete
serial/parallel ARGB comparisons match for Glacier in both cameras at all three
resolutions, and for all 18 DEFAULT replay cases. Glacier's full-size cockpit
frame hash is `503c8616` with zero, one, and three background workers.

Reproduce the supplied-track case with:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy RESTUNTS_RENDER_WORKERS=1 \
    out/sdl3-linux-x64/benchmark-hypervision --data-dir stunts --multi-core:on \
    --compose --track /path/to/glacier.cstrk 240
```

Set the worker count to `0`, `1`, or `2` and use CPU affinity for one, two,
or three physical cores. `--all-views` tests both cameras at all three
resolutions. Enabling workers also performs a complete serial/parallel pixel
comparison before each timed case. The replay form accepts `--start-only` to
restrict measurements to the initial cockpit at 1280x800.

### Glacier CPU optimization follow-up (2026-11-04)

Profiling the same Glacier scene found costs in terrain clipping and worker
coordination. Precise-ground clipping accounted for 23.4% of sampled serial CPU
time. Separate phase timing measured geometry at about 16.0 ms on one core and
8.8 ms on two cores, while building and merging raster commands increased from
about 1.2 ms to 2.0 ms. These diagnostic timings include instrumentation overhead;
the uninstrumented comparison below measures the complete renderer workload.

Geometry joins now reserve a batch once, compute disjoint output offsets, and
copy independent job results in parallel while preserving ordered boundaries for
dynamic work. Precise-ground painting splits both paint regions in one vertex
walk and alternates scratch buffers to avoid duplicate clipping and copies. Its
zero-numerator shortcut preserves signed zero; extended-precision builds retain
the original division expression. Padding each worker's mutable backend-buffer
metadata prevents adjacent jobs from updating the same cache line.

The comparison uses baseline `1072ea42` and the optimized renderer, built with
GCC 14.2 in Linux Release mode (`-O3`, the same instruction-set baseline). Each
configuration ran twice in forward and reverse order, alternating baseline and
optimized binaries. Each batch used eight warmup frames and 240 measured frames:
Glacier at race start, Countach cockpit, Full HyperVision at 1280x800, including
final ARGB composition. Affinity was `0`, `0,2`, or `0,2,4` for one, two, or three
separate physical cache groups, with zero, one, or two background workers.

Values below are mean elapsed milliseconds per frame; parentheses give the range
of the two batch means. Throughput improvement compares the optimized renderer
with the baseline at the same core count.

| Cores used | Baseline ms/frame (range) | Optimized ms/frame (range) | Throughput improvement |
| --- | ---: | ---: | ---: |
| 1 | 24.031 (23.952–24.109) | 21.747 (21.670–21.824) | 10.5% |
| 2 | 15.216 (15.158–15.274) | 12.754 (12.718–12.789) | 19.3% |
| 3 | 11.406 (11.379–11.432) | 9.294 (9.227–9.361) | 22.7% |

Mean process CPU time also decreased: 24.029 to 21.746 ms per frame on one core,
27.924 to 23.496 ms on two, and 28.849 to 23.683 ms on three. The optimized
one-to-two-core throughput gain was 70.5% (1.705x), compared with 57.9% (1.579x)
for the baseline in these paired runs. Three cores reached 2.340x the optimized
one-core throughput. The two-core result improves scaling but remains below a
90% gain.

Every timed case passed a complete serial/parallel ARGB comparison and retained
Glacier's cockpit hash `503c8616`. These measurements cover renderer throughput;
display upload, physics, audio, frame pacing, and VSync remain outside the timed
loop. The VM still exposes only three independent cache groups and a four-CPU
quota, so these results do not establish four-core scaling or interactive FPS on
the target hardware.

All 82 native CTest suites pass. All 18 DEFAULT views and all six Glacier
camera/resolution cases retained the
baseline hashes with one and three background workers, with complete
serial/parallel pixel comparisons. The terrain split also passes 10,240
byte-for-byte comparisons with the original clipping functions, including signed
zero and subnormal cases, under native SSE2 and isolated x87 evaluation modes.
AddressSanitizer and UndefinedBehaviorSanitizer instrumented all 237 game source
units and passed the worker, backend, geometry, extended-render and six-view
Glacier checks. Focused ThreadSanitizer builds instrumented 79 renderer/geometry
units and passed the worker, backend and geometry suites without race reports;
SDL, media libraries and unrelated core code were reused without TSan.

### Further Glacier CPU optimization (2026-11-04)

A new serial profile of `624ff3d9` still spent 17.78% of sampled CPU time in
precise-ground splitting and 4.63% in fragment appending. The renderer now retains
local terrain fragments after cutting out road surfaces. Paint selection,
shoreline splitting, camera transforms, and queue submission run on every frame.
The cache key includes the original terrain-face vertices, complete tile objects,
physical model, yaw, grade mode, and terrain base height. Track load and unload
clear the entries after rendering jobs finish.

Each tile uses a try-lock. Contention, changed inputs, allocation failure, or an
exhausted budget use the original clipping path. The aggregate cache limit is
32 MiB, including 129,600 bytes of fixed slot metadata. Glacier retained
4,814,506 bytes of dynamic entries plus that metadata: 4,944,106 bytes total,
about 4.72 MiB. This trades bounded memory and initial cache-fill work for lower
recurring CPU cost. Newly visible tiles can require further fills. The timings
below measure warmed rendering; they exclude the first frame that fills entries.

A separate two-core probe first initialized the renderer's job buffers, then
reset only the terrain cache. Three full-resolution cockpit fills took
12.39–12.79 ms, followed immediately by warm frames taking 9.00–10.83 ms. This
isolates cache-fill cost from the renderer's first-use allocation work.

Raster bins also classify solid, detail, and patterned commands once when built,
so each scanline visits the relevant lists while retaining submission order
within each class. Exact fixed-point shortcuts avoid unnecessary matrix products
for yaw-only and identity transforms.

The paired comparison uses `624ff3d9` as its baseline, with the same Linux GCC
14.2 Release settings (`-O3`) for both binaries. Each configuration ran twice in
forward and reverse order, alternating baseline and optimized binaries. Each
batch used eight warmup frames and 240 measured frames, with final ARGB
composition included: Glacier at race start, Countach cockpit, Full HyperVision
at 1280x800. Affinity selected `0`, `0,2`, and `0,2,4` for one, two, and three
independent physical cache groups, respectively.

Values are mean elapsed milliseconds per frame; parentheses show the range of
the two batch means. Throughput improvement compares the optimized renderer with
`624ff3d9` at the same core count.

| Cores used | Baseline ms/frame (range) | Optimized ms/frame (range) | Throughput improvement |
| --- | ---: | ---: | ---: |
| 1 | 22.111 (22.063–22.159) | 15.428 (15.320–15.536) | 43.3% |
| 2 | 13.012 (12.970–13.054) | 9.690 (9.687–9.692) | 34.3% |
| 3 | 9.727 (9.242–10.212) | 7.327 (7.192–7.462) | 32.8% |

Mean process CPU time decreased from 22.097 to 15.416 ms per frame on one core,
23.915 to 17.279 ms on two, and 24.334 to 17.498 ms on three. Two optimized cores
provided 59.2% more throughput than one optimized core; the baseline's gain in
these batches was 69.9%. Removing repeated geometry work improves throughput on
both paths, with a larger proportional benefit on one core. The one-to-two-core
gain remains below 90%.

Every timed case passed the complete serial/parallel ARGB comparison and retained
the `503c8616` cockpit hash. These measurements cover warmed renderer throughput,
including composition. They exclude display upload, physics, audio, frame pacing,
and VSync, and do not establish interactive FPS on the target hardware.

All 82 native CTest suites pass. All 18 DEFAULT, six Glacier, and six Ninhbinh
camera/resolution cases retain the `624ff3d9` hashes with one and three background workers, including complete
serial/parallel ARGB comparisons and simulation-state checks. The 4,917-frame
extended-render suite passes, including cold/warm/disabled caching, changed
camera/detail/paint/geometry inputs, reload/unload invalidation, budget fallback,
and concurrent requests for the same tile.

Low-detail extended-track filtering also normalizes occupancy markers before
indexing the legacy object catalog. Regression cases cover all three markers
at five detail levels in player and distant cells.

ASan/UBSan instrumented all 238 game source units and passed workers, backend,
geometry, the full extended-render suite, and six Glacier camera/resolution
comparisons. Focused TSan instrumented 81 game units plus SDL's atomic/spinlock
implementations and passed workers, backend, geometry, concurrent cache stress,
and all six Glacier views without race reports.

### Glacier rotating preview measurements (2026-11-04)

The comparison uses `6d08980c`, whose preview builds geometry serially, and the
renderer with the shared 20-tile object-distance policy and parallel preview
geometry. Both binaries used Linux Release builds with GCC 14.2, Full HyperVision
at 1280x800, and final ARGB composition. Each batch measured 120 frames after
eight warmup frames at each of the four frozen orbit headings. Two batches per
configuration ran in baseline/optimized/optimized/baseline order. CPU affinity
selected separate physical cache groups: `0` for one rendering thread and `0,2`
for the caller plus one background worker.

For each renderer and thread count, the table reports the median of eight case
p50 values: four headings in each of two batches. Throughput improvement compares
these medians at the same thread count.

| Total rendering threads | Baseline median p50, ms | Optimized median p50, ms | Throughput improvement |
| --- | ---: | ---: | ---: |
| 1 | 35.8645 | 31.3835 | 14.3% |
| 2 | 31.1970 | 18.2290 | 71.1% |

Individual two-thread case p50 values ranged from 30.915 to 31.614 ms for the
baseline and 17.966 to 18.583 ms for the optimized renderer. Mean process CPU time
per frame decreased from 36.195 to 31.512 ms with one thread and from 37.799 to
35.360 ms with two. At orbit tick 0, submitted scene primitives decreased from
109,189 to 86,894, a 20.4% reduction.

The timings measure the combined distance-detail policy and parallel geometry
change. They include composition and exclude menu overlays, display upload,
presentation, pacing, and physics. The live preview still presents at most
25 FPS. These are comparative renderer throughput measurements on this host,
with no claim about interactive FPS on the target hardware.

### Earlier results

A paired Linux Release comparison on 2026-10-01 used GCC 13.3 (`-O3`, the same
instruction-set baseline), a KVM guest with three virtual CPUs reporting an
Intel Core i5-4690 at 3.50 GHz, and no CPU affinity pinning. It used the DEFAULT
replay at ticks 0, 80, and 160, with cockpit and external views and full-track
geometry at each resolution. Each case warmed up before measuring 60 frames.
Two runs of each configuration alternated in this order: SuperSight serial,
HyperVision serial, HyperVision with one worker, then the reverse order.

Mean process CPU milliseconds per frame, averaged across the six scenes and
both runs:

| Resolution | SuperSight, serial | HyperVision, serial | CPU reduction | HyperVision, one worker |
| --- | ---: | ---: | ---: | ---: |
| 320x200 | 3.003 | 2.862 | 4.7% | 2.851 |
| 640x400 | 4.592 | 3.908 | 14.9% | 3.948 |
| 1280x800 | 9.222 | 6.926 | 24.9% | 7.064 |

The two HyperVision serial run means ranged from 2.752 to 2.973 ms at 320x200,
3.770 to 4.046 ms at 640x400, and 6.744 to 7.107 ms at 1280x800. Individual scenes
and elapsed-time tails varied with guest scheduling; some low-resolution cases
regressed. These measurements preceded the switch back to original horizon
artwork at 320x200: that row measures HyperVision with enhanced horizons against
SuperSight with original strips. Higher-resolution results are unaffected.

One background worker reduced mean elapsed time from 7.163 to 6.277 ms at
1280x800 (12.4%), with slightly higher process CPU time. At 640x400 elapsed time
changed from 3.984 to 3.652 ms; at 320x200 the difference was small, 2.928 to
2.875 ms. At that point, workers remained opt-in, with a serial default.

These measurements include scene rendering, shadows, frame copies, and replay
state checks; they exclude display upload/composition, VSync, audio, pacing,
and physics advancement. They establish a renderer CPU improvement on this
host, not interactive FPS or 60 FPS on the Core 2 Duo target. No target machine
was available for this change. The old renderer library was preserved before
implementation from revision `246d11fd4155567b3f8e3e475809b2b9e8b42fef`; its game
and SDL3 sources match the pre-change HEAD `91049fac6`. The benchmark source is
available in
[`benchmark-hypervision.c`](../src/restunts/benchmarks/benchmark-hypervision.c).
The [SuperSight measurements](supersight-performance.md) remain the record of
previous experiments.

### CPU optimization follow-up

The follow-up compares against the first HyperVision commit, `492d4770`, with
original horizons at 320x200 in both builds. The same host, compiler, replay,
18 scene/resolution cases, and 60-frame batches were used. Each worker setting
(0, 1, 2) was measured in baseline/candidate order and then in reverse order.

| Resolution | First HyperVision CPU ms | Optimized CPU ms | Reduction |
| --- | ---: | ---: | ---: |
| 320x200 | 2.761 | 2.358 | 14.6% |
| 640x400 | 3.994 | 3.396 | 15.0% |
| 1280x800 | 7.310 | 6.835 | 6.5% |

The changes batch complete skybox rows, emit opaque indexed spans without
repeating overlay checks for every cell, cache planar inverse-depth equations,
remove unused primitive mean-depth work, and reject wholly offscreen primitives
against frustum planes before projection. Skybox rows can also use the existing worker
pool. Jobs own disjoint logical rows and combine overlay-retirement counts only
after joining; unusual aliased sprite rows retain the serial fallback.

Shape preparation remains serial: its shared transform and queue state would
require substantial restructuring, while its measured cost was much smaller
than shading, skybox output, and shadows. Shadow traversal also remains serial.

At 1280x800 the optimized serial elapsed mean was 7.299 ms, versus 7.116 ms with
one worker and 7.531 ms with two workers. Process CPU rose from 6.835 ms to
7.279 and 7.539 ms. Scheduling variation is too large to establish a consistent
worker benefit in that experiment, so its default remained serial. All 18
captured views match byte-for-byte between zero and two workers. The high-resolution
serial elapsed comparison (7.333 to 7.299 ms) is also within this VM's timing
noise; the table reports process CPU time, not a guaranteed FPS increase.

### Moving Glacier visibility optimization (2026-11-04)

The supplied `glacier.cstrk` was measured while accelerating straight ahead for
ten simulated seconds in an automatic Countach, using the cockpit camera with
no dashboard and locked Full HyperVision at 1280x800. The deterministic workload
advances 200 physics ticks at 20 Hz and renders 600 interpolated visual samples
at 60 Hz. It runs without pacing, so reported FPS is renderer throughput,
including physics, interpolation, rendering, state checks, and final ARGB
composition. Display upload, presentation, audio playback, warmup, and pixel/hash
diagnostics are excluded. Ten simulated seconds can take a different amount of
wall time.

A serial sampling profile attributed about 20.9% of CPU time to scanline
conversion and its inlined visibility-span resolution. Visibility insertion now
leaves its scratch partition untouched until an incoming span first wins an
overlap. Fully hidden polygons avoid partition copies; a later first win
reconstructs the unchanged prefix before applying the original depth/decal
rules. Frustum-margin calculations also use standard `fabs`, allowing the
compiler to replace six external math calls per vertex with intrinsic operations
without changing the expressions or clipping margins.

Baseline `5a8f37c9` and the optimized renderer use GCC 14.2, Linux x86-64 Release
(`-O3 -DNDEBUG`, generic x86-64 instruction baseline), on a Xeon Platinum 8573C
VM. Each configuration ran three batches of 600 measured frames, alternating
baseline/candidate order and reversing the core-count order in the middle batch.
Affinity `0` with zero background workers measures one CPU; `0,2` with one
background worker selects two CPUs in separate L2 cache groups. The VM exposes
five logical CPUs, three cache groups, and a four-CPU quota. Both binaries use
the same moving-workload harness and resources.

| CPUs | Baseline FPS (batch range) | Optimized FPS (batch range) | Throughput change |
| --- | ---: | ---: | ---: |
| 1 | 57.51 (56.64–58.31) | 58.39 (54.40–61.10) | +1.5% |
| 2 | 90.36 (89.39–90.86) | 94.55 (93.88–95.14) | +4.6% |

Two-CPU mean elapsed frame time falls from 11.067 to
10.577 ms; total process CPU time, including workers, falls
from 19.662 to 18.791 ms/frame. Single-CPU variation
is too large to establish a reliable gain. These VM measurements do not establish
interactive FPS or performance on the Core 2 Duo target.

Reproduce the moving case with the optional benchmark target:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy RESTUNTS_RENDER_WORKERS=1 \
    taskset -c 0,2 out/sdl3-linux-x64/benchmark-hypervision --data-dir stunts \
    --track /path/to/glacier.cstrk --drive-seconds 10 --compose
```

Choose affinity CPUs appropriate to the host. `--drive-seconds N` requires
`--track` and a positive whole-second duration, and cannot combine with preview,
all-view, or positional frame-count modes. The harness reports both elapsed FPS
and process-CPU FPS, per-frame elapsed percentiles, start/end positions, crash
status, and representative framebuffer and serialized simulation-state hashes.
Serial/parallel comparisons run at ticks 0, 100, and 200 outside the timed loop.

All 600 interpolated framebuffer hashes match the baseline in a separate capture
run. Complete serial/parallel ARGB comparisons match at the three checkpoints,
and the final serialized state hash is `fa0fe5fb` in every batch. The car moves
forward without crashing. Captured middle/end frames were reviewed. Focused
regressions cover fully hidden spans, rejected prefixes and suffixes around a
later visible interval, and attached decals preserving supporting depth/family
at all three resolutions with serial and worker execution. The HyperVision,
shape3d, extended-render, replay-render, worker, framebuffer, interpolation, and
adaptive-quality tests pass; formatting and CRLF checks pass.

### Glacier mesh preparation and composition (2026-11-04)

The next profile after `b880ff1f` attributed about 6.2% of sampled serial CPU
time to `extended_geometry_transform_shape_paint` and 5.0% to final ARGB
composition. The retained changes reduce repeated work in those two phases:

- Identity/default-yaw mesh transforms copy unchanged vertex bytes together.
  Single-paint paved meshes copy their validated primitive records together and
  create their own terminator, without reading a source footer. Other rotations
  and material variants keep their conversion rules; paint selection and the
  renderer's repaint setting are selected once per mesh.
- Indexed 4x4 and 2x2 cells use fixed-width palette conversion. Empty cells look
  up their fallback color once. The general ARGB blending/fade path and pitched
  output remain supported.

The optimized profile attributes about 2.8% of serial CPU samples to mesh
conversion and 1.8% to composition. Diagnostic two-CPU phase timing measures
composition at about 0.30 ms/frame, versus 0.49 ms/frame before the change.
Sampling and phase timings are diagnostic; the uninstrumented whole-workload
comparison below determines the reported throughput improvement.

The baseline is `b880ff1f`. Both versions use the same Linux x86-64 GCC 14.2
Release build, generic instruction baseline, Xeon Platinum 8573C VM, resource
files, Full 1280x800 preset, and acceleration-only automatic Countach cockpit
path described above. Each batch advances 200 physics ticks and renders 600
interpolated samples: ten simulated seconds. Three baseline/candidate batches
alternate order, with the CPU-count order reversed in the middle batch. CPU
affinity is `0` with zero background workers or `0,2` with one background worker.

| CPUs | Baseline FPS (batch range) | Optimized FPS (batch range) | Throughput change |
| --- | ---: | ---: | ---: |
| 1 | 58.30 (58.21–58.48) | 61.25 (61.11–61.53) | +5.1% |
| 2 | 92.75 (91.80–93.89) | 96.86 (96.41–97.12) | +4.4% |

On two CPUs, mean elapsed time falls from 10.782 to
10.324 ms/frame, and total process CPU time including workers
falls from 19.117 to 18.251 ms/frame. FPS includes
physics, interpolation, rendering, and final ARGB composition; display upload,
presentation, audio playback, and pacing are excluded. These are VM throughput
measurements, rather than presented FPS or a Core 2 Duo hardware result. Use the
`--drive-seconds 10 --compose` command above to reproduce the workload.

All 600 interpolated frame hashes match `b880ff1f`; complete serial/parallel ARGB
comparisons at the three checkpoints and final state hash `fa0fe5fb` also match.
Ten targeted CTest suites pass, covering native/extended geometry and replay
physics, rendered replays, visibility, composition, workers, interpolation, and
adaptive quality. Focused tests check mixed primitive records with no source
footer, signed-coordinate extremes, unsupported yaw, empty meshes, independent
storage, fresh terminators, unchanged source buffers, and material variants.
ASan/UBSan also pass for these focused mesh tests with the transform and test
driver translation units instrumented. Source formatting, whitespace, CRLF,
and non-executable file modes are checked.

### Glacier visibility partition searches (2026-11-04)

A line-level profile after `bf900642` attributed about 9.2% of serial CPU samples
to visibility interval insertion, 7.1% to scan conversion, and 2.3% to depth
evaluation. About 4.2% fell on insertion's binary search. The next change reduces
that search and repeated material classification:

- Each scanline keeps a private hint for the last partition interval. An exact
  interval check and one neighbor check can find the next incoming span directly.
  The hint follows partition edits and resets on every row; workers share no
  mutable hint state.
- A uniform binary search handles other positions. Its candidate length halves
  independently of the comparison, allowing GCC to use conditional index moves.
- Both endpoint depths reuse one material classification. The depth expressions,
  decal support rules, tolerance, crossings, and existing span layout remain intact.

A separate serial counter run over the same ten-second drive, including warmup
and diagnostics, processes 23,017,388 incoming intervals in
both versions. Full searches fall from 23,017,388 to
6,961,816; the hint bypasses 69.75% of them.
Binary-search probes fall from 135,512,696 to 53,115,262.
Hint checks are separate from those probe counts. Counter instrumentation is
excluded from the throughput measurements.

The uninstrumented comparison uses baseline `bf900642`, Linux x86-64 GCC 14.2
Release, generic instructions, the same Xeon Platinum 8573C VM and resources,
and locked Full 1280x800 HyperVision. Each batch advances the automatic Countach
straight ahead for ten simulated seconds, using 200 physics ticks and 600
interpolated cockpit frames. Three baseline/candidate batches alternate order;
the middle batch reverses the CPU-count order. This round uses affinity `4` for
one CPU and `2,4` for two CPUs in separate L2 cache groups, with zero or one
background workers. The changed affinity and variable VM load mean absolute FPS
should be assessed within this round's paired measurements.

| CPUs | Baseline FPS (batch range) | Optimized FPS (batch range) | Throughput change |
| --- | ---: | ---: | ---: |
| 1 | 58.05 (57.79–58.49) | 59.89 (59.22–60.53) | +3.2% |
| 2 | 75.74 (74.16–77.49) | 77.65 (75.52–79.72) | +2.5% |

All paired batches improve, although the two-CPU batch ranges overlap. Two-CPU
mean elapsed time falls from 13.203 to 12.879 ms/frame;
total process CPU time including workers falls from 23.328 to
22.707 ms/frame. FPS includes physics, interpolation, rendering,
and final ARGB composition. Display upload, presentation, audio playback, and
pacing are excluded. These remain VM throughput measurements.

Reproduce the two-CPU workload with an appropriate host affinity:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy RESTUNTS_RENDER_WORKERS=1 \
    taskset -c 2,4 out/sdl3-linux-x64/benchmark-hypervision --data-dir stunts \
    --track /path/to/glacier.cstrk --drive-seconds 10 --compose
```

All 600 interpolated frame hashes and all six Glacier camera/resolution hashes
match the baseline. Complete serial/parallel checkpoint comparisons and final
state hash `fa0fe5fb` also match. Eight targeted renderer, replay, worker,
interpolation, and adaptive-quality CTest suites pass. A focused regression builds
adjacent one-pixel supports at varying partition lengths through the full
320/640/1280-pixel widths, then overwrites exact boundary intervals in reverse
order while checking pixels, depths, and families. ASan/UBSan visibility tests
pass with the changed backend and test driver instrumented. Baseline and
optimized visibility tests also pass with `-mfpmath=387 -fexcess-precision=fast`.
Formatting, whitespace, CRLF, and non-executable source modes are checked.

### Glacier edge storage and identity rotations (2026-11-04)

A fresh single-CPU sampling profile of baseline `f2099b9e` attributes about
19.2% of CPU samples to visibility scanning (including inlined partition
insertion), 6.3% to polygon command construction, and 5.8% to vertex transforms.
Two changes reduce work in these paths:

- Screen-space edge records shrink from 48 to 32 bytes. Planar polygons use
  their existing depth plane, so their edges no longer calculate, store, copy,
  or fetch redundant depth interpolation data. Warped polygons retain the same
  binary64 depth expressions in a separate sparse stream. Parallel command
  merges remap both streams with independent, disjoint prefix ranges.
- Instances identify an exact identity object rotation once during preparation.
  Bounded vertices then use equivalent signed-zero arithmetic instead of three
  full matrix dot products. Native signed-word vertices need no range check;
  precise vertices retain finite/range checks. Non-identity rotations,
  non-finite or overflowing coordinates, and extended-precision evaluation
  retain the full matrix path. Camera rotation and translation order stay exact.

Uninstrumented comparisons use Linux x86-64 GCC 14.2 Release on the same Xeon
Platinum 8573C VM, with Full 1280x800 HyperVision and final ARGB composition.
Each run accelerates the automatic Countach straight ahead on `glacier.cstrk`
for ten simulated seconds: 200 physics ticks and 600 interpolated cockpit
frames. Three baseline/candidate batches alternate execution order; the middle
batch also reverses the CPU-count order. Affinity is `4` for one CPU (no
background workers) and `2,4` for two CPUs (one background worker).

| CPUs | Baseline FPS (batch range) | Optimized FPS (batch range) | Throughput change |
| --- | ---: | ---: | ---: |
| 1 | 62.10 (61.53–62.86) | 63.96 (63.73–64.40) | +3.0% |
| 2 | 80.15 (79.87–80.47) | 81.37 (80.88–82.33) | +1.5% |

Every paired batch improves. Two-CPU mean elapsed time falls from 12.477 to
12.290 ms/frame; total process CPU time, including workers, falls from 22.148
to 21.706 ms/frame. These are uncapped VM throughput measurements, including
physics, interpolation, rendering, and composition. Display upload,
presentation, audio playback, and pacing are excluded. Variable VM conditions
mean absolute FPS should be compared within this round. Reproduce the workload
with the preceding `--drive-seconds 10 --compose` command.

All 600 interpolated frame hashes, all six Glacier camera/resolution hashes,
complete serial/parallel checkpoint pixels, and final state hash `fa0fe5fb`
match the baseline. Eight targeted renderer, replay, worker, interpolation,
and adaptive-quality CTest suites pass. Regressions cover mixed planar/warped
parallel builds, sparse-buffer growth, clipped polygons, appended builds, and
reuse with preserved depth. Binary64 transform fixtures check signed zeros,
subnormal and boundary values, non-finite fallbacks, native vertices, camera
rotations, both translation orders, and all four supported rounding modes.
Focused ASan/UBSan tests pass with each changed renderer unit and its test
driver instrumented. Baseline and optimized legacy visibility/transform tests
pass with `-mfpmath=387 -fexcess-precision=fast`; all six Glacier output hashes
also match between those x87 builds. The independent binary64 transform
reference is restricted to the evaluation modes that enable the shortcut.
Formatting, whitespace, CRLF, source modes, and core source line budgets pass.

### Glacier clip setup and command packing (2026-11-04)

A fresh sampling profile of baseline `a084000b` attributes about 6.5% of CPU
samples to polygon command construction, 5.0% to camera-space clip flags, and
5.0% to primitive queueing. Visibility scanning remains the largest individual
cost at 18.4%. A separate source-line profile places about 3.9% of samples on
the final command-record store. The retained changes reduce setup and copying:

- Prepare the four padded frustum coefficients once per instance. Their integer
  and dyadic arithmetic is exact at scales 1, 2, and 4; per-vertex products,
  floating-point margins, and comparisons keep their original order. The
  one-shot clip helper remains available for other callers.
- Stop projected polygon clip tests once the common rejection mask reaches
  zero. Query render scale only for lines and spheres that use it, before
  calculating their sizes.
- Reduce command records from 64 to 48 bytes by using bounded 16-bit edge
  counts, clipped row bounds, and the planar flag, with a compile-time limit
  guard. Depth planes remain binary64 and edge/depth offsets remain 32-bit.
  Compact records reduce command construction and parallel merge traffic.

Uninstrumented Linux x86-64 GCC 14.2 Release comparisons use the same Xeon
Platinum 8573C VM and Full 1280x800 HyperVision. Each run accelerates the
Countach straight ahead on `glacier.cstrk` for ten simulated seconds, with
200 physics ticks and 600 interpolated cockpit frames. Three baseline/candidate
batches alternate execution order, and the middle batch reverses CPU-count
order. Affinity is `4` for one CPU with no background workers and `2,4` for two
CPUs with one background worker.

| CPUs | Baseline FPS (batch range) | Optimized FPS (batch range) | Throughput change |
| --- | ---: | ---: | ---: |
| 1 | 64.82 (63.61–65.88) | 67.05 (66.59–67.52) | +3.4% |
| 2 | 80.71 (79.26–82.92) | 83.53 (82.83–84.79) | +3.5% |

Every paired run improves; the two-CPU batch ranges overlap slightly. Two-CPU
mean elapsed time falls from 12.391 to 11.972 ms/frame, and total process CPU
time including workers falls from 21.711 to 21.149 ms/frame. FPS includes
physics, interpolation, rendering, and final ARGB composition. Display upload,
presentation, audio playback, and pacing are excluded. These are uncapped VM
throughput measurements; compare absolute FPS within this round.

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy RESTUNTS_RENDER_WORKERS=1 \
    taskset -c 2,4 out/sdl3-linux-x64/benchmark-hypervision --data-dir stunts \
    --track /path/to/glacier.cstrk --drive-seconds 10 --compose
```

All 600 interpolated frame hashes, complete serial/parallel checkpoint pixels,
all six native Glacier camera/resolution hashes, and final state hash
`fa0fe5fb` match the baseline. Eight targeted renderer, replay, worker,
interpolation, and adaptive-quality CTest suites pass. New regressions check
known padded frustum boundaries, one-ULP margins, near depths, and context
refresh at every scale. A four-corner outline and its maximum 64-edge
subdivision produce identical pixels, depth, and family values with zero,
one, and three background workers across all three resolutions.

Focused ASan/UBSan visibility and geometry tests pass with all three changed
renderer units and their drivers instrumented; other native objects are reused.
Baseline and optimized visibility/geometry suites also pass with
`-mfpmath=387 -fexcess-precision=fast`, including sphere queueing. All six
Glacier view hashes match between these x87 builds. Formatting, whitespace,
CRLF, non-executable modes, and core source line budgets pass.

### Glacier indexed raster copies and native reads (2026-11-04)

A fresh single-CPU profile of baseline `e199b772` attributes about 4.2% of CPU
samples to the serial companion-cell raster pass and 5.4% to vertex transforms.
An operation diagnostic counts about 679 million high-resolution sample copies
across startup, warmup, checkpoints, and the drive; all eligible raster calls
use indexed COPY with no active ARGB cells. Two changes reduce this work:

- Plain indexed copies move complete 1-, 4-, or 16-byte companion cells directly,
  with uniform-color filling for cells without source samples. Forward traversal
  preserves cascading overlaps and 16-bit address wrapping. Valid self-copies
  keep their existing samples; empty self-copies materialize the fallback color.
  The shared cleanup and generation update run after either path. Existing
  generic handling continues to process overlays and other raster operations.
- Native vertex transformation reads the three signed little-endian coordinates
  directly into the high-resolution vector. This removes an out-of-line helper
  call and its temporary word vector. Validated indices fit the packed resource
  offset, and coordinate values, half scaling, rotations, and translation order
  retain their existing arithmetic.

Uninstrumented Linux x86-64 GCC 14.2 Release comparisons use the same Xeon
Platinum 8573C VM, Full 1280x800 HyperVision, and final ARGB composition. Every
run accelerates the automatic Countach straight ahead on `glacier.cstrk` for
ten simulated seconds: 200 physics ticks and 600 interpolated cockpit frames.
Five baseline/candidate pairs per CPU count alternate execution order, with
CPU-count order also reversed on alternating batches. Affinity is `4` for one
CPU with no background workers and `2,4` for two CPUs with one background worker.

Large timing outliers affect both versions. The table therefore reports median
complete-run FPS across all five batches, retaining every batch in the ranges.
Single-CPU throughput is essentially unchanged within this variability; all five
two-CPU pairs improve.

| CPUs | Baseline median FPS (batch range) | Optimized median FPS (batch range) | Median change |
| --- | ---: | ---: | ---: |
| 1 | 60.40 (44.75–61.06) | 60.15 (41.96–63.67) | -0.4% |
| 2 | 74.22 (55.94–75.69) | 81.01 (78.57–81.47) | +9.2% |

Two-CPU median elapsed time falls from 13.474 to 12.344 ms/frame; median total
process CPU time including workers falls from 23.496 to 22.143 ms/frame. These
are uncapped VM throughput measurements, including physics, interpolation,
rendering, and composition. Display upload, presentation, audio playback, and
pacing are excluded. Absolute FPS should be compared within this round.
Reproduce with the preceding `--drive-seconds 10 --compose` command.

All 600 interpolated frame hashes, complete serial/parallel checkpoint pixels,
all six native camera/resolution hashes, and final state hash `fa0fe5fb` match
the baseline. Eight targeted renderer, replay, worker, interpolation, and
adaptive-quality CTest suites pass. New COPY fixtures cover all scales,
independent source/destination wrapping, mixed valid and empty cells, guarded
neighbors, valid and empty self-copies, cascading forward overlap across the
address wrap, and ARGB fallback/retirement. The fixtures also pass against the
baseline implementation.

Focused ASan/UBSan composition and geometry tests pass with both changed
renderer units and their test drivers instrumented; other native objects are
reused. Baseline and optimized composition/geometry tests also pass with
`-mfpmath=387 -fexcess-precision=fast`, and all six Glacier view hashes match
between those x87 builds. Formatting, whitespace, CRLF, non-executable modes,
and core source line budgets pass.

### Glacier prepared pine submissions (2026-11-05)

Profiling baseline `381f9122` on the supplied Glacier track found repeated mesh
preparation underneath grove rendering. Each procedural pine was rotated,
scaled and scattered into a validated single-paint mesh, then immediately
allocated and copied again with identity rotation before submission. Flat groves
now submit that prepared mesh directly, borrowing its buffers until synchronous
queueing completes. Sloped groves retain their mutable copy and existing grade
and full-detail anchor handling. This removes one allocation, primitive walk,
mesh copy and free per submitted flat tree, without a persistent mesh cache.

Uninstrumented GCC 14.2 Linux x86-64 Release builds (`-O3 -DNDEBUG`) were compared
on an AMD EPYC 9V74 VM with five exposed CPUs and a four-CPU quota. Each run
accelerates the automatic Countach straight ahead for ten simulated seconds on
`glacier.cstrk`: 200 physics ticks and 600 interpolated cockpit frames, locked
Full HyperVision at 1280x800 with final ARGB composition. Physics, interpolation,
rendering, state checks and composition are timed; display upload, presentation,
audio playback, pacing, warmup and hash diagnostics are excluded.

Three one-CPU pairs and five two-CPU pairs alternate baseline/candidate order;
CPU-count order is also reversed between batches. Affinity is `4` with no
background workers and `2,4` with one background worker. The table reports
median complete-run FPS and retains every final batch in the ranges.

| CPUs | Baseline median FPS (range) | Optimized median FPS (range) | Median change |
| --- | ---: | ---: | ---: |
| 1 | 93.69 (91.38–96.38) | 95.03 (92.16–95.88) | +1.4% |
| 2 | 143.69 (131.38–151.88) | 145.12 (134.51–145.72) | +1.0% |

Median process CPU time including workers falls from 12.590 to 12.468 ms/frame
on two CPUs. These are small changes with overlapping ranges and mixed paired
results; this VM run does not establish a reliable end-to-end FPS gain. The
optimization removes redundant preparation while preserving the render work.
Reproduce using CPUs appropriate to the host:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy RESTUNTS_RENDER_WORKERS=1 \
    taskset -c 2,4 out/sdl3-linux-x64/benchmark-hypervision --data-dir stunts \
    --track /path/to/glacier.cstrk --drive-seconds 10 --compose
```

All 600 driving-frame hashes and all six native camera/resolution hashes match
the baseline. Complete serial/parallel checkpoint images and the final state
hash `fa0fe5fb` match; the car finishes the drive without crashing. Eight focused
renderer, worker, replay, interpolation and adaptive-quality CTest suites pass.
Existing grove submission tests cover both group sizes, all four rotations,
flat and sloped ground, both detail levels, and classic/HyperVision rendering.
A focused ASan/UBSan run instruments the changed submission unit and extended
render test driver; all 4,943 rendered regression frames pass with leak checking.

### Glacier uniform indexed-cell composition (2026-11-05)

A fresh profile of baseline `824e104a` attributed about 6.7% of serial CPU
samples to final ARGB composition. A diagnostic run including startup, warmup
and checkpoints found 36,926,284 uniform indexed cells and 2,369,716 mixed cells:
94.0% of the 4x4 cells contained one palette index. There were no empty or ARGB
cells in this workload. Uniform indexed cells now reuse one palette lookup and
the existing four-pixel-wide fill, replacing sixteen palette lookups. The test
uses unaligned-safe word copies and preserves every palette bit, including
alpha. Mixed cells, smaller scales and ARGB overlays retain their existing paths.

Uninstrumented GCC 14.2 Linux x86-64 Release builds (`-O3 -DNDEBUG`, generic
x86-64 instructions) were compared on the same AMD EPYC 9V74 VM: five exposed
CPUs and a four-CPU quota. Each run accelerates the automatic Countach straight
for ten simulated seconds on `glacier.cstrk`, rendering 600 interpolated cockpit
frames from 200 physics ticks, locked Full HyperVision at 1280x800. Physics,
interpolation, render/state checks and final ARGB composition are timed; display
upload, presentation, audio, pacing, warmup and hash diagnostics are excluded.

The final production build uses the normal link order. Three one-CPU pairs and
five two-CPU pairs alternate baseline/candidate order; CPU-count order also
reverses between batches. The main thread is pinned to CPU 4. In two-CPU runs,
one background renderer is pinned to CPU 2 using a temporary thread-creation
shim; its affinity setup occurs outside timing. `/proc` observations confirmed
both placements. This limits worker migration while preserving the same code
and worker count for both versions. The table retains every final batch.

| CPUs | Baseline median FPS (range) | Optimized median FPS (range) | Median change |
| --- | ---: | ---: | ---: |
| 1 | 90.16 (86.32–91.40) | 90.45 (83.11–96.60) | +0.3% |
| 2 | 126.98 (126.16–132.10) | 137.23 (115.02–138.67) | +8.1% |

Two-CPU median process CPU time including workers falls from 14.269 to
13.213 ms/frame; median elapsed time falls from 7.876 to 7.287 ms/frame.
Single-CPU results show no reliable gain. VM variation remains substantial,
including an optimized two-CPU outlier; three of five two-CPU pairs improve.
These are uncapped throughput measurements on this host, not presented FPS
or target-hardware results. Reproduce the workload with the existing
`--track /path/to/glacier.cstrk --drive-seconds 10 --compose` benchmark options;
the per-thread placement above is part of this measurement setup.

All 600 driving-frame hashes and all six native camera/resolution hashes match
the baseline; complete serial/parallel checkpoint images and final state hash
`fa0fe5fb` also match. The car completes the drive without crashing. New
composition tests pass against both baseline and optimized code. They cover
uniform companion colors that differ from legacy fallback pixels, zero and
nonopaque palette alpha, every possible single differing sample in a cell,
guarded padded output, source preservation, all scales and serial/worker output.
Eight focused renderer, worker, replay, interpolation and adaptive-quality CTest
suites pass. A focused ASan/UBSan build instruments all seven sources of the
composition test target and passes with leak checking. Formatting, whitespace,
CRLF line endings, non-executable source modes and core source budgets pass.

### Selective Haswell backport on contd2 (2026-11-05)

This branch starts at `969157ef` and applies the selected changes from
`f0109925` and `b5cc2368` for a separate Haswell performance comparison.
The original per-commit measurements describe cumulative versions; they do
not predict the performance of this new combination.

The contained-span and constant-depth work from `f0109925` is adapted to the
earlier visibility implementation. Its direct partition path requires at
least one unchanged end; exact whole-span replacements retain the base's
general path, excluding the earlier shortcut from `60254eef`. Private record
declarations move to an internal header with unchanged layouts. The complete
visibility regression and constant-depth bit tests accompany the backport.
Worker dispatch remains identical to `969157ef`.

The cell-candidate index and query tests from `b5cc2368` apply independently.
They retain candidate order, live footprint checks and track-storage lifetime.
The pine-mesh cache from `ef66b23a` and the indexed-cell/clipping changes from
`0c9c3d15` are excluded.

Nine focused native track, renderer, worker, replay, interpolation and
adaptive-quality suites pass. A ten-simulated-second Glacier drive compares
all 600 driving-frame hashes against `969157ef`; all match. Six
camera/resolution hashes, full serial/parallel checkpoint pixels and final
state `fa0fe5fb` also match, with no crash. Hash capture is diagnostic and its
FPS is excluded from performance comparisons.

HyperVision, composition and track checks each pass ASan/UBSan with leak
checking and TSan. Both baseline and backport pass the HyperVision and
composition x87 drivers, including stored-depth bit comparisons. Formatting,
CRLF endings, source modes and core line budgets pass. The combined branch
still needs a fresh Haswell measurement.

## Validation

The native Linux Release build and optional benchmark build pass. All 82 native
CTest suites pass, including host regressions, projection/visibility, worker
consistency, horizon artwork, track previews, interpolation, and presentation.
Geometry tests compare ordered serial/parallel scenes, live vertices, shape and
depth-family remapping, shadows, material overrides, origins, shared bounding
rectangles and serial callbacks. Composition tests compare faded overlays and
padded output rows at every scale.

Extended-object regressions cover the exact 20-tile cutoff, diagonal and vertical
camera distances, multi-tile footprints, native and rotated overlay bounds,
wrapped world coordinates, missing low meshes, and actual high/low submissions.
Overview fixtures compare full and distance-reduced previews with zero, one, and
three background workers while preserving gamestate, configuration, RNG, and
viewport clipping. The extended-render suite passes 4,943 frames.

The preview follow-up also passes AddressSanitizer/UndefinedBehaviorSanitizer and
focused ThreadSanitizer runs of the worker, backend, geometry, and overview
fixtures, plus all twelve real Glacier heading/resolution cases. Serial/parallel
ARGB hashes match in every case. The TSan build instruments preview preparation,
procedural meshes, math, renderer/cache code, and SDL atomic/spinlock sources;
other native dependency archives are reused.

AddressSanitizer and UndefinedBehaviorSanitizer pass the worker/backend/geometry
suites and all six Glacier camera/resolution cases, including pixel comparisons.
Focused ThreadSanitizer builds pass the worker tests and full geometry suite;
SDL and unrelated native archives were reused without TSan instrumentation.

The replay harness compares complete authoritative state and RNG values with
classic rendering, HyperVision, repeated renderer switching, and adaptive quality
changes. DEFAULT (10 Hz), DEFCRSH, 0A0A, HARDLAND, and SHAKING (20 Hz fixtures)
produce identical records across all four modes. Extra interpolated frames cover
ordinary playback, a hard landing, and a loop exit without changing physics.

Source formatting, EditorConfig, CRLF, and whitespace checks pass. Browser Release
builds with and without threading compile; the threaded build passes Chromium
smoke checks with actual renderer workers. The serial build passes on ordinary
HTTP; this environment's browser policy blocks local-file navigation. Windows,
DOS, and Android native cross-builds were not run here. Desktop launcher code
compiles and links, and Android Java launch-option regressions pass.
