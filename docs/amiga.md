# Amiga 500+ port

The native Amiga backend lives in `src/restunts/platform/amiga`, alongside the
DOS, SDL3 and TempleOS backends. It builds the same classic C game source list
as the 16-bit DOS executable, with a Motorola 68000 target and no SDL dependency.

The game renders at 320 × 200 and displays it at 4:3 with non-square pixels,
matching the DOS game. The non-interlaced screen uses 320 × 256 on PAL and
320 × 200 on NTSC, with five bitplanes and 32 hardware color registers selected
from the ECS 4,096-color palette. PAL output expands the 200 source rows during
planar conversion; game coordinates and artwork remain 320 × 200. It uses the
original Amiga release's palette tables and 3D material colors while retaining the DOS
artwork and game layouts. The game still renders indexed pixels internally,
then converts completed frames to Amiga planar pixels. On AmigaOS, enough free
RAM enables two planar buffers to keep conversion off the visible page. AROS
and low-memory launches use one planar buffer, so conversion can be visible
while the screen updates.

## Original Amiga colors

The palette reference contains 31 original tables of 32 RGB4 color words and
the original executable's 129 material color indexes. These numeric facts were
extracted from the supplied Amiga release. Source hashes, decoded resource
hashes, byte offsets, and observed DOS-to-Amiga pixel correspondences are
recorded in `src/restunts/tests/fixtures/amiga-palette-reference.json`;
`src/restunts/platform/amiga/palette_data.h` holds the runtime tables.
The repository includes no original Amiga images, sound, or executable code.

Menus select their original palette. During racing, registers 0–15 hold the
original 16-color world palette, and registers 16–31 hold the selected car's
16-color dashboard palette. A Copper list changes registers 16–31 at the replay
bar's scanline when that panel is visible. This uses the existing five-plane
screen and 32 registers; the world, dashboard, and replay panel have independent
pixel maps.

Maps for matched menu images and Countach/Ferrari dashboard artwork use observed
pixel pairs from the DOS and Amiga resources. Where a DOS index corresponds to
several original indexes, the most frequent correspondence is selected;
unobserved indexes use the nearest original palette color. Consequently, the
palette values and material table are exact, but DOS artwork, layout differences,
and ambiguous index mappings prevent a claim of pixel-identical Amiga screens.

Verify the checked-in numeric data without commercial assets, or additionally
compare it with local copies of the original releases:

```sh
python3 tools/scripts/verify-amiga-palette-data.py
python3 tools/scripts/verify-amiga-palette-data.py \
    --original-directory /path/to/extracted-amiga-release \
    --dos-directory /path/to/bb11-data
```

The Amiga directory must contain `disk1/`, `disk2/`, and the unpacked PPkc
resources under `decoded/disk1/` and `decoded/disk2/`. The second command checks
the original file hashes and table bytes, the DOS VGA palette, and the pixel
pairs used for the artwork maps. Scene selection and raster palette behavior
also have static original-executable references in the fixture. The original
executable's title screen was observed running under AROS; its menus and race
palette changes have not been validated by a live original-game trace.

## Download

**PR validation** and **Release** package the validated 68000 build as
`restunts-amiga-m68k.tar.gz`. The archive contains `restunts`, the bundled `cars/`
resources, `restunts.map`, this guide as `README.md`, and the standard package
README and manifest. It contains no original game data.

**Release** publishes the archive and its SHA-256 sidecar with signed build
provenance. See [release packages and verification](releases.md) for download
verification, then follow the run instructions below.

## Build

Use an `m68k-amigaos-gcc` SDK with AmigaOS headers and libnix. The build explicitly
targets the original 68000 and does not require an FPU or `ixemul.library`.
With the SDK installed at `/opt/amiga`:

```sh
export AMIGA_TOOLCHAIN=/opt/amiga
cmake -S . -B out/amiga -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/amiga-m68k.cmake \
    -DCMAKE_BUILD_TYPE=Release
cmake --build out/amiga --parallel 2
cmake --install out/amiga --prefix out/package-amiga --component Runtime
```

On Linux x86-64, `tools/scripts/install-amiga-toolchain.sh /opt/amiga` installs
the pinned SDK using Docker. It extracts the compiler without modifying the
host's package installation. An existing destination is never overwritten.

The output is `out/amiga/restunts`, an Amiga Hunk executable. The linker map is
`out/amiga/restunts.map`. An existing SDK on `PATH` also works without setting
`AMIGA_TOOLCHAIN`. Use a separate build directory from SDL or DOS builds.

## Run

