# Opponent artwork

The six finalized opponent portraits are stored in
[`assets/opponents/game/`](../../assets/opponents/game/). Each **160 x 166**
indexed PNG is twice the original tile's width and height, with the exact
`SDMAIN.PVS:!pal` palette, a solid backdrop, and the original frame and numbered
label. The portraits were reconstructed as photographs with the built-in imagegen
tool from the corresponding original `SDOSEL.PVS` portraits, then reduced to the
game's palette. The original game resources remain unchanged.

| File | Opponent | Original shape |
| --- | --- | --- |
| `opp1.png` | Squealin' Bernie Rubber | `SDOSEL.PVS:opp1` |
| `opp2.png` | Herr Otto Partz | `SDOSEL.PVS:opp2` |
| `opp3.png` | Smokin' Joe Stallin | `SDOSEL.PVS:opp3` |
| `opp4.png` | Cherry Chassis | `SDOSEL.PVS:opp4` |
| `opp5.png` | Helen Wheels | `SDOSEL.PVS:opp5` |
| `opp6.png` | Skid Vicious | `SDOSEL.PVS:opp6` |

SDL3 builds display the enhanced photos while HyperVision is enabled.
**F12** also switches the artwork while the opponent menu is open. If an
individual replacement cannot be loaded, that opponent keeps the original
portrait. Turning HyperVision off restores the original artwork for all six.
Clock retains its original illustration. Selecting a ghost replay replaces the
clock with a symmetric white pixel-art ghost on a black backdrop, with black
eye cutouts and a zigzag hem. The ghost uses the same illustration in classic
and enhanced modes.

The original portraits occupy 80 x 83 VGA pixels. The final tile's photo interior
is 148 x 158 at `(4,4)`. The loader crops this interior and displays it beneath
the original white border, numbered label and clipboard artwork. The game's
vertical VGA pixel correction gives the displayed portrait approximately 4:5
proportions; no display aspect correction is baked into the PNGs.

## Original reference extraction

With the original game files in `stunts/`, export lossless indexed references
and verify them with:

```sh
python3 tools/scripts/extract-opponent-portraits.py
python3 tools/scripts/extract-opponent-portraits.py --check
```

The standard-library extractor reuses the existing Stunts resource decoder and
writes to `out/opponents/original/`. It does not overwrite the enhanced photos.
The export manifest includes shape headers, original pixel hashes, and the
exact number masks used to verify preservation.

## Preparing the final portraits

Each base photograph was generated using only its matching original 74 x 79
photo crop as the visual reference, preserving its pose, expression, clothes
and accessories while resolving missing detail. The number was removed from
the reference and restored exactly afterward. The
[exact prompts and source paths](game-regeneration-prompts.json) record all six
initial calls and subsequent full-resolution corrections. Original pixel
speckles and digitization noise are excluded from the restoration. Requested
corrections edit the retained full-resolution photographs before reduction.
Bernie's folded shirt collars are green. Otto was rebuilt from the original game
portrait on 2026-10-02. He holds a gold monocle in his right hand; its single cord
follows the [user-drawn reference](game-sources/patches/opp2-cord-reference.png).
The straight hanging section forms the right side of a narrow loop; below the
chin it bends left, then returns upward on the jacket beside the jaw.
A localized cord patch preserves every master pixel outside its mask.
His lightweight near-black jacket
is collarless and unzipped over a black T-shirt. His lips form a natural partial
pucker, halfway toward a kiss. The reconstruction removes the original digital
noise instead of interpreting it as a clothing pattern.
No fallback CLI or API key was used.

Preparation follows the requested larger-to-smaller workflow:

1. Resize the reconstructed photograph into a 320 x 332 working tile, four times
   the original width and height, with a 296 x 316 photo interior. Use bilinear
   filtering for all six portraits, including Otto.
2. Apply a gentle 3 x 3 median to the photo at that working size to remove isolated
   specks while retaining facial features and intentional clothing patterns.
3. Fill the connected flat backdrop with its exact original palette color.
   Closely matching enclosed backdrop regions, such as Otto's arm gap, are also
   filled. Masks follow the new photograph's silhouette.
4. Reduce the photo and its backdrop mask to half size with the same selected
   filter, then map it to the exact game palette without dithering, added grain
   or sharpening. Otto's final reduction uses bilinear filtering.
5. Restore the original border and complete number rectangle as exact 2x copies.

The [final asset manifest](../../assets/opponents/game/manifest.json) records
source, working-image and final hashes, background colors, dimensions and
processing. The [preserved source gallery](game-sources/README.md) links the six
selected full-resolution photographs and six 4x working tiles in
`docs/opponents/game-sources/`; the runtime asset directory is
`assets/opponents/game/`.

With Pillow and original game data, rebuild or verify the final copies using:

