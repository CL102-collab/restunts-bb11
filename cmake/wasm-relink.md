# Relinking the browser game with modified audio libraries

This package supplies the game, SDL and WebM decoder WebAssembly objects,
original linker settings, enhanced artwork, page shell, and the corresponding Nuked OPL2 Lite
source under LGPL-2.1-or-later, plus the complete Munt libmt32emu source in
`../mt32emu/` under the same license. You may modify the application for your own use
and reverse engineer it to debug modifications to the LGPL-covered library.
The game does not check library signatures or prohibit modified replacements.

Copy the complete package to a writable location, keeping the adjoining
`../mt32emu/` source directory. Enter `share/restunts/wasm-relink/` and activate the Emscripten SDK
version recorded in `nuked-build-info.txt`, and install CMake 3.25 or newer. Modify
`nuked-opl2-lite/opl2.c`, preserving the public API and structure layouts in
`opl2.h`, or modify `../mt32emu/src/` while preserving the C API and C++
ROM-metadata interface used by the supplied objects. Then run:

```sh
emcmake cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The relink inherits the original package's `RESTUNTS_WASM_THREADS` setting.
For a threaded package, serve `build/restunts.html` over HTTPS or localhost with
`Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp`. For a serial offline package, open
`build/restunts.html` directly in a browser. The relink embeds enhanced artwork
again and does not need the original game source. Changing the thread setting
requires rebuilding the supplied application and SDL objects from source.
When the original package layout is retained, configuration also copies the
standalone `tracked.html` editor beside the rebuilt game. If you move this relink
directory separately, copy the packaged `tracked.html` beside `build/restunts.html`
to use the game page's editor link.
The same build relinks the independent audio runtime, embeds it with the
AudioWorklet and worker scripts, and then produces the self-contained game page.
Python 3 is required for this embedding step. Specialized audio application
objects and their source inputs are supplied in `audio/`. Threaded packages also
supply separate serial game/SDL/WebM archives for that audio realm; its emulator
libraries are rebuilt in the `audio-serial` sub-build without pthread flags.
Both runtime instances therefore use your modified Nuked OPL2 and Munt sources.

Changes to the public ABI require recompiling the application from its source.
The compiler options and SDK version must remain compatible with the supplied
objects; the original build's toolchain information is included alongside them.

Keep this relink directory, dependency notices and licenses with redistributed
browser packages, including the adjoining `../mt32emu/` source directory.
Original game resources and Roland ROMs are not included. Select your own game folder in the
browser to play. Enhanced artwork retains its own terms; inclusion in this
directory does not grant additional rights to it.