Use an A500+ with Kickstart 2.0 or later. Copy `restunts` to a writable directory
containing the original Broderbund Stunts 1.1 data (12 February 1991). From an
Amiga shell, change to that directory and run:

```text
Stack 20480
restunts --nointro
```

The executable uses the current directory for game data and saved tracks,
replays, and high scores. Original `.PRE`, `.PVS`, `.RES`, `.VCE`, `.TRK` and
`.RPL` files keep their DOS byte order. Filenames are case-insensitive on
AmigaDOS. Existing game options such as `--nointro`, `/ns`, and the physics
switches remain available; `restunts --help` lists them.
Alternatively, put `--data-dir PATH` before the other game arguments.

The release includes a `cars/` folder beside `restunts`, containing the Tesla
and McMurtry `.RES`, `.3SH` and `.VSH` resources. Copy this folder with the
executable. The game discovers custom cars automatically in `cars/` under
the game-data directory or beside the executable. Loose car resources in the
game-data directory and beside the executable remain supported and take
priority. Executable-relative resources also work with `--data-dir`.

Use the keyboard or mouse for the menus. Arrow keys control the car, and the
original game's gear, camera and replay controls remain in place.
Digital joystick input uses Amiga game port 2 when joystick control is selected.
Menu screens redraw immediately instead of using the DOS dissolve animation.

Audio uses the four Paula DMA channels with small generated waveforms driven
by the original music sequences and engine controls. This preserves the
game's sequencing and pitch changes, but does not reproduce AdLib FM timbres
or its nine-voice polyphony. Hardware updates are batched after timer catch-up;
long rendering or loading operations can delay audible changes.

## Memory and validation

The target is 1 MB Chip RAM with no Fast RAM. A DOS conventional-memory figure
does not include the VGA card's memory; this port additionally needs a 64,000-byte
indexed screen buffer and one or two planar buffers (51,200 bytes each on PAL,
40,000 bytes on NTSC). Native executable, stack, operating-system, and resource
allocations also occupy Chip RAM. Close
other applications before starting the game.

The indexed screen also serves as the race render buffer. Artwork unflipping
uses an 8,000-byte visited bitmap instead of a second 64,000-byte image. Display
and OS allocations are completed before the resource arena is sized, retaining
8 KiB for later OS work. The executable requests a 20 KiB stack. A second planar
buffer is enabled only when enough contiguous resource memory remains,
including a 272 KiB reserve for stock car, opponent and scenery resources.

Display flips preserve the operating system's original `BitMap` object and
change only its plane addresses, retaining the driver data needed for later
palette operations. AROS is detected through `aros.library` and always uses
one planar buffer: its graphics driver caches plane addresses that
`ScrollVPort` does not reliably refresh. This avoids stale alternating frames
under AROS. Earlier two-buffer AROS measurements below are historical results.

Diagnostic code is built separately to leave more memory for stock cars and
opponents in the shipping executable. The native platform self-test exercises
framebuffer addressing, palette and display setup, retrace, mouse coordinates,
100 Hz callbacks, Paula DMA and channel ownership, and AmigaDOS file operations.
Build `test-amiga-platform`, then run `test-amiga-platform --amiga-selftest`
from an Amiga shell.

For reproducible headless tests on Linux x86-64, install FS-UAE and the pinned
open AROS replacement ROM, then run the emulator harness:

```sh
cmake --build out/amiga --target test-amiga-platform
tools/scripts/install-amiga-emulator.sh /tmp/restunts-amiga-emulator
python3 tools/scripts/run-amiga-platform-test.py \
    --fs-uae /tmp/restunts-amiga-emulator/FS-UAE/Linux/x86-64/fs-uae \
    --mode selftest
python3 tools/scripts/run-amiga-platform-test.py \
    --fs-uae /tmp/restunts-amiga-emulator/FS-UAE/Linux/x86-64/fs-uae \
    --mode smoke --timeout 300
```

The harness requires Xvfb and libXtst; `--xvfb PATH` selects a local Xvfb
installation. It copies the executable and game assets to a temporary disk,
preserving the source saves. It includes the packaged `cars/` resources and
overlays car resources from the selected game folder's `cars/` directory.
Logs, configuration, native stdout, and screenshots
are retained. Test modes use `test-amiga-platform`; interactive capture uses the
shipping `restunts`. `--executable PATH` overrides either default.
A detached startup script closes the boot console; `--keep-cli`
also tests launching with the console retained. Supply your own licensed ROM
with `--kickstart PATH` to test Commodore Kickstart. The default configuration
is 68000, ECS, 1 MB Chip RAM, zero Fast RAM, and zero Slow RAM. The harness
rejects FS-UAE's implicit extra RAM, and loads explicit ROM files to avoid it.
FS-UAE's older bundled AROS ROM uses almost all 1 MB at startup; use the
installer-provided newer ROM.

