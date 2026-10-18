# Browser / WebAssembly build

The SDL3 platform also builds with Emscripten. The output is one offline
`restunts.html`: JavaScript, WebAssembly, enhanced artwork, and any supplied
optional Ogg Vorbis music replacements are embedded. **No original game data is
included**, including cars, tracks, replays, sounds, fonts, or menus. The build does not require a `stunts/`
directory. The redistributable package and relink kit also exclude that data.

## Build

Install the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html),
CMake 3.25+, and Ninja. Emscripten 6.0.10 is the tested SDK version. Activate its
environment, then run from the repository root:

```sh
. /path/to/emsdk/emsdk_env.sh
emcmake cmake -S . -B out/sdl3-wasm -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/sdl3-wasm
```

Use `emsdk_env.bat` in a Windows command prompt. This build uses the same pinned
SDL3 source as native builds. Native dump tools and native regressions are not
built for the browser. No pthreads, shared memory, server headers, CDN, or network
requests are needed at runtime. Emscripten's single-file output and embedded
artwork avoid requests for neighboring `.wasm`, `.js`, or `.data` files.

To embed replacement music, add any of `titl.ogg`, `slct.ogg`, `over.ogg`, and
`vict.ogg` to `assets/music/` before configuring CMake. These replace title,
selection, race-end, and victory music respectively. Each absent or unusable
replacement falls back to its original song. The decoder accepts mono/stereo
Ogg Vorbis at 8,000–192,000 Hz and is bundled with the game. Reconfigure and
rebuild after adding, changing, or removing recordings; the resulting HTML and
relink kit contain the configured files. No recordings are included by default.
See [music asset instructions](../assets/music/README.md).

## Play from a website or offline

You can open a hosted `restunts.html` directly in your browser; saving a copy
is optional. For offline play, save the HTML file anywhere on your device and
open it in your browser. `file://` is supported, so a local web server is not
needed. The HTML's location does not determine where the game looks for data.

1. Have your own Broderbund Stunts 1.1 game files in a folder on your device.
2. Click **Choose game folder** and select the folder containing those files.
   Supported browsers remember it for this page and reopen it when permission
   is still granted. Otherwise, click **Allow folder access** to approve access
   again. Use **Change game folder** to select a different folder.
3. Choose the startup options above the game. **Skip intro** is optional.
   **Sound enabled** starts on; uncheck it to disable music and sound effects
   (`/ns`). The New MIDI choice is remembered independently of this setting.
   **New MIDI** selects Ogg replacement music (`--ogg:on`); uncheck it for
   original AdLib music (`--ogg:off`). It is enabled by default.
   **Graphics settings** contains **Show FPS** (`--fps:on`),
   **Show Control Layout** (`--control-layout:on`), and the **HyperVision**
   selector, in that order. HyperVision offers **Off**, **Auto**, **Full**,
   **High**, **Medium**, and **Low**, matching Android. Auto is the browser
   default (`--hv:auto`); fixed presets pass the matching `--hv:` option, while
   Off starts without HyperVision. Show FPS and Show Control Layout start off.
   Enable Show Control Layout for the same touch controls as Android. On
   phones, use fullscreen landscape mode to put them in the black side areas.
   The separate **Driving rules** group below graphics contains **Power Gear**,
   **Original Collision Bugs**, **Left Corner Bias**, **OWOOT**, and **Amiga Grass**.
   Each group lays out its controls horizontally and
   wraps to more lines as needed, without horizontal scrolling. The first three rules
   preserve the original driving behavior by default. OWOOT and Amiga Grass
   start off; Amiga Grass uses the original Amiga grass grip for stock cars
   (`/amigagrass`). Custom-car surface adhesion is activated by the car resource
   capability flag. All settings are remembered for this page, including
   with read-only folder access. Earlier HyperVision checkbox choices are retained
   as Auto when enabled and Off when disabled.
4. Click **Start game**. This user gesture enables browser audio. Click the
   canvas to focus the game keyboard.

**Your game files and saves stay on your device and are never uploaded**, even
when you play from a website. Selecting a folder gives the game local access to
its files; it does not send them to the hosting server.

