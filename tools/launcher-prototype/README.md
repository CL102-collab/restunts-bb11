# wxWidgets launcher feasibility prototype

The production launcher is now in `src/restunts/launcher`; see
`docs/launcher.md`. This directory retains the standalone feasibility experiment.

This standalone experiment targets Windows XP SP2+ on 32-bit x86 without
MMX/SSE/SSE2/AVX. It is not part of the normal game build or release packages.
The game and its existing command-line options are unchanged.

The dialog exercises native controls, a native folder chooser, per-user
configuration, and starting a separate executable. The default child writes
its actual Unicode arguments and working directory as UTF-8 to
`launcher-probe-result.txt` beside the child executable. That directory must
therefore be writable when testing the probe.

Settings are saved on Start to
`%APPDATA%\Restunts\launcher-prototype.ini`, using an explicit local
`wxFileConfig` file. No machine-wide configuration is used. Cancel does not
save. This deliberately uses a separate file from the future full launcher.

## Feasibility result (2026-10-24)

The prototype built with Debian 12 GCC/G++ 12 and wxWidgets 3.2.12 ran on
Windows XP Professional SP3 (5.1.2600), using QEMU TCG's Pentium III CPU
model with SSE2 disabled. Native controls and the folder chooser opened;
Start launched the diagnostic child and closed the launcher. Reopening
without arguments restored the selected folder and checked Fullscreen.
The exported child arguments, working directory and per-user INI matched.
Both guest executables matched the SHA-256 hashes in their static audits.

Wine additionally verified Unicode paths, spaces, a trailing separator,
Fullscreen off, invalid-folder reporting and Cancel leaving settings intact.
The actual XP run used ASCII paths. XP SP2, a second Windows user and the
other desktop platforms remain untested. Full game launcher integration is
still deferred. See the packaged VALIDATION.md for evidence and scope.

## Build

Use CMake 3.25+, Ninja, Python 3 and Debian 12's MSVCRT-based MinGW-w64
i686 GCC/G++ 12 toolchain, matching the game's release baseline. Its win32
threading variant avoids a separate winpthreads dependency. GCC 13's win32
C++ runtime imports Vista condition-variable APIs even with XP API macros;
MSVCRT alone does not guarantee XP compatibility. UCRT toolchains are also
unsuitable for the XP experiment. wxWidgets 3.2.12 is fetched
from its official release archive and checked against its SHA-256 digest.
The prototype and wxWidgets are compiled with the same XP API and CPU flags;
prebuilt compiler/runtime objects are audited automatically after linking.
A failed audit fails the build. Keep the executables unstripped so the audit
can distinguish compiler data tables embedded in executable sections.

From the repository root on Linux:

```sh
cmake -S tools/launcher-prototype -B /tmp/restunts-launcher-build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/mingw-x86.cmake"
cmake --build /tmp/restunts-launcher-build \
    --target restunts-launcher-prototype launcher-probe-child --parallel 2
cmake --install /tmp/restunts-launcher-build \
    --prefix "$PWD/out/launcher-prototype-windows-x86-no-sse2-runtime"
```

Use a build directory on a filesystem supporting symbolic links; wxWidgets
creates `wx-config` during configuration. The installed directory is movable.
Only wxWidgets core/base are linked; web, media, networking, OpenGL and
optional image codecs are disabled. The application needs no wxWidgets,
GCC, C++ runtime or winpthreads DLLs beside it.

To launch an existing game instead of the diagnostic child:

```text
restunts-launcher-prototype.exe --game "C:\Games\Restunts\bin\restunts.exe" --data-dir "C:\Games\Stunts"
```

This mode forwards `--data-dir` only. The Fullscreen checkbox is saved, but
the game does not yet have the startup option to apply it. Default probe
mode forwards `--fullscreen:on` or `--fullscreen:off` to verify the selection.
Game-resource/version validation and the remaining launcher controls are
also deferred.

## Validation

```sh
for executable in out/launcher-prototype-windows-x86-no-sse2-runtime/*.exe; do
    python3 tools/launcher-prototype/check_windows.py "$executable" \
        --objdump i686-w64-mingw32-objdump \
        --output "${executable%.exe}-audit.json"
done
```

The static audit checks PE architecture/subsystem version, imported DLLs,
common post-XP API blockers and disassembled SIMD instructions, including
statically linked runtime code. It records the complete imports for review;
its API blocker list is not a complete Windows XP export database.

For each new binary, run both executables on the intended XP service pack
with a CPU that does not expose SSE2. A disposable QEMU overlay using TCG
and `-cpu pentium3` is suitable. Keep the original guest disk unchanged.

Exercise Browse, keyboard navigation, Fullscreen on/off, Start, Cancel and
error reporting. Check the child result and saved settings after restarting.
Use executable and data paths with spaces and non-ASCII characters. Repeat
under another Windows user to check isolation. In game mode verify that the
game stays running after the launcher exits.

Wine with its reported version set to XP is useful for GUI smoke testing;
it does not establish XP API availability or the CPU requirement.