The smoke test uses the original `DEFAULT.RPL`, car/dashboard assets, track
geometry, sound, simulation, and renderer. It exports `AMIGASMOKE.PPM` and
reports frame advancement, visible colors, and resource memory usage.

Focused endian/alignment, palette/conversion, and resource tests are also available:

```sh
cmake --build out/amiga --target test-amiga-endian test-amiga-display
python3 tools/scripts/run-amiga-platform-test.py \
    --executable out/amiga/test-amiga-endian --args='' \
    --expect 'Amiga endian and native sprite tests passed.'
python3 tools/scripts/run-amiga-platform-test.py \
    --executable out/amiga/test-amiga-display --args='' \
    --expect 'AMIGA DISPLAY TEST PASS'
cmake --build out/amiga --target test-amiga-rle
python3 tools/scripts/run-amiga-platform-test.py \
    --executable out/amiga/test-amiga-rle --args='' \
    --expect 'Amiga wrapped RLE tests passed.'
cmake --build out/amiga --target test-amiga-unflip
python3 tools/scripts/run-amiga-platform-test.py \
    --executable out/amiga/test-amiga-unflip --args='' \
    --expect 'Amiga in-place bitmap unflip tests passed.'
cmake --build out/amiga --target test-amiga-decompression
python3 tools/scripts/run-amiga-platform-test.py \
    --executable out/amiga/test-amiga-decompression --args='' \
    --expect 'Amiga decompression tests passed.'
```

Shared startup, command-line, and custom-car adhesion tests also build as native
68000 executables. They exercise default settings, option parsing and application,
help/version output, and stopped or moving inverted surface contact:

```sh
cmake --build out/amiga --target test-amiga-command-line test-amiga-startup test-amiga-car-adhesion
python3 tools/scripts/run-amiga-platform-test.py \
    --executable out/amiga/test-amiga-command-line --args='' --machine a4000 \
    --expect 'command line tests passed'
python3 tools/scripts/run-amiga-platform-test.py \
    --executable out/amiga/test-amiga-startup --args='' --machine a4000 \
    --expect 'startup option tests passed'
python3 tools/scripts/run-amiga-platform-test.py \
    --executable out/amiga/test-amiga-car-adhesion --args='' \
    --expect 'car surface adhesion tests passed' --timeout 180
```

Use `--machine a4000` for faster interpreted test execution. The binaries still
target the 68000. `--fps:on|off` is a startup option on Amiga, alongside the
existing power gear, collision, corner bias, Chicane Fast-Grass (`--cfg:on/off`),
grass grip, and OWOOT switches. Chicane Fast-Grass defaults to on; turn it off
to give green chicane areas ordinary grass grip and drag.
Surface adhesion activates automatically for a car with a one-byte `fand`
resource set to `1`; cars without that capability keep the original physics.

The settings update was cross-built and run in FS-UAE 3.2.35 with the pinned
open AROS ROM. Native command-line and startup suites passed on the A4000/040
profile; the adhesion suite passed on both the 68000 A500+ with 1 MB Chip RAM and
the A4000/040. The shipping executable also reached a cockpit with all common
physics switches, `--data-dir`, `--nointro`, `/ns`, and `--fps:on` supplied together;
the FPS overlay was visible. A500+ platform self-tests and the A4000 stock replay
smoke test passed. These runs do not substitute for physical Amiga validation.

Use `--mode capture --capture-at 110 --timeout 150` to capture an interactive
session; capture mode does not count as an automated pass. `--key 80:Return`
sends a menu key, and `--hold 180:30:Up` holds the accelerator for 30 seconds.
Optional `-DRESTUNTS_AMIGA_TRACE=ON` enables native startup/resource diagnostics.
Use `--video-standard ntsc` to test the NTSC display path; PAL is the default.
The harness presents the full nominal raster at 4:3, correcting FS-UAE's default
square PAL pixels. Use the `amiga-real-*.png` screenshots to inspect the displayed
aspect ratio. The `amiga-full-*.png` and `amiga-crop-*.png` files, like
`AMIGASMOKE.PPM`, store raw pixels and do not encode pixel aspect ratio. A normal drive started from
the main menu shows the full dashboard without the replay controls.

