# Skyboxes

The AI-refined versions of the five Stunts scenery sets are stored in
[`assets/skyboxes/`](../../assets/skyboxes/). Each enhanced image is exactly
four times the original width and height (16 times the pixel count).
The game retains its original artwork in the `*.PVS` resources; extracted
original PNG copies are not stored in the repository. This directory contains
the documentation and generation prompts.

| Scenery | Original panorama | Enhanced panorama |
| --- | --- | --- |
| Desert | 1024 × 24 | 4096 × 96 |
| Tropical | 1024 × 29 | 4096 × 116 |
| Alpine | 1024 × 30 | 4096 × 120 |
| City | 1024 × 20 | 4096 × 80 |
| Country | 1024 × 16 | 4096 × 64 |

Each `<scenery>.png` shows the whole panorama. The game consumes its four
`<scenery>-scen.png`, `-sce2.png`, `-sce3.png`, and `-sce4.png` strips.
Their original widths are 320, 192, 320, and 192 pixels, at panorama offsets
0, 320, 512, and 832. Strips have different heights and share a bottom edge.
The [asset manifest](../../assets/skyboxes/manifest.json) records the original
dimensions, scenery coverage, resource headers, palette metadata, and
extraction hashes.

## Custom horizons

Extended `.cstrk` tracks in SDL3 builds can select a custom horizon from the
`horizons/` subdirectory of the game data folder. Supply **one PNG**, for example
`horizons/Misty mountains.png`. Create the folder if it does not exist.
No metadata file or separate strips are needed.
The editor discovers images when you select or reconnect the Stunts folder and
uses the filename stem (`Misty mountains`) as its display name.

Custom artwork must meet these requirements:

- Width: exactly **4096 pixels**.
- Height: **any integer from 64 through 120 pixels**, including odd heights.
- Format: non-interlaced, 8-bit RGB or indexed PNG. Every used pixel must be
  opaque. RGBA, grayscale, and partially transparent images are unsupported.
- Sky: flat RGB **(93, 255, 255)**. Keep scenery aligned to the bottom edge and
  make the left and right edges join for a wrapping panorama.
- Filename: a basename ending in `.png` (case-insensitive), at most 127 UTF-8
  bytes including the extension. Spaces and Unicode are supported. Directory
  separators, control characters, and `<>:"|?*` are rejected.

The game pads the top with sky to the next multiple of four when needed; it
does not stretch or crop the image. It splits the panorama into 1280, 768,
1280, and 768-pixel strips and derives quarter-size artwork for classic mode
and the lowest HyperVision resolution. Higher HyperVision resolutions use the
full detail. Colors are mapped to the original VGA palette as with built-in art.
These horizons also appear in native track selector previews. The editor's
WebGL preview continues to use a flat sky.

Select a built-in horizon before choosing a custom one to set its fallback.
Tracks store both that built-in choice and the custom filename. Missing or
invalid artwork uses the fallback and preserves the filename when saving.
The image is not embedded in tracks, replays, or ghosts; distribute the PNG
with them and copy it into `horizons/` on each installation. Changing horizon
artwork does not affect race physics, checkpoint routes, or highscore identity.
Legacy `.trk` tracks retain their five built-in horizons.

The built-in assets described below still use four runtime strips. Custom
horizons use the single panorama file instead.

## Extraction and verification

From the repository root, validate the enhanced assets without game files:

```sh
python3 tools/scripts/check-skyboxes.py
```

The validator checks PNG integrity, exact four-times dimensions, panorama
placement, and added image detail using the asset manifest. Use `--directory`
to check another directory containing the enhanced PNGs and manifest.

For optional original-art reference exports, place Broderbund Stunts 1.1
resources in `stunts/`, then run:

```sh
python3 tools/scripts/extract-skyboxes.py
python3 tools/scripts/extract-skyboxes.py --check
```

The extractor writes disposable references to `out/skyboxes/original/`.
It reads the five `*.PVS` resources and the `SDMAIN.PVS:!pal` palette using
only the Python standard library, preserving every source palette index.
Use `--game-directory` and `--output-directory` to override the input and
output locations. Extraction does not regenerate or overwrite enhanced art.

## Enhanced rendering

F12 enables HyperVision in SDL3 builds. At 640 × 400 and 1280 × 800, driving
and track selector previews use the refined horizon strips. HyperVision at
320 × 200, classic mode, and the 16-bit Open Watcom renderer use the original
art. Missing, unreadable, or incorrectly sized enhanced images fall back to
their original strips. Scenery detail settings still apply.

