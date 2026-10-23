# Play Restunts on stock TempleOS

This package targets stock TempleOS 5.03 from the official `TempleOS.ISO`.
Start with an existing, working installation of the original TempleOS.
The distributable package contains the native HolyC port, documentation, and
installation/import tools, and the Tesla Roadster and McMurtry Spéirling custom cars.
**It contains no original game data.** Each player
must supply their own Broderbund Stunts 1.1 files (the 12 February 1991 release).
No fork, SDL, network connection, shared folder, or additional compiler is needed
inside TempleOS. TempleOS compiles the HolyC sources when you launch it.

The game renders 3D geometry at 640×400 with 16 colors and uses the PC speaker.
Projection and rasterization use the higher resolution to add detail. The image
fills the entire 640×480 display, matching the DOS game's intended 4:3 display
proportions. Its original 320×200 artwork was designed for nonsquare display
pixels. Original bitmap artwork and UI retain their source detail, displayed
at 2× horizontal and 2.4× vertical scale using nearest-neighbor sampling.
Keyboard and mouse input are supported.

The refreshed package restores the opponent-selection paper background and
matches the DOS EGA version's opponent portrait colors and dithering. If you
already play on TempleOS, see [Update an existing installation](#update-an-existing-installation).

## What to download

Unzip **RESTUNTS_STOCK_TEMPLEOS.ZIP on the host computer**. It contains:

| File | Purpose |
| --- | --- |
| `RESTUNTS.ISO` | HolyC port and custom cars on a native RedSea installation CD. |
| `README.MD` | This guide, readable on the host. |
| `SHA256SUMS.TXT` | SHA256 checksums for the package files. |
| `CONTENTS.TXT` | SHA256 checksums of the individual files inside the CD. |
| `BUILD.TXT` | Build-system ISO fingerprint and package details. |
| `DATA_CD_TOOL/` | Host Python tool and HolyC importer for your own game data. |

Use the ISO directly as a virtual CD. It is a **data CD**, not a bootable copy of
TempleOS. Keep booting your existing TempleOS hard disk. The ZIP does not need
to be opened inside TempleOS.

The instructions below use `T:` for the CD drive and `C:` for the writable
TempleOS installation. `DrvRep;` lists the drives. Use your actual letters if
they differ. A VM with 1 GiB RAM and two virtual CPUs is the tested configuration;
allow at least 32 MiB of free space for installation and room for your saves.

## 1. Put the game CD in the VM

For VirtualBox:

1. Shut down the TempleOS VM.
2. Open its **Settings → Storage**.
3. Select the existing CD/DVD drive on the IDE controller, click the disc icon,
   choose **Choose a disk file**, and select `RESTUNTS.ISO`.
4. Keep the installed TempleOS hard disk attached. In **System → Boot Order**,
   put **Hard Disk** before **Optical**.
5. Start the VM normally from its hard disk.

For QEMU, use the same working VM and hard disk as before, replacing its CD image
with `RESTUNTS.ISO`. With a conventional IDE CD drive, the relevant options are:

```sh
-cdrom /path/to/RESTUNTS.ISO -boot c
```

These are options to add to your existing QEMU command, not a complete command
that creates or replaces your OS disk. The image can also be burned as a disc
image for an already working TempleOS computer with a supported optical drive.

## 2. Open the CD inside TempleOS

Use a TempleOS terminal task. Type these commands, pressing Enter after each:

```c
DskChg('T');
Cd("T:/");
Dir;
```

The directory should contain `INSTALL.HC`, `PLAY.HC`, `VERIFY.HH`, `PORT`,
an empty `DATA` directory, and `README.MD`. `DskChg` refreshes removable media after a CD change.

If `T:` is not listed by `DrvRep;`, mount the optical drive first:

1. Run `Mount;`.
2. Press `T` at **Drive Letter** (this prompt takes a single key).
3. At **I/O Port Base0**, type the probe-list number for the CD/DVD device
   (for example, the entry identifying a QEMU or VirtualBox CD-ROM), then press Enter.
4. Press Enter at the next **Drive Letter** prompt to finish.
5. Run the three CD commands above.

Selecting the numbered probe entry supplies the controller ports and device unit
automatically. This is mounting an existing CD; partitioning and formatting are
not part of installing this game.

## 3. Install to the hard disk

While still in `T:/`, run:

```c
#include "INSTALL.HC";
```

The installer copies the CD contents to **`C:/Restunts`**, then compares every
copied file with its source. Wait for the installation-success message before
continuing. The sources are under `C:/Restunts/PORT`; `C:/Restunts/DATA` is
initially empty. Import your own game files in the next step.

The installer refuses to overwrite an existing destination, including a previous
installation or an incomplete copy. To choose another empty destination after
loading `INSTALL.HC`, run, for example:

```c
RestuntsInstall("C:/Restunts2");
```

Use the same destination in the launch commands below. Keep the game on a
writable hard disk: running the data directly from the CD would prevent saves.

## 4. Create a private CD from your own game files

Do this on the **host computer**, outside TempleOS. You need Python 3 and your
own extracted Stunts 1.1 directory. If the game is in an archive, extract it first.
Choose the directory containing `MISC.RES` or `MISC.PRE`, the car/shape resources,
and tracks. The tool copies supported game resources and uppercases their names;
it does not rename or modify the originals.

Open a host terminal in the extracted `RESTUNTS_STOCK_TEMPLEOS` package folder.
On Linux or macOS:

```sh
python3 DATA_CD_TOOL/MAKE_DATA_CD.PY \
    --data "/path/to/STUNTS11" \
    --output "../STUNTSDATA.ISO"
```

On Windows with Python installed:

```bat
py -3 DATA_CD_TOOL\MAKE_DATA_CD.PY --data "C:\Games\STUNTS11" --output "..\STUNTSDATA.ISO"
```

Replace the example game path with your own directory. Keep the files in
`DATA_CD_TOOL` together; the Python script uses the adjacent HolyC import helpers.
No QEMU, mounting software, or third-party Python library is needed to create
this private CD. The output path must not already exist.

`STUNTSDATA.ISO` contains **your game data**. Keep it for personal use, outside the
redistributable package; do not include it when sharing the port. The public
`RESTUNTS_STOCK_TEMPLEOS.ZIP` is the distribution package.

## 5. Import the private data CD into TempleOS

Replace the VM's `RESTUNTS.ISO` disc with your newly created `STUNTSDATA.ISO`.
In VirtualBox, use **Devices → Optical Drives → Choose a disk file**, or shut
down and change the optical image in Settings as in step 1. Keep booting the
installed TempleOS hard disk.

At a TempleOS terminal, enter these commands separately:

```c
DskChg('T');
Cd("T:/");
#include "IMPORT.HC";
```

The importer requires the port to be installed first. It copies the resources
into `C:/Restunts/DATA` and verifies every copied file against the private CD.
It refuses to import over a nonempty `DATA` directory, protecting existing saves.
Wait for the import-success message.

For a non-default installation directory, load `IMPORT.HC` as above, then call:

```c
RestuntsImport("C:/Restunts2");
```

If you already have a nonempty data directory, keep it and its saves. To make a
separate fresh installation, install the code CD into a new directory before
importing. A failed partial import is reported; inspect that directory before
retrying into another fresh installation.

The private CD is only for transfer. After import, the game reads and writes the
hard-disk copy, so you can eject it.

## 6. Launch and play

Open a **fresh terminal task** for each launch. A normal TempleOS terminal can
open another task with Ctrl+Alt+T. Then run:

```c
Cd("C:/Restunts");
#include "PLAY.HC";
```

Compilation can take a minute or more under software emulation. Wait for the
intro or main menu. The sources should compile without warnings.

On the main menu, use Left/Right to select **Let's Drive**, then press Enter.
With the standard default configuration, driving uses the keyboard and automatic
gears. An imported configuration may differ; choose keyboard control and automatic
transmission in the game if needed.

| Control | Action |
| --- | --- |
| Up | Accelerate. |
| Down | Brake. |
| Left / Right | Steer. |
| F1 / F2 / F3 | Change camera. |
| P | Pause. |
| Escape while driving | Open the replay viewer. |
| Ctrl+Q | Open the original **Exit to Dos** dialog; choose **Yes** to return to TempleOS. |

The exit dialog retains its original DOS wording. Escape at the main menu
returns to the intro; use Ctrl+Q to quit the game.

For a launch without the intro, use a fresh task and these commands instead:

```c
Cd("C:/Restunts/PORT");
#include "LOAD.HC";
Restunts("C:/Restunts/DATA", TRUE);
```

To use the original Amiga grass-grip coefficients for stock cars, pass a third
`TRUE` argument: `Restunts("C:/Restunts/DATA", TRUE, TRUE);`. This is the TempleOS
equivalent of `/amigagrass`; it defaults to `FALSE` and affects both the player
and opponent. Custom car IDs retain their own coefficients. Replays do not store
this option, so use the same setting when replaying a recording.

All implemented startup settings are also available in the fourth argument,
using the same option names as the command-line builds. For example:

```c
Restunts("C:/Restunts/DATA", TRUE, FALSE,
    "--pg:off --lc:off --lcb:off --dws:off /amigagrass --owoot /ns");
```

| Option | Effect |
| --- | --- |
| `--nointro` | Skip the intro; equivalent to the second `TRUE` argument. |
| `--pg:on`, `--pg:off` | Enable or disable the original power gear bug. |
| `--lc:on`, `--lc:off` | Enable or disable original collision behavior. |
| `--lcb:on`, `--lcb:off` | Enable or disable the original left corner bias. |
| `--dws:on`, `--dws:off` | Dual-Way Switching: preserve original penalty behavior when on; fix branch-switching penalties when off. |
| `/amigagrass` | Use Amiga grass grip for stock cars; equivalent to the third `TRUE`. |
| `--owoot` | Enable One Wheel On/Over Track rules. |
| `/ns` | Start with music and sound effects disabled. |
| `--fps:on`, `--fps:off` | Start with the FPS display enabled or disabled; supply once. F11 toggles it during play. |
| `--help`, `-h` | Print available TempleOS options and return without loading game data. |
| `--version`, `-v` | Print the build version and return without loading game data. |

Power gear, original collisions, left corner bias, and Dual-Way Switching default
to on. Amiga grass, OWOOT, and the FPS display default to off; intro and sound
default to on.
Custom-car surface adhesion is enabled only by its `fand=1` car-resource flag.
Physics on/off options are processed in order and the last value wins. Supply
`--fps` only once. Existing calls
with one, two, or three arguments retain their original behavior. The options
string accepts whitespace-separated arguments and single or double quotes;
backslashes remain literal. The argument array grows with the string, so
combining settings does not silently discard later flags.

For direct argument-array access, include the program name in slot zero:

```c
legacy_s8 *arguments[]={"RESTUNTS", "--pg:off", "--owoot", "/ns"};
RestuntsArgs("C:/Restunts/DATA", 4, arguments);
```

TempleOS always uses its native 16-color display and PC-speaker audio backend.
Legacy `/sXX`, `/h`, and `/nd` switches remain accepted but do not select another
native audio/video backend. SDL3-specific Ogg, HyperVision, and touch-control
options are unavailable on this platform.

Install and import once. On later boots, repeat only the launch commands.
Both CDs may be ejected after import. Tracks, replays, high scores, and
configuration files remain in the installed `DATA` directory.

The bundled custom-car resources are in `PORT/CARS`, beside `LOAD.HC`. The
game discovers them automatically, including when `Restunts` uses game data
from another installation. Additional compatible cars can go in this folder
or in `DATA/CARS`; unpacked and packed car resources are supported. Loose car
files in `DATA` retain priority, followed by loose files beside `LOAD.HC`,
`DATA/CARS`, and `PORT/CARS`. The private-data CD tool preserves an existing
`cars` subfolder and converts its names to uppercase for TempleOS.

## Update an existing installation

Quit the game and attach the new `RESTUNTS.ISO`. Install the updated code in a
new directory and continue using your existing game data and saves. The example
below keeps `C:/Restunts/DATA` and installs the new code in `C:/Restunts2`;
replace these paths if your installation differs, and choose an unused destination.

In a fresh terminal task, run:

```c
DskChg('T');
Cd("T:/");
#include "INSTALL.HC";
RestuntsInstall("C:/Restunts2");
```

Loading `INSTALL.HC` first tries the default `C:/Restunts` destination. If that
installation exists, the installer reports it and copies nothing. The final
command installs into the new directory. Wait for the installation-success message.

For each launch of the updated version, open another fresh terminal task and run:

```c
Cd("C:/Restunts2/PORT");
#include "LOAD.HC";
Restunts("C:/Restunts/DATA");
```

The updated code reads and saves in the existing `C:/Restunts/DATA` directory.
Keep that directory; no new private data CD or import is needed. Use these launch
commands for this setup because `PLAY.HC` expects `DATA` beside its own `PORT`
directory. You can eject the installation CD after installation.

## PC-speaker audio in a VM

For QEMU on a Linux host using PulseAudio (or its PipeWire compatibility server),
add these options to the existing VM command:

```sh
-audiodev pa,id=speaker -machine pcspk-audiodev=speaker
```

Use `qemu-system-x86_64 -audiodev help` to see the backends included in your QEMU
build. Other hosts can choose their native backend, such as `coreaudio` on macOS
or `dsound` on Windows. See the [QEMU PC-speaker documentation](https://www.qemu.org/docs/master/system/i386/pc.html).

VirtualBox's PC-speaker passthrough is Linux-only and supports beeps, not PCM
samples. Its ordinary sound-card setting does not enable this output. See the
[VirtualBox PC-speaker limitations](https://docs.oracle.com/en/virtualization/virtualbox/7.2/user/reference.html).
The game can still be played without sound; QEMU provides the tested speaker
emulation path.

## If something does not work

- **CD shows the old files:** use `DskChg('T');` again after changing the optical
  image, then `Cd("T:/");` and `Dir;`.
- **File system not supported:** use the supplied native RedSea `RESTUNTS.ISO`.
  A generic ISO9660/Joliet image made from the extracted files is unsuitable for
  the stock system's normal directory-copy commands.
- **Original game data is missing:** complete steps 4–5 using your own Stunts 1.1
  files. The installation CD contains the port and custom cars; supply the
  original game data separately.
- **Missing include file:** start with the exact `Cd` command above, preserve
  filename case, and keep `PORT/generated` intact. Extensions must stay uppercase.
- **Installer reports a copy mismatch:** check free space and the CD attachment.
  It leaves the partial destination for inspection; use a new empty destination
  for the next attempt. Do not replace an existing installation's save files.
- **No sound in the VM:** enable the hypervisor's PC-speaker emulation. An SB16,
  AC97, or HDA sound device by itself is not the game's audio output. QEMU can
  connect the PC speaker to a host audio backend using `pcspk-audiodev`.
- **Keyboard goes to the host:** focus/capture the VM first. Normal driving uses
  arrow keys, not WASD.

## Building the package again

This section is for maintainers on the host; a player only needs the ZIP above.
The builder requires Python 3, QEMU, `mkfs.fat` from dosfstools, and `mcopy` from
mtools. It boots the supplied official TempleOS ISO in a disposable guest and
uses the stock `RedSeaISO` writer. It never attaches an existing VM disk.

From the repository root:

```sh
python3 src/restunts/platform/templeos/tools/PACKAGE.PY \
    --iso /path/to/TempleOS.ISO \
    --output /path/to/new/RESTUNTS_STOCK_TEMPLEOS
```

The output path must not exist. This creates the package directory and a sibling
`RESTUNTS_STOCK_TEMPLEOS.ZIP`. Original input files are not renamed or changed.
The public builder has no game-data input. Its CD has the bundled Tesla and
McMurtry car resources in `PORT/CARS` and an empty `DATA` directory. The ZIP
includes the standalone private-data CD tool. The public build does not read
or package anyone's original game assets.

Official references: [TempleOS downloads](https://templeos.org/Downloads/),
[mounting and copying files](https://templeos.info/Wb/Doc/Install.DD.HTML), and
the [RedSea filesystem](https://templeos.info/Wb/Doc/RedSea.DD.HTML).
