# Documentation for users

## Running the game

Desktop builds include a [native startup window](launcher.md). Run
`restunts-launcher` (`restunts-launcher.exe` on Windows) to choose your original
game folder, startup options and Fullscreen setting. macOS packages also include
`Restunts.app`; AppImages open this window when started without arguments.

Desktop packages also include [a ghost generator window](genghost.md). Run
`genghost-gui` (`genghost-gui.exe` on Windows), or open `Genghost.app` on macOS,
to configure and run the included command-line generator. With a newly built
AppImage, run `restunts.AppImage --genghost` to open this window.


The experimental [Amiga 500+ port](amiga.md) builds a native AmigaOS
executable from the classic DOS game core. See its current performance limits.

An SDL3 [WebAssembly build](wasm.md) runs directly from a local HTML file
without a web server. Only enhanced artwork is embedded; select your own
original game folder in the browser before starting.

The [TempleOS/TinkerOS port](../src/restunts/platform/templeos/README.TXT) includes
HolyC sources, an uppercase-file packager, and an isolated QEMU test workflow.
The [stock TempleOS installation guide](templeos.md) covers the game CD,
installation on a fresh system, controls, and rebuilding the package. The port
projects and rasterizes 3D geometry at 640x400, then applies the DOS game's
4:3 display correction to fill the entire 640x480 display. Original 320x200
bitmap artwork and UI retain their source detail, displayed at 2x horizontal
and 2.4x vertical scale using nearest-neighbor sampling.

All builds require the original Broderbund Stunts 1.1 game data (12 Feb. 1991).
Startup checks the `gver` text in `MISC.RES`, or `MISC.PRE` when no unpacked
resource exists, and reports an error if it is missing, invalid, or different.
Other resources are not compared, so custom cars, graphics, opponents, tracks,
and horizons remain supported.

Run `restunts.exe` in DOSBox or DOSBox-X with `core=dynamic`, `cycles=max`, and `aspect=true`.
Mount `stunts/` directly as a DOS drive in the emulator.

For DOSBox-X, use these recommended settings in the `[dos]` section of your
configuration file:

```ini
[dos]
# DOS version (DOSBox-X only)
ver=7.1

# Enable XMS and UMB so the shell and kernel can use upper memory.
xms=true
umb=true

# Keep the shell and DOS kernel out of conventional memory (DOSBox-X only).
shellhigh=true
kernel allocation in umb=true

# Enable long file name support (DOSBox-X only)
lfn=true

# Disable DOSBox-X's low-memory padding to free conventional memory.
# Original dump tools need this space when loading large custom dashboards.
minimum mcb free=1
```

XMS and UMB must be enabled for the shell and kernel to load high. These settings
leave enough conventional memory for large custom dashboards and their parsing
workspace; `shellhigh=true` alone cannot do that when UMB is disabled.

### Sound modes

The desktop, Android and browser **Sound mode** list is ordered **No sound**,
**PC Speaker**, **Tandy**, **Ad Lib**, **MT-32**. Ad Lib remains the default,
and saved launcher preferences keep their meaning. `--soundmode:pc-speaker` selects the
original PC Speaker music and effects, using the original PC instrument resources
without ROMs or additional dependencies. Desktop, Android and browser builds
render that sound as PCM through the normal audio output. DOS32 instead drives
the real PC speaker; `--soundmode:adlib` uses its hardware OPL chip.

Desktop, Android and browser builds support `--soundmode:tandy`, using the
original TD instrument resources for Tandy three-voice music and effects. The
SDL3 backend emulates the NCR8496/PSSJ-3 sound generator and renders PCM through
the normal audio output. No ROM files are needed. Tandy emulation is unavailable
in DOS32, DOS16, Amiga and TempleOS builds.

Desktop, Android and browser builds also emulate Roland MT-32 music and effects
with `--soundmode:mt-32`, using separately supplied control/PCM ROMs. See
[MT-32 setup](mt32.md) for ROM-folder detection and model selection.
`--nosound` (or `/ns`) disables both music and effects, overriding sound-mode
selection regardless of argument order. Original DOS16 driver selection is unchanged.

### Replacement music

