# Release packages and verification

The **PR validation** and manual **Release** workflows call the same
`build-packages.yml` workflow. Every successful run produces the following
archives and a SHA-256 sidecar for each archive:

| Archive | Target baseline |
| --- | --- |
| `restunts-android-armv7.zip` | 32-bit ARM (`armeabi-v7a`), Android 5.0+; Galaxy S5 SM-G900F |
| `restunts-android-arm64.zip` | 64-bit ARM (`arm64-v8a`), Android 5.0+; phones and Android TV |
| `restunts-amiga-m68k.tar.gz` | Motorola 68000, Amiga 500+, Kickstart 2.0+; no FPU or `ixemul.library` |
| `restunts-dos16.zip` | 16-bit DOS, Open Watcom |
| `restunts-dos32.zip` | 32-bit DOS, DJGPP/SDL3, bundled CWSDPMI |
| `restunts-linux-arm32.tar.gz` | ARMv7 hard-float, Debian/Raspbian 12+; Pi 2 and newer |
| `restunts-linux-arm64.tar.gz` | AArch64, Debian/Raspbian 12+ |
| `restunts-linux-riscv64.tar.gz` | RISC-V64 (`rv64gc`, `lp64d`), Debian 13+ |
| `restunts-linux-x86.tar.gz` | 32-bit x86 with SSE2, Debian 12+ |
| `restunts-linux-x86-no-sse2.tar.gz` | 32-bit x86 without SSE2, Debian 12+ |
| `restunts-linux-x64.tar.gz` | x86_64, Debian 12+ |
| `restunts-linux-appimage-x64.tar.gz` | x86_64 AppImage, Ubuntu 10.04-compatible sysroot, glibc 2.11+ |
| `restunts-linux-appimage-x86.tar.gz` | 32-bit x86 AppImage with SSE2, Ubuntu 10.04-compatible sysroot, glibc 2.11+ |
| `restunts-linux-appimage-x86-no-sse2.tar.gz` | 32-bit x86 AppImage without SSE2, Ubuntu 10.04-compatible sysroot, glibc 2.11+ |
| `restunts-linux-appimage-armv7.tar.gz` | ARMv7 hard-float AppImage, Raspbian Wheezy, glibc 2.13+; Pi 2 and newer |
| `restunts-linux-appimage-arm64.tar.gz` | AArch64 AppImage, Debian Jessie ABI, glibc 2.19+; requires a 64-bit OS |
| `restunts-linux-appimage-riscv64.tar.gz` | RISC-V64 AppImage, Debian 13 Trixie ABI, glibc 2.41+ |
| `restunts-windows-arm64.zip` | ARM64, Windows 10+ |
| `restunts-windows-x86.zip` | 32-bit x86 with SSE2, Windows XP SP2+ |
| `restunts-windows-x86-no-sse2.zip` | 32-bit x86 without SSE2, Windows XP SP2+ |
| `restunts-windows-x64.zip` | x86_64, Windows 7+ |
| `restunts-freebsd-x64.tar.gz` | x86_64, FreeBSD 14.4 |
| `restunts-freebsd-x86.tar.gz` | 32-bit x86 with SSE2, FreeBSD 14.5+ |
| `restunts-freebsd-armv7.tar.gz` | ARMv7 hard-float, FreeBSD 14.5+ |
| `restunts-freebsd-arm64.tar.gz` | AArch64, FreeBSD 14.4 |
| `restunts-freebsd-riscv64.tar.gz` | RISC-V64, FreeBSD 15.0; GTK 2 launcher |
| `restunts-openbsd-x64.tar.gz` | x86_64, OpenBSD 7.9 |
| `restunts-openbsd-x86.tar.gz` | 32-bit x86 with SSE2, OpenBSD 8.0+ |
| `restunts-openbsd-arm64.tar.gz` | ARM64, OpenBSD 7.9 |
| `restunts-openbsd-riscv64.tar.gz` | RISC-V64, OpenBSD 7.9 |
| `restunts-netbsd-x64.tar.gz` | x86_64, NetBSD 10.2 |
| `restunts-netbsd-armv7.tar.gz` | ARMv7 hard-float (`earmv7hf`), NetBSD 10.2+ |
| `restunts-netbsd-arm64.tar.gz` | AArch64, NetBSD 10.2 |
| `restunts-netbsd-riscv64.tar.gz` | RISC-V64, NetBSD 11.0 |
| `restunts-netbsd-x86.tar.gz` | 32-bit x86 with SSE2, NetBSD 10.2 |
| `restunts-netbsd-x86-no-sse2.tar.gz` | 32-bit x86 without SSE2, NetBSD 10.2 |
| `restunts-haiku-x64.tar.gz` | x86_64, Haiku R1/beta6 |
| `restunts-haiku-x86.tar.gz` | 32-bit x86 with SSE2, modern GCC ABI, Haiku R1/beta6 |
| `restunts-macos-universal.tar.gz` | Intel and Apple Silicon, macOS 11.0+ |
| `restunts-browser.zip` | Offline HTML/WebAssembly |
| `restunts-templeos.zip` | Native HolyC port and RedSea installation CD for stock TempleOS 5.03 |

