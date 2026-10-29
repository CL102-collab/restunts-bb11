# Chocolate Stunts replay format

`.csrpl` is an SDL3 format. Existing `.rpl` files retain their original layout and
recording behavior. A `.cstrk` race records `.csrpl`, with the complete track
embedded so subsequent track edits do not change replay playback.

Each recording stores an unsigned 16-bit numeric **replay revision**. New
recordings currently use **v5**. The revision identifies the track-format and
physics behavior of the game that recorded the inputs; it is separate from the
revision stored in the embedded track.

The current replay revision is `CSTRK_VERSION + CSRPL_PHYSICS_REVISION`.
Increasing the track revision automatically advances the replay revision.
Increment the physics revision independently when simulation changes can alter
replay playback, keeping that increment when the track revision later advances.
The track and replay revision numbers can therefore diverge. A new recording
uses the running game's replay revision, including when its track was loaded
with a different track revision. Loading, seeking, branching, suspending for a
ghost, and resaving an existing replay retain its original recorded revision;
a fresh recording uses the current game revision.

Replay revisions older than the running game's current replay revision are
unsupported and rejected before activation, including existing v1–v4 recordings.
The game does not emulate older physics. Increasing either the track revision
or the physics revision also raises the minimum replay revision.

The game shows an OK-only warning when a selected replay or ghost has a newer
recorded revision than the current game. The warning names both revisions and
explains that physics and playback may differ, then allows continuing on a
best-effort basis using the known layout. Matching revisions do not show a
warning. The embedded track is validated separately under the track format's
revision rules.

All integers are unsigned little endian. The file is exactly a 64-byte header,
one complete `.cstrk` file, and one input byte per frame. There is no padding or
trailing data. The frame count is 32 bits, twice the original field's width.

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 8 | `CSRPL\r\n\x1a` signature |
| 8 | 2 | Recorded replay revision, 5–65,535; current and minimum revision `5` |
| 10 | 2 | Header size, 64 |
| 12 | 4 | Flags, zero |
| 16 | 4 | Embedded `.cstrk` size in bytes |
| 20 | 4 | Recorded frame count |
| 24 | 2 | Simulation rate, 10 or 20 frames/second |
| 26 | 2 | Recorded driving-rule bitmask, described below |
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

The driving-rule bitmask records the startup configuration used when the
recording began. A set bit means **On** and a clear bit means **Off**. Zero means
all seven driving rules were off. No setting is inferred from the replay's
revision or from the currently running game.

| Bit | Mask | Driving rule |
| --- | --- | --- |
| 0 | `0x0001` | Power Gear |
| 1 | `0x0002` | Original Collision Bugs |
| 2 | `0x0004` | Left Corner Bias |
| 3 | `0x0008` | Dual-Way Switching |
| 4 | `0x0010` | Chicane Fast-Grass |
| 5 | `0x0020` | Amiga Grass |
| 6 | `0x0040` | OWOOT |
| 7–15 | `0xff80` | Reserved; must be zero |

The normal startup defaults produce `0x001f`: the first five rules are on,
and Amiga Grass and OWOOT are off. All 128 combinations of the seven rules
are valid. Unknown rule bits are rejected before a replay is activated.

Selecting a replay or ghost recorded with different driving rules shows an
OK-only warning. It lists all seven recorded rules in the order above, each
marked On or Off, and explains that playback may desync. Matching rule sets
produce no rule warning. The game continues using its existing startup rules;
it does not apply the replay's settings. A newer replay revision can also
produce the separate revision warning.

Loading, seeking, branching, suspending for a ghost, and resaving retain the
recorded rule set, even if the running game has different rules. Starting a
fresh recording captures the current startup configuration.

Car IDs and the display name are fixed byte arrays, not C strings. Each sample
uses bits 0–5 for accelerate, brake, steer right, steer left, gear up, and gear
down. Bits 6–7 must be zero. Older replay revisions are rejected. Decoders
retain the recorded revision and driving rules and reject unsupported flags,
unknown rule bits, nonzero reserved bytes, rates, metadata, malformed tracks,
mismatched lengths, and unsupported input bits before returning a usable replay
view. Encoding a decoded replay preserves its recorded revision and rules
without relabeling its inputs as a new recording.

`csrpl.c` implements serialization without relying on packed C structures.
`extended_replay.c` provides growing input storage, ordered rewind snapshots,
and 32-bit timeline calculations with 64-bit intermediates.
`extended_replay_runtime.c` integrates those facilities with SDL3 simulation.
It stores frame, recording, finish, and penalty clocks alongside each saved
`GAMESTATE`, preserving the legacy packed state and physics arithmetic.

There is no ten-minute recording window or thirty-second history deletion in
extended recordings. Available memory and the 32-bit frame count bound the
recording. At 20 Hz, the count represents approximately 6.8 years.
