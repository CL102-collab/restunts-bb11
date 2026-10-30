# Chocolate Stunts HTML track editor

Open `tracked.html` beside the browser game, or follow **Track editor** in
the browser launcher. Every native SDL3 package, including 32-bit DOS and the
Android ZIPs, includes `share/restunts/editor/tracked.html`. Open that file in
a browser; the editor is a single offline HTML file with its styles and scripts
embedded. From a source checkout, open
`src/restunts/platform/wasm/track-editor.html`
with its adjacent CSS and JavaScript sources. The game's **Edit** button opens
the HTML editor when an extended track is selected.

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

Use **Horizon**, above **Track notes**, to choose Desert, Tropical, Alpine, City,
or Country, matching the original editor. The choice is saved in the track and
used in the game and its track preview. Horizon changes support undo and redo.
New tracks default to Desert.

The footer shows the editor's current `.cstrk` version after "Built for the next
great track." The current version is v8. Revision v3 through v7 tracks also open
and are upgraded to v8 while preserving their original overpass road materials.
Revisions below v3 cannot be opened. Tracks store their version in the file
header; saving keeps the version after any upgrade. Opening a newer version shows an OK-only warning
before loading it; features from newer editors may not work correctly.

## Terrain

Terrain tools appear in the order Paint, Shore, Level, Raise and Lower.
Paint shows one selected square preview of the current grass, ice, dirt or water
surface. Select that primary surface, then choose Shore to show a second material
row above the four corner previews. The second row contains only the other three
surfaces, so any two different materials can meet at a corner, including Water
as the primary surface. The first corner is selected initially. Click another
preview or use Q / E to change the corner independently of track pieces.
Mixed material strokes support undo and redo.

Paint also shows a **Lower surface** section with **Grass, Dirt, Ice, Water**
choices, in that order. Each icon shows the selected material below a neutral,
locked hill. Choose one to paint the lower flat part of a tile at any elevation
without changing the hill surface or any heights. On an outer hill corner it
finds the low flat triangle automatically, preserving any grass, dirt, ice or
water on the hill. The same rule applies to 0–1, 1–2, 2–3 and 3–4 hill levels.
Entirely flat tiles are painted in full at any elevation, including level 4.
You can drag over different hill materials, orientations and elevations in one
stroke. Straight inclines and inner corners have no lower flat part and stay
as they are.
If an existing material boundary crosses the hill, Lower surface leaves that
tile unchanged and explains that it cannot preserve both hill materials.
Click the regular Paint tile preview or a primary surface to paint whole tiles
again. Lower surface strokes support undo and redo and save in the existing
track format.

Level shows five square choices, starting with Ground (0), then heights 1
through 4. These choices appear only while Level is selected; click a map tile
to set its corners to the chosen height and apply the selected terrain surface
to it and any slopes created by the height change. Level supports grass, dirt,
ice and water. Switching between these surfaces keeps Level active and
preserves its height selection.
Height and material changes undo together; unrelated cells keep their materials.
Raise and Lower change shared corners one level per click.
Adjoining corners adjust to retain the original game's flat tiles, straight
inclines, and inner and outer hill corners. Each tile spans at most one level;
alternating high/low corners and two-level diagonal slopes are not supported.
The same 450-unit rise over a 1,024-unit tile is used at every elevation, so
higher hills keep the original slope angles. Paint can apply any surface,
including water, to terrain at any elevation without changing its shape.

On a straight incline, place track pieces in any direction. Palm tree, Cactus
and Pine tree scenery also accepts straight inclines. The editor requires level
ground for divided highways, highway transitions and other scenery; raised
flat ground is allowed. Existing files containing these elements on slopes still
open, validate, save and play normally. Use Remove to delete those elements in
the editor.

Multi-tile elements, including big turns, work when every occupied tile follows
one continuous straight slope with the same direction and grade. The piece's
rendering, collision and route geometry follow that plane; banked roads, elevated
decks, loops and other stunts retain their own shape above the incline. A piece
cannot span a change in slope direction or a flat-to-slope transition.

With HyperVision, Palm tree, Cactus and Pine tree stay upright on inclines.
Their trunks extend down to meet the terrain while the foliage and branches
keep their original shape.

These generalized placements require `.cstrk` v6. Aligned roads and compatible
ramps keep their original hill-road or hill-span models. The aligned tunnels and
slaloms supported by v5 retain their slope behavior. Original `.trk` placement
and driving behavior remain unchanged.

Hill-corner tiles under occupied cells still accept no track pieces. The empty
corners of chicanes, big turns, banked turns, elevated turns and large branches
can contain hill corners beside a flat road; the road's supporting vertices must
stay on its support plane. Terrain edits that would invalidate an existing piece
are rejected; remove or move that piece first.

