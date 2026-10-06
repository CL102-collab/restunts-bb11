# SDL3 builds

The SDL3 platform in `src/restunts/platform/sdl3/` builds the game (`restunts`),
physics replay dumper (`repldump`), and renderer dumper (`pixldump`) for Windows,
Linux, FreeBSD, OpenBSD, NetBSD, Haiku, macOS, 32-bit DOS, and [WebAssembly for offline browsers](wasm.md). The existing Open Watcom 16-bit DOS build remains available
through `make -C src/restunts restunts repldump pixldump`; its platform code stays
under `src/restunts/platform/dos/`.

## Dependencies and supported build targets

CMake 3.25+, a C99 GCC-compatible compiler (plus C++ for SDL on Windows and Haiku), a build
tool, Git, and network access are required. CMake fetches and verifies SDL3 upstream commit
[`015489c672f24feed28c2aa2cdd6176df95329f3`](https://github.com/libsdl-org/SDL/tree/015489c672f24feed28c2aa2cdd6176df95329f3).
That revision includes SDL's DOS backend and the gameport/Sound Blaster timing fix
used by the [reference DOS port](https://github.com/murphy666/stuntsengine/pull/3).
All platforms use the same revision; no separate SDL fork is required.

| Target | Compiler and baseline |
| --- | --- |
| Linux x64 | GCC, Debian 12 build baseline. |
| Linux x86 | GCC multilib or an i386 environment, Debian 12 build baseline; separate SSE2 and no-SSE2 packages. |
| Linux ARM32 and ARM64 | Debian 12 cross-compilers; ARMv7 hard-float (Pi 2+) or AArch64. Debian/Raspbian 12+ baseline. |
| FreeBSD x64 | Native compiler, FreeBSD 14.4 amd64 build baseline. |
| OpenBSD x64 | Native Clang, OpenBSD 7.9 amd64 build baseline. |
| NetBSD x64 and x86 | Native GCC, NetBSD 10.2 amd64/i386 build baseline; separate x86 SSE2 and no-SSE2 packages. |
| Haiku x64 and x86 | Native modern GCC/G++, Haiku R1/beta6; x86 uses the hybrid system’s modern GCC ABI and SSE2. |
| macOS arm64 and x86_64 | Apple Clang and macOS SDK; helper scripts target macOS 11.0+. Universal builds are available. |
| Windows x86 | MinGW-w64 with MSVCRT; Windows XP SP2+ runtime, XP API target; separate SSE2 and no-SSE2 packages. |
| Windows x64 | MinGW-w64; Windows 7 API target. |
| Windows ARM64 | LLVM-MinGW; Windows 10+ API target. |
| WebAssembly | Emscripten; single offline HTML, user-supplied game folder. See [browser build](wasm.md). |
| DOS | DJGPP GCC 12.2.0, 32-bit DPMI executable; VGA and a DPMI host. |

The baseline describes build settings. Successful builds and automated tests do
not establish runtime compatibility with every old OS or CPU. Windows XP/7, older
non-SSE2 processors, and physical DOS hardware need testing on those systems.
SDL's DOS minimum is i386 with 4 MB RAM; the game's actual memory and performance
requirements can be higher. See [SDL's DOS notes](https://github.com/libsdl-org/SDL/blob/015489c672f24feed28c2aa2cdd6176df95329f3/docs/README-dos.md).

## Linux

On Debian 12 x64, install the compiler, build tools, and common desktop drivers:

```sh
sudo apt-get update
sudo apt-get install build-essential cmake ninja-build git curl ca-certificates pkg-config \
    libasound2-dev libpulse-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
    libxi-dev libxfixes-dev libxss-dev libxtst-dev libwayland-dev libxkbcommon-dev libudev-dev \
    libdrm-dev libgbm-dev libegl1-mesa-dev libgl1-mesa-dev
cmake -S . -B out/sdl3-linux-x64 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/sdl3-linux-x64
ctest --test-dir out/sdl3-linux-x64 --output-on-failure
```

For x86 on an x64 Debian host, enable i386 packages and install `gcc-multilib`
and the corresponding `:i386` development libraries. Use a separate build tree:

```sh
cmake -S . -B out/sdl3-linux-x86 -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/linux-x86.cmake
cmake --build out/sdl3-linux-x86
```

For ARM cross-builds, use `cmake/toolchains/linux-arm32.cmake` with
`gcc-arm-linux-gnueabihf` and the `:armhf` driver development packages, or
`cmake/toolchains/linux-arm64.cmake` with `gcc-aarch64-linux-gnu` and `:arm64`
packages. Enable that architecture with `dpkg --add-architecture` before
installing its packages. The ARM32 toolchain selects ARMv7 and VFPv3-D16 with
the hard-float ABI. `tools/scripts/install-package-dependencies.sh` records the
complete package list used by the Debian 12 build jobs.

Append `-DRESTUNTS_SSE2=OFF` for a 32-bit x86 build without SSE/SSE2/AVX code in
the game, Nuked OPL2 Lite, or bundled SDL. Use another fresh build tree when changing CPU options.
The same option is available for Windows x86 and NetBSD i386; x64 requires SSE2. A build without
SSE2 still uses the toolchain's x86 instruction-set baseline and system runtime;
it is not an 8086 executable. The DOS toolchain selects i386 and disables SSE2
by default.

`-DRESTUNTS_SYSTEM_SDL=ON` uses an installed SDL 3.4+ CMake package instead of the
pinned source. This option is incompatible with `RESTUNTS_SSE2=OFF`, because
CMake cannot control the instruction set of a prebuilt SDL library.

## BSD

FreeBSD, OpenBSD, and NetBSD use the same SDL3 backend as the other desktop
builds. Release jobs use FreeBSD 14.4 amd64, OpenBSD 7.9 amd64, and NetBSD 10.2
amd64/i386. Each OS has its own binary archive and system dependencies.

Install the build dependencies on the matching BSD using
`tools/scripts/install-bsd-package-dependencies.sh` as root, with the desired
release target (`freebsd-x64`, `openbsd-x64`, `netbsd-x64`, `netbsd-x86`, or
`netbsd-x86-no-sse2`). Run this helper with `sh`; it installs Bash as well.
OpenBSD needs its X11 base/development sets. On NetBSD 10.2 the helper installs
missing X11 sets from checksum-verified release archives. These are build
prerequisites; a running graphical desktop and its audio drivers are needed to play.

For a local native x64 build, for example on FreeBSD:

```sh
cmake -S . -B out/sdl3-freebsd-x64 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/sdl3-freebsd-x64 --parallel 2
cmake --install out/sdl3-freebsd-x64 --prefix "$PWD/out/package-freebsd-x64" \
    --component Runtime
out/package-freebsd-x64/bin/restunts --data-dir "$PWD/stunts"
```

Use a corresponding output directory on OpenBSD. On NetBSD, include the
pkgsrc and X11 prefixes and disable precompiled headers, which conflict with
the native compiler's address layout:

```sh
PKG_CONFIG_PATH=/usr/pkg/lib/pkgconfig:/usr/X11R7/lib/pkgconfig \
    cmake -S . -B out/sdl3-netbsd-x64 -G Ninja -DCMAKE_BUILD_TYPE=Release \
    '-DCMAKE_PREFIX_PATH=/usr/pkg;/usr/X11R7' -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON
```

Use that directory for the build and install commands above. To build
NetBSD x86, run in an **i386 userspace** with its native compiler and libraries
and use a separate output directory such as `out/sdl3-netbsd-x86`. SSE2 is
enabled by default. For a build without SSE2, add `-DRESTUNTS_SSE2=OFF` at
configure time and use another fresh build tree. This option disables
SSE/SSE2/AVX in the game, bundled SDL and Nuked; system libraries retain their
own CPU requirements. It cannot be combined with a prebuilt system SDL.
The CI jobs create the i386 userspace from checksum-verified NetBSD release
sets inside an amd64 VM, then build with the 32-bit compiler there.

Local regression tests are available after supplying original game data in
`stunts/`:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    ctest --test-dir out/sdl3-freebsd-x64 --output-on-failure
```

To produce a release archive from a clean committed checkout on the matching
BSD, set the source commit and run the package helper:

```sh
export GITHUB_SHA="$(git rev-parse HEAD)"
bash tools/scripts/build-package.sh freebsd-x64
```

Replace the target for the other four packages. Archives and checksums appear
under `dist/packages/`. The release workflow also supplies build-run provenance
and attestations. Keep `bin/`, `lib/`, and `share/` together when moving an
extracted package. The executables find Nuked relative to their location;
artwork discovery works without requiring NetBSD's optional `/proc` mount.
BSD CI checks executable architecture, package contents and startup after
relocation. Interactive graphics/audio and older hardware require native testing.

## Haiku

The native `haiku-x64` and `haiku-x86` targets use Haiku R1/beta6 and SDL3's Haiku
video and audio drivers. The [Haiku guide](haiku.md) covers build dependencies,
CMake commands, runtime packaging, and reproducible QEMU tests. Release CI
builds both targets in Haiku VMs and checks the installed packages after relocation.

## macOS

Install the Xcode Command Line Tools (`xcode-select --install`), then CMake
3.25+, Ninja, Git, and Python 3 (for example, `brew install cmake ninja git python`).
From the repository root, build the native architecture and install its runtime:

```sh
bash tools/scripts/build-macos.sh
```

Use `--arch arm64`, `--arch x86_64`, or `--arch universal` to select the target.
Output directories are `out/sdl3-macos-<arch>` and `out/package-macos-<arch>`;
`native` resolves to the shell's `arm64` or `x86_64` architecture. After adding
original game data to `stunts/`, `--test` also runs native CTest regressions.
SDL is linked statically, and `libnuked-opl2.dylib` stays replaceable under the
runtime package's `lib/` directory. Keep `bin/`, `lib/`, and `share/` together.

To run an extracted runtime package without installing build tools:

```sh
bash /path/to/package/run-restunts.sh --data-dir /path/to/Stunts -- --nointro
```

See the root README for the complete
[macOS build instructions](../readme.md#macos-host-macos-backend) and
[precompiled-package instructions](../readme.md#macos-running-an-already-compiled-package),
including architecture selection, archive extraction, and direct executable use.

## Windows with MinGW-w64

Cross-compile from Linux after installing `mingw-w64`. SDL is built statically,
so the bundled-SDL build does not require a separate `SDL3.dll`:

```sh
cmake -S . -B out/sdl3-windows-x64 -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-x64.cmake
cmake --build out/sdl3-windows-x64
cmake -S . -B out/sdl3-windows-x86 -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-x86.cmake
cmake --build out/sdl3-windows-x86
```

The outputs are `restunts.exe`, `repldump.exe`, and `pixldump.exe` in their build
directory, together with the required `nuked-opl2.dll`. Native Windows builds can use a MinGW-w64 environment with CMake and
Ninja; select its GCC compiler directly instead of a Linux cross toolchain file.
MSVC is not a supported compiler for this port.

The x86 release builds use Debian 12's MSVCRT-based MinGW toolchain for Windows
XP compatibility. The pinned SDL imports `IsWow64Process`, which requires
[Windows XP SP2 or newer](https://learn.microsoft.com/en-us/windows/win32/api/wow64apiset/nf-wow64apiset-iswow64process).
The x64 release builds select Windows 7 APIs. A local UCRT
compiler may require additional runtime support on older Windows versions;
use the CI toolchain when reproducing those release baselines.

For Windows ARM64, put the LLVM-MinGW toolchain's `bin/` on `PATH`, then select
`-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-arm64.cmake`. It uses
`aarch64-w64-mingw32-clang` and targets Windows 10 or newer. The exact downloaded
LLVM-MinGW version and checksum are in `tools/scripts/package-toolchains.conf`.

## DOS with DJGPP

The pinned Linux x64 cross-toolchain is
[build-djgpp v3.4 / GCC 12.2.0](https://github.com/andrewwutw/build-djgpp/releases/download/v3.4/djgpp-linux64-gcc1220.tar.bz2).
On Linux, install `libfl2` as well (DJGPP binutils uses `libfl.so.2`).
Download and verify it before extracting:

```sh
mkdir -p out/toolchains/djgpp
curl -fL https://github.com/andrewwutw/build-djgpp/releases/download/v3.4/djgpp-linux64-gcc1220.tar.bz2 \
    -o out/toolchains/djgpp-linux64-gcc1220.tar.bz2
printf '%s  %s\n' 8464f17017d6ab1b2bb2df4ed82357b5bf692e6e2b7fee37e315638f3d505f00 \
    out/toolchains/djgpp-linux64-gcc1220.tar.bz2 | sha256sum -c -
tar -xjf out/toolchains/djgpp-linux64-gcc1220.tar.bz2 -C out/toolchains/djgpp --strip-components=1
export PATH="$PWD/out/toolchains/djgpp/bin:$PATH"
cmake -S . -B out/sdl3-dos -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/djgpp.cmake -DRESTUNTS_BUILD_TESTS=OFF
cmake --build out/sdl3-dos
```

This creates 32-bit DOS `.exe` files. Place a compatible DPMI host such as
[`CWSDPMI.EXE`](https://sandmann.dotster.com/cwsdpmi/) alongside them when the
DOS environment does not supply one. CI and release DOS32 packages include
CWSDPMI in `bin/` and its accompanying redistribution notice under `share/`.
Original game data is supplied separately by the player.

The DOS video path uses indexed VGA 320x200 with HyperVision off. Enabling
HyperVision with F12 renders the 3D scene at 1280x800 and selects a VESA mode
that can display it with the original 4:3 aspect ratio (normally 1280x1024,
with a 1280x960 image and black borders). Indexed modes are preferred; true-colour
VESA modes are supported too. If no sufficiently large mode is available, SDL
scales the 1280x800 rendering to the largest available viewport and logs a warning.
Disabling HyperVision restores Mode 13h. High-resolution rendering requires more
RAM and processing power than the original mode.

Original music and effects write the real AdLib-compatible OPL2 chip at port
`388h`, which DOSBox also emulates. An AdLib or compatible Sound Blaster FM device
is needed for those sounds. Optional Ogg Vorbis music uses SDL's PCM output and
requires a supported digital-audio device, such as a compatible Sound Blaster.
If original audio initialization fails, the native game reports a warning and
continues silently. Desktop builds
synthesize the same FM registers through
[Nuked OPL2 Lite](https://github.com/nukeykt/Nuked-OPL2-Lite), licensed under
LGPL-2.1-or-later, and send PCM to SDL. The library is dynamically linked and
replaceable. The driver retains AD15's octave-crossing pitch-bend table and
signed rounding. Sustained engine
voices with a half-rate carrier use equivalent integer operator multipliers
and a halved base pitch. This preserves the carrier frequency while preventing
low-RPM rounding from permanently changing the oscillators' relative phase.
The adjustment applies only to compatible continuous FM instruments; musical
notes and instruments with pitch-dependent envelopes, vibrato, key scaling or
multiplier controllers retain their original setup. Nuked runtime writes are
buffered to preserve same-tick key-off/key-on transitions.

In DOSBox/DOSBox-X use `core=dynamic`, `cycles=max`, and `aspect=true`. Aspect
correction displays 320x200 VGA pixels at their intended 4:3 shape. Mount a
directory containing the executable, data, and DPMI host, then run `RESTUNTS.EXE`. Stop an
automated DOSBox process with SIGKILL; graceful termination can open a modal
confirmation dialog. Real gameport joystick behavior requires hardware or a
configured DOSBox joystick; the reference port did not establish that coverage.

## Running and controls

Run `restunts --help` (or `restunts -h`) to list the command-line options for
that build. Run `restunts --version` (or `restunts -v`) to print the Git revision
and compilation date, using the version format from the Options screen. Both
commands print to standard output and exit before initializing SDL, graphics,
audio, or game resources, so no original game files are needed. They also work
in the Open Watcom 16-bit DOS game executable.

Help and version switches are case-sensitive and can appear alongside game
options; the first help or version switch wins. A directory argument following
an initial `--data-dir` is treated as a path, even if its name matches a switch.
The legacy `/h` spelling retains its Hercules meaning; use `-h` for help.

The source checkout contains only a placeholder under `stunts/`; building does
not fetch game data. Copy Broderbund Stunts 1.1 resources into `stunts/`, or extract
the [BB11 archive used by the existing DOS CI](https://github.com/CommonLoon102/restunts4d-oracles/releases/download/v1.0.1/BB11.zip)
there before running the game or the audio regression. The archive must place
files such as `ADSKIDMS.VCE` and `DEFAULT.RPL` directly in that folder.

Resources are read from the current directory, or from `--data-dir` when it is
the first argument. Existing game and dump parameters follow it unchanged:

```sh
out/sdl3-linux-x64/restunts --data-dir stunts --nointro
out/sdl3-linux-x64/repldump --data-dir stunts DEFAULT.RPL
out/sdl3-linux-x64/pixldump --data-dir stunts DEFAULT.RPL 2 0 5
```

Windows uses the corresponding `.exe` names. Dump outputs and saved game data
are written in the selected data directory, so it must be writable. Keep the
original game resources and replay/car additions together there.

On Linux, BSD, Haiku, Windows, and macOS, the interactive game defaults to serial rendering with
zero background render workers. `RESTUNTS_RENDER_WORKERS=auto` enables automatic
parallel rendering, or use a count from `0` through `7`. See the
[render-worker settings](../readme.md#hypervision-and-fps-display) for Linux and
PowerShell examples.

Desktop rendering uses two owned ARGB presentation pages. Composition writes the
hidden page and flips it to the front only after the complete frame is ready.
Exposure and resize events repaint the frozen front page, preserving its pixels,
palette colors, and dimensions while a new frame is drawn or HyperVision changes
resolution. Driving, replay playback, animated intro frames, and car-preview
refreshes use explicit frame boundaries; menu fades and the initial car-menu
dissolve retain their incremental updates. Pages are released on video shutdown.
This internal buffering does not force SDL's swapchain buffer count or replace
monitor synchronization. The DOS indexed/VESA presentation path is unchanged.

Windows, Linux, BSD, Haiku, and macOS enable VSync by default in windowed and fullscreen mode,
including classic rendering and HyperVision. SDL may use timed pacing when a
renderer cannot synchronize to the display. Set `RESTUNTS_VSYNC=0` before
launching the game to disable it; `1`, an empty value, or an unset variable
selects the default. Invalid values log a warning and enable VSync. If SDL cannot
apply the setting, the game logs a warning and continues. The existing classic
and HyperVision frame-rate targets and physics schedule remain in place; waiting
for a display refresh does not count as CPU load for automatic quality changes.
DOS and batch dump tools are unaffected. See the
[VSync examples](../readme.md#hypervision-and-fps-display) for Linux and PowerShell.

Desktop windows apply the VGA vertical 6:5 pixel-aspect correction: the original
320x200 framebuffer fills a 4:3 image. Nearest-neighbour scaling preserves sharp
pixel edges, and resizing adds black borders to retain that aspect. Renderer dump
images use the classic renderer's original 320x200 pixel data for parity checks.
With HyperVision on, the 3D scene and both car-selection previews start at 1280x800;
its lower quality presets also support 640x400 and 320x200. Their geometry
is projected and rasterized at the higher resolution; menu artwork, dashboard,
and replay controls keep their original pixel detail. F12 works in both car
selection screens and the track preview as well as driving and replay views.
Driving, replay, and track selector previews use
[AI-refined skybox PNGs](skyboxes/README.md) at 640x400 and 1280x800, with artwork
at four times the original width and height. The 320x200 mode uses the original
horizon strips. Banked and inverted views work at every HyperVision resolution.
Runtime installs include the required
`bin/skyboxes/` directory; retain it beside the executable when packaging.
Missing or invalid textures fall back to the original artwork. Menus accept
keyboard, mouse, joystick, and controller input interchangeably, regardless of
the driving input method selected in Options. SDL3 builds detect controllers
automatically using SDL's standard gamepad mappings for Xbox, PlayStation, and
other recognized controllers. Connecting or disconnecting a controller during
play updates the available devices automatically.

During a race, moving the right stick selects controller steering; pressing a
keyboard steering key selects keyboard steering. Only the most recently used
steering source affects the wheel. Returning the stick to center does not take
steering back from the keyboard. Controller pedals and gear changes remain
available independently of steering ownership. The original Mouse and Joystick
driving selections also remain available.

The right stick steers and navigates menus. Right-stick up accelerates and down
brakes, using the existing joystick's on/off pedal behavior. Horizontal steering
is graded and can be combined with either pedal. The left stick rotates the F3
custom camera during driving and replay playback; it has no driving action.
Moving the right stick also starts the race during the truck unloading sequence,
just like pressing a keyboard arrow.
Holding a right-stick or D-pad direction repeats menu navigation, including
track and replay file selection. Both the right stick and all four D-pad
directions navigate the replay controls. While driving, D-pad left retains Q
and D-pad right has no action.

| Controller input | Xbox / PlayStation label | Action |
| --- | --- | --- |
| Bottom face button | A / Cross | Enter |
| Left or right trigger | LT or RT / L2 or R2 | Space |
| Right stick click | R3 | Enter in menus and at replay controls |
| D-pad up / down | Same | Up / Down in menus and replay controls; shift up / down while driving |
| Right face button | B / Circle | Escape |
| Left or right bumper | LB or RB / L1 or R1 | Escape |
| Top face button | Y / Triangle | T: switch player / opponent view |
| Left face button | X / Square | D: show / hide dashboard |
| D-pad left | Same | Left in menus and replay controls; Q while driving |
| D-pad right | Same | Right in menus and replay controls |
| View button | View / Share or Create | C: cycle camera |
| Menu button | Menu / Options | Open the race or replay menu |

Escape or B / Circle returns from opponent selection or either car-selection
screen to its previous menu without activating the highlighted button.
Choosing Re-start driving from the replay menu asks "Re-start driving?" using
the same No / Yes choices as Exit to Dos. Escape, B / Circle, or either bumper
cancels without restarting the race.

Controller steering uses the same incremental steering commands as the existing
joystick implementation, so replay files keep their original format. Replays
record gas and brake as on/off flags rather than pedal percentages.

Existing keyboard driving and replay controls remain available:

- **Up/Down** accelerate and brake; **Left/Right** steer.
- **F1–F4** select cockpit, follow, custom, and trackside cameras.
- **Alt+Enter** toggles desktop fullscreen, preserving the 4:3 image and restoring
  the previous window size when leaving fullscreen. Keypad Enter also works.
- **Shift+F10** switches between original AdLib and replacement Ogg music on
  music screens, keeping the current song position. It is inactive during races;
  plain **F10** retains its track-editor function.
- **F11** or **Shift+F11** toggles the FPS and render-time display on or off,
  including in both car-selection screens. Render time excludes physics and
  presentation waits.
- **F12** toggles HyperVision Auto on or off; **Shift+F12** cycles the fixed
  **Full → High → Medium → Low** presets.
- **Ctrl+F12** saves the last completed frame as a PNG in the game data directory
  (`--data-dir`, or the current directory by default). Files are named
  `SHOT0001.PNG` through `SHOT9999.PNG`, skipping names that already exist.
  Desktop and browser captures use the game's render resolution; the SDL3 DOS
  build captures its display surface. Holding the shortcut saves only once.
  Browsers save to the selected game folder when possible, otherwise download
  the PNG.
- Hold **Q** to rewind a live race; release it to resume from that point.
- **T** switches between the player and opponent or selected ghost view.
- **Escape** leaves driving or replay playback; closing the desktop window exits
  the game and removes its temporary ghost cache.

See the [gameplay notes](../readme.md#hypervision-and-fps-display) for HyperVision,
ghost selection, and rewind behavior.

The SDL3 build supports AdLib music and effects. Other original executable DOS
sound drivers (MT-32, PC speaker, and Sound Blaster sampled-speech paths) are not
ported. Original DOS16 builds retain their existing driver selection. Native
sequencing remains on the main thread; SDL only consumes generated PCM, so an
audio callback cannot race resource loading or freeing.

## Optional Ogg Vorbis music

All SDL3 builds, including 32-bit DOS and WebAssembly, can replace the game's
four songs with files from `assets/music/`:

| Filename | Song |
| --- | --- |
| `titl.ogg` | Title / intro |
| `slct.ogg` | Selection / main menu |
| `over.ogg` | Race end |
| `vict.ogg` | Victory |

Original AdLib music is selected at startup. Use `--ogg:on` to start with Ogg
replacements enabled, or `--ogg:off` to select the default explicitly. These
options are case-insensitive; specify at most one. Press **Shift+F10** during the
intro, menus (including the track editor), or race-end/victory screens to enable
replacement music; press it again to return to AdLib. This selection lasts for
the current game session and cannot be changed during a race. Switching takes
effect at the current song position, including after loops, without restarting
the song. For matching musical positions, replacements should use the original
song timing and loop length.

Use lowercase filenames and Ogg Vorbis audio with one or two channels at
8,000–192,000 Hz. Ogg Opus and other codecs are unsupported. Every replacement
loops from its beginning while its song is active. The existing Options menu
music toggle, music volume/fades, and audio suspension apply; effects retain
their original sound. Each missing, unreadable, unsupported, or broken song
falls back independently to its original music at the current position. Original game resources remain
required. The repository includes replacements for all four songs, preserving
their original loop lengths without added ending tails or fade-outs. The
race-end track retains its original pause before repeating.

Native builds search `assets/music/` in the working directory, then beside the
executable; next they search `music/` in those same two locations, followed by
the source checkout's `assets/music/` path recorded at build time. `--data-dir`
sets the working directory to that game-data folder; without it, the startup
working directory is retained. Installed packages put available replacements in
`bin/music/`. Files can also be added to
these native runtime locations after building.

CMake packages whichever named files are present in the source `assets/music/`
directory. Reconfigure before rebuilding/installing after changing that set.
Browser builds embed them at `/assets/music/` in the offline HTML and relink kit;
reconfigure and rebuild after any music-file change. The game-folder picker does
not import subfolders. See [the asset instructions](../assets/music/README.md).

The bundled stb_vorbis decoder is compiled into the game on every SDL3 target;
no separate Vorbis library installation is required. Its license is included at
`share/licenses/restunts/stb-LICENSE`. The 16-bit Open Watcom DOS build keeps its
original music behavior.

## Validation and CI

The shared **Build release packages** workflow builds all distribution targets for
**PR validation** and **Release**, using Debian 12 for the Linux baseline.
The matrix includes ARMv7, ARM64, both x86 SSE2 variants, Windows ARM64, and a
Universal macOS 11+ package. Native BSD VM jobs add FreeBSD, OpenBSD, and NetBSD
x64 plus NetBSD x86 with and without SSE2. They check package contents, binary architecture,
and relocated startup without game data. Haiku R1/beta6 VM jobs build
and check the x64 and x86 packages. The cross-built targets do not run game
tests. Linux/BSD/Haiku/macOS archives use `.tar.gz` to preserve
executable permissions; DOS, Windows, and browser archives use `.zip`.

Native Linux SDL3 tests cover the platform layer, AdLib synthesis, worker
lifecycle, and replacement of the packaged Nuked library. Audio tests check
audible PCM, pitch, engine frequency, volume, modulation, key-off, native
engine-definition pointers, unavailable-device fallback, and batch-mode cleanup.
The shared **PR validation** and **Release** workflows run the complete physics
corpus and configurable renderer coverage for the selected platforms. Their
`platforms` input is a nonempty JSON array of unique names from `dos` and
`sdl3`, defaulting to `["dos","sdl3"]`. Use `platforms: '["sdl3"]'` for Linux
x64 SDL3 only, `platforms: '["dos"]'` for DOS only, or
`platforms: '["dos","sdl3"]'` for both. Like `cameras`, this input is available
in the manual workflows and the reusable **Build and validate** workflow.
Game tests and replay reports run only for selected platforms. Both workflows
build all distribution packages regardless of the replay-test selection.

Both platforms use identical shard plans and archived Borland references. The
`cameras`, `target`, and `renderer-test-percentage` inputs apply to both; setting
the percentage to 100 tests every eligible renderer replay. SDL3 shard results
and reports have an `sdl3-` artifact prefix and fail on mismatches, execution
errors, or incomplete coverage. These dump comparisons exercise physics and
framebuffer rendering; interactive display, input, and audio have separate
platform regressions.

For an isolated local comparison against freshly generated DOS physics and
renderer oracles, run:

```sh
python3 tools/scripts/validate-native.py --build-directory out/sdl3-linux-x64 \
    --output out/native-validation --count 3
```

This requires DOSBox and a fresh output directory. It checks preserved oracle
checksums, uses `core=dynamic` and `cycles=max`, and kills DOSBox after each
capture. Full replay-corpus validation and physical hardware checks are separate
from the build matrix; a successful sample is not evidence that all replays,
controllers, or sound hardware have been exercised.

CI archives contain an installed package: executables in `bin/`, Nuked's shared
library in `lib/` on Linux/BSD/Haiku/macOS or `bin/` on Windows, dependency license notices, and
the exact Nuked source and rebuild instructions. DOS packages omit Nuked and
DOS32 includes CWSDPMI. The browser archive retains its Nuked relinking kit.
The manual **Release** workflow publishes the exact build archives after
validation and provenance checks. See [release verification](releases.md).

## Packaging and Nuked's license

Create a redistributable directory from a completed build:

```sh
cmake --install out/sdl3-linux-x64 --prefix out/package-linux-x64 --component Runtime
out/package-linux-x64/bin/restunts --data-dir "$PWD/stunts" --nointro
```

Distribute the complete directory, including `THIRD-PARTY-NOTICES.txt` and
`share/`. Linux, BSD, Haiku, and macOS executables find the library relative to their installed location,
so the package can be moved; Windows loads the DLL beside the executable.
Do not distribute a desktop executable alone. `--component Tests` installs a
separate test package with the same license and source material.

Nuked's full LGPL-2.1 license is in
`share/licenses/restunts/Nuked-OPL2-LICENSE`; its grant allows later versions too.
The exact library sources, local modification notices, standalone build script,
build settings, and parent build-control snapshots are in
`share/restunts/nuked-opl2-lite/`. The `README.md` there explains how to rebuild
and substitute the shared library without relinking the application.
`THIRD-PARTY-NOTICES.txt` in the package root includes the required permission
for own-use modification and reverse engineering to debug library modifications.
These terms do not relicense unrelated code or game assets.

The desktop game's second intro screen acknowledges Nuked, and
`restunts --licenses` prints the notice and package locations without needing
game resources or a display. The package regression verifies relocation and
replacement with a modified library built solely from the shipped source:

```sh
python3 tools/scripts/test-nuked-package.py --build-directory out/sdl3-linux-x64
```

## Capturing a live audio problem

Desktop builds can record the complete OPL register stream independently of
race replays. This captures synthesis history that an `.rpl` file does not
contain, including engine starts, RPM changes and the restart on replay seeking.
Tracing is off unless `RESTUNTS_AUDIO_TRACE` names an output file. Each game
launch overwrites that file; choose a new name to preserve an earlier recording.

From the repository root on Linux:

```sh
RESTUNTS_AUDIO_TRACE="$PWD/engine.trace" \
  ./out/sdl3-linux-x64/restunts --data-dir ./stunts --nointro
```

In PowerShell, set `$env:RESTUNTS_AUDIO_TRACE = "$PWD\engine.trace"` before
launching the game. Reproduce the changed engine tone, then use replay seeking
to restore it and let the restored sound play briefly. Note the approximate
race times of both events, quit normally, and retain the `.trace` file.
Unset the variable afterward to disable recording. Recording flushes once per
second; normal shutdown writes the final sample position. A disk error disables
tracing while the game keeps running.

Render the recorded synthesizer output without the game or SDL:

```sh
python3 tools/scripts/render-opl-trace.py engine.trace engine.wav
```

The renderer needs Python 3.9+ and a C compiler (`cc` by default, or `--cc`). It
builds a temporary helper from the vendored Nuked sources and replays the
recorded write timing. The WAV contains all generated PCM; SDL queue clears are
recorded as markers but are not cut from the WAV. Device latency, resampling by
the audio device and hardware playback faults are outside the trace. Keep the
original trace so it can be re-rendered after changes to the audio driver.
