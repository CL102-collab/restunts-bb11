# Relinking the browser game with a modified OPL library

This package supplies the game, SDL and WebM decoder WebAssembly objects,
original linker settings, enhanced artwork, page shell, and the corresponding Nuked OPL2 Lite
source under LGPL-2.1-or-later. You may modify the application for your own use
and reverse engineer it to debug modifications to the LGPL-covered library.
The game does not check library signatures or prohibit modified replacements.

Copy this entire directory to a writable location. Activate the Emscripten SDK
version recorded in `nuked-build-info.txt`, and install CMake 3.25 or newer. Modify
`nuked-opl2-lite/opl2.c`, preserving the public API and structure layouts in
`opl2.h`, then run:

```sh
emcmake cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Open `build/restunts.html` directly in a browser. The relink step embeds all
enhanced artwork again and needs neither a web server nor the original game source.
When the original package layout is retained, configuration also copies the
standalone `tracked.html` editor beside the rebuilt game. If you move this relink
directory separately, copy the packaged `tracked.html` beside `build/restunts.html`
to use the game page's editor link.
Changes to the public ABI require recompiling the application from its source.
The compiler options and SDK version must remain compatible with the supplied
objects; the original build's toolchain information is included alongside them.

Keep this relink directory, dependency notices and licenses with redistributed
browser packages. Original game resources are not included. Select your own game folder in the
browser to play. Enhanced artwork retains its own terms; inclusion in this
directory does not grant additional rights to it.
