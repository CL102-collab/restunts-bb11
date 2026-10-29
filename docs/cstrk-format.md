# Chocolate Stunts track format

`.cstrk` is the extended track format for SDL3 builds, including the
SDL3 WebAssembly build. The current file-format revision is **v4**, supported
by the game and HTML editor. DOS, Amiga and TempleOS builds continue to use `.trk`.
The format stores a 1–90 by 1–90 tile terrain grid, terrain heights from zero
through four hill levels, independent terrain and road surfaces, track objects,
a horizon choice, and up to 8,000 ASCII text bytes of notes. One hill level
retains the original game's 450 geometry-unit height; horizontal tile size
remains 1,024 units.

The pure storage codec lives in `src/restunts/c/cstrk.c` and `cstrk.h`. It does
not alter vehicle physics or legacy file layouts. A replay on an extended track
uses `.csrpl`, embedding the complete `.cstrk` file rather than truncating it to
the legacy replay track section.

## Binary layout

All integer fields are little-endian, unsigned unless explicitly marked signed.
There is no implicit padding, compression, native C structure image, or trailing
data. The revision is numeric metadata: an unsigned 16-bit integer. The
minimum supported revision is v3; older extended-track revisions are rejected.
New tracks are created with revision v4. Revision v3 tracks upgrade to v4 in
memory, preserving their shared overpass surface as both upper and lower road
materials, so subsequent saves use v4 without changing their appearance or grip.

Files with a revision higher than the reader supports show an OK-only warning
and are loaded on a best-effort basis using the known layout. A file marked
v15 or v101 is newer than a reader supporting v4. Its revision alone does not
make loading fail, and its numeric revision is retained when saving.

Best-effort loading still validates header size, horizon, dimensions, object
records, notes, and exact payload lengths. Invalid structures remain errors.
The header and payload layout below applies to every accepted revision.

The header occupies exactly 32 bytes:

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | 8 | Magic: `43 53 54 52 4b 1a 0d 0a` (`CSTRK`, SUB, CR, LF) |
| 8 | 2 | Numeric format revision, 3–65,535; current revision `4` |
| 10 | 2 | Header size, `32` |
| 12 | 4 | Horizon, `0`–`4` |
| 16 | 2 | Width in tiles, 1–90 |
| 18 | 2 | Height in tiles, 1–90 |
| 20 | 4 | Notes length, 0–8,000 |
| 24 | 4 | Object count, 0 through width × height |
| 28 | 4 | Payload length in bytes, excluding this header |

The payload contains these consecutive arrays:

1. `(width + 1) × (height + 1)` corner-height bytes.
2. `width × height` two-byte terrain cells.
3. `object_count` sixteen-byte object records.
4. `notes_length` ASCII text bytes, without a NUL terminator.

Consequently the file length is exactly
`32 + (width+1)*(height+1) + 2*width*height + 16*object_count + notes_length`.
The largest structurally permitted file in the current layout occupies 162,113 bytes.

## Horizon

The horizon uses the original game's scenery choices:

| Value | Horizon |
| --- | --- |
| 0 | Desert |
| 1 | Tropical |
| 2 | Alpine |
| 3 | City |
| 4 | Country |

New tracks default to Desert. Every valid revision may select any of these
horizons. Values outside this range and nonzero upper bytes are rejected.
The selected horizon and revision are retained in embedded replay tracks.
Horizon changes and revision metadata changes preserve the track's highscore
table.

## Terrain

Coordinates use a top-left origin: x increases rightwards and y downwards.
All arrays are row-major. Corner `(x,y)` has index `y*(width+1)+x`; cell `(x,y)`
has index `y*width+x`. Cells share their terrain corners, preventing cracks.

Each corner height is an integer from 0 through 4. Every cell uses one of the
original game's thirteen terrain profiles: flat ground, four straight inclines,
four outer corners with one high corner, or four inner corners with one low
corner. The highest and lowest corners of a cell differ by at most one level.
Alternating high and low corners and two-level diagonal slopes are invalid.
Terrain edits propagate neighboring heights to preserve these profiles.

One level rises 450 world units across a 1,024-unit tile, retaining the original
straight incline of about 23.7 degrees and the steepest corner incline of about
31.9 degrees. Higher hills translate the same profiles upward; they do not change
the slope angle. Rendering and collision select the same original profile,
independently of the painted surface or Shore direction.

Each two-byte terrain cell contains:

| Byte | Meaning |
| --- | --- |
| 0 | Primary surface: grass `0`, ice `1`, dirt `2`, water `3` |
| 1 | Packed corner direction and secondary surface, described below |

The second byte contains:

| Bits | Meaning |
| --- | --- |
| 0–2 | Corner: none `0`, northwest `1`, northeast `2`, southeast `3`, southwest `4` |
| 3–4 | Secondary surface code: water `0`, dirt `1`, ice `2`, grass `3` |
| 5–7 | Reserved, `0` |

The secondary surface code is the terrain surface ID XOR `3`. A zero
direction requires the entire packed byte to be zero; unused secondary material
data is not stored.
Unknown directions and reserved bits are rejected.

The chosen corner triangle uses the secondary material; the other triangle
uses the primary material. Shore supports every pair of different materials,
including Water as the primary material, in all four directions. Same-material
corners are invalid. Surface is independent of elevation,
so every material combination can also appear on slopes and above ground.

The C model uses `half_water_corner` for the direction and `corner_surface`
for the secondary material. New cells default that field
to Water. The field is ignored when the direction is zero.