Immediate game files and custom opponent animations in
`opponents/animations/opp1win.webm` through `opp6win.webm`, or `opp1lose.webm`
through `opp6lose.webm`, are loaded. Animation names describe the opponent's
result. Use WebM with VP8 video; animations play silently while HyperVision is
on, keeping the game's music. Missing or unusable clips retain the original
animation. Other subfolders, executables, and the HTML are ignored.
Filenames are resolved case-insensitively. Identical files differing
only in case are accepted; conflicting contents report an error. The selected
folder must contain the original game resources, including `MAIN.RES` (or
`MAIN.PRE`), `FONTDEF.FNT`, and `FONTN.FNT`. Other missing resources are reported by
the game. Files are read locally and are never uploaded.

On **Start game**, the game checks the version text in `MISC.RES` (preferred) or
`MISC.PRE`. It requires Broderbund Stunts 1.1 (12 Feb. 1991), while allowing
modifications to other resources. A missing or incompatible version displays an
error on the page; reload and choose the correct game folder before starting
again.

## Saving and browser support

Chrome and Edge support a folder picker with **read/write** permission. After
that permission is granted, game saves are written directly into the selected
folder, preserving existing filename casing. The game waits for each write to
finish before treating the file as closed. Disk failures are reported to the
game and on the page. Deletions are also propagated. Closing or reloading a tab
while a save is in progress can interrupt it; wait until the page reports the
save is complete.

The selected directory handle and startup settings are stored in IndexedDB
for this HTML's location; original game data is not stored in that database. On
reopening, the game checks read/write permission and asks for approval through
**Allow folder access** only when needed. Clearing browser storage, moving the
HTML, or browser restrictions can require selecting the folder and startup
choices again. If storage is unavailable, folder selection, startup options, and
saving still work for the current session.

Firefox, Safari, and other browsers without that API use a read-only directory
picker and require selecting the folder each time. The page shows **Export saves** and **Import saves, tracks, or replays**
only on this fallback path (or after a direct-write failure). Export downloads a
JSON bundle of changed/new tracks, replays, high scores, and configuration files;
it does not copy unchanged original resources. Select the game folder and import
that bundle before starting the next session. Imported files are available to
the game in memory; this fallback does not overwrite the selected disk folder.
Deleting files in fallback mode affects the current session only. Landscape
cache files are regenerated rather than exported.

**Ctrl+F12** captures the last completed game frame as a PNG. With read/write
folder access, screenshots are saved directly to the selected game folder as
`SHOT0001.PNG` through `SHOT9999.PNG`, skipping existing names. With read-only
folder access, or if the folder write fails, the PNG is downloaded instead.
Screenshots are separate from the JSON save export.

