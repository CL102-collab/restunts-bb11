# Startup settings by platform

The ported game uses the same driving options on DOS16, Amiga, TempleOS,
SDL3 desktop builds (including DOS32), Android, and web. Original Stunts and
the preserved assembly executable retain their original interfaces.

| Setting | Parameter | Default |
| --- | --- | --- |
| Original power gear behavior | `--pg:on` / `--pg:off` | On |
| Original collision behavior | `--lc:on` / `--lc:off` | On |
| Original left corner bias | `--lcb:on` / `--lcb:off` | On |
| Original Amiga grass grip for stock cars | `/amigagrass` | Off |
| One Wheel On/Over Track rules | `--owoot` | Off |
| Skip intro | `--nointro` | Off |
| Disable music and sound effects | `/ns` | Off (sound on) |
| FPS display | `--fps:on` / `--fps:off` | Off |

Physics switches and FPS values are case-insensitive. For power gear,
collision, and corner bias, the last value wins. Supply the FPS option once;
invalid or repeated FPS options report an error. F11 toggles FPS during play.
Replays do not record driving options: use the same settings when recording
and playing them.

Custom-car surface adhesion activates automatically when the selected car has
a one-byte `fand` resource set to `1`. It is loaded independently for the player
and opponent on every ported platform. Absent, zero or invalid flags keep the
original physics; no command-line or launcher setting is required. An optional
`adhs` resource configures attachment distance, alignment angle and materials.
Defaults are 12 world units, 60 degrees, and paved/dirt/ice/grass; malformed
settings disable adhesion for that car. See [the resource format](car-adhesion.md)
for bounds, compatibility and measurement details.

## Launching

DOS16, Amiga, and SDL3 executables accept switches after the executable name:

```text
restunts --nointro --pg:off --lc:off --lcb:off /amigagrass --fps:on
```

`--help` / `-h` and `--version` / `-v` exit without opening game data. Use
`--data-dir DIR` as the first option on DOS16, Amiga, and SDL3 to select the
directory for game data and saves. Quote paths containing spaces. The browser
uses its folder picker; Android uses the game-folder selector and storage
controls. DOS16 paths follow the DOS filesystem's naming rules.

TempleOS accepts the same common switches in the fourth argument:

```c
Restunts("C:/DATA", FALSE, FALSE,
    "--pg:off --lc:off --lcb:off /amigagrass --fps:on");
Restunts("C:/DATA", FALSE, FALSE, "--help");
```

The existing second and third arguments still enable skipping the intro and
Amiga grass grip. `RestuntsArgs(directory, argc, argv)` accepts a native argument
vector, with the program name in `argv[0]`. See the [TempleOS guide](templeos.md).

Android and web expose all common settings above in their startup controls,
remember selections, and pass the corresponding parameters to the shared
engine. **Sound** enabled means `/ns` is omitted.

## Options backed by a particular platform

| Option | Platforms with the implementation |
| --- | --- |
| HyperVision `--hv:auto/full/high/medium/low` | All SDL3 builds, including Android, web, and DOS32. Classic ports use their native renderer and F12 SuperSight. |
| Ogg replacement music `--ogg:on/off` | SDL3 builds. Android/web label this **New MIDI**. Browser default is on; native default is off. |
| Touch control layout `--control-layout:on/off` | Android and web. |
| Tilt steering | Android launcher and sensor backend. |
| DOS audio driver `/sXX` | DOS16: AD, PC, MT, TD; SB selects AdLib FM. Other ports use their native audio backend. |
| Hercules/mode-4 request `/h` | DOS16. Other ports use their native display backend. |
| Dependency notices `--licenses` | SDL3 builds except DOS32. |

The legacy `/nd` switch has no effect. Compatibility handling of `/sXX` and
`/h` on other ports does not add alternate hardware drivers there. Features
without a platform backend are not offered as working launcher choices.

See [Android](android.md), [web](wasm.md), [Amiga](amiga.md), and
[SDL3](sdl3.md) for platform controls and build instructions.
