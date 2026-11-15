# Chocolate Stunts replay format

`.csrpl` is an SDL3 format. Existing `.rpl` files retain their original layout and
recording behavior. A `.cstrk` race records `.csrpl`, with the complete track
embedded so subsequent track edits do not change replay playback. Races against
UNIT also record `.csrpl` on classic `.trk` tracks. Those recordings embed the
original 1,802-byte classic track and retain classic track physics.

Each recording stores an unsigned 16-bit numeric **replay revision**. New
recordings currently use **v21**. The revision identifies the track format,
physics behavior, and filename metadata layout of the game that recorded the
inputs; it is separate from the revision stored in the embedded track.

The current replay revision is
`CSTRK_VERSION + CSRPL_PHYSICS_REVISION + CSRPL_FILENAME_REVISION + CSRPL_OPPONENT_REVISION`.
Increasing the track revision automatically advances the replay revision.
Increment the physics revision independently when simulation changes can alter
replay playback, keeping that increment when the track revision later advances.
Filename metadata and opponent recording have independent revision offsets.
The track and replay revision numbers can therefore diverge. The current track
revision is v15, physics revision is 4, filename revision is 1, and opponent
revision is 1. Physics revision 1 corrected wheel contact at consecutive hill
slopes in replay v5; replay v6 also supports the
sloped tunnels and slaloms introduced by track v5. Replay v7 supports
generalized straight-slope placements introduced by track v6. Physics revision 2
corrects opposite-direction hill-road classification and body contacts on graded
track pieces in replay v8. Replay v9 adds the Tunnel with slalom element
introduced by track v7. Replay v10 adds the Barrel roll ramp and its
contact-derived airborne roll momentum, introduced by track v8. Physics revision 3
in replay v11 requires OWOOT barrel-roll attempts to pass the ordered airborne
roll milestones before touchdown. Native physics decides whether the landing
survives; OWOOT adds no separate attitude requirement. Receiving-road tire contact
counts before the car center reaches the road, and a completed airborne roll
remains complete through later rotation. Physics revision 4 in replay
v12 retains fractional wheel coordinates in solid contact checks and resolves
mesh-road contact against the same exact triangles. Drivable tops and internal
mesh seams use road response while sides, undersides and supports remain solid,
allowing valid barrel-ramp approaches with fixed collision (`--lc:off`). A new
recording uses the running game's replay revision, including when its track was
loaded with a different track revision. Replay v13 adds the pine tree group
scenery introduced by track v9. Replay v14 adds the six-tree groves introduced
by track v10. Both pine groups derive positions and sizes deterministically from
track tile coordinates and tree index, shared by rendering and collision checks.
Replay v15 adds the drive-through Wheat field scenery introduced by track v11.
Replay v16 adds the Tulip field and Vine field scenery introduced by track v12.
Replay v17 expands the stored track name to thirteen bytes, retaining twelve-character
filenames plus a NUL terminator. This metadata change does not change replay physics.
Replay v18 adds the Blue and Red checkpoint gates and finish requirement
introduced by track v13. Checkpoint progress is rebuilt during playback and
restored by seeking, rewind, and ghost suspension.
Replay v19 adds track v14's combined Blue / red checkpoints. A shared gate
counts in both color sets, and its progress follows the same replay rules.
Replay v20 adds track v15's optional custom horizon filename. The PNG remains
an external file in `horizons/`; missing or invalid artwork uses the embedded
track's built-in fallback without changing playback or discarding the filename.
Replay v21 adds four-byte opponent identifiers, recorded opponent commands,
and the option to embed a classic track. The original six opponents retain their
existing input-only replay behavior.
Loading, seeking, branching, suspending for a ghost, and resaving an existing
replay retain its original recorded revision; a fresh recording uses the current
game revision.

Replay revisions v1–v10 remain unsupported and are rejected before activation.
Version v11 remains supported with its historical solid-contact precision and
wheel-plane response. New recordings use v12's precise contacts; loading,
seeking, branching and resaving v11 retain v11 behavior. The minimum supported
revision and the v12 physics cutoff are fixed compatibility boundaries, separate
from the current recording revision.

The game shows an OK-only warning when a selected replay or ghost has a newer
recorded revision than the current game. The warning names both revisions and
explains that physics and playback may differ, then allows continuing on a
best-effort basis using the known layout. Matching revisions do not show a
warning. The embedded track is validated separately under the track format's
revision rules.