The launcher's **OGG music replacement** checkbox (formerly **New MIDI**) controls
recorded replacement songs independently of the selected synthesizer. Use
`--ogg:on` to enable replacements or `--ogg:off` to hear the selected original
synthesizer. Replacement music is off by default on every platform; saved
launcher preferences still take precedence.
Press **Shift+F10** during the
intro, menus, or race-end/victory screens to switch to the included Ogg music.
Press it again to return to the selected synthesizer at the same song position. The selection lasts
for the current game session; the shortcut is inactive during races. Plain
**F10** keeps its track-editor function. Missing or broken recordings use the
selected original synthesizer.
See [replacement music details](sdl3.md#optional-ogg-vorbis-music).

### Dashboard visibility

The dashboard starts visible when you begin or restart a drive. Uncheck
**Show Dashboard** in the desktop, Android, or browser launcher's
**Graphics settings**, or pass `--dashboard:off` to start with it hidden.
`--dashboard:on` selects the default. Press **D** during a drive or replay
to toggle the dashboard.

### F3 camera views

Press **F3** to select the original adjustable camera. Press F3 again to cycle
through three chase views, then back to the original view:

1. Original F3 view, with its existing angle, zoom, and dashboard visibility.
2. Three right steps and two down steps from the default angle, with two zoom-in steps.
3. The same chase angle at the default zoom.
4. The same chase angle with three zoom-out steps from the default zoom.

The three chase presets hide the dashboard and raise their viewing angle on
descents to stay above the car and sloping road behind it. Cycling back to the
original view or selecting another camera restores the original F3 position and
prior dashboard visibility. Returning to F3 from another camera starts with the
original view. Replay pan and zoom controls still work; the next F3 preset uses its fixed position.

<a id="supersight-and-fps-display"></a>

### Classic video modes

SDL3 desktop, browser and Android builds support **VGA** (the default), **EGA**,
**CGA**, and **Tandy** in the **Video mode** dropdown under **Graphics settings**.
The choice is remembered separately from HyperVision. Command-line launches use
`--videomode:vga`, `--videomode:ega`, `--videomode:cga`, or `--videomode:tandy`.
DOS32 keeps its existing VGA/EGA/CGA choices and has no startup window. Tandy
video is unavailable in DOS32, DOS16, Amiga and TempleOS builds.

EGA, CGA and Tandy use the original DOS bitmap conversion tables, material
colors, and dithering patterns. Tandy reproduces the original 320x200, 16-color
mode. EGA also displays sixteen colors; CGA uses the original
black, cyan, magenta, and light-gray palette. Custom bitmap resources without
conversion tables use color matching. HyperVision always retains VGA graphics,
regardless of the selected classic mode. Switching HyperVision off restores
the selected classic graphics. Original 16-bit DOS video-driver selection is
unchanged.

### HyperVision and FPS display

Press **F12** while driving or viewing a replay to toggle HyperVision. In SDL3
builds (Windows, Linux, BSD, Haiku, macOS, 32-bit DOS, and WebAssembly), it starts with
the entire 30 x 30 track, detailed models, and 1280x800 internal rendering. Driving and replay rendering
adapt to sustained CPU load to target 60 FPS, using these stages in order:

| Stage | Visible area and geometry | Internal rendering |
| --- | --- | --- |
| 0 | Full track, full geometry | 1280x800 |
| 1 | Large camera mask below | 1280x800 |
| 2 | Large camera mask | 640x400 |
| 3 | Large camera mask | 320x200 |
| 4 | Small camera mask below; minimum quality | 320x200 |

The masks below face north. `C` is the active camera tile, `H` selects full
geometry, `L` selects available low geometry, and `.` excludes the tile. A
missing low model falls back to its full model. Both masks follow the active
camera and rotate with its viewing direction, including diagonal headings.

```text
Large mask    Small mask
..LLLLL..     LLLLL
.LLLLLLL.     LLLLL
.LLLLLLL.     LHHHL
LLHHHHHLL     HHHHH
LLHHHHHLL     .HCH.
LLHHHHHLL
LLHHHHHLL
.LLHHHLL.
..LHHHL..
...HCH...
```

Pressing **F12** toggles **Auto** and **Off**. Pressing it while a locked preset
is active turns HyperVision off; the next F12 selects Auto. **Shift+F12** enables
HyperVision with **Full**, then cycles through these locked presets:

| Preset | Stage | View | Internal resolution |
| --- | ---: | --- | --- |
| Full | 0 | Full HyperVision | 1280x800 |
| High | 1 | Large mask | 1280x800 |
| Medium | 2 | Large mask | 640x400 |
| Low | 3 | Large mask | 320x200 |

Shift+F12 after Low returns to Full. From Off or Auto, Shift+F12 starts at
Full. Locked presets remain selected across race/replay resets and do not
automatically reduce or restore quality. The small-mask stage is available only
in Auto. Each F12 or Shift+F12 press displays `HyperVision: <name>` for two seconds
below the FPS and render-time lines, even when F11 is disabled.

To start with adaptive HyperVision, pass `--hv:auto`. To start with a locked
preset, pass exactly one of `--hv:full`, `--hv:high`, `--hv:medium`, or `--hv:low`
to the executable. For example:

```sh
./restunts --hv:medium
```

The preset is active from the intro onward. These options are case-insensitive.
Multiple preset options, including duplicates, and unknown presets report an
error and exit. Without a preset option, HyperVision starts off as usual. The
browser launcher enables `--hv:auto` by default through its **HiperVision**
checkbox. F12 and Shift+F12 can change the selection later. The options apply to SDL3 builds,
including 32-bit DOS.

An entire multi-tile object stays visible if any of its tiles is inside the
mask. Any `H` overlap keeps full geometry; otherwise an `L` overlap uses low
geometry when available. The same policy applies to rendered cars: their occupied
tiles determine visibility and geometry. `C` always keeps full geometry. The
graphics menu's scenery setting still applies.

A sustained overload reduces one stage after about half a second at the target
frame rate; isolated stalls do not trigger a reduction. About three seconds of
frames with spare processing time restore one stage. Failed recovery attempts
increase the wait before trying again. The small mask at quarter resolution is the floor;
there are no further reductions if the machine still cannot reach 60 FPS.

With **Widescreen** enabled (`--widescreen:on`), HyperVision races and replays
use the widest supported aspect ratio fitting the current window or display.
Other screens and classic rendering keep their 4:3 image with black side borders.
Widescreen starts off; `--widescreen:off` explicitly disables it. Desktop, Android,
and web launchers remember the checkbox, immediately before Show FPS. The initial
desktop window and web canvas follow the display's supported widescreen ratio.

| Display aspect | Full / High | Medium | Low |
| --- | --- | --- | --- |
| 20:9 (desktop, Android, web) | 1600x720 | 800x360 | 400x180 |
| 16:9 | 1280x720 | 640x360 | 320x180 |
| 16:10 | 1280x800 | 640x400 | 320x200 |
| 3:2 | 1080x720 | 540x360 | 270x180 |

Auto uses the same resolution steps. On desktop, Android, and web, displays
wider than 20:9, including 21:9, center the 20:9 image with black side borders.
Displays narrower than 3:2 retain 4:3. Racing reveals more scenery at the sides
without stretching the scene. Dashboard artwork remains centered with its original
proportions, leaving scenery visible beside it. Corner HUD text follows the race
image's edges, and replay controls remain centered. DOS32 selects a suitable VESA
mode and fits the widest available aspect up to 16:9 inside it.

Quality-level resolution changes affect only the 3D scene. The completed frame
stays at the maximum resolution for its aspect ratio, preserving dashboard,
replay controls, OSD text, restored backgrounds and other artwork at the same
quality in every preset. Menus and other 2D screens remain high resolution.
The selected 3D resolution also applies to the intro night-driving scene,
showroom car and 3D track preview; their text and artwork stay full resolution.
The output keeps its selected presentation aspect, window/fullscreen size, and
DOS VESA display mode.
At quarter resolution, HyperVision disables car shadows and uses the original horizon artwork. This
applies to locked Low and both of Auto's lowest stages; higher resolutions restore
shadows and enhanced horizons. Physics and replay data are unaffected. Switching HyperVision
off restores the original draw distance, detail policy, and rendering limits;
enabling it starts at full quality.

HyperVision replaces the SDL3 SuperSight rasterizer with visibility spans resolved
from polygon depth, without painter-style sorting. Its target is a 2 GHz Core 2 Duo;
small visual differences are acceptable to reduce CPU cost. Classic rendering and
replay compatibility remain separate requirements. See the
[HyperVision architecture and measurement notes](hypervision.md).
The earlier enhancement was inspired by Alberto Marnetto's
[SuperSight](https://marnetto.net/2025/02/20/broderbund-stunts-1).

The Open Watcom 16-bit DOS build retains its 110-tile SuperSight mode, with
up to 592 primitives in a 13 KiB rendering buffer and reduced distant detail
or visibility when crowded scenes exceed that capacity. F12 toggles it on and off
and displays `SuperSight: On` or `SuperSight: Off` for two seconds, even when the
FPS counter is hidden. This build has no SuperSight presets.

In SDL3 builds, HyperVision starts 3D rendering at **1280x800**, four times the
original width and height. Player and opponent
car-selection previews use the same higher resolution; F12 also works in those
screens, in the main menu, and in the track preview. The main-menu, showroom
and opponent-selection backgrounds use [restored pixel artwork](menus/README.md)
at twice the original width and height while HyperVision is enabled, like the
enhanced opponent portraits.
Turning it off restores the original backgrounds. Dashboard artwork, replay
controls, and other menu UI retain their original pixel detail and size. HyperVision clips
custom dashboards and the 3D view above visible replay controls so they remain
unobstructed. Switching HyperVision off restores 320x200 rendering and the original
dashboard layout.

For `.cstrk` tracks, the track menu shows a rotating 3D view while HyperVision
is enabled. The camera circles the average center of road and stunt tiles on
complete lap routes, counting each occupied tile equally. Disconnected pieces,
dead ends and scenery do not affect its center or bounding box. It looks toward
that center at 45 degrees down, at least twelve hill levels above it; larger
courses raise the orbit enough to keep all lap pieces visible. Tracks without
a valid complete lap use the map center. The scene extends behind the title and
best recorded time. The live preview is capped at 25 FPS. F11 toggles
FPS and render time over the preview; the counter measures presented preview
frames, and timing excludes pacing and display synchronization. Turning
HyperVision off restores the 2D surface map.

Extended-track driving, replays, and the rotating preview use available authored
low-detail meshes for objects farther than 20 tile lengths from the camera,
including the Full preset. Distance is measured in 3D to the nearest point of the
complete object's bounds, including its footprint, height, overlays, grove trees,
and grade. Camera height counts too. Any part within 20 tiles, including exactly
20 tiles, keeps full detail; models without a low alternative keep their full
meshes. Vine fields, terrain meshes and road cutouts keep their full geometry,
and vehicles keep their existing detail policy. Multi-core rendering also
prepares the preview's geometry in parallel.

HyperVision can use optional car meshes with up to 65,535 vertices and
primitives per mesh. Their `.H3D` companions provide enhanced geometry while
the original model remains available to the classic renderer. See
[HyperVision car meshes](hypervision-meshes.md) for the format and fallback
behavior. The McMurtry companion imports detailed exterior geometry from the
supplied high-resolution source, with palette materials and native animated
wheels. Its classic fallback and driving physics retain the existing model.

In SDL3 builds, HyperVision corrects each car model's visible ride height so
its underside meets the driving surface, including cars with spherical wheels.
This presentation offset follows the car's rotation and preserves suspension
movement and jumps without changing physics or replay data. The original renderer retains the original car positioning.

HyperVision uses fewer segments for wheels and spheres that occupy only a small
part of the screen. Segment counts follow their projected size; nearby round
shapes retain their full detail. Full HyperVision keeps full authored meshes on
classic tracks. Classic reduced-detail rendering can select authored low-detail
meshes. Extended tracks use the 20-tile object-distance policy above.

In SDL3 builds, HyperVision adds car-shaped shadows on nearby surfaces below
each car. Small cached silhouettes follow the model's size, body, wheels, and
suspension. Light comes from the south at about 70 degrees above the horizon,
with a short northward extension and soft edges. Shadows shrink as the car
rises above the surface. Opaque roads and ramps block shadows from reaching
surfaces below them. Grille surfaces receive half-strength shadows and pass half
the shadow strength to the next surface; successive grilles halve it again.
Cars do not receive shadows, ghosts do not cast them, and the viewed car's own
shadow is hidden in the F1 cockpit camera. The player
and opponent car-selection showrooms use the same shadows beneath their rotating
car previews while HyperVision is on at 640x400 or 1280x800.

HyperVision also uses [AI-refined skybox artwork](skyboxes/README.md), with
each horizon image at four times its original width and height, at 640x400 and
1280x800. For built-in horizons, the 320x200 mode and classic rendering use the
original skybox artwork. Custom horizons generate their classic artwork from
the installed PNG. Banked and upside-down views remain supported at every HyperVision resolution.
Missing enhanced PNGs fall back to the original strips. The Open Watcom 16-bit DOS
version retains its existing renderer.

The opponent-selection and opponent car-selection screens also use
[enhanced portraits](opponents/README.md) while HyperVision is on. The final set
in `assets/opponents/game/` contains
160 x 166 tiles, twice the original width and height, with the exact game palette
and solid backgrounds. F12 switches portraits in both menus; missing or unreadable
replacements fall back individually to the originals. Original numbered labels
and the clipboard frame are preserved. The selected full-resolution sources and
4x working tiles are archived in `docs/opponents/game-sources/` for regeneration.

In SDL3 builds, you can also supply your own opponent win/lose animations as
**WebM videos encoded with VP8**. Put `opp1win.webm`, `opp1lose.webm`, through
`opp6win.webm`, `opp6lose.webm` in `opponents/animations/` in the game data
folder or beside the executable. Win and lose refer to the opponent's result.
HyperVision loops each clip in the original result-screen frame, keeping the
game music; missing or unsupported clips use the original animation. F12 also
works on this screen. See [custom opponent animations](opponents/README.md#custom-win-and-lose-animations)
for paths, encoding and 32-bit DOS details.

SDL3 builds also include [UNIT](unit-opponent.md), a seventh opponent with
its own driving planner, a pixel-art robot portrait, and a paginated biography.
UNIT uses the selected car. A ten-second planning screen precedes a new race;
live planning then uses at most one worker, with a bounded synchronous Web fallback.
UNIT follows its planned route independently of the human car; it does not follow,
brake for, or steer around the player. Normal car-to-car collisions still apply.
Races against UNIT record CSRPL, including the `UNIT` identifier and its driving
commands so playback reproduces the recorded decisions.

HyperVision also targets **60 FPS** in SDL3 driving, replay playback, the nighttime
intro, and rotating car previews. Driving and replay playback interpolate between
completed physics states. At the normal 20 Hz simulation rate, each new keyframe
first displays a one-third blend toward it from the previous keyframe, then a
two-thirds blend about 16.7 milliseconds later, then the complete new keyframe
after about 33.3 milliseconds. This keeps visual motion about 33.3 milliseconds
behind the physics timeline at each presentation, before rendering and display
delays, and avoids corrections between predicted motion and the next real update.
Car position, rotation, suspension, and follow cameras use the same interpolation
fraction. Recorded ghosts are sampled at that same visual time even when their
replay uses a different physics frame rate.

Input sampling, recording, and authoritative physics retain their original 10 or
20 Hz schedule. A 10 Hz simulation uses six visual samples per keyframe interval
and an approximately 83.3-millisecond visual delay; slow replay playback increases
the delay to cover its longer keyframe intervals. Late rendering skips overdue
visual samples and never predicts beyond the newest state. Crashes, sinking, and explosions
follow confirmed gameplay events. Interpolated state never enters replay data;
toggling F12 during a replay does not change its simulated result. Seeking,
pausing, rewinding, and camera changes reset interpolation history.

Desktop SDL3 builds use two internal presentation pages. Each complete frame is
composed into the hidden page, including its palette colors and HyperVision detail,
then flipped to the front before SDL receives it. Window exposure and resizing
repaint that completed page while the next frame is being drawn. This prevents
partial game-frame updates from reaching presentation without queuing an extra
frame. Display synchronization still depends on SDL and the graphics driver.

If fullscreen tears but a maximized window is smooth, desktop SDL3 builds can
start in a borderless maximized window with `RESTUNTS_BORDERLESS=1`:

```sh
RESTUNTS_BORDERLESS=1 ./out/sdl3-linux-x64/restunts --data-dir stunts
```

This keeps ordinary window status and uses the desktop's usable area; a panel or
dock may remain visible. Alt+Enter still toggles fullscreen, returning to the
borderless window when fullscreen is turned off. Omit the variable or set it to
`0` to start with the normal window. This option does not change interpolation,
frame pacing, or VSync. It has no effect in DOS, browser, or batch builds.
Maximization depends on window-manager support.

On Windows, Linux, BSD, Haiku, and macOS, VSync is enabled by default in both windowed and fullscreen
mode, with or without HyperVision. The game requests synchronization to the display's
vertical refresh; SDL may use timed pacing when a renderer cannot synchronize.
The existing classic and 60 FPS HyperVision targets still apply. Set
`RESTUNTS_VSYNC=0` to disable it, or `RESTUNTS_VSYNC=1` to enable it explicitly.
For example, on Linux:

```sh
RESTUNTS_VSYNC=0 ./out/sdl3-linux-x64/restunts --data-dir stunts
```

In PowerShell, set `$env:RESTUNTS_VSYNC = '0'` before launching `restunts.exe`.
Unset the variable to restore the default. Invalid values warn and select the
default. If the renderer cannot apply the requested setting, the game warns and
continues. The setting does not affect DOS builds or batch dump tools. Display
synchronization waits are excluded from automatic HyperVision quality measurements.

Multi-core rendering is on by default in SDL3 builds except 32-bit DOS.
Use `--multi-core:off` or clear **Multi-core rendering** in the startup window's
**Graphics settings** group to select serial rendering. Desktop, Android, and
supported browser launchers remember the checkbox; a saved choice takes
precedence over the default. `--multi-core:on` selects the default explicitly:

```sh
./out/sdl3-linux-x64/restunts --data-dir stunts --hv:full --multi-core:on
```

When enabled, the renderer uses the detected logical CPU count and keeps the
calling thread working alongside background workers. `RESTUNTS_RENDER_WORKERS`
can override the background worker count for measurements; `0` selects serial
execution and `auto` restores automatic selection. Native builds cap the pool at
63 background workers; threaded browser builds cap it at seven. The environment variable
has no effect while multi-core rendering is off. Browser threads require an
isolated HTTPS or localhost page; see the [browser build guide](wasm.md).
Small scenes can remain serial. Physics and replay recording are independent
of the worker count. If worker creation fails, rendering uses the workers that
were created or runs serially.

See [HyperVision measurement methods](hypervision.md#performance-measurement)
for the current benchmark scope. The earlier
[SuperSight CPU measurements](supersight-performance.md) document the previous
renderer and do not establish HyperVision performance.

Press **F11** to toggle the diagnostic display on or off. In
SDL3 builds, it shows both FPS and render time. The FPS counter measures presented
frames over approximately one second and rounds down, for example `20 FPS`. The
render-time line shows a value such as `8.4ms`: the average
render duration of the latest 100 completed frames, or the available frames while
warming up, rounded to one decimal place. Rendering and framebuffer composition
are measured before presentation waits; physics, frame pacing, and VSync waits
are excluded. Changing renderer clears the duration history. The original 16-bit
DOS build shows only FPS. If your browser intercepts F11 for fullscreen, enable
**Show FPS** in Graphics settings before starting.

The target color threshold is 20 FPS in classic mode and 60 FPS in SDL3
HyperVision. The temporary HyperVision preset message appears below the timing
line after F12 or Shift+F12, independently of F11. Automatic adaptation works
whether F11 is on or off.
F11 and F12 also work in both car-selection screens and during the
nighttime driving intro without skipping the animation, including in 16-bit DOS. F12 also
works in the track preview and opponent-selection menu, with the same two-second
status display. SDL3 builds additionally support Shift+F12 in these locations.
F11, F12, and SDL3's Shift+F12 also retain their display actions throughout the
intro screens and the main menu's idle replay without dismissing playback.
The main menu, opponent biographies, and static track-selection previews hide
F11 diagnostics; the F11 setting is retained when returning to a screen that
displays them. HyperVision previews of extended tracks show the same diagnostics
as races and car-selection previews.

HyperVision and the FPS display start off by default. Use `--fps:on` to show
FPS from startup on every port, or `--fps:off` to select the default explicitly.
SDL3 also shows render time. The FPS option is case-insensitive and must appear
only once. Its selection persists until changed or the game exits.
The renderer and display shortcuts work in all driving and replay cameras,
including opponent and ghost views and paused replays. Holding a shortcut key
changes its selection only once.

Press **Shift+F11** to toggle the current session's MPH display. Speed appears at
the bottom right during races and player-controlled replays. The shortcut also
works during the intro and the main menu's idle replay without interrupting
either. Use `--mph:on` to enable it at startup or `--mph:off` to select
the default. The desktop, Android, and web startup windows remember
**Show MPH (Shift+F11)**, which appears immediately after **Show FPS**.

### Race against a ghost

In **Opponent**, choose **Clock**, then **Ghost** to select a replay. The game
uses the replay's track and changes the description to **Race against a Ghost.**
Your car and transmission stay selected, and you can change them before driving.
The recorded player's car follows the replay as a silent, black grille ghost;
it cannot collide with your car or change the track. At the end of the replay,
it stays in its final position. Restarting or rewinding keeps it synchronized
with your race time. Press **T** to switch between your view and the ghost's
view; **F1** through **F4** select the camera mode for either car.

All game builds also show the ghost's racing line. It follows
the recorded path even when the ghost is too far ahead to see. Each 2-meter
section compares the ghost's speed there with your current speed: **red** means
you are more than 3 MPH faster and should slow down, **green** means you are
more than 3 MPH slower and should accelerate, and **yellow** means you are
within 3 MPH either way. The colors update as your speed changes. The line has
the width of the road's center markings, so it appears wider nearby and narrower
in the distance, and follows the recorded slopes and banking. DOS16 and
TempleOS omit guidance for a frame if it would exceed the renderer's polygon
budget, preserving the complete road and car scene.

Choose **Clock** again to remove the ghost. Loading a different track, changing
its layout in the editor, or selecting an AI opponent also clears the ghost.
Canceling the replay picker keeps the previous selection. The ghost selection
lasts for the current game session; saved replays retain the ordinary replay
format and do not include the ghost.

Preparing the ghost reconstructs the selected replay before driving and caches
its positions in a temporary `GHxxxxxx.TMP` file in the game directory. The game
removes this file when the selection is cleared or the game exits normally.
The 16-bit DOS build streams the racing line from a second temporary
`GLxxxxxx.TMP` file, removed when the race ends.
As with ordinary replay playback, use the physics switches used to record it.

### Rewind while driving

Hold **Q** during a race to rewind, then release it to continue driving from
that point. The race stays visible without opening the replay controls or a
menu. Rewind speed doubles after holding Q for ten seconds and stops at the
start of the available recording. Releasing Q replaces the later recording
with your new driving; the run is marked as modified, like continuing from a
replay, and does not qualify for the normal high-score table.

### Optional parameters

See [startup settings by platform](parameters.md) for the complete common
options, Android/web controls, TempleOS calls, and platform-specific features.

Run `restunts.exe --nointro` to skip the startup intro and open the main menu
immediately after initialization. The intro remains available when leaving the
main menu. This switch is case-sensitive and can be combined with the other
startup options.

Run `restunts.exe --pg:off` to correct the original power gear bug, including
anti-power gear and the loss of aerodynamic deceleration while accelerating.
Original physics remain the default for replay compatibility. `--pg:on` explicitly
selects the original behavior. These switches are case-insensitive; if both are
supplied, the last one wins. They can be combined with `--nointro` and other
startup options.

Run `restunts.exe --lc:off` to disable Legacy Collision and restore the earlier
32-bit signed interpolation for wheel collisions with walls and track planes.
Legacy collision behavior remains the default for replay compatibility; `--lc:on`
explicitly selects it. These switches are case-insensitive, and the last one
wins. They can be combined with `--pg:off`, `--nointro`, and other startup options.
With `--lc:off`, collisions also check the wheel's movement through the underside
clearance of elevated track surfaces, preventing fast cars from skipping the
collision zone behind a loop. The impact is checked against the actual surface
footprint, preserving clear passages underneath and beside it. Surfaces at both
ends of the movement are checked, including the surface a wheel just left when
moving onto the ground or another segment. Car edges between the wheels are
also checked against finite walls, catching impacts with the start of a ramp
side wall that individual wheel paths can miss. Slalom stones use their full
finite bounds for these checks, including wheel movement that crosses an entire
stone between frames. The car stops at the first contact.
With `--lc:off`, body collisions also distinguish separate surfaces that reuse
one collision-plane template. Moving from a hill into the clear space beneath
an open ramp, or between elevated surfaces at different heights, no longer
causes a false crash merely because the selected surface changes. Actual
crossings of either surface and collisions at continuous surface joins remain.
With `--lc:off`, collision-induced sideways heading offsets and opponent spin
also decay fully to zero in either direction, preventing a permanent steering
bias after contact. Legacy mode retains the original negative-rounding behavior.
Renderer clipping retains its original arithmetic.

Run `restunts.exe --lcb:off` to disable the original left corner bias: left turns
gain a stronger steering response from signed rounding in wheel-heading and skid
calculations. With the bias disabled, those angle calculations round symmetrically
for both directions. Original behavior remains the default; `--lcb:on` explicitly
selects it. These switches are case-insensitive, and the last one wins. They
can be combined with `--pg:off`, `--lc:off`, `--nointro`, and other startup
options. Collision recovery rounding remains controlled separately by `--lc:off`.

Run `restunts.exe --dws:off` to disable **Dual-Way Switching**, correcting the
penalty calculation when cutting from one branch of a split track to the other.
The original switching behavior remains the default; `--dws:on` explicitly
selects it. These switches are case-insensitive, and the last value wins.
They are available on every port and can be combined with other startup options.

Run `restunts.exe --cfg:off` to disable **Chicane Fast-Grass**: areas outside
the visible road use the background terrain. Water sinks the car, and grass,
ice, and dirt retain their normal grip and drag. The visible road retains its
road surface. Fast-grass remains enabled by default; `--cfg:on`
explicitly restores it. The switches are available on every port, are
case-insensitive, and use the last on/off value. **Amiga Grass** also affects
grass areas when fast-grass is disabled.

Run `restunts.exe --amigagrass` to use original Amiga grass grip for the 11 stock
cars. This option is off by default and leaves road grip unchanged. Custom car
IDs retain their authored grass grip. The switch is case-insensitive and can
be combined with the other physics and startup options. As in the original,
the Indycar, Jaguar XJR-9 and Porsche 962 skid earlier on grass; some stock
cars gain grass grip, while the NSX, Countach, Ferrari GTO and Corvette
retain their DOS grass grip.

The ported physics dump tool accepts the same switches after the replay name:
`repldump.exe 0681 --pg:off --lc:off --lcb:off --dws:off`. Replays do not store
these options, so use the same physics settings for recording and playback. Original
assembly executables and the renderer dump tools retain their existing interfaces.

### OWOOT driving rules

Run `restunts.exe --owoot:on` to require at least part of one player wheel to remain
on or above the road. The check uses the car model's tire geometry, including
steering, suspension, pitch, and roll, and is independent of camera position,
zoom, and detail level. Red-white rumble strips on large corners count as grass.
Driving beneath an elevated road does not count as being above it.

Stunts must be followed along their intended route: the switch also checks
ordered progress through loops, corkscrews, slaloms, and other stunt elements.
An airborne car may cross the single off-road tile between aligned ramps or
bridges. The whole gap tile is exempt. The exception requires a launch from
the connected approach and ends at the receiving road; it does not permit
arbitrary flights over grass. The
`r*` OWOOT replay corpus contains only single-tile gaps between connected
elevated road ends, including gaps occupied by scenery. Flight may continue
over the receiving road for additional tiles.

A violation triggers the normal crash immediately. Only the player is checked,
including during replay playback. Rewinding restores the OWOOT progress along
with the car state. `--owoot` is shorthand for `--owoot:on`; `--owoot:off`
disables the rules, which are off by default. These switches are case-insensitive,
and the last recognized value wins. They can be combined with `--pg:off`,
`--lc:off`, and `--nointro`.

The physics dump tool also accepts `repldump.exe <replay> --owoot`. Replays do not
store the switch, so launch with the same settings for recording and playback.
See [OWOOT validation notes](owoot-validation.md) for replay audit findings.

With OWOOT enabled, `repldump.exe R0019.RPL --owoot` also writes `R0019.owo`, containing
exactly `pass` or `fail`. Passing requires finishing the race without an OWOOT
violation; ordinary crashes and unfinished replays fail too. The result starts
as `fail` and changes to `pass` only after successful processing. File-writing
or replay-loading errors return a nonzero exit code; check that code as well
when validating a batch. With OWOOT disabled, no `.owo` file is created or updated.

### Replay driving-time reports

In the ported DOS16 and SDL3 physics dump tools, `repldump.exe R0019.RPL --time`
also writes `R0019.tim` beside the normal dump output. The file contains the
player's driving time to the finish on the first line, excluding penalties and
any recording frames after finishing. The second line contains the penalty
counted at the finish, in minutes and seconds (`0:00` when there is no penalty).
The first line uses the replay viewer's minutes, seconds, and hundredths format.
The two lines are separated by CRLF, with no alignment spaces or trailing
newline. For example:

```text
3:03.00
0:30
```

The `.tim` file starts empty before replay loading and receives a time only if
the player finishes. A replay that crashes before finishing or never finishes
leaves it empty. Replay-loading and file-writing errors return a nonzero exit
code; check that code when processing batches. Without `--time`, no `.tim` file
is created or updated.

The flag is case-insensitive and can be combined with physics switches and
`--owoot`: `repldump.exe R0019.RPL --time --owoot --dws:off` writes both the
`.tim` driving and penalty times and the independent `.owo` verdict.

Physics dumps always simulate at the recording's stored 10 or 20 Hz rate,
matching the replay viewer. `--time` only adds its report; it does not change
the `.BNI` dump or an `.owo` verdict.

### Needle colours

The ported game includes the [needle colour mod](https://wiki.stunts.hu/wiki/Needle_colour_mod).
Each car selects its dashboard needle colours through the Red #5 word
(labelled "needle colour" in CarWorks), at offset `0xAE` within its `simd`
resource. The low byte sets the speedometer colour. The high byte sets the
tachometer colour; when it is zero, the tachometer uses the low byte too.
For example, `0x040F` selects palette index 15 for the speedometer and 4 for
the tachometer. The stock value `0x0010` keeps both needles white, and
`0x0000` makes both black. Digital speedometer digits are unaffected.

### McMurtry Speirling PURE custom car

The `MCMU` car has classic showroom/racing meshes rebuilt from the supplied
high-resolution Speirling model and an optional detailed `.H3D` companion
imported directly from that source for HyperVision. Both use seven paints and
native animated wheels. A digital race cockpit, single-speed electric drive
and fan adhesion retain the 2026 production PURE tuning. Exterior proportions
follow the source mesh. Install it with:

```sh
python3 tools/scripts/generate-mcmurtry-speirling.py --install stunts
```

See [car details, specifications and model sources](mcmurtry-speirling.md).
Verify the generated package with the same command plus `--check`.

### Tesla Roadster Rocket custom car

The `TERR` car includes a grey model based on the supplied reference photos,
with black glazing, a digital cockpit, and one fixed forward ratio. Its
performance approximates the October 2026 Rocket announcements. Install it with:

```sh
python3 tools/scripts/generate-tesla-roadster.py --install stunts
```

SDL3 release packages include the Tesla and McMurtry cars as `TERR.zip` and
`MCMU.zip` in their `cars/` resource folder, alongside `music/` and `skyboxes/`
where those are supported. Each archive contains its four classic resources;
`MCMU.zip` also includes the enhanced `.H3D` model. The game loads the ZIPs
without extraction. DOS16, Amiga and TempleOS packages retain loose car files.
The repository keeps the source resources in `assets/cars/`; authoring metadata
remains in `tools/scripts/cars/`.
The generator's `--install stunts` option writes the four resources to
`stunts/cars/`. Restunts discovers compatible cars there automatically and
continues to load loose cars from the game-data and executable folders.

SDL3 builds also load cars directly from four-character ZIP archives such as
`CDOR.ZIP` in any car search directory. Put the matching car resources at the
archive root; adding or removing the ZIP installs or removes that car. See
[ZIP car resources and conflict priority](sdl3.md#zip-car-resources).

SDL3 releases include Glacier, Keukenhof and Ninh Binh in the track selector.
The source files are in `assets/tracks/`, alongside `assets/cars/`.

Add your own `.TRK` or SDL3 `.CSTRK` tracks to `tracks/` inside the game-data
folder or beside the executable. The game searches loose game-data files, loose
executable files, game-data `tracks/`, executable `tracks/`, then any bundled
track directory. It matches filenames without case sensitivity and lists each
filename once. Optional `.HIG` and `.CSHIG` tables can accompany the tracks.
Saved tracks and updated scores go into the selected game-data folder.

The generator checks both dashboard resources against DIA3 after extraction
and game preparation. See [car details and references](../tools/scripts/cars/READTERR.TXT).

Fan adhesion activates automatically when a car includes the optional one-byte
`fand` resource containing `1`; it needs no startup option or launcher control.
The optional `adhs` resource customizes the maximum attachment distance,
alignment angle, and allowed materials independently for each car. TERR uses
12 world units, 60 degrees, and paved/dirt/ice/grass. Existing `fand=1` cars
without `adhs` retain those defaults; invalid settings disable adhesion.
Fan adhesion holds tire contacts against supported drivable surfaces,
including inverted and vertical surfaces at a standstill, and releases when
the car leaves the surface. Every ported platform supports it. Cars without
that flag keep the original physics; original Stunts ignores both resources.
See [adhesion resource settings](car-adhesion.md) for the binary format,
limits, and measurement rules.


The [Android port](android.md) runs in landscape on Android 5.0 or newer
for both 32-bit ARM and ARM64. Import your original game folder or ZIP on first
launch; game data is not bundled in the APK. Transparent touch circles support
driving, menu navigation, and replay controls. Manual shift buttons appear only
while racing with manual gears; replay swipes skip ten seconds. The launcher
offers PC Speaker, Tandy, Ad Lib and MT-32 sound/ROM selection,
**OGG music replacement**, **Show FPS**, **Show MPH (Shift+F11)**, **Show Control Layout**, **Tilt steering**,
and all six **HyperVision** presets. **Driving rules** keeps **Power Gear**,
**Original Collision Bugs**, **Left Corner Bias**, **Dual-Way Switching**,
**Chicane Fast-Grass**, **Amiga Grass**, and **OWOOT**,
with the same defaults as the command-line options.
Upper-right **C**/**T** circles send the camera/follow keyboard shortcuts.

## Extended track editor

The offline HTML editor creates `.cstrk` tracks up to 90×90 tiles, with four
terrain levels, independent terrain and road surfaces, ASCII notes, and undo/redo.
Select your Stunts folder before editing. Every SDL3 package includes the
standalone `tracked.html` editor: beside the browser game, or at
`share/restunts/editor/tracked.html` in native packages, including 32-bit DOS
and the Android ZIPs. Open it in a browser; no CSS or JavaScript sidecars are
needed. The browser launcher also links the editor. The current `.cstrk`
revision is v16. Revisions v3 through v15 open as v16 while preserving their
overpass road materials; revisions below v3 are unsupported. Newer revisions
show a warning and load using the known format.

Custom horizons use one PNG in your Stunts folder's `horizons/` subdirectory.
The editor discovers valid images when you select or reconnect that folder and
shows their filenames without `.png` in the Horizon selector. Images must be
4096 pixels wide and any integer height from 64 through 120 pixels, in opaque
8-bit RGB or indexed PNG format without interlacing. Use RGB (93, 255, 255)
for the sky, align scenery to the bottom, and join the left and right edges.
The game generates classic artwork and uses the full image for enhanced rendering.
Tracks and replays store the filename, so share the PNG alongside them. Missing
or invalid images use the saved built-in fallback and retain the custom reference.
See [custom horizons](skyboxes/README.md#custom-horizons) for details.

Switch from the 2D editor to **3D preview** to explore the track with WebGL and
original models from your selected Stunts folder. Click for mouse look; WASD moves,
Space rises, and Shift descends. Hold Alt or enable Caps Lock for 3× speed. Esc
releases the mouse, and the 2D editor retains its zoom and map position.

The Roads palette includes **Blue checkpoint** and **Red checkpoint** on paved,
dirt or ice roads. Their gates use the start/finish design in blue or red,
without an arrow. Finish a race by passing every checkpoint of either color
present on the track, in any order and from either direction, below the signs
and between the poles. A track with only one color requires all checkpoints of
that color; tracks without checkpoints keep the usual finish rule. Genghost
searches the valid checkpoint routes, including both color choices when present.
Blue and Red checkpoints require `.cstrk` v13. **Blue / red checkpoint** uses a
flag with a blue top half and red bottom half, counts in both color sets, and
is required on either route. It supports the same road surfaces and rotations
and requires `.cstrk` v14.

The Stunts palette includes **Slalom with blue checkpoint**, **Slalom with red
checkpoint**, and **Slalom with blue / red checkpoint**. Each combines the
standard slalom barriers with the corresponding checkpoint gate. These
variants require `.cstrk` v16.

The Scenery palette includes **Wheat field**, a one-tile patch of golden crop
rows. Adjacent tiles form larger fields, rotation changes the row direction, and
straight inclines are supported. Cars drive through the crops using the terrain
beneath them. **Tulip field** adds ten narrow flower rows ordered green, red, pink,
blue, yellow, purple, teal, pink, white and yellow, with green edging and soil
separators.
**Vine field** adds twenty-four short, narrow green rows with triangular tops
and evenly spaced soil separators. Both support hillside inclines, rotation and
adjoining fields. All three appear in the game and the editor's 2D and 3D views.
Tulip and Vine fields require `.cstrk` v12.

The palette also includes **Pine trees (10)** and **Pine trees (6)**. Both
groups vary positions and sizes by tile, breaking up rows and repeated
patterns across neighboring tiles. Opening the same track always restores the
same forest. On slopes, each pine retains the single tree's upright crown and
slope-following trunk roots.

The Stunts palette includes **Tunnel with slalom**, combining a tunnel shell
with slalom barriers as one piece. It supports paved, dirt and ice roads,
rotations, and straight inclines. **Barrel roll ramp** is a directional two-tile
launch stunt: a solid concrete entry leads into an elevated grille ramp with a
twisted lip. The car carries its acquired roll into free flight after takeoff.

Hills retain the original game's straight inclines and inner and outer corners
at every elevation. Every track piece and scenery object can be placed on a
straight incline in any direction. Multi-tile pieces require one continuous
slope beneath their occupied cells; hill corners and changes of grade cannot
support a piece. A shared geometry transform makes roads, stunt shapes and
scenery follow the slope. These placement rules apply only to `.cstrk` tracks.

SDL3 builds race on these tracks and record `.csrpl` replays with 32-bit frame
counts and no ten-minute recording window. Original `.trk` and `.rpl` behavior
is preserved. See the [editor guide](track-editor.md),
[track format](cstrk-format.md), and [replay format](csrpl-format.md).
