# Haiku builds and VM testing

The native `haiku-x64` and `haiku-x86` targets use the SDL3 platform backend
on Haiku R1/beta6. The 32-bit target uses modern GCC's x86 ABI on the hybrid
x86_gcc2 system, rather than the legacy GCC 2 compiler. Both release targets
require SSE2. They include the game,
physics and renderer dump tools, enhanced artwork, and replaceable Nuked OPL2
library. Supply your own original Broderbund Stunts 1.1 game files.

## Native build

Install dependencies on the matching Haiku system:

```sh
sh tools/scripts/install-haiku-package-dependencies.sh haiku-x64
```

If dependency installation requests a reboot, restart Haiku before configuring.
A system update can leave the newly installed tools inactive until the next boot.

The [desktop startup window](launcher.md) uses HaikuPorts' patched wxQt 3.2.4
package with Qt 5 and the native Haiku platform/style plugins. The installer
selects `wxqt_devel`, `qt5_devel`, `libsdl_devel` and `qthaikuplugins` on x64,
or their modern x86 equivalents on the hybrid system. SDL 1.2 development files
are required by wxQt's link flags; the game itself uses the pinned SDL3.
CMake defaults to `RESTUNTS_SYSTEM_WX=ON` on Haiku to retain those port fixes. Keep wxQt, Qt and
the platform/style plugins installed when running the package. Browse opens
Haiku's native folder picker. Verify startup, folder selection, saved preferences
and sound on the target architecture before distributing a package.

Use `haiku-x86` instead on 32-bit Haiku. Before a manual CMake build there,
select the modern compiler environment with `export PATH="$(setarch -p x86)"`.
Use a separate output directory for each architecture. For x86, replace
`haiku-x64` with `haiku-x86` in the following commands:

```sh
cmake -S . -B out/sdl3-haiku-x64 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/sdl3-haiku-x64 --parallel 2
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    ctest --test-dir out/sdl3-haiku-x64 --parallel 2 --output-on-failure
cmake --install out/sdl3-haiku-x64 --prefix "$PWD/out/package-haiku-x64" \
    --component Runtime
out/package-haiku-x64/bin/restunts-launcher
# Direct command-line launch remains available:
out/package-haiku-x64/bin/restunts --data-dir "$PWD/stunts" --nointro
```

The package and VM test helpers run independent CTest cases in parallel using
`RESTUNTS_BUILD_JOBS`, which defaults to `2`. Set it to the number of CPUs available
to the guest. CTest keeps the existing timing-sensitive UNIT cases serial. The
`host-regressions` case also runs alone because it already compiles its fixture
variants in parallel; its fixture execution remains serial.

The package helper supplies a 600-second default CTest timeout;
`RESTUNTS_TEST_TIMEOUT_SECONDS` overrides it for slower emulated machines.
CI also sets `RESTUNTS_GENGHOST_TEST_TIMEOUT_MULTIPLIER=4` for fixed ghost and
rendering workloads. The helper passes that multiplier to CMake so CTest and
ghost subprocess limits agree; local builds default to `1`.
For software emulation, use `ctest --timeout 7200` or set
`RESTUNTS_TEST_TIMEOUT_SECONDS=7200` when running the package helper. The host
regression test compiles many fixtures and can exceed the default timeout.
Set `RESTUNTS_RENDER_REPLAY_TIMEOUT_SECONDS=1800` as well for the replay
renderer's per-process timeout under slow software emulation.

The tests require the original game assets in `stunts/`. The game uses Haiku's
native graphics and audio drivers; dummy drivers are only for automated tests.
Keep the installed `bin/`, `lib/`, and `share/` directories together when moving
the package. `--data-dir` selects the writable game and save directory.

## Release archives

From a clean committed checkout on the matching Haiku architecture:

```sh
export GITHUB_SHA="$(git rev-parse HEAD)"
bash tools/scripts/build-package.sh haiku-x64
```