Browser support for directory writes is described in
[Chrome's File System Access documentation](https://developer.chrome.com/docs/capabilities/web-apis/file-system-access)
and [MDN's directory picker reference](https://developer.mozilla.org/en-US/docs/Web/API/Window/showDirectoryPicker).
Both paths work offline. Browser settings and policies may restrict folder
access. Audio needs a user gesture; browser shortcuts can take precedence over
some function keys. Use the page's **Fullscreen** button or **Alt+Enter** for
fullscreen mode. Entering fullscreen requests landscape orientation even when
the phone is held upright, and keeps it in landscape as the device rotates.
The orientation lock is released when fullscreen ends. This requires browser
support for the Screen Orientation lock API; if it is unavailable or refused,
fullscreen still works with the browser's normal orientation behavior.
With the canvas focused, **F12** toggles HyperVision Auto and **Shift+F12** cycles
fixed **Full → High → Medium → Low** presets. **F11** or **Shift+F11** toggles
the FPS and render-time display on or off. Use Shift+F11 to avoid the browser
fullscreen shortcut assigned to F11. **Shift+F10** switches between New MIDI
and original AdLib music during the intro, menus, and race-end/victory screens.

The browser presents frames through SDL3's WebGL renderer and uses WebAudio
output. Scene rendering runs on the CPU and is serial;
`RESTUNTS_RENDER_WORKERS` does not enable threads. Asyncify preserves the existing
nested game/menu loops. Browser waits share one scheduler: explicit waits also
satisfy the bounded input/presentation yield interval, and SDL's additional
implicit sleeps are disabled. Each wait resumes through a `MessageChannel` task
so repeated short waits do not accumulate the browser's nested-timer minimum.
The existing game clock still targets 60 visual FPS in HyperVision; physics and
replay formats use the same game code as native builds. This does not require
an Emscripten main-loop callback or change the native VSync setting.
Background tabs may still be throttled by the browser.

## Distribution and rebuilding the audio library

```sh
cmake --install out/sdl3-wasm --prefix out/package-wasm --component Runtime
```

The installed HTML runs by itself, but redistribute the complete package to
include the required dependency notices, licenses, and Nuked OPL2 relinking
materials. Original game files must be supplied separately by each player.
Do not copy them into the package.

The browser links Nuked OPL2 Lite statically. The package includes its exact
source and a standalone relink kit under `share/restunts/wasm-relink/`, containing
the compiled application/SDL objects, enhanced artwork, any embedded optional
music replacements, HTML shell, and build instructions. Recipients can modify
and rebuild Nuked and relink the HTML without the game's source or original game
data. Use the SDK version recorded
in the kit. See its `wasm-relink.md` and `THIRD-PARTY-NOTICES.txt` for details.

## Verification

The pacing regression builds a small WebAssembly fixture with the real Asyncify
wait implementation, then runs it in Playwright without original game data.
It checks that input polling shares explicit waits, repeated short waits resume
through browser message tasks without nesting timers, and waits complete even
when animation callbacks are unavailable. Activate the Emscripten SDK and install
Playwright as described below, then run:

```sh
NODE_PATH="$PWD/out/wasm-test-tools/node_modules" node tools/scripts/test-wasm-pacing.js \
    --sdl-include out/sdl3-wasm/_deps/sdl3-src/include
```

Use `--browser firefox` for Firefox. `--emcc` accepts an explicit compiler path,
and `--sdl-include` names the SDL3 public include directory from a configured build.

The actual WebAssembly file I/O regression checks asynchronous commit ordering
and failure propagation with Node.js (included in the SDK):

```sh
emcc -O2 -Wno-pointer-sign -DRESTUNTS_SDL3 -DRESTUNTS_HEADLESS \
    -sASYNCIFY -sASYNCIFY_STACK_SIZE=65536 -sENVIRONMENT=node -sEXIT_RUNTIME=1 -sASSERTIONS=1 \
    src/restunts/tests/test-wasm-fileio.c src/restunts/platform/sdl3/file.c \
    src/restunts/c/fileio.c -o out/test-wasm-fileio.js
node out/test-wasm-fileio.js
```

The launch-form browser test needs Node.js and Playwright, but no Emscripten
build or game files. It checks startup flags, sound enable/disable, remembered
settings, migration of removed preferences, mobile layout, and fullscreen canvas sizing
with a stub for the native runtime:

```sh
NODE_PATH="$PWD/out/wasm-test-tools/node_modules" node tools/scripts/test-wasm-shell.js
```

Set `CHROMIUM_EXECUTABLE_PATH` to use an existing Chromium installation.
This form test does not exercise the native touch controls or renderer.

The full browser smoke test uses Node.js and Playwright with Chromium installed:

```sh
npm install --prefix out/wasm-test-tools playwright
out/wasm-test-tools/node_modules/.bin/playwright install chromium firefox
NODE_PATH="$PWD/out/wasm-test-tools/node_modules" node tools/scripts/test-wasm.js \
    --html out/sdl3-wasm/restunts.html --data-dir stunts
```

Add `--browser firefox --output out/wasm-browser-test/firefox-results` to run
the same smoke test in Firefox. Chromium and Firefox have both been tested.
Add `--idle-only` to check that the main-menu idle demo starts and keeps playing;
this waits for the normal idle timeout and requires `DEFAULT.RPL` in the supplied folder.

The game data path is a local test input only; it is not embedded or packaged.
The test opens the HTML with `file://`, rejects runtime network dependencies,
checks that original resources are absent until a folder is selected, exercises
keyboard gameplay and fallback save round trips, and checks direct-write
behavior using test directory adapters. Remembered-folder checks exercise real
IndexedDB across reloads with simulated permission approval, denial, and retry.
The real native directory picker and permission dialogs still require a manual
browser check.
