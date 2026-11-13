# Optional music replacements

The repository includes Ogg Vorbis replacements for all four songs below.
Each recording preserves its original loop length, with no added ending tail
or fade-out. The race-end track retains its original pause before repeating.

| Filename | Original track |
| --- | --- |
| `titl.ogg` | Title / intro music (`TITL`) |
| `slct.ogg` | Selection / main-menu music (`SLCT`) |
| `over.ogg` | Race-end music (`OVER`) |
| `vict.ogg` | Victory music (`VICT`) |

Original synthesized music is selected at startup (`--ogg:off`), using Ad Lib,
PC Speaker, or the separately configured MT-32 backend. The **OGG music replacement** option
(formerly **New MIDI**) is independent of that choice. Use `--ogg:on` to
start with replacements enabled instead. **Shift+F10** enables these
replacements on music screens and switches back to the selected synthesizer
on the next press.
The choice lasts for the current game session; it cannot change during a race.
Switches keep the current song position instead of restarting it. Plain **F10**
retains its track-editor function. Use the original song timing and loop length
when making replacements so both versions stay musically aligned.

Use these lowercase filenames. Files must contain Ogg Vorbis audio with one
(mono) or two (stereo) channels and a sample rate from 8,000 through 192,000 Hz.
Other codecs in an Ogg container, such as Opus, are not supported. Each track
loops from its beginning while that song is active. The existing music toggle,
volume/fades, and game audio suspension apply to replacements; effects keep
their original sound.

Replacement is independent for each song: supply one, several, or all four.
Missing, unreadable, unsupported, or broken files fall back to the corresponding
original music. A decoding or playback-queue failure during a song also resumes
its original music at the current position. Original game resources are still required.

All SDL3 builds support replacements, including 32-bit DOS and WebAssembly.
The bundled stb_vorbis decoder needs no separate Vorbis library installation.
The original Open Watcom 16-bit DOS build keeps its existing music behavior.

## Native file locations

For each song, the game checks these locations in order and uses the first
replacement it can open and validate:

1. `assets/music/` relative to the current working directory.
2. `assets/music/` relative to the executable directory.
3. `music/` relative to the current working directory.
4. `music/` relative to the executable directory.
5. The source checkout's `assets/music/` path recorded when the game was built.

When `--data-dir` is supplied, startup changes the working directory to that
game-data folder; otherwise it uses the directory from which the game was started. Installed native packages put optional replacements in
`bin/music/`, beside the executable. Add or remove source files before
reconfiguring CMake and rebuilding/installing a package. Native users can also
copy replacement files into any of the runtime locations after building.

## Browser builds

The browser build embeds whichever of the four files exist in `assets/music/`
when CMake is configured. Reconfigure and rebuild after adding, changing, or
removing recordings. Embedded files are available at `/assets/music/` and are
included in both the offline HTML and the package's relink kit. If a recording
was not embedded, its song uses the original music. The browser's game-folder
picker does not import music subfolders.

See [SDL3 builds](../../docs/sdl3.md#optional-ogg-vorbis-music) and
[browser builds](../../docs/wasm.md) for build and runtime details.
