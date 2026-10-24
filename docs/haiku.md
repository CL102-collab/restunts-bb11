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
selects `wxqt_devel`, `devel:libQt5Widgets` and `qthaikuplugins` on x64, or their
modern x86 equivalents on the hybrid system. CMake defaults to
`RESTUNTS_SYSTEM_WX=ON` on Haiku to retain those port fixes. Keep wxQt, Qt and
the platform/style plugins installed when running the package. The launcher
still needs native Haiku build and GUI validation.

Use `haiku-x86` instead on 32-bit Haiku. Before a manual CMake build there,
select the modern compiler environment with `export PATH="$(setarch -p x86)"`.
Use a separate output directory for each architecture. For x86, replace
`haiku-x64` with `haiku-x86` in the following commands:

```sh
cmake -S . -B out/sdl3-haiku-x64 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/sdl3-haiku-x64 --parallel 2
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    ctest --test-dir out/sdl3-haiku-x64 --output-on-failure
cmake --install out/sdl3-haiku-x64 --prefix "$PWD/out/package-haiku-x64" \
    --component Runtime
out/package-haiku-x64/bin/restunts-launcher
# Direct command-line launch remains available:
out/package-haiku-x64/bin/restunts --data-dir "$PWD/stunts" --nointro
```

Tests have a per-test timeout of 600 seconds in the package helper;
`RESTUNTS_TEST_TIMEOUT_SECONDS` overrides it for slower emulated machines.
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
        ctest --test-dir out/sdl3-haiku-x64 --output-on-failure --timeout 600
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

The shared package workflow builds both Haiku targets, runs native CTest
regressions and the Nuked library replacement test, verifies relocated startup,
and validates the resulting archives. Regression game data is checksum-verified
and used only in the test checkout; it is excluded from all runtime archives.

Both jobs use the repository's VM helper with checksum-pinned images: the x64
builder image and the official x86 install media. After dependency installation,
they restart the guest to activate any system update before building and packaging.
Hosted CI enables and requires KVM to meet its build and test time limits. Local
validation can use the software-emulation options described above. Full golden replay
corpus validation remains in the existing Linux SDL3 and DOS jobs.

GitHub Pages links to these build instructions until a release includes the
new archives. See [release verification](releases.md) and
[GitHub Pages publishing](github-pages.md) for those workflows.
