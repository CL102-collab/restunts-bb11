# Documentation for developers

## Repository contents
	docs
		Various technical docs related to (re)stunts itself.

	src\restunts
		Project directory containing disassembly, ported c and makefiles to
		produce various executables based on the (re)stunts code.

	stunts
		Broderbund Stunts 1.1, the game.

	tools
		Contains setup scripts for Open Watcom 2 and bundled Windows build and
		testing tools.


### Contents of src\restunts:

	src\restunts\asmorig
		Contains asm code from the disassembled original exe.

	src\restunts\c
		Contains c functions ported from the disassembly.

	src\restunts\dos
		Makefile to build restunts for DOS.

	src\restunts\platform\dos
		DOS specific code.

	src\restunts\platform\amiga
		Native Amiga 500+ port: Motorola 68000, 320x200, 32 colors, Paula audio.

	src\restunts\platform\templeos
		Native HolyC port for TempleOS/TinkerOS: 640x400 rendering, 16 colors, PC speaker.

	src\restunts\repldump
		Tool based on the original game code, loads replays and dumps the game
		state contents at each frame in a file for further analysis.

	src\restunts\pixldump
		Replay-driven renderer test tool. It renders every frame incrementally
		and hashes the 320x200 camera framebuffer at frame 0 and every subsequent frame.

	src\restunts\genghost
		SDL3 desktop tool that searches headless physics simulations for faster
		driving inputs and writes a replay for use as a ghost.


The C core groups related sources and headers under `audio`, `car`, `data`, `frame`,
`ghost`, `headless`, `highscore`, `hires`, `math`, `menu`, `opponent`, `owoot`,
`physics`, `race`, `renderer`, `replay`, `shape2d`, `shape3d`, `stateply`,
`supersight`, `track`, `ui` and `video`.
The `data` folder holds full-game data definitions, resource strings, and lookup tables.
The `frame` folder owns classic frame rendering, camera setup, terrain and object
rendering, shared frame context, and frame diagnostics.
The `ghost` folder owns ghost playback, camera state, and racing-line guidance.
The `headless` folder holds headless startup support and shared engine and track data.
The `highscore` folder owns score tables, race-result screens, and result text.
The `math` folder owns trigonometry, vector and matrix operations, and rectangle helpers.
The `physics` folder owns vehicle collision, grip, and suspension helpers.
The `stateply` folder owns car-state updates, motion, and wheel-contact helpers.
The `video` folder owns frame interfaces, framebuffer selection, and page management.
The `extended` folder owns CSTRK/CSRPL logic and its codec headers, including
classic and high-resolution rendering that depends on decoded track or replay
data. The `hires` folder contains only HyperVision implementation: depth-based
visibility, high-resolution rendering, adaptive quality, interpolation, enhanced
artwork, settings, and render workers. HyperVision functions and renderer-owned
state use `hires` names. The `supersight` folder owns the classic SuperSight
visibility mask, queue/detail policy, settings, and status display. The two
renderers keep independent state. The `renderer` headers only route shared game
API calls to the selected implementation; classic rendering and shared
diagnostics stay outside both renderer folders.

CSTRK/CSRPL codecs and gameplay implementations live under `src/restunts/c/extended`
in files whose names start with `extended_`. Other core files delegate format work
through those modules. Public codec headers keep their existing names and APIs.
The shared format-aware OWOOT and generator route engines also live in
`extended_` modules so both track formats use the same traversal algorithms.

## How to build