Use `haiku-x86` for the 32-bit package. The helper selects the modern compiler
on hybrid Haiku, builds and runs the native tests, checks replacement of the
packaged Nuked shared library, and checks startup after moving the runtime.
Archives and SHA-256 sidecars are written to `dist/packages/`. The release
validator checks ELF architecture and executable permissions as well as all
required files, licenses, sources, artwork, and file hashes. Original game
resources are excluded from the archives.

## Cross builds and target testing

CI compiles the game, tools, and CMake test executables on Ubuntu using a Haiku
cross compiler and a sysroot imported from the matching Haiku R1/beta6 guest.
The SDK helper requires x86-64 Linux with GCC/G++, 32-bit C++ development libraries,
GNU Make, Bison, Flex, gawk, Texinfo, Python 3, curl, and the usual archive utilities.
The project build also needs CMake 3.25 or later, Ninja, and pkg-config. Choose an
SDK path without whitespace.

After installing and activating dependencies, export that guest's sysroot:

```sh
sh tools/scripts/export-haiku-cross-sysroot.sh haiku-x64 /boot/home/haiku-sysroot.tar.gz
```

Copy the archive to Linux, then prepare the SDK and build:

```sh
export RESTUNTS_HAIKU_SDK_ROOT="$PWD/out/haiku-sdk-x64"
sh tools/scripts/prepare-haiku-cross.sh haiku-x64 "$RESTUNTS_HAIKU_SDK_ROOT" \
    /path/to/haiku-sysroot.tar.gz
export GITHUB_SHA="$(git rev-parse HEAD)"
bash tools/scripts/build-package.sh haiku-x64
```

The SDK helper verifies the pinned Haiku build-tools sources and retains the
compiler under the SDK root. Import a fresh sysroot after changing the guest's
packages. Both cross compilers use the modern ABI; the hybrid x86 target does not
use GCC 2.

The cross configuration enables `RESTUNTS_TESTS_ON_TARGET` so the target tests
remain available for execution inside Haiku. CI keeps the source checkout at
`/boot/home/restunts` on both Linux and Haiku, copies the configured test build tree
and staged runtime into the guest without compiler objects, then runs:

```sh
bash tools/scripts/test-haiku-build.sh haiku-x64 --cross-built
```

Use `haiku-x86` for the hybrid 32-bit guest. This runs the Haiku executables with
the guest's Python, CMake, and compiler tools. Node-based tests remain optional;
configure with `-DRESTUNTS_TARGET_TEST_NODE=node` only if Node is installed in the
guest. The native compiler is still required: `host-regressions` builds source
fixtures with Haiku's compiler, and the Nuked library replacement test builds a
replacement shared library. Native builds also use the same test helper.

## QEMU validation

On a Linux host, install Python 3, QEMU system emulation and image utilities,
OpenSSH clients, curl, and zstd. For Debian or Ubuntu:

```sh
sudo apt-get install python3 qemu-system-x86 qemu-utils openssh-client curl zstd
```

The helper uses KVM when accessible, otherwise software emulation. Each VM
uses two virtual CPUs and 2 GiB of RAM by default. Images are downloaded into
the selected directory, verified against pinned SHA-256 hashes, and booted
through a writable overlay. SSH and optional VNC listen only on loopback.
For hosts where nested hardware virtualization is unstable, pass
`--accel tcg --cpus 1 --boot-timeout 600 --ssh-timeout 60` to `start` and
`prepare-x86` to use software emulation. It is slower; run one guest at a time
on constrained hosts.
The downloaded image's SSH key is public test infrastructure; use these VMs
only for disposable local build work. Use a persistent directory instead of
`/tmp` to retain downloaded images across host restarts.

Start the x64 VM with an optional VNC display on localhost port 5900:

```sh
python3 tools/scripts/haiku-vm.py --directory /tmp/restunts-haiku64 \
    start --port 2223 --vnc-display 0
```

