# Amiga 500+ port

The native Amiga backend lives in `src/restunts/platform/amiga`, alongside the
DOS, SDL3 and TempleOS backends. It builds the same classic C game source list
as the 16-bit DOS executable, with a Motorola 68000 target and no SDL dependency.

The display is 320 × 200, non-interlaced, with five bitplanes and 32 colors
selected from the ECS 4,096-color palette. Original VGA artwork is mapped to
32 colors; the original 256 colors cannot all be displayed at once. The game
still renders indexed pixels internally, then converts completed frames to
Amiga planar pixels. With enough free RAM, two planar buffers keep conversion
off the visible page. Low-memory launches use one planar buffer, so conversion
can be visible while the screen updates.

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
indexed screen buffer and one or two 40,000-byte planar buffers. Native executable,
stack, operating-system, and resource allocations also occupy Chip RAM. Close
other applications before starting the game.

The indexed screen also serves as the race render buffer. Artwork unflipping
uses an 8,000-byte visited bitmap instead of a second 64,000-byte image. Display
and OS allocations are completed before the resource arena is sized, retaining
8 KiB for later OS work. The executable requests a 20 KiB stack. A second planar
buffer is enabled only when enough contiguous resource memory remains,
including a 272 KiB reserve for stock car, opponent and scenery resources.

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
preserving the source saves. Logs, configuration, native stdout, and screenshots
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

Use `--mode capture --capture-at 110 --timeout 150` to capture an interactive
session; capture mode does not count as an automated pass. `--key 80:Return`
sends a menu key, and `--hold 180:30:Up` holds the accelerator for 30 seconds.
Optional `-DRESTUNTS_AMIGA_TRACE=ON` enables native startup/resource diagnostics.

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
