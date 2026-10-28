# Chocolate Stunts HTML track editor

Open `track-editor.html` beside the browser game, or follow **Track editor** in
the browser launcher. Native packages install it in `share/restunts/editor/`;
the Edit button opens it for a selected Chocolate Stunts track. From a source
checkout, open `src/restunts/platform/wasm/track-editor.html`.

Select your Stunts folder before editing. The editor reads the original local
MAIN, TEDIT and SDTEDIT resources for its palette and icons. It does not bundle
or upload game files. Supported browsers can write tracks into that folder;
other browsers require folder selection and download the resulting files.
Reconnect a remembered folder when the browser requests permission.

The controls occupy the left 30% of a desktop window and the 2D track occupies
the right 70%. Use Hide tools / Show tools above the canvas to collapse or reopen
the toolbar while keeping the same zoom and map center. On small screens the
tools use a wide scrollable drawer below that toggle. The Terrain / Track library
switch shows one section at a time. Each section remembers its settings; selecting
Build, Remove or an element group opens the track library. New tracks can have
any dimensions from 1 by 1 through 90 by 90 tiles. Use the New dialog to choose
dimensions. Names use the game's 1–8 character ASCII filename stems.

## Terrain

Terrain tools appear in the order Paint, Shore, Level, Raise and Lower.
Paint shows one selected square preview of the current grass, ice, dirt or water
surface. Shore shows four square previews combining water with the selected
grass, dirt or ice; its first corner is selected initially. Click another
preview or use Q / E to change the Shore corner independently of track pieces.

Level shows five square choices, starting with Ground (0), then heights 1
through 4. These choices appear only while Level is selected; click a map tile
to set its corners to the chosen height and apply the selected terrain surface
to it and any slopes created by the height change. Level supports grass, dirt
and ice; Water and Level cannot be selected together. Switching between those
three land surfaces keeps Level active and preserves its height selection.
Height and material changes undo together; unrelated cells keep their materials.
Raise and Lower change shared corners one level per click.
Adjoining corners adjust to form connected slopes, as in SimCity 2000. Paint
can apply any surface, including water, to terrain at any elevation.

Build a continuous slope before placing a stunt. The complete piece needs a
single supporting plane over its footprint, including both rows or columns of
a two-tile piece. A piece tilts as a whole. Terrain edits that would bend a
placed piece are rejected; remove or move that piece first.

## Track pieces

Choose a road surface independently of the element group. Dirt and ice variants
are not separate palette groups. Incompatible pieces are disabled. The expanded
surface choices cover slalom, tunnel, large branches, divided highway, chicanes,
elevated roads, banked roads and all corkscrews. Pipes and loops retain their
original surface restrictions. Scenery is available with every road surface
selection and keeps its original appearance.

Original editor grouping and icons come from the selected game resources.
Each shape appears once in the library. The four direction arrows update all
piece thumbnails and select the variation to place; Q / E also change direction.
Left/right branches and chicanes remain separate shapes. The Shore tool shows
its water-corner arrows in the Terrain section when selected.
Large turns, banked turns, chicanes and elevated turns claim only their occupied
cells, leaving their empty cells available for scenery or another fitting piece.

Drag to build or paint. Right-button dragging always pans with any tool.
Click or hold and drag the Navigator minimap to reposition the main view.
Navigation does not edit the track or add undo steps. A building or painting
stroke is one undo operation. Notes changes, terrain operations and placements
also participate in undo and redo. Notes
support up to 8,000 printable ASCII characters, with tabs and line breaks.

| Action | Shortcut |
| --- | --- |
| Place / remove / pan | B / R / H |
| Rotate | Q / E |
| Pan temporarily | Right or middle mouse drag, or Space and drag |
| Zoom | Mouse wheel |
| Move tile cursor / place | Arrow keys / Enter |
| Undo / redo | Ctrl+Z / Ctrl+Shift+Z |
| Save / open | Ctrl+S / Ctrl+O |
| Element groups | F1–F10 |

## Files and game support

The new editor loads and saves **.cstrk** files only. It does not import `.trk`.
See [the track format](cstrk-format.md) for the versioned binary layout.

SDL3 builds, including the browser build, support the new formats. Track and
replay selectors include both legacy and new files; `.cstrk` and `.csrpl` names
are blue, with extensions displayed to distinguish matching stems. Legacy builds
retain their original formats.

Racing on `.cstrk` records **.csrpl**, which embeds the full track and has a
32-bit frame count, growable input storage and replay checkpoints. It preserves
the complete recording beyond ten minutes. Seek, rewind and branching use the
full frame count. The practical recording limit is available memory and the
platform's file I/O capacity; the current file adapter supports files below
2 GiB. Original `.trk` recording behavior and `.rpl` files remain unchanged.
See [the replay format](csrpl-format.md) for details.

The original `.hig` table stores legacy track bytes and 16-bit times. New-format
tracks do not write scores to an unrelated legacy high-score table. Full race
times and penalties remain available on the evaluation screen and in replays.