Upload a source checkout or dedicated test copy. Use a copy without existing
`out/`, `build/`, or `dist/` directories to avoid transferring generated files.
Include your original game resources in its `stunts/` directory for tests:

```sh
python3 tools/scripts/haiku-vm.py --directory /tmp/restunts-haiku64 \
    upload /path/to/test-checkout /boot/home/restunts
python3 tools/scripts/haiku-vm.py --directory /tmp/restunts-haiku64 ssh bash -c '
    set -eu
    cd /boot/home/restunts
    sh tools/scripts/install-haiku-package-dependencies.sh haiku-x64
'
# Activate dependencies even when pkgman also upgraded the Haiku system.
python3 tools/scripts/haiku-vm.py --directory /tmp/restunts-haiku64 stop
python3 tools/scripts/haiku-vm.py --directory /tmp/restunts-haiku64 \
    start --port 2223 --vnc-display 0
python3 tools/scripts/haiku-vm.py --directory /tmp/restunts-haiku64 ssh bash -c '
    set -eu
    cd /boot/home/restunts
    cmake -S . -B out/sdl3-haiku-x64 -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build out/sdl3-haiku-x64 --parallel 2
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        ctest --test-dir out/sdl3-haiku-x64 --parallel 2 --output-on-failure --timeout 600
    python3 tools/scripts/test-nuked-package.py --build-directory out/sdl3-haiku-x64
    cmake --install out/sdl3-haiku-x64 --prefix "$PWD/out/package-haiku-x64" \
        --component Runtime
'
```

For x86, the helper constructs an 8 GiB BFS disk from the checksum-verified
official hybrid x86 installation image. It uses the x64 guest's native BFS
tools to copy files and boot metadata, enable SSH, and skip the initial locale
prompt. This avoids an interactive installation in CI. Preparation starts a
temporary x64 VM with the source and target as separate virtual disks, then
shuts it down:

```sh
python3 tools/scripts/haiku-vm.py --directory /tmp/restunts-haiku32 \
    prepare-x86 --port 2225
python3 tools/scripts/haiku-vm.py --directory /tmp/restunts-haiku32 \
    start --arch x86 --port 2224 --vnc-display 1
```

The separate bootstrap SSH port avoids interfering with the running x64 VM.
Disk preparation can take several minutes. The prepared base image is retained
for reuse. Upload and build as above, replacing
both `haiku64` with `haiku32` and `haiku-x64` with `haiku-x86`. For the restart,
use `start --arch x86 --port 2224 --vnc-display 1`. After restarting, add
`export PATH="$(setarch -p x86)"` before running CMake or Python. Repeat any
software-emulation and timeout options whenever restarting either architecture.

For graphical testing, launch the installed game with `--data-dir` and inspect
it through VNC. Automated tests use dummy drivers and cannot establish audio
output quality or hardware controller support. Stop the guests when finished:

```sh
python3 tools/scripts/haiku-vm.py --directory /tmp/restunts-haiku64 stop
python3 tools/scripts/haiku-vm.py --directory /tmp/restunts-haiku32 stop
```

## CI and downloads

The shared package workflow cross-builds both Haiku targets on Linux, then runs
native CTest regressions and the Nuked library replacement test inside each matching
guest. It verifies relocated startup and validates the resulting archives.
Regression game data is checksum-verified and used only in the test checkout;
it is excluded from all runtime archives.

Both jobs use the repository's VM helper with checksum-pinned images: the x64
builder image and the official x86 install media. After dependency installation,
they restart the guest to activate any system update before testing. Hosted CI
requires KVM and assigns all available runner CPUs to the VM and the build/test
helpers. The Linux cross build moves compilation out of the guest; parallel CTest
execution reduces the remaining native test time while retaining serial exclusions.
Local validation can use the software-emulation options described above. Full golden
replay corpus validation remains in the existing Linux SDL3 and DOS jobs.

GitHub Pages links to these build instructions until a release includes the
new archives. See [release verification](releases.md) and
[GitHub Pages publishing](github-pages.md) for those workflows.
