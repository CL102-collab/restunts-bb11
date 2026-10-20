# Trackdata

Trackdata is a set of 23 runtime data structures containing information about the current track and the lap being driven or replayed. Individual structures are named `trackdataXX`, where `XX` is a two-digit number. Their documented organization is chiefly based on the [restunts project](https://wiki.stunts.hu/wiki/Restunts), with some parts also discovered independently.

Adapted from [Trackdata on Stunts Wiki](https://wiki.stunts.hu/wiki/Trackdata), [revision 4417](https://wiki.stunts.hu/index.php?title=Trackdata&oldid=4417), accessed on 2026-10-20. The article was last edited on 2021-02-23.

## Finding Trackdata in memory

The most convenient structures to search for in a debugger are:

- `trackdata11`: the highscore table.
- `trackdata13`: the replay file header; see [Replay File Format](replay.md).
- `trackdata14`: the element map stored in the track file.

Different parts of Trackdata may be loaded depending on the current game phase: before a lap, while driving, during a replay, or after a lap.

## Data structures

The offsets and byte sizes below are those published in the wiki for the **1991 DOS versions of Stunts**. Hexadecimal values use the `0x` prefix; decimal sizes are in bytes. A word occupies two bytes. Undocumented fields and tentative interpretations remain marked as such.

| Structure | Offset (hex) | Size (hex) | Size (bytes) | Layout | Description | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| `trackdata01` | `0x0000` | `0x070A` | 1802 | 901 words | Track continuity: each value is the index of the next element in driving order. Alternative dual-way paths follow the main path. | The 901-value capacity of the driving-order structures is reflected in the "too many track pieces" validation error. |
| `trackdata02` | `0x070A` | `0x070A` | 1802 | 901 words | Split points: at a dual-way split, the value is the driving-order index of the first element of the alternative path. | |
| `trackdata03` | `0x0E14` | `0x070A` | 1802 | 901 words | Opponent path: the sequence of elements to be driven by the opponent, starting at `0x0`, the start/finish line. | Occasionally changes when restarting a race or reloading a replay. |
| `trackdata04` | `0x151E` | `0x0080` | 128 | 64 words | Player aerodynamic resistance lookup table, indexed by `speed_mph >> 2`. | |
| `trackdata05` | `0x159E` | `0x0080` | 128 | 64 words | Opponent aerodynamic resistance lookup table, indexed by `speed_mph >> 2`. | |
| `trackdata06` | `0x161E` | `0x0080` | 128 | Not documented | Not documented. | |
| `trackdata07` | `0x169E` | `0x0080` | 128 | 64 words | Not documented. | |
| `trackdata08` | `0x171E` | `0x0060` | 96 | 48 words? | Not documented. | Layout is tentative. |
| `trackdata09` | `0x18FE` | `0x0180` | 384 | 64 3D word vectors | F4 camera positions, in absolute coordinates and graphical units. | |
| `trackdata10` | `0x1A1E` | `0x0120` | 288 | 48 3D word vectors | Corner sign positions, in absolute coordinates and graphical units. | |
| `trackdata11` | `0x1B8A` | `0x016C` | 364 | [Highscore File Format](https://wiki.stunts.hu/wiki/Highscore_File_Format) | Highscore table. | |
| `trackdata12` | `0x1C7A` | `0x00F0` | 240 | Not documented | Not documented. | |
| `trackdata13` | `0x1C9A` | `0x001A` | 26 | [Replay File Format](replay.md) | Replay header. | |
| `trackdata14` | `0x2019` | `0x0385` | 901 | [Track File Format](https://wiki.stunts.hu/wiki/Track_File_Format) | Track file element map, plus the scenery byte. | Appears to control only graphical rendering, rather than the physical model. |
| `trackdata15` | `0x239E` | `0x0385` | 901 | [Track File Format](https://wiki.stunts.hu/wiki/Track_File_Format) | Track file terrain map, plus the padding byte. | |
| `trackdata16` | `0x527E` | `0x2EE0` | 12000 | [Replay File Format](replay.md) | Replay input buffer. | Identified by the article as the safe memory area used by [Bliss](https://wiki.stunts.hu/wiki/Bliss) track metadata. |
| `trackdata17` | `0x5603` | `0x0385` | 901 | 901 bytes | Track elements in driving order. | |
| `trackdata18` | `0x5988` | `0x0385` | 901 | 901 bytes | Not documented. | Likely related to element connectivity checks; values appear to be bit flags. |
| `trackdata19` | `0x5D0D` | `0x0385` | 901 | Track file-like map | Indices into `trackdata10` for corner signs. | |
| `trackdata20` | `0x64B9` | `0x07AC` | 1964 | Not documented | Not documented. | The first 1802 bytes are a copy of the track file. |
| `trackdata21` | `0x683E` | `0x0385` | 901 | 901 bytes | Grid column of track elements in driving order. | |
| `trackdata22` | `0x6BC3` | `0x0385` | 901 | 901 bytes | Grid row of track elements in driving order. | |
| `trackdata23` | `0x6BF3` | `0x0030` | 48 | 48 bytes? | Not documented. | Layout is tentative. |

Offsets are retained as published, including apparent gaps and overlaps; they should not be reconstructed by summing the preceding sizes.

## Attribution

This document adapts the Stunts Wiki contributors' Trackdata article into Markdown, with local documentation links, explicit hexadecimal notation, and labels for undocumented fields. The source content is available under [Creative Commons Attribution-NonCommercial-ShareAlike 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/); this adaptation is provided under the same license.
