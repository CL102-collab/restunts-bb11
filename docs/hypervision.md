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

## Multi-core rendering

`--multi-core:on` enables a persistent worker pool; `--multi-core:off` is the
default. The desktop, Android, and browser Graphics settings expose the same
selection. DOS32 remains serial. Browser threads require a threaded build and
cross-origin isolation, as described in [the browser guide](wasm.md).

Extended-track terrain and objects are prepared in private geometry contexts.
Jobs own their projected vertices, shape metadata, shadow receivers, origins,
and bounding rectangles. The caller joins groups in the original submission
order, remaps shape and depth-family indices, and inserts cars, start flags,
and ghost lines at the same points as serial rendering. Material overrides and
shadow receiver heights are resolved when groups join.

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

### Workers

Render workers are opt-in through `--multi-core:on` or the startup checkbox.
With the default `--multi-core:off`, no workers are created, even if
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
2.875 ms. Workers therefore remain opt-in, with a serial default.

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
worker benefit here, so the default remains serial. All 18 captured views match
byte-for-byte between zero and two workers. The high-resolution serial elapsed
comparison (7.333 to 7.299 ms) is also within this VM's timing noise; the table
reports process CPU time, not a guaranteed FPS increase.

## Validation

The native Linux Release build and optional benchmark build pass. All 82 native
CTest suites pass, including host regressions, projection/visibility, worker
consistency, horizon artwork, track previews, interpolation, and presentation.
Geometry tests compare ordered serial/parallel scenes, live vertices, shape and
depth-family remapping, shadows, material overrides, origins, shared bounding
rectangles and serial callbacks. Composition tests compare faded overlays and
padded output rows at every scale.

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