During driving, HyperVision rotates the panorama continuously with the camera,
including sideways and upside-down views at every supported resolution. It
preserves the original level panorama scale and projects the horizon with the
same camera as the road.
Missing enhanced strips use the original artwork with the same rotation.
The original renderer retains its historical banking cutoff.

PNG colors are mapped once to the original VGA palette when loaded. This
preserves the game's palette fades and indexed rendering, while retaining
the new spatial detail. The flat sky color is RGB (93, 255, 255), palette
index 116. Only the current scenery set is cached.

The loader searches `skyboxes/` in the game data directory, then beside the
executable, and finally `assets/skyboxes/` in a source build.
`cmake --install` installs the 20 runtime strips in `bin/skyboxes/`, with
DOS-compatible names `sky0-0.png` through `sky4-3.png`. The first number
selects desert, tropical, alpine, city, or country; the second selects the
four resource strips in the order listed above. Copy that directory along
with a packaged executable.

## AI refinement provenance

The built-in imagegen tool generated one atlas for each scenery set.
The chosen style was faithful to the original art, with smoother edges and
richer detail. No API key or fallback CLI was used.

For each reference, the original 1024-pixel panorama was split into four
256-pixel quarters. Each quarter was bottom-aligned in a 256 × 64 cyan row;
the four rows were stacked and enlarged to a 1024 × 1024 reference atlas.
This makes the extremely thin panoramas suitable inputs for imagegen while
keeping their relative scale.

The generated atlases were 1254 × 1254. Their four landscape bands are joined
at native resolution, excluding blended divider pixels. The complete panorama
is split into the original resource spans before resampling so taller neighboring
terrain cannot bleed across a strip boundary. Each scenery set uses one vertical
scale, anchored at the bottom, with at least two solid sky rows above every
complete skyline. This keeps all mountain peaks and tree crowns inside the exact
4x image dimensions without changing scale at strip boundaries.

Only bright cyan connected to the sky is normalized to the original sky color;
distant blue mountains and water keep their colors. The corrected bottom pixel
rows are preserved, and the preview panoramas are rebuilt from the runtime
strips. The complete peaks were recovered from the existing AI source artwork;
no new scenery was generated for that packing correction. The country
regeneration below subsequently replaced its artwork from the originals.

The exact initial common prompt follows; each invocation appended
`Scene: desert.`, `Scene: tropical.`, `Scene: alpine.`, `Scene: city.`,
or `Scene: country.`.

```text
Use case: precise-object-edit.
Asset type: faithful HD remaster of a Stunts 1990 racing-game skybox texture atlas.
The reference is an edit target, already enlarged with nearest-neighbor to the required 1024x1024 canvas. Refine the coarse pixelated scenery into beautifully detailed, crisp hand-painted retro game background art: smooth silhouette edges, finely modeled surfaces and subtle natural texture. Keep the original colors and aesthetic.
CRITICAL locked layout: exactly FOUR separate thin horizontal landscape strips, one at the bottom of each 256-pixel-tall row. Their bottom baselines are y=255, y=511, y=767 and y=1023. Preserve the exact scenery arrangement, scale, every peak and valley, height, and location. Do not enlarge the scenery vertically or move it. These strips join horizontally in row order into one panorama. Preserve edge continuity. The large empty flat bright cyan areas between strips must remain exactly solid RGB(93,255,255), including all existing sky pixels around the terrain. Do not add clouds, sky gradients, fog, suns, frames, labels, lettering, new objects or transparency. Do not make one big landscape or change the four-row grid. No pixel blocks, no dithering or blur. Improve existing terrain detail only.
Output: a single 1024x1024 PNG texture atlas in exactly the same layout as the reference.
```

## Country flower-field regeneration