## Track pieces

Choose a road surface independently of the element group. Dirt and ice variants
are not separate palette groups. Every road and stunt piece supports paved,
dirt and ice, including loops, pipe entrances, pipes, half pipes and all
corkscrews. Scenery is available with every road surface selection and keeps
its original appearance.

The **Stunts** group includes **Tunnel with slalom**, combining the tunnel shell
and slalom barriers in one tile. It supports all four directions, every road
surface, flat ground and straight inclines. These combined elements require
`.cstrk` v7 and remain distinct from ordinary tunnels and slaloms after saving.

**Barrel roll ramp** is a directional, two-tile launch stunt in **Stunts**.
The first tile has an asphalt road on a solid concrete approach. Yellow dashed
center lines continue across the elevated second tile, whose black grille uses
the stock stunt pattern and concrete supports. Its banked launch lip starts the
car rolling around its front-to-rear axis. The car completes the roll freely in
the air after leaving the ramp. The editor arrow points from the concrete
approach toward the grille launch. Start near 100 mph on level paved terrain
and leave a clear landing straight beyond the lip; the car, speed and terrain affect the roll and landing. Route checks allow
up to six empty cells before a ground-level landing road. Barrel roll ramps
require `.cstrk` v8. With OWOOT enabled, the car must pass sideways, inverted,
the opposite side and back upright in order while airborne before landing; an
incomplete roll fails even when the tires reach the receiving road.

The **Elevated** group offers **Overpass over asphalt**, **Overpass over dirt**
and **Overpass over ice**. These choices set the road below the bridge. The
**Paved / Dirt / Ice** road selector sets the upper deck independently, allowing
all nine combinations. Both roads rotate together, and their separate materials
are shown in the palette, placement preview and saved track.

Original editor grouping and icons come from the selected game resources.
Native rotations share one library entry; overpasses have three lower-road
choices. The four direction arrows update all piece thumbnails and select the
variation to place; Q / E also change direction.
Left/right branches and chicanes remain separate shapes. The Shore tool shows
its surface corner choices in the Terrain section when selected.
Big turns, banked turns and elevated turns leave both their inside and outside
corner cells available for scenery or another fitting track piece. Chicanes also
leave two opposite corner cells available. Large branches left and right leave
their empty inside corner available. These placement rules apply to extended
`.cstrk` tracks; original `.trk` rules remain unchanged.

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
are red, with extensions displayed to distinguish matching stems. Legacy builds
retain their original formats.

Use **Check track** to check terrain, placement, surfaces and the race route.
The editor walks the route using the game's connection, branch and jump rules.
Errors identify the column and row using the editor's numbering (starting at 1).
A valid result confirms the data and route; it does not predict whether a car
can physically complete every jump. Unfinished tracks can still be saved.
The SDL3 game also checks the route before a race and displays any failure.

Scenery does not connect roads. Jumps retain the original game's runway and
gap-length requirements. Its runway counter requires two qualifying pieces,
including the takeoff ramp, before a gap. A chicane resets that counter, so a
chicane followed immediately by a ramp can leave too little runway. This is a
layout check; it does not measure the car's speed or change driving physics.
The browser's route tables are generated from the same catalog as the game;
regenerate them with `python3 tools/scripts/generate-editor-route-data.py` if
that catalog changes.

Racing on `.cstrk` records **.csrpl**, which embeds the full track and has a
32-bit frame count, growable input storage and replay checkpoints. It preserves
the complete recording beyond ten minutes. Seek, rewind and branching use the
full frame count. The practical recording limit is available memory and the
platform's file I/O capacity; the current file adapter supports files below
2 GiB. Original `.trk` recording behavior and `.rpl` files remain unchanged.
Each replay records its own revision, which also advances for physics changes,
and the seven driving rules enabled or disabled when recording began. Older
replay revisions are rejected; newer revisions show a warning before playback.
A driving-rule mismatch shows a separate warning listing all recorded rules.
The current game instance keeps its startup settings.
See [the replay format](csrpl-format.md) for details.

Completed eligible races on `.cstrk` save their seven best times in a matching
**.cshig** file beside the track. The table supports long races and includes
penalties. The track selector displays its best score. Original `.trk` tracks
continue to use `.hig`.

Scores belong to the terrain and placed pieces of that track revision. Editing
notes or changing the order of stored objects preserves the table; changing
the course starts an empty table, saved when its first qualifying result is
entered. If the track file changes during a race, the game refuses to award a
score against that different course. See [the score format](cshig-format.md).