Each Android target builds both the game and Genghost as separate installable
APKs for its ABI. CI verifies both apps' signatures, SDK levels, native files and
assets, and tests folder/ZIP import. PR packages use debug signing keys; trusted
Release dispatches sign both apps with the configured persistent release key.
Updates require the same signing key for each app. A different key may require
uninstalling the old app and removing its private data. See
[Android builds and controls](android.md) and [Genghost for Android](genghost-android.md)
for setup, signing and device testing.

These are build baselines, not a claim that CI exercises every OS version and
CPU. The full golden replay suites run on native Linux SDL3 and/or 16-bit DOS,
as selected by the `platforms` input. That selection never reduces the package
matrix. BSD packages compile in x64 virtual machines running the matching
OS release. ARMv7, ARM64, RISC-V64, FreeBSD x86, and OpenBSD x86 cross-compile
against checksum-verified target SDKs and target dependency packages. CI
checks the finished archives and executable architecture; ARM64 and RISC-V64
target VMs also check startup after relocation without game data. Native
x64 jobs compile directly; NetBSD x86 uses an i386 userspace inside the
amd64 VM. These checks do not verify physical-board graphics or audio support.
Haiku x64 and x86 build in Haiku R1/beta6 VMs and run native regressions,
including relocated-package and Nuked replacement tests. ARM32 targets ARMv7;
it does not cover the original Pi 1 or Pi Zero. The no-SSE2 option controls the game, bundled SDL,
and Nuked library; system libraries retain their own CPU requirements.