## Objects and original hill placement

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | 2 | Already-oriented legacy catalog tile ID, 1–214 |
| 2 | 1 | Anchor x |
| 3 | 1 | Anchor y |
| 4 | 1 | Additional clockwise quarter turns, 0–3 |
| 5 | 1 | Road surface: paved `0`, dirt `1`, ice `2` |
| 6 | 1 | Footprint width in map axes, 1–2 |
| 7 | 1 | Footprint height in map axes, 1–2 |
| 8 | 1 | Occupied-cell bitmask, nonzero |
| 9 | 1 | Support height at the anchor's top-left terrain corner, 0–4 |
| 10 | 1 | Signed slope in x, −1, 0, or +1 |
| 11 | 1 | Signed slope in y, −1, 0, or +1 |
| 12 | 1 | Lower overpass road surface: paved `0`, dirt `1`, ice `2` (v4+) |
| 13 | 3 | Reserved, `0` |

The ordinary road surface selects the upper road on an overpass. The independent
lower surface selects its road underneath. Only overpass IDs 101 and 102 may
store a nonzero lower surface; all other objects require paved `0` in that field.
Revision v3 reserves all four bytes at offsets 12–15 as zero. The reader rejects
nonzero reserved bytes in either revision and upgrades accepted v3 tracks to v4.
A v3 overpass inherits its lower material from its ordinary surface because the
older renderer and collision model used that selection for both roads. New v4
overpasses default the lower surface to paved independently of the upper road.
Future revisions retain their numeric metadata and use the known v4 layout.
An explicit in-memory v3 export requires equal upper and lower overpass
surfaces and writes zero at byte 12, preserving the original shared-surface format.

Rotation is additional to the rotation already represented by the original tile
ID; zero preserves that catalog tile's orientation. Footprint dimensions and
mask describe the result after this additional rotation. The mask bit for local
cell `(column,row)` is `1 << (row*footprint_width+column)`. Bits outside the
footprint must be zero. For a 2×2 footprint, mask `7` claims its top-left,
top-right and bottom-left cells, leaving bottom-right available for another
object such as a decoration. Only claimed cells participate in overlap checks.
Every object must fit completely inside the map, including unclaimed cells in
its footprint rectangle. At most one object may claim each cell.

The support metadata at local terrain corner `(dx,dy)` is
`base_height + slope_x*dx + slope_y*dy`. Every corner throughout the footprint
rectangle, including corners around unclaimed cells, must exactly equal this
plane and remain within 0–4. Ordinary pieces require a flat footprint at one
constant elevation. Inclines accept only one-tile pieces approved by the
original hill-road substitution table: an aligned straight road or a compatible
ramp variant. A sloped record therefore has exactly one nonzero slope axis.
Hill-corner terrain accepts no track objects, as in the original editor.

The game substitutes the original full-tile hill-road or flat hill-span model,
including its collision and route data, at the terrain's lower base elevation.
It does not rotate a flat model onto the incline or add connector strips.
Intrinsic banking, elevated decks, loops and other stunt geometry retain their
original shapes on flat plateaus at any supported elevation.

The codec validates IDs, fields, bounds, terrain support, and occupied-cell
overlap without loading the game's assets. Resource-aware consumers must also
check that each ID's supplied footprint and selected surface are supported by
their catalog. A serialized mask is not permission to remove collision from
an inherently occupied part of a catalog object.

Serialized internal hill/overlay IDs 182–214 are rejected by game admission;
the runtime resolves approved hill pieces to these models internally. All road
and stunt pieces accept paved, dirt and ice surfaces, including loops, pipe
entrances, pipes and half pipes. Scenery accepts all surface values but ignores
this field; new scenery placements use paved `0`. Turns, branches, highways,
stunts and scenery require flat ground throughout their footprint. These catalog
rules are checked by the editor and game in addition to the storage codec's
structural validation. Free-form saddles, two-level diagonal cells and unsupported
tilted pieces are invalid.

## Notes

Notes contain ASCII text only: printable bytes `0x20`–`0x7e`, tab `0x09`,
line feed `0x0a`, and carriage return `0x0d`. NUL, other controls, DEL and bytes
above `0x7f` are rejected. Their limit is 8,000 bytes, including line endings.
The C model adds a terminating NUL in memory; that terminator is not serialized.

## C API and validation

Call `cstrk_init` once on a track before `cstrk_create` or `cstrk_decode`.
Call `cstrk_free` when finished; repeated freeing is safe. Operations returning
`int` return 1 for success, 0 for failure, and accept an optional error buffer.
Failed create/decode, add-object and set-notes operations preserve the existing
track. Decode builds and validates a temporary model before replacing it.

`cstrk_encode` allocates its output using `malloc`/`calloc`; the caller releases
that buffer with `free`. `cstrk_validate_bytes` validates a complete file,
including terrain and object semantics, without retaining a decoded model.
All sizes are checked against format bounds before allocations or payload reads.
This codec handles storage; game collision, rendering and resource admission
belong to the SDL3 extended-track integration.

The standalone test `src/restunts/tests/test-cstrk.c` covers an independently
specified binary fixture, every truncation of that fixture, malformed headers
and objects, all horizon choices, current and future numeric revisions, rejection
of obsolete revisions, all 12 material pairs in four corner directions, sparse
overlays, positive and negative slopes, ASCII limits, transactional failure,
and the maximum-size 90×90 map.
