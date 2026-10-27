# Chocolate Stunts track format

`.cstrk` version 1 is the extended track format for SDL3 builds, including the
SDL3 WebAssembly build. DOS, Amiga and TempleOS builds continue to use `.trk`.
The format stores a 1–90 by 1–90 tile terrain grid, terrain heights from zero
through four hill levels, independent terrain and road surfaces, track objects,
and up to 8,000 ASCII text bytes of notes. One hill level retains the original
game's 450 geometry-unit height; horizontal tile size remains 1,024 units.

The pure storage codec lives in `src/restunts/c/cstrk.c` and `cstrk.h`. It does
not alter vehicle physics or legacy file layouts. A replay on an extended track
uses `.csrpl`, embedding the complete `.cstrk` file rather than truncating it to
the legacy replay track section.

## Binary layout

All integer fields are little-endian, unsigned unless explicitly marked signed.
There is no implicit padding, compression, native C structure image, or trailing
data. Version 1 readers reject unknown versions, header sizes, flags, reserved
fields, and payload lengths. Later extensions require a new supported version.

The header occupies exactly 32 bytes:

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | 8 | Magic: `43 53 54 52 4b 1a 0d 0a` (`CSTRK`, SUB, CR, LF) |
| 8 | 2 | Version, `1` |
| 10 | 2 | Header size, `32` |
| 12 | 4 | Flags, `0` |
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
The largest structurally permitted version 1 file occupies 162,113 bytes.

## Terrain

Coordinates use a top-left origin: x increases rightwards and y downwards.
All arrays are row-major. Corner `(x,y)` has index `y*(width+1)+x`; cell `(x,y)`
has index `y*width+x`. Cells share their terrain corners, preventing cracks.

Each corner height is an integer from 0 through 4. Horizontally and vertically
adjacent vertices may differ by at most one level. Terrain editing can propagate
height adjustments to neighboring vertices to preserve this condition, in the
style of SimCity 2000. A terrain cell may be nonplanar; an object footprint must
be supported by one consistent plane.

Each two-byte terrain cell contains:

| Byte | Meaning |
| --- | --- |
| 0 | Surface: grass `0`, ice `1`, dirt `2`, water `3` |
| 1 | Half-water corner: none `0`, northwest `1`, northeast `2`, southeast `3`, southwest `4` |

The half-water value identifies the water triangle at that corner; the other
triangle uses the selected terrain surface. It supports water adjoining a hill
corner. Surface is independent of elevation, so water, ice and dirt are also
representable above the base level.

## Objects and rigid support planes

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
| 12 | 4 | Reserved, `0` |

Rotation is additional to the rotation already represented by the original tile
ID; zero preserves that catalog tile's orientation. Footprint dimensions and
mask describe the result after this additional rotation. The mask bit for local
cell `(column,row)` is `1 << (row*footprint_width+column)`. Bits outside the
footprint must be zero. For a 2×2 footprint, mask `7` claims its top-left,
top-right and bottom-left cells, leaving bottom-right available for another
object such as a decoration. Only claimed cells participate in overlap checks.
Every object must fit completely inside the map, including unclaimed cells in
its footprint rectangle. Version 1 permits at most one claimed object per cell.

The support plane at local terrain corner `(dx,dy)` is
`base_height + slope_x*dx + slope_y*dy`. Every corner throughout the footprint
rectangle, including corners around unclaimed cells, must exactly equal this
plane and remain within 0–4. Thus a two-tile slope can rise two full levels.
Placed pieces tilt as a whole with this plane; they do not deform to fit uneven
terrain. Intrinsic geometry such as banking or an elevated road's deck is in
addition to its terrain support plane.

The codec validates IDs, fields, bounds, terrain support, and occupied-cell
overlap without loading the game's assets. Resource-aware consumers must also
check that each ID's supplied footprint and selected surface are supported by
their catalog. A serialized mask is not permission to remove collision from
an inherently occupied part of a catalog object.

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
and objects, sparse overlays, positive and negative slopes, ASCII limits,
transactional failure, and the maximum-size 90×90 map.