Integers are little endian. Header fields are unsigned; opponent commands have
the signed fields documented below. The file is exactly a 64-byte header, one
embedded track, one player input byte per frame, and, when enabled, one eight-byte
opponent command per frame. There is no padding or trailing data. The frame count
is 32 bits, twice the original field's width.

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 8 | `CSRPL\r\n\x1a` signature |
| 8 | 2 | Recorded replay revision, 11–65,535; current revision `21`, minimum `11` |
| 10 | 2 | Header size, 64 |
| 12 | 4 | Format flags, described below (zero before v21) |
| 16 | 4 | Embedded track size in bytes |
| 20 | 4 | Recorded frame count |
| 24 | 2 | Simulation rate, 10 or 20 frames/second |
| 26 | 2 | Recorded driving-rule bitmask, described below |
| 28 | 4 | Player car resource ID |
| 32 | 1 | Player paint material |
| 33 | 1 | Player transmission, automatic 0 or manual 1 |
| 34 | 1 | Opponent number; 0 means none, 1–6 are original opponents, 7 is UNIT |
| 35 | 4 | Opponent car resource ID |
| 39 | 1 | Opponent paint material |
| 40 | 1 | Opponent transmission |
| 41 | 13 | Display track name, byte array (v17+) |
| 54 | 4 | Four-character opponent identifier when opponent number >6 (v21+); otherwise zero |
| 58 | 6 | Reserved, all zero (v21+) |
| 64 | Track size | Complete validated `.cstrk`, or a 1,802-byte classic track |
| 64 + track size | Frame count | Player input samples |
| 64 + track size + frame count | 8 × frame count | Opponent commands, only when the opponent-command flag is set |

Revisions v11 through v16 retain their original nine-byte track-name field at
bytes 41–49 and fourteen reserved zero bytes at 50–63. Revisions v17–v20
use bytes 41–53 for the name and reserve bytes 54–63. Revision v21 retains
the name layout and uses bytes 54–57 for the optional opponent identifier.
The header remains exactly 64 bytes, so the embedded track and input offsets do
not move. The reader selects
and validates the reserved range using the recorded replay revision.

In memory, older names are zero-extended to thirteen bytes. Encoding an older
revision rejects a name with nonzero bytes beyond its original nine-byte field;
it never silently truncates the name. Existing nine-byte names without a NUL
remain valid byte arrays and round-trip unchanged. Normal new filenames use up
to twelve characters followed by a NUL. This metadata expansion does not change
the original packed `GAMEINFO` name field or the legacy `.rpl` format.

The format flags at byte 12 are independent of the driving-rule bitmask:

| Bit | Mask | Meaning |
| --- | --- | --- |
| 0 | `0x00000001` | Embedded track is the original 1,802-byte classic track layout |
| 1 | `0x00000002` | An eight-byte opponent command follows for each recorded frame |
| 2–31 | `0xfffffffc` | Reserved; must be zero |

Both flags require revision v21 or later. Without the classic-track flag, the
embedded payload must be a complete validated `.cstrk`. Classic payloads retain
their original bytes; the loader does not convert them into extended tracks.

An opponent number greater than six requires a four-character identifier and
an opponent command stream. UNIT uses number 7 and identifier `UNIT`. The
identifier is a fixed four-byte array of uppercase ASCII letters or digits,
like a car ID, without a NUL terminator. Original opponents and races without
an opponent keep these bytes zero and do not have an opponent command stream.

Each opponent command records the decisions actually applied in that simulation
frame, after planning and collision avoidance. Commands are serialized field by
field, independent of C structure padding:

| Offset within command | Bytes | Meaning |
| --- | --- | --- |
| 0 | 1 | Input bits 0–5, with the same meanings as player inputs |
| 1 | 1 | Reserved flags, zero |
| 2 | 2 | Signed absolute steering angle, −240 through 240 |
| 4 | 2 | Signed route index, zero or greater |
| 6 | 2 | Route point index, zero through 255 |

The command count equals the player frame count. Playback, seeking, and ghost
extraction consume these recorded decisions without rerunning the planner.
Consequently, changing the available CPU count or the planner's timing cannot
change the replay's opponent decisions. Branching truncates both streams at the
same frame; suspending for a ghost and resuming retain both streams and the
opponent identifier.

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

Car IDs, opponent identifiers, and the display name are fixed byte arrays, not C
strings. Each player input sample uses bits 0–5 for accelerate, brake, steer
right, steer left, gear up, and gear
down. Bits 6–7 must be zero. Older replay revisions are rejected. Decoders
retain the recorded revision and driving rules and reject unsupported flags,
unknown rule bits, nonzero reserved bytes, rates, metadata, malformed tracks,
mismatched lengths, invalid opponent commands, and unsupported input bits before
returning a usable replay view. Encoding a decoded replay preserves its recorded
revision and rules without relabeling its inputs as a new recording.

The C codec and runtime sources live in `src/restunts/c/extended/`.
`extended_csrpl.c` implements serialization without relying on packed C structures.
`extended_replay.c` provides growing input storage, ordered rewind snapshots,
and 32-bit timeline calculations with 64-bit intermediates.
`extended_replay_runtime.c` integrates those facilities with SDL3 simulation.
It stores frame, recording, finish, and penalty clocks alongside each saved
`GAMESTATE`, preserving the legacy packed state and physics arithmetic.

There is no ten-minute recording window or thirty-second history deletion in
extended recordings. Available memory and the 32-bit frame count bound the
recording. At 20 Hz, the count represents approximately 6.8 years.
