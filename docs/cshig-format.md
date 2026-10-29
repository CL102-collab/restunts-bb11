# Chocolate Stunts highscore format, version 1

`.cshig` stores the seven highscores for an SDL3 `.cstrk` track. Legacy tracks
continue to use the original `.hig` format. A `.hig` file contains only its seven
score records; it does not embed track data.

A `.cshig` file contains a 32-byte header, a canonical `.cstrk` gameplay identity,
and seven 58-byte score records. All integers are unsigned little endian. There
is no padding or trailing data. The codec serializes fields explicitly rather
than writing C structures.

| Header offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 8 | Signature: `CSHIG\x1a\r\n` |
| 8 | 2 | Version, 1 |
| 10 | 2 | Header size, 32 |
| 12 | 4 | Flags, zero |
| 16 | 4 | Canonical embedded track size |
| 20 | 2 | Record count, 7 |
| 22 | 2 | Record size, 58 |
| 24 | 4 | Payload size: track size + 406 |
| 28 | 4 | Reserved, zero |

The gameplay identity is a complete valid CSTRK file with its revision normalized
to the current track revision v3, its cosmetic horizon reset to Desert, and
its notes removed: notes length is zero, notes bytes are absent, and payload
size is adjusted. Older extended-track revisions are unsupported.
Its 16-byte serialized object records are sorted in ascending bytewise order;
all other terrain and object data are retained exactly. Therefore changes to
revision metadata, notes, horizon, or object ordering keep the same scores.
Changes to dimensions, terrain heights, either material in a surface corner,
corner direction, or any object record produce a different identity. Track filenames are not part of this
identity. The codec compares the canonical bytes directly, without relying on a
hash collision assumption. Semantically equivalent but differently encoded object directions
are still different identities.

Decoders validate the embedded CSTRK and require it to already have this
canonical representation. They reject noncanonical revisions, embedded notes,
nondefault horizons, unsorted object records, invalid metadata, nonzero reserved
fields, malformed records, inconsistent lengths, truncation, and trailing data.
Identity data is at most 154,113 bytes, making the complete file at most 154,551
bytes.

| Record offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 17 | Player name field |
| 17 | 24 | Car name field |
| 41 | 1 | Legacy car display flag |
| 42 | 8 | Opponent field |
| 50 | 8 | Time in twentieths of a second |

Each text field must contain a NUL byte within its fixed capacity. Other bytes
may use printable ASCII or the game's extended font: bytes 32–126 and 128–255.
Nonzero control bytes and DEL are rejected, including after an earlier NUL.
Multiple NUL-separated strings in a field are allowed for the legacy display
layout. The car flag is preserved as an uninterpreted byte.

Nonempty times are positive 64-bit values. `UINT64_MAX` marks an empty row;
zero is invalid. Records appear in nondecreasing time order, with empty rows
last. Equal times retain their existing order, and a new equal time ranks
after the existing equal times. Only the first seven qualifying records are
retained. New empty tables contain NUL-filled name fields and empty time
sentinels.

Times use a common 20 Hz unit regardless of simulation rate. A 20 Hz frame
count is stored unchanged; a 10 Hz count is multiplied by two using a 64-bit
intermediate. For example, `UINT32_MAX` frames at 10 Hz becomes 8,589,934,590
ticks, which exceeds a 32-bit field. The codec retains the full 64-bit range
below the empty sentinel.

`cshig.c` owns serialization, canonical identity comparison, ranking, and
insertion. Successfully decoded tables own their identity allocation. Failed
create, decode, encode, and insert operations preserve the previous table or
output arguments, allowing the game to report damaged files without replacing
them with a new empty table. Filesystem replacement and dialogs are handled by
the SDL3 runtime adapter, not this codec.