Scenery capture fixtures preserve the original geometry and terrain. The ramp
and windmill views relocate the start/finish in DOS `DEFAULT.TRK`; the water view
relocates it beside the original pond in Amiga `JOES.TRK`. Each changes only the
old and new start tiles. The diner view uses Amiga `SKIDS.TRK` unchanged, with the
native replay camera adjusted before continuing the drive. Fixtures are staged
as temporary `DEFAULT.TRK` copies, with their source hashes and byte edits
recorded beside the capture artifacts; original game files remain intact.

The palette update passes the source-data verifier, host regressions, and
AddressSanitizer checks. Native A500+ platform, display, and replay smoke tests
pass, as do the final A4000 platform and smoke tests using the AROS single-buffer
fallback. Captured world pixels match the exact original 16-color palette;
the visible replay bar independently matches its original palette. The broader
development results and performance measurements below predate this update.

This is an experimental functional port, with substantial performance work
still required for comfortable play on a stock 7 MHz 68000. The optimized
68000 conversion alone measured approximately 686 ms per complete screen.
The full-detail replay/cockpit diagnostic averaged approximately 5 seconds per
frame, rising to 6.1 seconds with the largest tested stock resource combination,
on a cycle-exact 68000 with 1 MB Chip RAM. Extra RAM addresses allocation
pressure; it does not fix CPU speed. A native planar renderer and further
68000 optimization would be needed for practical frame rates.

Validation completed during development includes all 32 SDL CTest checks
(including the host regression suite), native endian/alignment, wrapped RLE,
decompression, bitmap unflipping, display/palette tests, and the native platform
self-test. The original 16-bit DOS game, `repldump`, and `pixldump` also build
with the repository's pinned Open Watcom compiler. The strict 1 MB race
test loaded the original race assets, rendered four cockpit frames, advanced
three replay inputs, and used a resource high-water mark of 233,664 bytes in
a 287,808-byte arena. An additional test using the default Countach's assets
passed at a 245,520-byte resource high-water mark. The largest stock resource
combination identified by the memory budget (LM02 player, Audi opponent, Alpine
scenery) passed the same test at 265,872 bytes in a 286,768-byte arena. Its replay
fixture changes only the car, opponent and scenery selections in a copied
`DEFAULT.RPL`; the original recorded input remains intact.

Interactive testing also confirmed menu navigation, starting a normal drive,
acceleration, pause/resume,
collision rendering, the post-race results screen, and high-score file creation
with sound enabled. Launching without arguments displayed the title and entered
the 3D intro; the complete unattended intro sequence was not tested. No commercial
Kickstart ROM or physical Amiga was available; native execution tests used open
AROS in FS-UAE.

## A4000 compatibility tests

The same 68000 executable also passes tests using FS-UAE 3.2.35's A4000/040
profile with AGA, 2 MiB Chip RAM and 4 MiB motherboard Fast RAM. The game still
uses its 320 × 200 renderer and 4:3, 32-color display. Select this profile with
`--machine a4000`:

```sh
python3 tools/scripts/run-amiga-platform-test.py \
    --fs-uae /tmp/restunts-amiga-emulator/FS-UAE/Linux/x86-64/fs-uae \
    --machine a4000 --mode selftest
python3 tools/scripts/run-amiga-platform-test.py \
    --fs-uae /tmp/restunts-amiga-emulator/FS-UAE/Linux/x86-64/fs-uae \
    --machine a4000 --mode smoke
```

This sets `motherboard_ram = 4096`, with no Zorro II, Zorro III or Slow RAM.
The emulator's effective configuration was checked for 68040 CPU/FPU/MMU, AGA,
the requested memory sizes, and disabled JIT. The default harness profile
remains the unexpanded A500+ and passed its platform test again.

The A4000 platform self-test passes with two planar display buffers and a
589,824-byte resource arena. Both replay fixtures pass, and their exported
images match the A500+ output byte for byte:

| Replay fixture | Resource high-water mark | Average render/physics time per frame |
| --- | ---: | ---: |
| Original `DEFAULT.RPL` | 233,664 bytes | 90 ms |
| LM02 player, Audi opponent, Alpine scenery | 265,872 bytes | 97 ms |

An interactive run also confirmed main-menu navigation, starting a normal race,
acceleration, visible pause/resume, collision, the results screen, return to the
main menu, high-score file creation, and normal exit with the unchanged shipping
executable.

Both runs leave 1,995,256 bytes of Chip RAM free. These timings measure four
frames in FS-UAE's host-dependent **fastest interpreter mode**, with JIT off.
They are not predictions for a physical 25 MHz A4000: this emulator's 68040
timing model is not cycle-exact, and its frequency controls do not provide a
validated stock-speed benchmark. Testing uses the open AROS ROM.