This records the earlier broad-field version, superseded by the
[small flower patch regeneration](#country-small-flower-patch-regeneration).

The country artwork was regenerated from the original extracted panorama with
its original four-quarter atlas layout, using the built-in imagegen tool.
The red source pixels represent red poppy fields, the pink pixels represent
pink flower fields, and the yellow pixels represent yellow flower fields.
Each colored patch is an area of densely flowering countryside, with fine
collective bloom texture. Individual flowers, petals, leaves, and blades of
grass are too distant to resolve. This replaces the autumn-tree interpretation.
The original game resources are unchanged.

A second imagegen pass reduced the coarse bloom shapes in the first draft while
preserving its red, pink, and yellow field patches. The final atlas is
1254 × 1254. Its four landscape bands are joined in row order, excluding
blended divider rows, then packed with one bottom-anchored vertical scale.
Connected bright cyan sky is normalized to RGB (93, 255, 255). The new artwork
supplies the complete terrain, including its bottom rows. The four runtime
strips remain 1280 × 64, 768 × 56, 1280 × 60, and 768 × 56; the panorama is
rebuilt from those strips at 4096 × 64. Each strip retains at least two solid
sky rows above its complete skyline. A localized imagegen repair blends the
red-field join at panorama x=1024; only its lower valley was composited back
with feathered edges, retaining the skyline and surrounding artwork.
No API key or fallback CLI was used.

The exact generation prompt was:

```text
Use case: precise-object-edit.
Asset type: remastered Stunts country skybox, a four-row atlas of distant countryside.
Input image: ORIGINAL pixel-art country scenery. Use this original as the composition map. Repaint it as refined naturalistic game background art while retaining its small-scale rolling hills and green palette.

The key feature is PATCHES OF FLOWER FIELDS in THREE DISTINCT COLORS:
- The original RED pixels map to rich crimson/scarlet-red fields of flowering poppies.
- The original light PINK pixels map to rose-pink flower fields.
- The original YELLOW pixels map to bright golden-yellow flower fields.
Keep red, pink, and yellow clearly distinct; do not combine red and pink into peach/orange. Follow the positions of their colored source clusters closely. Keep many irregular patches of different sizes, with green meadow visible between them, including small outlying patches. Each original colored pixel represents an area of densely flowering countryside containing thousands of blooms, NOT one flower. Expand source clusters into low irregular patches of many flowers that spread along the gentle hill slopes.

VISUAL APPROACH: the colored fields must look like dense flowering vegetation seen from a great distance. Use fine, irregular, collectively mottled bloom texture within each red, pink and yellow patch, with uneven natural field margins and subtle darker gaps, giving the impression of thousands of tiny unresolved flower heads. Not smooth single-color ovals, clean vector shapes, glowing blobs, orange dirt, painted stains, or a few huge ribbons. The red poppy fields must unmistakably read as red flower fields. The green hills should have restrained natural meadow tonal variation and convincing softly modeled contours. No visible individual petals, flower icons, stalks, leaves, bushes, trees or grass blades anywhere, including the bottom edge. All terrain is hundreds of meters away, never close foreground.

LOCKED GEOMETRY: exactly FOUR thin horizontal landscape bands at the bottom of four equal-height rows, one band per row. On a 1024x1024 canvas their baselines are y=255,511,767,1023. Keep the reference hill silhouettes, valleys, horizontal positions and height: terrain is only about 40-60 pixels tall in each 256-pixel row. Do not enlarge the landscape or fill the empty cyan space. Preserve the original overall hill layout with smooth non-pixelated edges. Row1 right edge continues row2 left edge; row2 right continues row3 left; row3 right continues row4 left; row4 right wraps to row1 left. Make these adjoining edges share the same heights, lighting and field colors, without visible vertical seams.

Keep sky and empty row space perfectly flat opaque RGB(93,255,255). No clouds, gradient, atmospheric blue extra hills, sky texture, sun, buildings, paths, labels, borders or transparency. No coarse pixel blocks or dithering.
Output: one 1024x1024 PNG atlas preserving exactly the original four-row composition.
```

The exact distance-scale correction prompt was:

```text
Edit the attached four-row country skybox atlas. Keep its exact composition, low hill silhouettes, small terrain height, four row baselines, and the red/pink/yellow field locations.

Change ONLY the scale and surface of the vegetation. These are agricultural flower fields viewed from ONE KILOMETER away. The current rounded flower-like clumps are much too large. Replace EVERY rounded bloom/clump shape with a flat distant FIELD carpet of color. There must be no identifiable flower heads, no scalloped/round petals, no tiny icons, no stems and no grass blades. Make the blossom texture at least FIFTEEN TIMES FINER than now: each bloom is far smaller than a single image pixel. What is visible is the aggregate: many irregular interwoven patches of crimson-red poppy fields, rose-pink flower fields, and yellow flower fields. Preserve all THREE DISTINCT COLORS. A red patch is a whole poppy field, not one poppy. Use very subtle fine tonal grain across the field surfaces and gently irregular feathered patch margins. Retain a convincing soft collective flowering texture, not shiny smooth blobs or flat geometrical ovals. Smoothly model the green distant hills too, removing all coarse bush-like mounds.

Do not enlarge or move the hills. Exactly FOUR THIN landscape bands with baselines at 25%,50%,75%,100% of square image height. Keep terrain confined to the bottom 40–60 pixels of each 256-pixel row on the 1024px reference grid. Sequential rows form one continuous panorama, so their green edge heights and colors must connect and the last row wraps to the first. Keep all remaining sky flat exact opaque RGB(93,255,255). No sky gradients, clouds, texture, extra blue hills, trees, new objects, text, frames or transparency.

Return the same four-row PNG atlas with only this correction to landscape/flower-field distance scale.
```

The exact localized join-repair prompt was:

```text
Repair the visible join in this small landscape texture. At the exact middle there is a vertical line: a dark green rectangular corner abuts a red flower field. Paint over this entire center join and make a natural continuous green valley with an irregular red poppy-field edge that tapers into the green meadow. The red area MUST NOT start at a straight vertical edge. Blend the surrounding colors and terrain texture seamlessly through the center. Preserve the yellow field on the left and pink/red fields on the right, and retain the existing skyline height. Keep the outermost left/right edges unchanged. This is an enlarged crop of a DISTANT skybox: retain the existing extremely fine aggregate flower-field texture and green meadow, no new detail, no grass blades, no individual blossoms, no trees. Keep the sky solid exact RGB(93,255,255). Single horizontal landscape strip, same aspect ratio and exact composition. No text.
```

## Localized continuity repairs

All 20 enhanced strips were reviewed internally and bottom-aligned in the
game's repeating order: `scen -> sce2 -> sce3 -> sce4 -> scen`. The individual
strips are sections of that cycle; they are not independently repeating tiles.

The built-in imagegen tool repaired localized skyline cuts and vertical
texture/lighting splices at the following enhanced-panorama x coordinates.
Here, x=0 denotes the 4096-to-0 wrap. Other joins and natural cliffs or
building edges were retained.

| Scenery | Repaired joins |
| --- | --- |
| Desert | 0, 1280, 2048, 3072, 3328 |
| Tropical | 0, 2048, 3072 |
| Alpine | 0, 1024, 2048 |
| City | 2048 |
| Country | 0, 1024, 2048, 3072 |

Generated crop repairs were aligned to the existing artwork and composited
through narrow local masks. Every pixel outside those masks remains exact;
97.46% of the combined panorama pixels are unchanged. Dimensions, opaque RGB,
flat cyan sky, and at least two sky rows above each strip's skyline are
preserved. Repairs adjacent to shorter strips fit their available height
before splitting, preventing new clipped peaks. The five panoramas match
the runtime strips pixel for pixel. Original game resources are unchanged.

The [repair prompts](continuity-repair-prompts.txt) record the exact built-in
imagegen instructions. No API key or fallback CLI was used.

## Bottom-row repair

The final two pixel rows had a shared warm/dark border artifact. The built-in
imagegen tool continued the existing terrain downward through those two rows
and ten additional temporary rows. Overlapping horizontal crops included
wrapped context, keeping the strip joins and panorama repeat continuous.
Generated pixels were aligned and color-matched to the last untouched row.
Each runtime strip was saved ten rows taller, then cropped by exactly ten rows
with Sharp. All 20 strips and five panoramas retain their original dimensions.
Every pixel above the final two rows is identical to the pre-repair artwork.
The original game resources are unchanged. The exact
[outpainting prompts](bottom-row-repair-prompts.txt) used the built-in tool;
no API key or fallback CLI was used.

## City chimney restoration

A localized built-in imagegen repair restores the original factory motif in
`city-sce4.png`: the two oversized pine trees become a dark chimney, and the
crane becomes grey smoke drifting upward and right. Generated pixels are
composited through a small mask, preserving foreground buildings and every
pixel outside the repair, including all bottom rows and strip joins. The
matching `city.png` panorama contains the same patch. The other strips and
original game resources are unchanged. See the exact
[repair prompts](city-chimney-repair-prompts.txt); no fallback CLI or API key
was used.

## Country small flower patch regeneration

The current country panorama and four runtime strips were regenerated from
the original `COUNTRY.PVS` reference with the built-in imagegen tool. Small,
separate yellow, red, and purple flower patches replace the earlier broad
fields, leaving most of the rolling hills green. Temporary original exports
remain in the ignored work directory, outside the asset tree.

Four overlapping generated sections follow the original hill profile and form
a continuous panorama in the renderer's `scen -> sce2 -> sce3 -> sce4 -> scen`
order. Overlaps include the final-to-first wrap. Flat cyan sky is normalized
to the game palette, and generated flower hues retain the requested purple.
The panorama was assembled with ten extra bottom rows at 4096 x 74, then
cropped with Sharp to 4096 x 64. Each runtime strip likewise has a ten-row
extension that is cut off, retaining its exact original four-times dimensions.
All strip joins, generation joins, the wrap, and final bottom rows were
reviewed. Other scenery sets are unchanged. The exact
[generation prompt](country-small-patches-prompts.txt) records the built-in
imagegen request; no fallback CLI or API key was used.
