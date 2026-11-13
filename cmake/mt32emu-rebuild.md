# Rebuilding the bundled MT-32 emulator

This directory contains the complete corresponding libmt32emu 2.8.6 source from
Munt commit `91d2205eb8f2591d177b257b92e2c75f830508de`, its LGPL-2.1-or-later
license, a Restunts build wrapper, and the original project/toolchain scripts
in `build-context/`. Upstream sources are unmodified. No Roland
ROM images or original game resources are included.

Use a C/C++ compiler for the package's CPU and operating system, compatible with
the options in `mt32emu-build-info.txt`. Modify the source while preserving its
public C API and the C++ ROM-metadata interface used by the game. Use a
compatible C++ compiler ABI as well as the original target architecture. Then
configure the wrapper:

```sh
cmake -S rebuild -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Replace `lib/libmt32emu.so` on Linux/BSD/Haiku, `lib/libmt32emu.dylib` on macOS,
or `bin/mt32emu.dll` on Windows with the rebuilt library. Copy the file from
`build/mt32emu-core/`; match the original architecture, including universal
macOS slices where applicable. Supply the original toolchain and compatibility
flags when targeting an older runtime. The game does not verify library
signatures or prevent modified compatible replacements.

Android loads `libmt32emu.so` from its APK. Rebuild the library with the matching
Android NDK/ABI/API and repack/re-sign an APK for your own use, or rebuild the app
from source with the modified library. Android requires matching signatures for
updates; a differently signed package may require uninstalling first. Keep user
data backed up before replacing an installed app.

For a browser package, the adjoining `../wasm-relink/` kit rebuilds this source
statically with Emscripten and links it to the supplied application objects.
Follow that kit's `wasm-relink.md`; it retains the package's thread setting and
SDK requirements. Public API changes require rebuilding the application too.

Keep this complete source directory, notices, licenses and browser relink kit
with redistributed packages. The emulator's license does not grant rights to
Roland ROMs or game assets. Players provide their own compatible control and PCM
ROM dumps separately.