The FreeBSD 15.0 RISC-V64 SDK and startup VM use signed dependencies from the
[anyvm-org package repository](https://github.com/anyvm-org/freebsd-pkg-repo).
That target uses a GTK 2 launcher because the repository does not provide GTK 3;
other ordinary Linux/BSD packages use GTK 3. AppImages bundle GTK 2.

## Package contents

Supply your own original Broderbund Stunts 1.1 game files. None of those files
are distributed in these archives. Enhanced opponent portraits and skyboxes
are included for the SDL3 game; the browser embeds them in `restunts.html`.
Packages retain their directory layout so the game can locate those assets.

Every release includes the Tesla Roadster Rocket (`TERR`) and McMurtry Speirling
PURE (`MCMU`). Only their eight `.RES`, `.3SH` and `.VSH` resources are shipped:
SDL3 native packages install them in `bin/cars/`, DOS16 and Amiga keep `cars/`
beside the executable, Android bundles them in both APKs, and the browser embeds
`/assets/cars/` and retains the files in its relinking kit. TempleOS includes them
in `PORT/CARS` on the installation CD. The original game files remain required.
Car authoring metadata, readmes and editable models are not packaged.

Car discovery and loading search loose files in the selected game-data folder
and beside the executable before their `cars/` subfolders and the bundled
resource folder. Any compatible car placed in these folders is discovered
automatically; duplicate car IDs appear once, with loose files taking priority.

Desktop, DOS and Amiga packages also include a `tracks/` folder beside the
executables. Add your own `.TRK` files there; SDL3 builds also support `.CSTRK`
tracks. The game checks loose game-data files, loose executable files, game-data
`tracks/`, then executable `tracks/`, and lists duplicate filenames only once
without case sensitivity. Optional `.HIG`/`.CSHIG` tables load from these folders
too; new tracks and updated scores are saved in the selected game-data folder.

SDL3 desktop packages contain the game, its startup window, the command-line ghost
generator and its GUI, physics dumper, renderer dumper, statically linked SDL,
its license, and replaceable Nuked OPL2 and Munt libmt32emu shared libraries.
Their exact source, licenses and rebuild instructions are under
`share/restunts/nuked-opl2-lite/` and `share/restunts/mt32emu/`. Keep the complete
package together. Roland control/PCM ROM images are proprietary user data and
are excluded from every package, including browser HTML and relink kits.

The Linux AppImage archive also contains `bin/restunts.AppImage`, embedding the
complete runtime, enhancement artwork, notices, licenses and both audio-library rebuild kits.
The ordinary binaries and source kit remain available outside the AppImage.
CI also uploads each standalone image as an `appimage-<target>` artifact for
direct download; restore its executable bit with `chmod +x restunts.AppImage`
after downloading. Release archives retain the complete package and its manifest.
With no arguments, the AppImage opens the game startup window. To open the ghost
generator window, put `--genghost` first. Remaining arguments go to that GUI.
Game arguments without this selector continue to start the game directly:

```sh
bin/restunts.AppImage --genghost
bin/restunts.AppImage --genghost --data-dir "/path/to/Stunts"
bin/restunts.AppImage --data-dir /path/to/Stunts
```

The `--genghost` selector requires a newly built AppImage; existing released
images are unchanged. The mounted runtime and desktop module caches remain
available until the GUI and its search process finish.

AppImages use FUSE to mount their filesystem. Docker containers usually lack
FUSE, so extract the image first and run its launcher from any working directory:

```sh
/path/to/bin/restunts.AppImage --appimage-extract
./squashfs-root/AppRun --data-dir /path/to/Stunts
./squashfs-root/AppRun --genghost --data-dir "/path/to/Stunts"
```

Extraction also allows replacing `squashfs-root/usr/lib/libnuked-opl2.so` with a
library rebuilt from the included sources. The three x86 AppImages use a verified
legacy sysroot targeting Ubuntu 10.04's glibc 2.11. A modern compiler runs on the
build host; the host's newer glibc is neither linked nor bundled. Each AppImage
includes its GTK, X11 extension and audio dependencies, their licenses and required
source. The host supplies the X11/XCB core libraries (`libX11.so.6`,
`libX11-xcb.so.1` and `libxcb.so.1`) alongside its OpenGL drivers. Bundling old
copies of these core libraries can prevent modern GLX/EGL drivers from loading
and force SDL to use software presentation. No `LD_PRELOAD` workaround is needed.

The ARMv7 AppImage uses the original Raspbian Wheezy packages (glibc 2.13);
it requires ARMv7 hardware, such as Pi 2 or newer, rather than Pi 1 or the original Pi Zero. The
ARM64 AppImage uses Debian Jessie packages (glibc 2.19). Wheezy had no ARM64
release. Raspberry Pi OS first offered a 64-bit Buster beta in 2020 and a
stable 64-bit Bullseye release in 2022; both exceed this ABI baseline.
The game sources are shared unchanged between these packages.

Both Linux RISC-V64 packages target Debian 13 Trixie, Debian's first official
RISC-V release, with glibc 2.41. The ordinary archive cross-builds in a Debian 13
container with Debian's RISC-V compiler and development packages. The AppImage
uses a pinned Debian 13 RISC-V SDK and a static RISC-V AppImage loader. The CPU
baseline is `rv64gc` with the `lp64d` ABI; vector extensions are not required.
The AppImage bundles desktop dependencies while leaving glibc, its ELF loader
and graphics drivers to the host. This is a Debian 13 ABI baseline, not a claim
that every Linux distribution or RISC-V board has been tested.

ARM and RISC-V64 CI jobs test the exact archive with QEMU user emulation and
the pinned userspace loader/libraries: historical Raspbian/Debian for ARM and
Debian 13 for RISC-V64. The shared `tools/scripts/test-appimage-arm.sh` driver
checks AppImage extraction, ELF architecture/ABI, dependency resolution,
information commands, menu startup, a replay frame checksum and non-silent
ALSA output. It records logs and `result.json` compatibility evidence. These
are userspace tests on the CI host's kernel; target kernels, FUSE mounting
and physical graphics/audio devices require separate testing. RISC-V64 has a
separate `Test linux-appimage-riscv64 on debian13` job that downloads and
verifies the uploaded package before running these checks against a fresh
pinned Debian 13 runtime sysroot. ARM checks run within their package jobs.

PR and Release CI build all six AppImage variants. The three x86 variants test
the exact uploaded archive bytes on Ubuntu 10.04, Ubuntu 26.10, Fedora 20 and Fedora 44. These are the oldest
supported and newest stable Ubuntu/Fedora releases selected for this matrix.
Ubuntu 10.04 tests run as an ordinary user in QEMU with its original kernel
and userspace; current Ubuntu and Fedora tests use isolated Docker userspaces. The 32-bit packages run
with compatible 32-bit system libraries, including on current 64-bit distributions.
The no-SSE2 variant also runs on QEMU's Pentium III CPU model.

The tests check launcher options and shared libraries, keep the game menu
running with `--nointro`, verify a replay frame against its expected hash, and
capture non-silent output from the bundled ALSA backend. The Ubuntu 10.04 VM
also checks ordinary FUSE launching and records the actual X11 menu window.
Container tests use AppImage extraction so they do not require FUSE. Original
game data is downloaded separately for testing and is never included in a package. Compatibility failures block both
PR validation and release publication.

Every SDL3 archive includes the single offline `tracked.html` track editor,
with styles and JavaScript embedded. Native packages, including DOS32 and Android
ZIPs, install it at `share/restunts/editor/tracked.html`; open it in a browser.
The browser package puts it beside the playable `restunts.html` and also includes
notices, licenses, and `share/restunts/wasm-relink/`. The relinking kit contains Nuked's
source and the adjoining Munt source, plus object/archive files needed to rebuild
the HTML with either modified audio library. Only the HTML is needed to play; distribute the complete archive.

Each Android ZIP contains `bin/restunts.apk` for the game and `bin/genghost.apk`
for the ghost generator, both matching the archive's ARMv7 or ARM64 ABI. It also
includes both apps' setup guides, dependency notices/licenses, and the exact
Nuked and Munt sources/build settings. The game APK contains both shared audio
libraries and their notices. Enhancement assets are inside the game APK;
select your original game folder separately in each app.

DOS32 uses hardware Ad Lib/OPL or PC Speaker and includes neither Nuked nor
Munt binaries/sources. PC Speaker mode requires no additional audio library or ROMs.
Original DOS16 driver selection, including physical MT-32, remains available.
DOS32 includes `bin/CWSDPMI.EXE` and the accompanying CWSDPMI redistribution
material. Copy the DOS32 executables, CWSDPMI and enhanced artwork directories
into the writable directory containing your original game files. DOS16 keeps its executables at the archive root; copy them into the original
game directory, including the `cars/` folder, before running.

The Amiga archive contains the native `restunts` executable, its `cars/` folder,
linker map and [Amiga guide](amiga.md). It uses Paula audio and needs no SDL or
Nuked library. Copy the executable and cars together to a writable game directory.

The TempleOS archive contains `RESTUNTS.ISO`, the [installation guide](templeos.md)
as `README.MD`, and `DATA_CD_TOOL/` for importing your own game files. It retains
`BUILD.TXT`, `CONTENTS.TXT` and `SHA256SUMS.TXT` from the validated installation
package. The ISO is a RedSea data CD for an existing stock TempleOS installation.
The release archive adds `README-PACKAGE.txt` and `PACKAGE-MANIFEST.json` to the
same installation payload available as the `RESTUNTS_STOCK_TEMPLEOS.ZIP` CI artifact.

Each package also contains `PACKAGE-MANIFEST.json`, recording the source commit,
build run and hashes of its packaged files. The corresponding `<archive>.sha256`
file hashes the finished archive. Missing required artwork, runtime libraries,
source files or licenses fail packaging; unexpected files also fail validation.

## Creating a release

Keep **Settings > General > Releases > Enable release immutability** enabled.
It applies to newly published releases. GitHub documents the setting in
[Preventing changes to your releases](https://docs.github.com/en/code-security/how-tos/secure-your-supply-chain/establish-provenance-and-integrity/prevent-release-changes).

Run the **Release** workflow on the source branch/commit you want to release,
providing a new tag and optional title. Choose `dos`, `sdl3`, or both for replay
validation. Existing formatting, host and regression checks remain required.

The workflow builds and archives all targets, reusing the validated Amiga
executable and TempleOS installation package. It attests the finished archives
and uploads them as `packages-<target>` Actions artifacts. The publishing job
downloads those artifacts from the same run. It verifies the complete target
set, archive checksums and signed provenance for the expected source commit,
builder workflow and run attempt. It never rebuilds or repacks the binaries.

Publication creates a draft, attaches the verified archives and checksums,
and downloads the attachments again to check their bytes before publishing.
The published release is checked for immutability and the expected source tag.
A missing package, failed test or integrity mismatch prevents successful
publication. Existing release assets are never overwritten.

The optional `draft` input leaves the verified release unpublished. Drafts
remain mutable until publication; publishing a draft later through GitHub's UI
does not repeat the workflow's prepublication checks. Use the normal publishing
path for verification immediately before publication. If a release attempt
fails, start a fresh workflow run: immutable artifact names cannot be overwritten,
and publication requires all attestations to belong to one run attempt.
A failed run may leave a draft for inspection; resolve that draft/tag before
attempting another release with the same name, or choose a new unused tag.

## Verifying a download

Use a current [GitHub CLI](https://cli.github.com/) on a supported machine.
Verification can happen on a modern computer before copying a package to DOS
or an older Windows installation. Replace the example tag and archive below
with your release and platform:

```sh
repo=CommonLoon102/restunts-bb11
tag=v1.2.3
archive=restunts-linux-x64.tar.gz
gh release download "$tag" --repo "$repo" \
    --pattern "$archive" --pattern "$archive.sha256"
sha256sum --check "$archive.sha256"
gh release verify "$tag" --repo "$repo"
gh release verify-asset "$tag" "$archive" --repo "$repo"
gh attestation verify "$archive" --repo "$repo" \
    --signer-workflow "$repo/.github/workflows/build-packages.yml" \
    --deny-self-hosted-runners
```

The checksum checks the download. Release verification checks GitHub's signed
release attestation. Build verification checks the signed association with the
repository and packaging workflow; its output identifies the source commit and
build invocation. For an exact source requirement, add
`--source-digest FULL_COMMIT_SHA --signer-digest FULL_COMMIT_SHA` to the last
command, using the commit associated with the release tag.

See GitHub's documentation for [immutable releases](https://docs.github.com/en/code-security/concepts/supply-chain-security/immutable-releases)
and [attestation verification](https://cli.github.com/manual/gh_attestation_verify).
These mechanisms establish origin and detect substitution; they do not prove
that reviewed source, dependencies or the build environment are malware-free.