SDL3 builds support Linux, BSD, Haiku, Windows, macOS, 32-bit DOS, and
[offline HTML/WebAssembly](#sdl3-webassembly-build-offline-html).

### SDL3 native builds: Linux, BSD, Haiku, Windows, macOS, and 32-bit DOS

The CMake build produces the game (`restunts`), physics dumper (`repldump`),
and renderer dumper (`pixldump`) for all native targets. SDL3 desktop builds also
include the offline ghost generator (`genghost`); see [its guide](genghost.md)
for track/replay inputs, physics restrictions, and search settings. Run the commands
below from the repository root. Use a separate build directory for each target,
architecture, compiler, and host; do not reuse a native Windows build directory
from WSL, or vice versa.

CMake caches absolute source and build paths. If a checkout was moved, copied,
or opened through a different shared-folder mount, an existing build tree can
report that its cache or source directory does not match. Regenerate it from
your current repository path with `--fresh`, then build:

```sh
cmake --fresh -S . -B out/sdl3-linux-x64 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/sdl3-linux-x64 --parallel 2
```

For other targets, use their build directory and repeat the toolchain and CPU
options from the corresponding recipe. `--fresh` clears previously cached
configuration options, so include any custom options you want to retain.

| Build host | Linux backend | Windows backend | macOS backend | DOS backend |
| --- | --- | --- | --- | --- |
| Linux x64 | Native GCC, x64 or x86 multilib | MinGW-w64 cross-compiler, x64 or x86 | Build on a Mac | DJGPP cross-compiler, 32-bit DPMI |
| Windows x64 | GCC inside WSL2 | Native MSYS2 UCRT64 for x64; MinGW-w64 inside WSL2 for x86 | Build on a Mac | Native DJGPP in MSYS2, or DJGPP inside WSL2 |
| macOS Apple Silicon or Intel | Use a Linux environment | Use a Windows or Linux environment | Apple Clang and macOS SDK; arm64, x86_64, or universal | Use a documented Linux or Windows environment |

CMake 3.25 or newer, Ninja, Git, Python 3.9 or newer, and the selected compiler
must be on `PATH`. Python bundles the standalone track editor.
CMake downloads and verifies the pinned SDL3 source automatically, so the
first configure needs network access. SDL3 is linked statically by default;
no separate SDL installation is required. Desktop builds also produce the
required Nuked OPL2 and Munt libmt32emu shared libraries. The pinned Munt
source is also downloaded and verified at configure time; proprietary Roland
ROMs are never downloaded or embedded. MSVC is not supported by this project.

Building does not download game assets. Before running the game or the audio
tests, place the Broderbund Stunts 1.1 files in `stunts/`. `ADSKIDMS.VCE` and `DEFAULT.RPL` must be directly inside `stunts/`.

#### Linux host: prerequisites

These Bash commands target Debian 12 or Ubuntu 24.04 on x64. They also apply
inside an x64 WSL2 distribution; Windows setup is described below.

```sh
sudo apt-get update
sudo apt-get install build-essential cmake ninja-build git curl ca-certificates \
    pkg-config python3 unzip bzip2
```

#### Linux host: Linux backend

Install the desktop driver development libraries and GTK 3 development files
for the native startup window, then configure and build:

```sh
sudo apt-get install libasound2-dev libpulse-dev libx11-dev libxext-dev \
    libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxtst-dev \
    libwayland-dev libxkbcommon-dev libdecor-0-dev libudev-dev libdrm-dev libgbm-dev \
    libegl1-mesa-dev libgl1-mesa-dev libgtk-3-dev
cmake -S . -B out/sdl3-linux-x64 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/sdl3-linux-x64 --parallel 2
```

The [native startup window](launcher.md) is enabled by default and uses
wxWidgets with GTK 3 on Linux. To build without it, add
`-DRESTUNTS_BUILD_LAUNCHER=OFF` to the CMake configure command.

For **32-bit Linux** on the same x64 host, install C and C++ multilib and the
i386 versions of the driver and GTK libraries, then use the Linux x86 toolchain:

```sh
sudo dpkg --add-architecture i386
sudo apt-get update
sudo apt-get install gcc-multilib g++-multilib \
    libasound2-dev:i386 libpulse-dev:i386 \
    libx11-dev:i386 libxext-dev:i386 libxrandr-dev:i386 libxcursor-dev:i386 \
    libxi-dev:i386 libxfixes-dev:i386 libxss-dev:i386 libxtst-dev:i386 \
    libwayland-dev:i386 libxkbcommon-dev:i386 libdecor-0-dev:i386 libudev-dev:i386 libdrm-dev:i386 \
    libgbm-dev:i386 libegl1-mesa-dev:i386 libgl1-mesa-dev:i386 libgtk-3-dev:i386
cmake -S . -B out/sdl3-linux-x86 -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/linux-x86.cmake
cmake --build out/sdl3-linux-x86 --parallel 2
```

The build directory contains `restunts`, `repldump`, `pixldump`, `genghost`, and
`libnuked-opl2.so`. After adding the game assets, test and run the x64 build:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    ctest --test-dir out/sdl3-linux-x64 --output-on-failure
out/sdl3-linux-x64/restunts --data-dir stunts --nointro
```

Use `out/sdl3-linux-x86` instead for the x86 build.

#### Linux host: Windows backend

Install MinGW-w64 and build each desired Windows architecture in its own tree:

```sh
sudo apt-get install mingw-w64
cmake -S . -B out/sdl3-windows-x64 -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-x64.cmake
cmake --build out/sdl3-windows-x64 --parallel 2
cmake -S . -B out/sdl3-windows-x86 -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-x86.cmake
cmake --build out/sdl3-windows-x86 --parallel 2
```

Each directory contains `restunts.exe`, `repldump.exe`, `pixldump.exe`,
`genghost.exe`, and `nuked-opl2.dll`. Run the executables and test binaries on
Windows. For a complete transferable package, use the packaging commands below.

#### Linux host: DOS backend

Install the pinned [DJGPP GCC 12.2.0 cross-toolchain](https://github.com/andrewwutw/build-djgpp/releases/tag/v3.4).
Its Linux binaries require `libfl2`. Download and check the archive before
extracting it:

```sh
sudo apt-get install libfl2
mkdir -p out/toolchains/djgpp-linux
curl --fail --location --retry 3 \
    https://github.com/andrewwutw/build-djgpp/releases/download/v3.4/djgpp-linux64-gcc1220.tar.bz2 \
    --output out/toolchains/djgpp-linux64-gcc1220.tar.bz2
printf '%s  %s\n' 8464f17017d6ab1b2bb2df4ed82357b5bf692e6e2b7fee37e315638f3d505f00 \
    out/toolchains/djgpp-linux64-gcc1220.tar.bz2 | sha256sum -c -
tar -xjf out/toolchains/djgpp-linux64-gcc1220.tar.bz2 \
    -C out/toolchains/djgpp-linux --strip-components=1
export PATH="$PWD/out/toolchains/djgpp-linux/bin:$PATH"
cmake -S . -B out/sdl3-dos -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/djgpp.cmake -DRESTUNTS_BUILD_TESTS=OFF
cmake --build out/sdl3-dos --parallel 2
```

The outputs are `out/sdl3-dos/restunts.exe`, `repldump.exe`, and `pixldump.exe`.
These are 32-bit DOS/DPMI programs. They do not use the desktop Nuked library.
See the DOS runtime instructions below for game data and the DPMI host.

#### Windows host: Windows backend

Install [MSYS2](https://www.msys2.org/) and open its **UCRT64** terminal.
Update MSYS2 first; if the updater asks you to close the terminal, reopen
UCRT64 and run the update again. Then install the native Windows tools:

```sh
pacman -Syu
pacman -S --needed git mingw-w64-ucrt-x86_64-gcc \
    mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
```

Change to the repository root in that terminal (for example,
`cd /c/src/restunts-bb11` for a checkout at `C:\src\restunts-bb11`). Select
UCRT64's native GCC directly; the MinGW toolchain files above are for Linux
cross-compilation:

```sh
cmake -S . -B out/sdl3-windows-x64-msys2 -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build out/sdl3-windows-x64-msys2 --parallel 2
```

After adding game assets, run the native tests and game in the same terminal:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    ctest --test-dir out/sdl3-windows-x64-msys2 --output-on-failure
./out/sdl3-windows-x64-msys2/restunts.exe --data-dir stunts
```

Keep `nuked-opl2.dll` beside the three `.exe` files. For **Windows x86** from a
Windows host, use WSL2 and the Linux-host Windows x86 recipe above. UCRT64 is
a 64-bit toolchain; use the separate i686 MinGW-w64 compiler for x86.

#### Windows host: Linux backend with WSL2

Install an x64 Linux environment with
[Windows Subsystem for Linux](https://learn.microsoft.com/en-us/windows/wsl/install).
In an **Administrator PowerShell** window, run:

```powershell
wsl --install -d Ubuntu-24.04
```

Restart Windows if requested and complete Ubuntu's first-run username/password
setup. Open the distribution from PowerShell:

```powershell
wsl -d Ubuntu-24.04
```

The resulting shell is **Linux Bash**. Open your checkout there, or clone the
branch you want to build into the Linux filesystem. A Windows checkout such as
`C:\src\restunts-bb11` is accessible with `cd /mnt/c/src/restunts-bb11`, but a
checkout under your WSL home directory avoids cross-filesystem build overhead.

Run **Linux host: prerequisites** and **Linux host: Linux backend** above for
Linux x64 or x86 executables. The same WSL shell can also run **Linux host:
Windows backend** for Windows x64/x86 and **Linux host: DOS backend** for DOS.
All three recipes use Linux tools inside WSL; their target executables retain
the selected backend's format. Use fresh build directories if the checkout has
already been built with native Windows tools. The pinned `linux64` DJGPP archive
requires an x64 Linux environment, not an ARM64 WSL distribution.

#### Windows host: DOS backend

In the **MSYS2 UCRT64** terminal configured above, install download/extraction
tools and get the standalone Windows DJGPP toolchain. This archive includes
its required Windows DLLs:

```sh
pacman -S --needed curl unzip
mkdir -p out/toolchains/djgpp-windows
curl --fail --location --retry 3 \
    https://github.com/andrewwutw/build-djgpp/releases/download/v3.4/djgpp-mingw-gcc1220-standalone.zip \
    --output out/toolchains/djgpp-mingw-gcc1220-standalone.zip
printf '%s  %s\n' 6f88b531d216f4d92668c960b5cde9a829b5611e06d2c3e431041e33f01c1a52 \
    out/toolchains/djgpp-mingw-gcc1220-standalone.zip | sha256sum -c -
unzip -q out/toolchains/djgpp-mingw-gcc1220-standalone.zip \
    -d out/toolchains/djgpp-windows
export PATH="$PWD/out/toolchains/djgpp-windows/djgpp/bin:$PATH"
cmake -S . -B out/sdl3-dos-windows -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/djgpp.cmake -DRESTUNTS_BUILD_TESTS=OFF
cmake --build out/sdl3-dos-windows --parallel 2
```

The same three DOS executables are created in `out/sdl3-dos-windows/`.
The WSL2 route above is also available and uses the same Linux DJGPP toolchain
as CI. DOS tests are disabled in these builds because DOS executables cannot
be run directly by the host's CTest process.

#### macOS host: macOS backend

The macOS target uses the shared SDL3 backend with Apple's native window,
input, audio, and graphics drivers. Build on a Mac with the Xcode Command Line
Tools and the macOS SDK. The helper script targets macOS 11.0 or newer on both
Apple Silicon (`arm64`) and Intel (`x86_64`). That is a build setting; older
macOS releases still require runtime testing.

In Terminal, install Apple's tools, complete the installer, then install
CMake 3.25+, Ninja, Git, and Python 3 using [Homebrew](https://brew.sh/):

```sh
xcode-select --install
brew install cmake ninja git python
```

Homebrew must already be installed and on `PATH`. SDL3 itself is fetched and
built by CMake; a Homebrew SDL installation is not needed. From the repository
root, build and package for the Mac running the command:

```sh
bash tools/scripts/build-macos.sh
```

The script selects `arm64` or `x86_64` from the current shell architecture,
builds Release executables in `out/sdl3-macos-<arch>/`, and installs the complete
runtime into `out/package-macos-<arch>/`. On Apple Silicon, use a native Terminal
or select `--arch arm64` explicitly if the shell runs through Rosetta.
To select an architecture or create a universal package for both CPU families:

```sh
bash tools/scripts/build-macos.sh --arch arm64
bash tools/scripts/build-macos.sh --arch x86_64
bash tools/scripts/build-macos.sh --arch universal
```

Each selection has a separate build and package directory. A universal build
contains both architectures in every executable and the audio library. CMake's
[`CMAKE_OSX_ARCHITECTURES`](https://cmake.org/cmake/help/latest/variable/CMAKE_OSX_ARCHITECTURES.html)
and [SDL's macOS build guide](https://wiki.libsdl.org/SDL3/README-macos) describe
this mechanism. Test each architecture on a matching Mac; an Intel-only build
needs Rosetta to run on Apple Silicon, and an arm64-only build cannot run on Intel.

Use `--jobs 4` to change build parallelism, or `--build-dir DIR` and
`--package-dir DIR` to choose output folders. Additional CMake options follow
`--`, for example `-- -DRESTUNTS_BUILD_TESTS=OFF`. To change the minimum OS,
set `MACOSX_DEPLOYMENT_TARGET` before configuring a fresh build directory.

To configure manually instead, this example targets Apple Silicon:

```sh
cmake -S . -B out/sdl3-macos-arm64 -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$(xcrun --find clang)" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build out/sdl3-macos-arm64 --parallel 2
cmake --install out/sdl3-macos-arm64 --prefix out/package-macos-arm64 --component Runtime
```

Use `x86_64` in both directory names and the architecture option for Intel;
use `universal` in the directory names and
`'-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64'` for a universal build.
The build directory contains `restunts`, `repldump`, `pixldump`, `genghost`, and
`libnuked-opl2.dylib`. The package keeps executables in `bin/`, the replaceable
audio library in `lib/`, and enhanced artwork beside the executables.

After placing the original game data in `stunts/`, run the native tests and game:

```sh
bash tools/scripts/build-macos.sh --arch arm64 --test
bash tools/scripts/run-macos.sh --runtime-dir out/package-macos-arm64 \
    --data-dir stunts -- --nointro
```

Replace `arm64` with `x86_64` on Intel. `--test` uses SDL's dummy video/audio
drivers for CTest and requires the game fixtures described above. To run tests
without rebuilding:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    ctest --test-dir out/sdl3-macos-arm64 --output-on-failure
```

The launcher also accepts the build directory with `--runtime-dir`.

The **PR validation** and **Release** workflows build one Universal package for
Apple Silicon and Intel, targeting macOS 11.0 or newer. The
`packages-macos-universal` artifact contains `restunts-macos-universal.tar.gz`,
which preserves executable permissions. CI runs game tests on native Linux
and/or 16-bit DOS; the macOS job builds and checks package contents.

#### macOS: running an already compiled package

No compiler, Homebrew, CMake, or separate SDL installation is needed for the
default runtime package. Download `restunts-macos-universal.tar.gz` from a
release for either Apple Silicon or Intel. When downloading the
`packages-macos-universal` CI artifact instead, unzip its outer GitHub Actions
archive first. Extract the runtime archive to a writable folder:

```sh
mkdir -p "$HOME/Games/restunts"
tar -xzf "$HOME/Downloads/restunts-macos-universal.tar.gz" -C "$HOME/Games/restunts"
```

Keep the entire extracted package together, including `bin/`, `lib/`, `share/`,
and `run-restunts.sh`.
Copy your original Broderbund Stunts 1.1 data into a separate writable folder,
for example `$HOME/Games/Stunts-data`, with `MISC.RES` (or `MISC.PRE`),
`ADSKIDMS.VCE`, tracks, cars, and the other game resources directly inside it.
Then launch from Terminal:

```sh
bash "$HOME/Games/restunts/run-restunts.sh" \
    --data-dir "$HOME/Games/Stunts-data" -- --nointro
```

Omit `-- --nointro` to watch the intro, or pass other game arguments after `--`,
for example `-- --hv:medium`. The launcher finds its own package regardless of
the terminal's working directory. The equivalent direct command is:

```sh
"$HOME/Games/restunts/bin/restunts" --data-dir "$HOME/Games/Stunts-data" --nointro
```

Saved games, replays, and dump outputs go into the selected game-data folder.
Use `repldump`, `pixldump`, or `genghost` from the same `bin/` folder with their
normal arguments. The [ghost generator](genghost.md) resolves input and output
paths from your working directory. The runtime is a Terminal-launched program;
there is no Finder `.app` bundle. On keyboards that assign system actions to function keys, use
Fn/Globe with F11/F12 for the game's timing and HyperVision controls.

#### Linux RISC-V64: Debian 13 and AppImage

The `linux-riscv64` and `linux-appimage-riscv64` targets use Debian 13 Trixie
and glibc 2.41 as their userspace baseline. Debian 13 is the first official
Debian release for RISC-V. Both packages target `rv64gc` with the `lp64d` ABI,
so they do not require RISC-V vector extensions. The AppImage bundles desktop
dependencies while using the host's glibc, ELF loader and graphics drivers.
See the [Linux build instructions](sdl3.md#linux) and
[release package guide](releases.md) for cross-build and runtime details.

#### BSD hosts: FreeBSD, OpenBSD, and NetBSD

The shared SDL3 backend has these release targets:

| Target | Runtime baseline |
| --- | --- |
| `freebsd-x64` | FreeBSD 14.4, amd64 |
| `freebsd-x86` | FreeBSD 14.5+, i386; SSE2 enabled |
| `freebsd-armv7` | FreeBSD 14.5+, ARMv7 hard-float |
| `freebsd-arm64` | FreeBSD 14.4, AArch64 |
| `freebsd-riscv64` | FreeBSD 15.0, RISC-V64 |
| `openbsd-x64` | OpenBSD 7.9, amd64 |
| `openbsd-x86` | OpenBSD 8.0+, i386; SSE2 enabled |
| `openbsd-arm64` | OpenBSD 7.9, ARM64 |
| `openbsd-riscv64` | OpenBSD 7.9, RISC-V64 |
| `netbsd-x64` | NetBSD 10.2, amd64 |
| `netbsd-armv7` | NetBSD 10.2+, ARMv7 hard-float (`earmv7hf`) |
| `netbsd-arm64` | NetBSD 10.2, AArch64 |
| `netbsd-riscv64` | NetBSD 11.0, RISC-V64 |
| `netbsd-x86` | NetBSD 10.2, i386 userspace; SSE2 enabled |
| `netbsd-x86-no-sse2` | NetBSD 10.2, i386 userspace; SSE2 disabled |

Each target produces a separate `.tar.gz` archive. CI compiles on x64 BSD
virtual machines running the matching OS release. ARMv7, ARM64, RISC-V64,
FreeBSD x86, and OpenBSD x86 cross-compile with verified target SDKs and
dependency packages. CI checks archive contents and executable architecture;
ARM64 and RISC-V64 target VMs also verify the exact archives and relocated
startup. Native x64 jobs compile directly; NetBSD x86 uses an i386 userspace
inside the amd64 VM.
See the [BSD build instructions](sdl3.md#bsd) for dependencies, native and
cross builds, and package creation. Build and relocated-startup checks do not
establish graphics/audio support or game performance on every physical board.

#### Haiku hosts: x86-64 and x86

The `haiku-x64` and `haiku-x86` targets build natively on Haiku R1/beta6
x86-64 and x86 with modern GCC and SDL3. They produce separate `.tar.gz`
archives containing the game, dump tools, enhanced artwork, and replaceable
Nuked library. See the
[Haiku build and VM testing guide](haiku.md) for dependencies, local builds,
package creation, and the QEMU workflow used for validation.

#### SDL3 build options, packages, and DOS runtime

Desktop regression binaries are built by default. Add
`-DRESTUNTS_BUILD_TESTS=OFF` at configure time if you only need the game and
dump tools. Run CTest in the native build environment. For cross-builds, run
the test binaries on the target OS; generated CTest files refer to the original
build and source paths.

For a **32-bit Linux, Windows, or NetBSD build without SSE2**, add
`-DRESTUNTS_SSE2=OFF` to its configure command and choose another build tree
(for example, `out/sdl3-linux-x86-nosse2`). This disables SSE/SSE2/AVX in the
game, Nuked, and bundled SDL. x64 requires SSE2; DOS uses the i386 baseline
and disables SSE2 by default. `-DRESTUNTS_SYSTEM_SDL=ON` can use an installed
SDL3 CMake package instead of the pinned source, but cannot be combined with
`RESTUNTS_SSE2=OFF`.

Install a complete runtime package from the build directory you used:

```sh
cmake --install out/sdl3-linux-x64 --prefix out/package-linux-x64 --component Runtime
cmake --install out/sdl3-windows-x64 --prefix out/package-windows-x64 --component Runtime
cmake --install out/sdl3-dos --prefix out/package-dos --component Runtime
```

Run only the install commands for targets you built. For native Windows
builds, substitute `out/sdl3-windows-x64-msys2` or `out/sdl3-dos-windows` as
the build directory. Packages contain `bin/` executables, desktop Nuked and Munt
libraries in Linux/BSD/Haiku/macOS `lib/` or Windows `bin/`, and dependency sources/notices under
`share/`. Keep the complete package together when moving or distributing it.
Game data is separate; desktop programs accept `--data-dir stunts` as their
first option when launched from the repository root.

For DOS, copy the three executables from the package's `bin/` into a separate
DOS game directory with the Stunts resources and the enhanced artwork folders.
CI and release DOS32 packages include [CWSDPMI.EXE](https://sandmann.dotster.com/cwsdpmi/)
in `bin/`; copy it beside the executables too. For a manual CMake install, add
a compatible DPMI host if the DOS environment does not provide one. Mount that directory in DOSBox/DOSBox-X, set
`core=dynamic`, `cycles=max`, and `aspect=true`, then run `RESTUNTS.EXE`.
Stop automated DOSBox runs with SIGKILL to avoid the shutdown confirmation.

See [the SDL3 guide](sdl3.md) for controls, audio details, package contents,
and native replay validation.

### SDL3 WebAssembly build: offline HTML

This target produces a single `restunts.html`. The default build supports serial
offline `file://` play. Configure `-DRESTUNTS_WASM_THREADS=ON` in a separate build
directory for multi-core rendering, and serve that build over HTTPS or localhost
with cross-origin isolation headers. It embeds the program, enhanced skyboxes, and enhanced
opponent portraits. **Original game files, including tracks and cars, are not
included** and are not needed to build the HTML. Players supply their own game
folder when starting it.

Install CMake 3.25+, Ninja, Git, Python 3, and the
[Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html).
The commands below use the tested Emscripten **6.0.10**. Install and activate the
SDK once; load its environment again in each new terminal. If you already have
that SDK installed, use its location instead of cloning another copy.

#### Linux host: WebAssembly backend

These Bash commands also work inside WSL2. Keep the SDK on a filesystem that
supports symbolic links, such as your Linux home directory. From the repository
root:

```sh
git clone https://github.com/emscripten-core/emsdk.git "$HOME/emsdk"
"$HOME/emsdk/emsdk" install 6.0.10
"$HOME/emsdk/emsdk" activate 6.0.10
. "$HOME/emsdk/emsdk_env.sh"
emcmake cmake -S . -B out/sdl3-wasm -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/sdl3-wasm --parallel 2
```

The output is `out/sdl3-wasm/restunts.html`.

#### Windows host: WebAssembly backend

Use **Command Prompt** with CMake, Ninja, Git, and Python 3 on `PATH`.
Run from the repository root and using a different build directory than other builds (e.g. WSL2):

```bat
git clone https://github.com/emscripten-core/emsdk.git "%USERPROFILE%\emsdk"
call "%USERPROFILE%\emsdk\emsdk.bat" install 6.0.10
call "%USERPROFILE%\emsdk\emsdk.bat" activate 6.0.10
call "%USERPROFILE%\emsdk\emsdk_env.bat"
call emcmake cmake -S . -B out/sdl3-wasm-windows -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/sdl3-wasm-windows --parallel 2
```

The output is `out/sdl3-wasm-windows/restunts.html`. The browser build produces
only the game; native dump tools and regression executables are disabled.

#### Run and package the HTML

For direct offline play, open `restunts.html` from `restunts-browser.zip`
or a local serial build. For the threaded build, extract the full
`restunts-browser-threaded.zip` and start the local server with
`Play-In-Browser.bat` on Windows 11, `./Play-In-Browser.sh` on Linux, or
`Play-In-Browser.command` on macOS. For launcher prerequisites and manual
hosting, see the [browser guide](wasm.md#play-from-a-website-or-offline).
Click **Choose game folder**, select your Stunts folder, then click **Start game**.
Supported browsers remember the selected folder and may ask you to **Allow folder
access** again when reopening the HTML. Use **Change game folder** to select a
different folder. These browsers can save directly after read/write permission;
others require folder selection each time and show save import/export controls.

To create the distribution package from a Linux/WSL2 build:

```sh
cmake --install out/sdl3-wasm --prefix out/package-wasm --component Runtime
```

For a native Windows build, replace `out/sdl3-wasm` with
`out/sdl3-wasm-windows`. Distribute the complete package, including dependency
licenses and both audio libraries' complete sources/relink materials. It contains
no original game data or Roland ROM images.
See [the browser build guide](wasm.md) for browser support, saving, and tests.

### Original 16-bit DOS builds with Open Watcom

The DOS compiler, assembler, and linker are Open Watcom 2, pinned to the official
[2026-09-01 build](https://github.com/open-watcom/open-watcom-v2/releases/tag/2026-09-01-Build).
The setup scripts verify its SHA256 and install into ignored `tools/watcom/`.
The shared release pin is `tools/scripts/open-watcom.conf`. WCC, WASM, WLINK,
and GNU Make 4.3 or newer run natively on Linux or Windows. The standard build
does not require Wine or DOSBox. Python 3.9 or newer is required for the
`*-original` targets, which prepare assembler-compatible copies of the
preserved original sources.

Git is required for the ported game builds. The Options menu shows
`Chocolate Stunts` and `Version <githash> (Mmm dd yyyy)`, using the first seven
characters of the checkout's commit and the compilation date (for example,
`Version d37a5c8 (Sep 18 2026)`). Incremental builds refresh both the commit
and compilation date.

#### On Windows

1. Install the toolchain from PowerShell at the repository root (Windows 10/11
   `tar.exe` and PowerShell are required):

   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\scripts\install-open-watcom.ps1
   ```

2. In cmd.exe at the repository root, run:

   ```text
   cd src\restunts
   setpath
   make restunts repldump pixldump
   ```

3. To build the original game and both dump tools from source as
   `stunts/restunto.exe`, `stunts/repldumo.exe`, and `stunts/pixldumo.exe`,
   install Python 3.9 or newer and run in the same cmd.exe window:

   ```text
   make restunts-original repldump-original pixldump-original
   ```

#### On Linux (x86-64)

1. Install GNU Make 4.3 or newer, Bash, curl, tar, xz, and
   coreutils. Run the setup script from the repository root:

   ```sh
   tools/scripts/install-open-watcom.sh
   ```

2. Build with native GNU Make:

   ```sh
   make -C src/restunts restunts repldump pixldump
   ```

3. To build the original game and both dump tools from source as
   `stunts/restunto.exe`, `stunts/repldumo.exe`, and `stunts/pixldumo.exe`,
   run from the repository root after installing Python 3.9 or newer:

   ```sh
   make -C src/restunts restunts-original repldump-original pixldump-original
   ```

The makefiles use `python3` on Linux and `python` on Windows; override
`PYTHON` if needed.

#### On both platforms

Build outputs are copied to `stunts/`.

The supported targets are:

| Target | Result |
| --- | --- |
| `restunts` | Builds `restunts.exe` from the ported C game and DOS platform layer. |
| `restunts-original` | Assembles the original game and links `restunto.exe` with WLINK. |
| `repldump` | Builds the C physics dump tool, `repldump.exe`. |
| `pixldump` | Builds the C renderer dump tool, `pixldump.exe`. |
| `repldump-original` | Builds `repldumo.exe` from the original assembly and physics dump wrapper. |
| `pixldump-original` | Builds `pixldumo.exe` from the original assembly and renderer dump wrapper. |
| `test-dos-platform` | Builds the DOS platform ABI test, `tests/build/watcom/<configuration>/DOSPLAT.EXE`. |
| `test-race-memory` | Builds the full-game race memory test, `tests/build/watcom/<configuration>/RACEMEM.EXE`. |
| `test-ghost-racing-line` | Builds the DOS16 ghost reconstruction and racing-line renderer test, `tests/build/watcom/<configuration>/GHOSTLIN.EXE`. |
| `test-driving-options` | Builds DOS16 startup, driving-settings, and command-tail regression executables. |
| `clean` | Removes generated build objects and candidate executables. |

Run the DOS16 racing-line check with
`python3 tools/scripts/run-ghost-racing-line-dos-test.py` after building its target.
It requires DOSBox and the original game resources in `stunts/`.

The `*-original` dump targets assemble the original game code with WASM,
compile the dump wrappers with WCC, and link them with WLINK. The resulting
executables are rebuilt development tools. CI uses the independent Borland
`repldumo.exe` and `pixldumo.exe` preserved under
[tools/oracles/borland](../tools/oracles/borland/README.md), whose SHA256 hashes
are recorded alongside them. The archived renderer captures every frame
incrementally. Source builds never replace the archived files.

The original game assembly under `src/restunts/asmorig/` is preserved
unchanged. The WASM build runs
[the source adapter](../tools/scripts/prepare-wasm-original.py) to prepare
compatible copies under `asmorig/build/watcom/<configuration>/wasm/source/`.
All three `*-original` targets reuse these objects. The `restunto.exe` link
response file reproduces the original segment order, disables automatic segment packing,
and preserves the original 8000-byte stack. The old empty `segments.obj`
layout helper is replaced by WLINK ordering directives; its ASM source is
retained unchanged.

See [the assembler guide](assembler.md) for generated-source handling,
original-code layout requirements, and object/executable comparison commands.

`makerepldump.bat` builds both games and both physics dump tools;
`makepixldump.bat` builds both renderer dump tools. Both stop on build errors.

`pixldump-original` builds `stunts/pixldumo.exe` from the original game assembly
and the current capture wrapper. Local `pixelcheck.sh` runs use the renderer
reference in `stunts/`. `validate-toolchain.py` takes it from
`--candidate-directory`, which defaults to `stunts/`, and uses the archived
physics oracle. CI copies both archived Borland oracles into its isolated
test directory after copying the source-built executables.

#### Assembler

All assembly targets use Open Watcom WASM from the same pinned installation
as WCC and WLINK, with 8086 code generation. `-zcm=tasm` selects WASM's
built-in compatibility mode for the preserved assembly syntax; it does not
require the Turbo Assembler tools.

#### Compiler, linker, and debugging symbols

All DOS C targets use Open Watcom 2 WCC and WLINK. `LINKER=wlink` is the only
supported linker setting. `setpath.bat` puts the pinned Watcom tools before
bundled utilities on PATH and sets `WATCOM` and `INCLUDE` accordingly.

Shared flags live in `src/restunts/watcom.mk`: 8086 instructions, the medium
memory model (far code and near data), the stack-based C calling convention,
signed `char`, and byte-packed structures. These settings preserve the
original game's 16-bit data layout and assembly interfaces. The custom DOS
startup initializes the stack and BSS; compiler stack probes are disabled.
The runtime libraries and headers come from the same pinned Watcom release.
Portable game C uses size optimization (`-os`). The DOS platform layer and
startup are compiled without optimization (`-od`) because the pinned compiler
can incorrectly merge branches around inline assembly interrupt calls.
DOS resource pointers are explicitly normalized to a paragraph plus an offset
below 16 bytes before they reach fixed-segment sprite code.
[Watcom huge-pointer arithmetic](https://github.com/open-watcom/open-watcom-v2/blob/2026-09-01-Build/bld/clib/cgsupp/a/pia.asm)
preserves larger offsets; normalization retains the Borland representation
and prevents bitmap reads from wrapping at a 64 KiB boundary.

Use `CONFIG=debug` to request Watcom C debug information. C optimization is
disabled except for the original renderer wrapper described below:

```text
make CONFIG=debug restunts
```

For `pixldump-original`, `pixldump.c` and `murmur3.c` retain size optimization
(`-os`) and use line debugging (`-d1`) without local-variable information.
This preserves the release code generation and stack layout required by the
original rendering code. Other objects use their usual debug flags.

WLINK writes Watcom debug information; debug builds also request WASM line
information.

#### The toolchain

| Purpose | Active tool |
| --- | --- |
| 16-bit DOS C compilation | Open Watcom 2 `binnt/wcc.exe` or native Linux `binl64/wcc` |
| 16-bit DOS linking | Open Watcom 2 `binnt/wlink.exe` or native Linux `binl64/wlink` |
| C headers and runtime | Open Watcom 2 `h/` and `lib286/` |
| Assembly | Open Watcom 2 `binnt/wasm.exe` or native Linux `binl64/wasm` |
| Build orchestration | GNU Make 4.3 or newer (bundled 4.4.1 on Windows) |
| Original-source preparation | Python 3.9 or newer, for the `*-original` targets with WASM |
| Running and testing | DOSBox / DOSBox-X |

Current makefiles select Watcom executables by their full installation paths.
Open Watcom supplies all C headers and runtime libraries. Regression oracles
retain their original Borland-built machine code.

#### Debugging restunts.exe

`CONFIG=debug` builds Watcom debug information and writes linker map files
beside the executables. Use a debugger that supports Watcom's format.

## pixldump

pixldump and pixldumo use the same mandatory parameters. The number of
parameters selects the output mode:

```text
pixldump.exe <replay> <camera> <target>
pixldump.exe <replay> <camera> <target> <frame>

pixldumo.exe <replay> <camera> <target>
pixldumo.exe <replay> <camera> <target> <frame>
```

| Parameter | Accepted values | Description |
| --- | --- | --- |
| `replay` | Replay base name, `.rpl`, or `.RPL` filename | Replay to render. The extension may be omitted. |
| `camera` | `1`, `2`, `3`, or `4` | Selects F1, F2, F3, or F4 respectively. |
| `target` | `0` or `1` | Selects the player (`0`) or opponent (`1`). Opponent mode requires a replay containing an opponent. |
| `frame` | `0` through `65535` | Exact frame to render. It must not exceed the replay's final frame. Supplying it selects BMP mode. |

With three parameters, the tools generate the normal hash dump. pixldumo writes
`<replay>.PDO` and pixldump writes `<replay>.PDD`. The first line is `PIXLDUMP 2`,
terminated by CRLF, identifying capture with incremental redraws on every frame.
Each subsequent CRLF-terminated row
contains the decimal frame number, one space, and the MurmurHash3_x86_32 of the
raw 64,000-byte Mode 13h framebuffer, with seed 0. The hash is eight lowercase
hexadecimal digits, most significant digit first, including leading zeroes.
The tools hash every frame from frame 0 through the replay's final frame.
Old full-redraw, MD5, and five-frame sampled dumps are incompatible; the
regression runner rejects their missing version header or missing frames and
regenerates them automatically.

With four parameters, the tools generate only the requested 320x200 indexed BMP
using the game's VGA palette. The filename includes the camera, target, frame,
and renderer:

```text
<replay>.<camera>.<target>.<frame>.PDO.bmp
<replay>.<camera>.<target>.<frame>.PDD.bmp
```

For example, `default.2.0.5.PDD.bmp` is frame 5 from the ported renderer, using
the F2 helicopter camera and following the player.

Examples:

```text
REM Generate a player/F2 hash dump.
pixldump.exe default.rpl 2 0

REM Generate an opponent/F4 hash dump with the original renderer.
pixldumo.exe default.rpl 4 1

REM Generate a player/F1 BMP at frame 5.
pixldump.exe default.rpl 1 0 5
```

pixldump must reproduce the original renderer, including its bugs. In particular,
polygon depth averages use unsigned division for non-power-of-two vertex counts
even when near-plane clipping retains a negative depth sum. This can put a grille
behind opaque surfaces, as in `0027.rpl`, camera 2, player, frame 665. Preserve
this behavior in the C port; the original engine in `asmorig` remains the oracle.

Opponent route selection also preserves original resource overreads. Track tile
IDs index the opponent's `sped` data beyond its 16 speed entries, into following
resource chunks and bytes left by decompression. The route loader reconstructs
these bytes in a fresh, zero-initialized allocation, retaining the original
compressed-source placement and enough owned tail storage for every byte-sized
index. Ordinary cached resources have discarded this tail and cannot supply it.
Clamping costs at the declared resource size changes route choices: in
`0034.rpl`, it selects the alternate branch and first diverges at frame 943.
Do not replace the reconstruction with either zero costs or reads from adjacent
allocations. Host tests cover decoder residue and poisoned memory; the golden
replays cover the resulting opponent behavior.

The C renderer also preserves the original sphere bounding-box writes used by
crash explosions and the renderer stack values reused by stopped-wheel physics.
The pixel-dump wrapper supplies the archived caller context, deriving addresses
from the DOS load segment, decoded arguments, and resource allocations. See
[renderer parity notes](renderer-parity.md) for the assembly evidence and
regression coverage.

Both modes render every frame from frame 0 with incremental redraws, maximum
graphical detail, and hidden dashboard and replay controls. BMP capture retains
the preceding framebuffer history up to the requested frame. Invalid arguments
are rejected before an output file is created. The
complete output path, including its generated suffix, must fit in 127
characters. BMP filenames require DOS long-filename support; the supplied
`tools/scripts/dosbox.proc.conf` enables it for DOSBox-X.

## pixelcheck

On Linux, `pixelcheck.sh` builds or reuses both executables, runs them with the
same replay, camera, target, and optional BMP frame, and compares the resulting
files:

```text
tools/scripts/pixelcheck.sh <replay-file> <rebuild> <camera> <target>
tools/scripts/pixelcheck.sh <replay-file> <rebuild> <camera> <target> <frame>
```

| Parameter | Accepted values | Description |
| --- | --- | --- |
| `replay-file` | Replay filename under `stunts/` | Replay passed to both executables. |
| `rebuild` | `true` or `false` | `true` rebuilds both executables first; `false` reuses the existing executables in `stunts/`. |
| `camera` | `1`, `2`, `3`, or `4` | Camera passed to both executables: F1, F2, F3, or F4 respectively. |
| `target` | `0` or `1` | Player (`0`) or opponent (`1`) passed to both executables. |
| `frame` | `0` through `65535` | Present in the BMP form only. It selects the exact frame compared by both executables. |

Examples:

```text
# Rebuild both executables and compare player/F2 hash dumps.
tools/scripts/pixelcheck.sh 0610.rpl true 2 0

# Reuse existing executables and compare opponent/F4 hash dumps.
tools/scripts/pixelcheck.sh 0610.rpl false 4 1

# Reuse existing executables and compare player/F1 BMPs at frame 5.
tools/scripts/pixelcheck.sh 0610.rpl false 1 0 5
```

Set the `PIXELDUMP_TIMEOUT_SECONDS` environment variable to a positive integer
to override the default 120-second timeout for each DOSBox run.


## C coding style

Project `.c` and `.h` files under `src/` use tabs with a width of four columns,
a 100-column target, K&R braces (function opening braces on their own line),
and a required braced body for every `if`, `else`, `for`, `while`, and `do`.
Conventional `else if` chains are allowed. Empty loops also need braces. Keep
CRLF line endings, as required by `.gitattributes`.

Core `.c` files under `src/restunts/c` are split by responsibility into units of
at most 800 code lines, excluding blank lines and comments. Internal headers
share declarations and context; implementations stay in separate `.c` files.

The desktop launcher, ghost generator window and MT-32 ROM scanner use C++11
and follow the same whitespace and brace rules. The game and scanner tests
explicitly select C++11 so modern host compilers do not emit C++14 sized-delete
calls missing from the pinned legacy AppImage runtime.
Their `.cpp` files are included in the formatting checks below.

C sources use the C99 features supported by Open Watcom (`-zastd=c99`).
Open Watcom requires constant expressions in array initializer lists; assign
runtime values explicitly or with a loop.
Declare local variables close to their first use, combining the declaration
and first assignment when possible. Keep declarations in the enclosing scope
when values are shared across branches or loops, and preserve initialization
order and object lifetime.

Two standard tools check the style directly:

- [editorconfig-checker](https://github.com/editorconfig-checker/editorconfig-checker)
  reads the whitespace rules in `.editorconfig`. Its Python package installs
  the `ec` command. `.editorconfig-checker.json` limits discovery to `src/`
  and permits alignment spaces after indentation tabs. C/H and HolyC HC/HH files
  have source formatting rules.
- [clang-format](https://clang.llvm.org/docs/ClangFormat.html) reads C layout
  and brace rules from `.clang-format`. Keep its whitespace settings consistent
  with `.editorconfig`. Include order is preserved.

Install the pinned tools once in a Python virtual environment:

```sh
python3 -m venv .venv
# Linux/macOS:
. .venv/bin/activate
# Windows cmd.exe: .venv\Scripts\activate.bat
python -m pip install -r tools/scripts/requirements-format.txt
```

From the repository root, check against `.editorconfig` with one command:

```sh
ec
```

Check C formatting too (Bash or Git Bash):

```sh
git ls-files -z 'src/*.c' 'src/*.cpp' 'src/*.h' | xargs -0 -r clang-format --dry-run --Werror
```

Format all tracked project C/H files:

```sh
git ls-files -z 'src/*.c' 'src/*.cpp' 'src/*.h' | xargs -0 -r clang-format -i
```

For an individual file, use `clang-format -i path/to/file.c`. Add new files to
Git before running the commands based on `git ls-files`. CI runs both checks
for pull requests and before releases. No project-specific C formatter or checker
script is needed.

Braces are required even where clang-format cannot insert them automatically,
including macro bodies, empty loops, and bodies spanning preprocessor
branches. Review those cases manually; a successful clang-format check does
not prove that those cases have braces.

## HolyC coding style

Project `.HC` and `.HH` files follow the naming conventions in Terry Davis's
[TempleOS glossary](https://github.com/cia-foundation/TempleOS/blob/archive/Doc/Glossary.DD)
and the compact layout of his HolyC sources. Use two spaces per indentation
level, a 100-column target, and CRLF line endings. Spaces preserve the two-column
nesting without depending on the eight-column tab stops used by TempleOS.
Put function and class opening braces on their own line and control-flow
opening braces on the control line. Retain existing braced bodies; a style
change must not rewrite control flow.

Use `MixedCaps` for functions, `C`-prefixed `MixedCaps` for classes, and
`lower_case` for parameters, local variables, globals, and class members.
Keep constants in `ALL_CAPS`. Project code retains the `legacy_*` types and
named constants. Compact expression and argument spacing as in `x=y+z` and
`Fun(src,dst)`; keep pointer declarations readable as `legacy_u8 *src`.
Translated shared C functions have an `Rs` prefix to avoid collisions with
HolyC built-ins. Existing `Temple*`, `Test*`, and `Restunts` entry points retain
their names.

The port's `.clang-format` and `tools/STYLE.PY` define the HolyC layout separately
from the C rules above. The script uses clang-format 18.1.8, then compacts
horizontal whitespace while preserving language tokens. With the formatter
installed as described in the
[port README](../src/restunts/platform/templeos/README.TXT), run:

```sh
python3 src/restunts/platform/templeos/tools/STYLE.PY \
    --formatter /tmp/restunts-templeos-env/bin/clang-format --check
```

Omit `--check` to apply formatting. Optional file paths limit the operation;
without paths it includes all `.HC` and `.HH` files in the port, including
`generated/`. The style tool formats whitespace; it does not rename symbols.
Change shared C code or `tools/TRANSLATE.PY` and regenerate generated sources
instead of editing them by hand. The translator emits the same layout and
`generated/NAMES.JSON`, which maps shared C names to their HolyC names.

## C# coding style

The regression application and its tests under `tools/scripts/dumpsrv` use four-space
indentation, opening braces on their own line, braced control-flow bodies, a
100-column target, and CRLF line endings. `.editorconfig` defines these rules.
Use the .NET 10 SDK to check or apply formatting:

```sh
dotnet format tools/scripts/dumpsrv/dumpsrv.slnx --verify-no-changes
dotnet format tools/scripts/dumpsrv/dumpsrv.slnx
```

## Complexity audit

See [the complexity report](complexity.md) for measurements, completed
refactors and audit results.


## CI packages and releases

**PR validation** and **Release** build every distribution package: 16-bit and
32-bit DOS; Linux ARMv7, ARM64, RISC-V64, x86 with and without SSE2, and x64,
including AppImage variants; Windows ARM64, x86 with and without SSE2, and x64;
FreeBSD, OpenBSD, and NetBSD x64, ARM64 and RISC-V64; FreeBSD and NetBSD ARMv7;
FreeBSD and OpenBSD x86 with SSE2; NetBSD x86 with and without SSE2; and
Haiku x64 and x86. The workflow also builds Universal macOS, serial and threaded
browser packages, Android ARMv7 and ARM64 (both Android 5.0+), Amiga 68000,
and stock TempleOS.
The reusable **Build release packages** workflow is used by **PR validation** and **Release**.

Packages contain the platform's runtime files, but no original game data.
SDL3 packages include enhanced artwork. SDL3 desktop, browser and Android packages
include the Nuked and Munt sources and licenses. Roland ROMs are supplied
separately and excluded from every release. DOS32 uses hardware Ad Lib/OPL or
PC Speaker and omits both emulation libraries.
DOS32 also includes CWSDPMI. Amiga uses Paula audio, and TempleOS uses the PC
speaker. The TempleOS ZIP includes the installation ISO and private-data import tools.

A release publishes the exact archives downloaded from its build artifacts,
after verifying their checksums and signed build provenance. Immutable releases
lock the published assets and source tag. See [release packages and verification](releases.md)
for the platform baselines, package contents, and verification commands.

## CI replay validation

CI runs in five phases, each requiring the previous phase to pass:

1. C/H formatting, C# regression service tests, host regression tests, native
   sanitizer regressions, and shard planning run in parallel.
2. Build the selected platforms: DOS executables with timer/cleanup checks,
   or the Linux x64 SDL3 game and dump tools with platform regressions.
3. Run all physics replay shards for the selected platforms and validate coverage.
4. Run renderer replay shards and validate coverage for each selected platform,
   camera, and target.
5. Publish a combined physics/renderer report for each selected platform,
   camera, and target. DOS uses `partitions_all-cam<camera>-target<target>`;
   SDL3 uses the same name with an `sdl3-` prefix.

A failed phase skips the later phases. Physics and renderer phase diagnostics
remain available in their individual artifacts if replay validation fails.

The Linux x64 sanitizer jobs cover the host and SDL3 regression suites,
including the renderer-worker and audio tests. AddressSanitizer splits the
suite across two jobs. One runs extended ghost and fine-tune tests, and the
other runs every remaining test. UndefinedBehaviorSanitizer runs the complete
suite in one job. ThreadSanitizer splits the same coverage across 36 jobs.

CTest registers the extended ghost suite as `genghost-extended-<part>-<shard>`:
parts 1, 2, and 3 each have two shards; part 4 has three. Part 4 covers fork and
shared-gate searches. The script's `--part` option still runs that complete part;
add `--shard` to run one shard, or omit both options to run the entire suite.
`genghost-search-1` and `genghost-search-2` divide the search regression; its
script runs both parts when `--part` is omitted. The restricted-driving search
uses two distinct seeds with 1,000 evaluations each, retaining a total budget of
2,000 evaluations while checking both generated replays. Each shard,
`genghost-fine-tune`, `genghost-parallel`, `extended-render`, and `render-replay`
has a dedicated TSan job. Another job covers the remaining `genghost-` tests.
After the first sanitizer group finishes, 20 TSan jobs run the remaining
host and SDL3 regressions. They assign each CTest test to a shard using
observed TSan durations. Each sanitizer runs every regression once across
its jobs. Both groups gate PR validation and releases for every platform
selection. Each job uses debug symbols, frame pointers,
and fail-on-error sanitizer settings. ASan uses Clang 18 and also checks for
leaks; TSan and UBSan use GCC. The ASan linker
flags preserve removal of unused fixture data while retaining checks on all
globals that remain in the executable. Bundled SDL3, wxWidgets, and Nuked are
instrumented along with the game, launcher, and tests. Sanitizer builds disable
SDL D-Bus support because the headless dummy drivers do not need desktop IPC.
Test logs are retained in `sanitizer-<variant>-logs` artifacts, including on
failure. Dedicated TSan variants use `thread-<test-name>` and
`thread-genghost-other`. The 20 shards use `thread-1` through `thread-20`.
TSan's runner lowers ASLR mapping
entropy to keep its shadow-memory range available. Sanitizer jobs allow twenty
minutes per replay subprocess and use a sixty-minute default CTest timeout,
with scaled limits for ghost and rendering regressions. ASan and UBSan jobs
allow ninety minutes; TSan jobs use the six-hour GitHub-hosted runner limit.
Normal replay tests retain their two-minute limit (five minutes with `--full`);
set `RESTUNTS_RENDER_REPLAY_TIMEOUT_SECONDS` to a positive integer to override it.

To reproduce ASan locally on Linux, install `clang-18`, `libclang-rt-18-dev`,
and `llvm-18`. Use matching C and C++ compilers with the same sanitizer flags.
Use a fresh build directory and keep `CFLAGS` and `LDFLAGS` set while running
CTest so the standalone host tests receive the same instrumentation.
Original game data must be present in `stunts/`, as for the normal SDL3 regressions:

```sh
export CC=clang-18
export CXX=clang++-18
export LDFLAGS=-Wl,-z,start-stop-gc
export CFLAGS='-O2 -g -fsanitize=address -fno-omit-frame-pointer -fno-sanitize-recover=all'
export CXXFLAGS="$CFLAGS"
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export ASAN_SYMBOLIZER_PATH=/usr/bin/llvm-symbolizer-18
export TSAN_OPTIONS=halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
export RESTUNTS_RENDER_REPLAY_TIMEOUT_SECONDS=1200
export RESTUNTS_GENGHOST_TEST_TIMEOUT_MULTIPLIER=8
cmake -S . -B out/sanitizer-address -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DRESTUNTS_BUILD_TESTS=ON -DSDL_DBUS=OFF \
    -DRESTUNTS_GENGHOST_TEST_TIMEOUT_MULTIPLIER="$RESTUNTS_GENGHOST_TEST_TIMEOUT_MULTIPLIER"
cmake --build out/sanitizer-address --parallel 2
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    ctest --test-dir out/sanitizer-address --output-on-failure --no-tests=error --timeout 3600
```

Replace `address` with `thread` or `undefined` in both the flag and build
directory for the other sanitizers, and use `CC=gcc` and `CXX=g++` to match CI.
Set `RESTUNTS_GENGHOST_TEST_TIMEOUT_MULTIPLIER=4` for UBSan or `40` for TSan,
using the same value for the environment and CMake option. These budgets keep
the complete evaluation counts and replay assertions under instrumentation, and
also scale UNIT race, subprocess, and native rendering fixture deadlines.
CMake also scales the planning allowance for its prepared UNIT race commands,
stopping early once a complete clean native lap is ready. The instrumented UNIT
race binary scales the native pump, continuation, and recheck CPU time guards
by the same multiplier to account for instrumentation overhead. Native tick quotas,
chunk limits, exact-state handoffs, and race-frame limits remain unchanged.
Timed-target normal runs retain the ten-second preparation window and original
CPU guards.

The Glacier OWOOT, native-grid independence, four no-stall, and prepared-worker
regressions use `test-unit-race-fixed-work` in every build configuration. This test-only
target uses the same bounded native tick quotas in normal builds and sanitizers.
Completion tests stop after 1,024 preparation callbacks or a clean native lap; paired grid
runs complete exactly 1,024 callbacks. Startup route evaluation runs synchronously. Each
live request waits for the previous worker batch; rollouts and physics still run alongside
newly published work.

Track completion, penalties, stalls, worker proposals, and full replay checks remain
enabled. Clear-road threaded completion cases require accepted live proposals without
forcing them to replace cached controls. Native-grid encounters compare UNIT controls
before physical contact and verify the resulting replay. The preparation watchdog reserves
half the scaled CTest limit; expiry fails the test instead of selecting a different
partial preparation state. `unit-fixed-work`, `unit-fixed-work-budget`, and
`unit-planner-fixed-work` check the logical work policy and reject incomplete preparation
budgets.

The standard timed race target, native-grid, threaded-planner, and desktop tests retain
their timer and asynchronous startup coverage.

Update `CXXFLAGS` to match `CFLAGS`. The host runner accepts `CFLAGS` and
`LDFLAGS` as whitespace-separated arguments, without shell evaluation. TSan on
Linux hosts with high ASLR entropy may need
`sudo sysctl -w vm.mmap_rnd_bits=28`, as in CI, before running the tests.
Use a separate shell or unset the exported variables before ordinary builds.

The `platforms` input selects the replay-test builds, tests, and reports. It is a
nonempty JSON array of unique names from `dos` and `sdl3`. **Build and validate**,
**PR validation**, and **Release** default to `["sdl3"]`. For example:

- `platforms: '["dos"]'` selects DOS only.
- `platforms: '["sdl3"]'` selects Linux x64 SDL3 only.
- `platforms: '["dos","sdl3"]'` selects both.

Unknown names, duplicates, and empty arrays fail validation before building.
Unselected replay platforms are skipped. **PR validation** and **Release**
always build every distribution package, including the DOS executables, even
when replay tests select only SDL3. Haiku package jobs also run native CTest
regressions; full golden replay corpus validation remains in the selected DOS
and Linux x64 SDL3 jobs.

Physics covers the full golden replay set; renderer tests use the configured
percentage (the reusable and manual workflows default to 100%). Both DOS and
SDL3 dump tools use the same golden corpus, plans for each target, archived
Borland oracles, complete-output checks, and byte-for-byte
comparisons (`.BNI` against `.BIN`, `.PDD` against `.PDO`). SDL3 candidates run
natively on Linux x64 with dummy video and audio drivers; missing references
are still generated by the preserved DOS executables through DOSBox-X.
The `cameras` input is a nonempty JSON array of unique camera IDs from 1
through 4, defaulting to `[1,2,3,4]` in all three workflows. The `targets`
input is a nonempty JSON array of unique target IDs: player (`0`) and opponent
(`1`), defaulting to `[0,1]`. Use `targets: '[0]'` for the player alone or
`targets: '[1]'` for the opponent alone. Invalid IDs, duplicates, and empty
arrays fail validation before building.
These inputs are available in the manual **PR validation** and **Release**
workflows and passed to **Build and validate**, which calls **Replay tests**
for each selected platform, camera, and target. For example,
`cameras: '[1,2,3,4]'` with `targets: '[0,1]'` runs eight **Renderer replays**
jobs per platform. Pull-request runs use the fallback values in
`pr-validation.yml`.

Physics runs once per selected platform. Its report uses the first selected
camera and target as metadata and is reused for every renderer combination.
Every selected platform and camera uses the same plan for a given target.
Renderer percentage, shard count, worker count, and timeout inputs apply
equally to DOS and SDL3. A failed renderer job does not cancel the others.
The final reports require every selected platform, camera, and target to
pass. Each report combines that platform's physics diagnostics with its
selected camera and target's renderer diagnostics. Opponent rendering selects
only replays containing an opponent.

Each CI shard verifies the archived checksums and uses the preserved Borland
dump executables to generate missing references. Renderer shards download
`PDOs-cam<camera>-target<target>.zip` for the selected camera and target.
Physics shards download `BINs.zip` from oracle release `v1.0.4`. Oracle
downloads run for both targets. Unavailable or failed cache downloads produce
a warning and allow testing to continue. Missing or invalid references are
generated locally. Renderer cache metadata records the camera and target so
a changed view cannot reuse an incompatible reference.

The C# application in `tools/scripts/dumpsrv` runs these comparisons on Linux, Windows,
and GitHub Actions. Its HTTP service, direct runner, and report merger share the
same engine. See the [service and runner guide](../tools/scripts/dumpsrv/README.md)
for publishing, service parameters, client options, and local execution.

Set `renderer-test-percentage` (an integer from 1 to 100) when manually
starting **PR validation** or **Release** to change renderer coverage. Calls
to the reusable `build-and-validate.yml` workflow can set the same input;
pull-request events use the fallback in `pr-validation.yml` (currently
100% for cameras 1 through 4, both targets, and SDL3). The HTTP service
retains its separate 100% default for `RendererTestPercentage`.

Every top-level `.rpl` file is considered, regardless of filename structure.
Physics uses the full corpus. For target `1`, renderer selection first
filters for a nonzero opponent type in the replay header. Renderer sampling
then uses the eligible, ordinal-sorted list before shard balancing; the
percentage applies to that list. For target `0`, all replays remain eligible.
The CI planning job reads recorded tick counts from replay headers and
balances physics and renderer shards separately, targeting totals
within ±2% of each phase's average. It publishes one plan per selected target
as `target0.json` or `target1.json` in the `replay-shard-plan` artifact. The
physics assignments are identical in both plans; renderer assignments use
each target's eligible sample. Replay jobs, oracle extraction, and coverage
checks consume the matching plan and validate its target; the C# application
selects its lists by shard ID. The planner's `--target` option and the C#
application's `Target` setting still select one target per invocation.
A corpus with no opponents has empty renderer shards for target `1`, while
physics still covers every replay.
Workers within a shard still receive round-robin lists whose replay counts
differ by at most one. Sampling is independent of shard and worker counts.
CI validates completed replay identities from the JSON shard results, so missing
or duplicate coverage, processing errors, and byte mismatches fail validation.

The reusable workflow defaults to 20 shards with 5 workers each, 120 seconds
per physics execution, and 980 seconds per renderer execution. It checks C/H
formatting, host regressions, and C# tests and formatting before the selected
builds. When enabled, the DOS build uses native Open Watcom 2 and checks the
DOS platform ABI, dump timer masking, and failure cleanup. Run the C# checks
locally with the .NET 10 SDK:

```sh
python3 tools/scripts/test-plan-replay-shards.py
dotnet test tools/scripts/dumpsrv/dumpsrv.slnx --configuration Release
dotnet format tools/scripts/dumpsrv/dumpsrv.slnx --verify-no-changes
```

### Workflow dependency lockfile

The generated [.github/workflows/actions.lock](../.github/workflows/actions.lock)
pins direct and transitive action dependencies. The **Verify workflow lockfile**
CI check verifies that it matches the workflows. Do not edit the lockfile manually.

Install the pinned GitHub CLI extension, then regenerate the lockfile after
changing action dependencies:

```sh
gh extension install github/gh-actions-lock --pin v0.4.1
gh actions-lock
```

Validate it locally with `gh actions-lock --verify`.

### Compiler migration validation

For a deterministic comparison of 100 evenly spaced golden replays in both
physics and rendering, build `repldump`, `pixldump`, and `pixldump-original`, then run:

```sh
python3 tools/scripts/validate-toolchain.py --output out/watcom-validation
```

Use a new output directory. This verifies the archived Borland checksums,
uses the archived physics oracle and freshly built incremental renderer reference,
records SHA-256 fingerprints of executables and inputs, and generates fresh
outputs in an isolated DOS directory. The shared C# runner checks complete
per-frame physics data and camera-2/player framebuffer hashes byte for byte.
See [the oracle guide](../tools/oracles/borland/README.md) for coverage, timeout,
and cache options. Run the platform ABI check separately:

```sh
make -C src/restunts test-dos-platform
python3 tools/scripts/run-dos-platform-test.py
```

Check the conventional-memory budget with a complete race, including the
player dashboard and both 320x200 VGA render pages:

```bash
make -C src/restunts test-race-memory
python3 tools/scripts/run-race-memory-test.py --dosbox dosbox
python3 tools/scripts/run-race-memory-test.py --dosbox dosbox-x
```

This regression loads DIA3 for the player and CSIL for the opponent on
`DEFAULT.TRK`, draws with SuperSight off, on, then off again, and reports the
remaining conventional memory. It checks page isolation and presentation, changes
gears and gauges, and reloads the race to check drawing and resource release.
It also requires that the conventional-memory race framebuffer is absent. Run it
in both DOSBox and DOSBox-X using the
[recommended memory settings](users.md#running-the-game). The bundled game and
custom car resources are copied to an isolated directory. The test links
all normal game objects; its small test entry makes its memory budget slightly
stricter than the game.
