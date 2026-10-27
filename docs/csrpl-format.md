# Chocolate Stunts replay format, version 1

`.csrpl` is an SDL3 format. Existing `.rpl` files retain their original layout and
recording behavior. A `.cstrk` race records `.csrpl`, with the complete track
embedded so subsequent track edits do not change replay playback.

All integers are unsigned little endian. The file is exactly a 64-byte header,
one complete `.cstrk` file, and one input byte per frame. There is no padding or
trailing data. The frame count is 32 bits, twice the original field's width.

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 8 | `CSRPL\r\n\x1a` signature |
| 8 | 2 | Version, 1 |
| 10 | 2 | Header size, 64 |
| 12 | 4 | Flags, zero |
| 16 | 4 | Embedded `.cstrk` size in bytes |
| 20 | 4 | Recorded frame count |
| 24 | 2 | Simulation rate, 10 or 20 frames/second |
| 26 | 2 | Reserved, zero |
| 28 | 4 | Player car resource ID |
| 32 | 1 | Player paint material |
| 33 | 1 | Player transmission, automatic 0 or manual 1 |
| 34 | 1 | Opponent type, 0 through 6 |
| 35 | 4 | Opponent car resource ID |
| 39 | 1 | Opponent paint material |
| 40 | 1 | Opponent transmission |
| 41 | 9 | Legacy display track name, byte array |
| 50 | 14 | Reserved, all zero |
| 64 | Track size | Complete validated `.cstrk` |
| 64 + track size | Frame count | Input samples |

Car IDs and the display name are fixed byte arrays, not C strings. Each sample
uses bits 0–5 for accelerate, brake, steer right, steer left, gear up, and gear
down. Bits 6–7 must be zero. Decoders reject unsupported versions, flags,
reserved bytes, rates, metadata, malformed tracks, mismatched lengths, and
unsupported input bits before returning a usable replay view.

`csrpl.c` implements serialization without relying on packed C structures.
`extended_replay.c` provides growing input storage, ordered rewind snapshots,
and 32-bit timeline calculations with 64-bit intermediates.
`extended_replay_runtime.c` integrates those facilities with SDL3 simulation.
It stores frame, recording, finish, and penalty clocks alongside each saved
`GAMESTATE`, preserving the legacy packed state and physics arithmetic.

There is no ten-minute recording window or thirty-second history deletion in
extended recordings. Available memory and the 32-bit frame count bound the
recording. At 20 Hz, the count represents approximately 6.8 years.