```sh
python3 -m pip install Pillow
python3 tools/scripts/prepare-original-opponent-upscales.py
python3 tools/scripts/prepare-original-opponent-upscales.py --check
```

The script reads `docs/opponents/game-sources/full-resolution/`, prepares the
4x RGB tiles in `docs/opponents/game-sources/working-4x/`, and writes the final
indexed tiles to `assets/opponents/game/`. The optional source-archive
[`processing.json`](game-sources/processing.json) maps portrait filenames to
`nearest` or `bilinear`; unspecified images retain the bilinear default. An
explicit `--processing-config PATH` can select another configuration.
`--check` compares the regenerated bytes without writing. The preserved source
images retain their hashes and
source or composition records alongside them. Normal builds and the installed
game need only the final six indexed PNGs, without Pillow or imagegen access.

## Historical conversion tools and records

`prepare-opponent-portraits.py` is the earlier conversion tool for 296 x 316
full-bleed photo interiors. It bilinearly reduces a source to 296 x 316 and then
148 x 158, quantizes to the exact game palette without dithering, and expands
by nearest neighbor into 2 x 2 sample blocks. Those comparison assets and their
root-level masters are no longer included in `assets/opponents/`.
The finalized tiles use `prepare-original-opponent-upscales.py` instead.

For a separate experiment with the historical conversion, provide a source
directory containing six `oppN.png` photos and a separate output directory:

```sh
python3 tools/scripts/prepare-opponent-portraits.py \
    --source-directory docs/opponents/game-sources/full-resolution \
    --output-directory out/opponents/legacy-photo-conversion
```

The earlier [generation prompts](generation-prompts.txt),
[Otto correction](otto-monocle-correction.txt),
[Joe correction](joe-sunglasses-correction.txt), and
[Skid regeneration](skid-original-regeneration.txt) describe the earlier master
set. The [current generation records](game-regeneration-prompts.json) retain exact
historical prompts and generated source paths; their `preserved_*` fields link
only images retained in the selected source archive.

## Runtime paths

The loader first searches for `game/oppN.png`, then accepts a root-level
`oppN.png` as an optional compatibility fallback. Within each tier it checks
`opponents/` in the game data directory, beside the executable, then this
checkout's `assets/opponents/` directory. Missing, invalid or incorrectly sized
prepared files fall through to the next usable replacement; if none exists,
the original game portrait remains visible. HyperVision gates all replacements.

The prepared `game` tier accepts the final 160 x 166 tile format and the earlier
296 x 316 photo-interior format. For a final tile, the `(4,4,148,158)` interior is
expanded by nearest neighbor into the 296 x 316 enhanced photo area, preserving
every palette color. Legacy 296 x 316 files are used without resizing. The menu
continues to draw its original number, border and clipboard over the photograph.

CMake runtime installation copies only the final six prepared PNGs to
`bin/opponents/game/`. The archived photographs and working tiles are for future
edits and regeneration. No extra original game resource files are required.

## Custom win and lose animations

SDL3 builds can replace each opponent's result-screen animation with a silent
**WebM video using the VP8 codec** while HyperVision is enabled. Put your files
in `opponents/animations/` inside the original game data directory, or beside
the executable:

```text
opponents/
  animations/
    opp1win.webm
    opp1lose.webm
    opp2win.webm
    opp2lose.webm
    ...
    opp6win.webm
    opp6lose.webm
```

The number uses the same opponent mapping as the portraits above. `win` and
`lose` describe **the opponent's result**: `opp1win.webm` plays when Bernie beats
you, and `opp1lose.webm` plays when you beat Bernie. You can replace any subset
of these twelve clips. Neutral results continue to use the original artwork.

The search order is the game data directory, the executable directory, then
this checkout's `assets/opponents/animations/`. On 32-bit DOS, use the shorter
`opponent/anim/` directory instead. The `.webm` extension requires long-filename
support there (for example, DOSBox-X with `lfn=true`). The 16-bit DOS build keeps
the original animations.

A clip loops at its recorded frame timing inside the original animation frame;
it is stretched to that frame's width and height. Keep the same proportions as
the original VGA artwork to avoid distortion. The existing border, result text,
buttons and game music are preserved, and video audio is ignored. **F12** also
switches HyperVision while the result screen is open. Missing, unreadable or
unsupported clips fall back to the original animation.

For example, with an FFmpeg build that includes the VP8 encoder:

```sh
ffmpeg -i my-animation.mp4 -an -c:v libvpx -pix_fmt yuv420p opp1win.webm
```

The `.webm` extension alone does not select the codec: VP9 and AV1 video are
unsupported. Clips may be at most 2048 pixels wide or tall, 60 seconds long,
and 32 MiB in file size. Original game resources are still required for the
fallback, layout and result text.
