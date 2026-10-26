# Startup settings by platform

The ported game uses the same driving options on DOS16, Amiga, TempleOS,
SDL3 desktop builds (including DOS32), Android, and web. Original Stunts and
the preserved assembly executable retain their original interfaces.

| Setting | Parameter | Default |
| --- | --- | --- |
| Original power gear behavior | `--pg:on` / `--pg:off` | On |
| Original collision behavior | `--lc:on` / `--lc:off` | On |
| Original left corner bias | `--lcb:on` / `--lcb:off` | On |
| Dual-Way Switching | `--dws:on` / `--dws:off` | On |
| Chicane Fast-Grass | `--cfg:on` / `--cfg:off` | On |
| Original Amiga grass grip for stock cars | `--amigagrass` | Off |
| One Wheel On/Over Track rules | `--owoot` | Off |
| Skip intro | `--nointro` | Off |
| Disable music and sound effects | `/ns` | Off (sound on) |
| FPS display | `--fps:on` / `--fps:off` | Off |

Physics switches and FPS values are case-insensitive. For power gear,
collision, corner bias, dual-way switching, and chicane fast-grass, the last value wins. Supply the
FPS option once; invalid or repeated FPS options report an error. F11 toggles FPS
during play.
Replays do not record driving options: use the same settings when recording
and playing them.

**Dual-Way Switching** preserves the original penalty-time behavior when on.
Use `--dws:off` to charge skipped elements when switching between branches of a
dual-way track. See [Penalty time](https://wiki.stunts.hu/wiki/Penalty_time) and
[Dual-way switching](https://wiki.stunts.hu/wiki/Dual-way_switching).

**Chicane Fast-Grass** keeps the green areas of chicane elements as fast as
road. Use `--cfg:off` to give those areas ordinary grass grip and drag while
keeping the visible road unchanged. `--amigagrass` also applies to those grass
contacts when enabled.

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
restunts --nointro --pg:off --lc:off --lcb:off --dws:off --cfg:off --amigagrass --fps:on
```

`--help` / `-h` and `--version` / `-v` exit without opening game data. Use
`--data-dir DIR` as the first option on DOS16, Amiga, and SDL3 to select the
directory for game data and saves. Quote paths containing spaces. The browser
uses its folder picker; Android uses the game-folder selector and storage
controls. DOS16 paths follow the DOS filesystem's naming rules.

TempleOS accepts the same common switches in the fourth argument:

```c
Restunts("C:/DATA", FALSE, FALSE,
    "--pg:off --lc:off --lcb:off --dws:off --cfg:off --amigagrass --fps:on");
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
| Ogg replacement music `--ogg:on/off` | SDL3 builds. Android/web label this **New MIDI**. Web and desktop launcher default to on; direct native CLI defaults to off. |
| Initial fullscreen `--fullscreen:on/off` | Windows, Linux, macOS, FreeBSD, OpenBSD, NetBSD, and Haiku SDL3 builds. Defaults to off. |
| Touch control layout `--control-layout:on/off` | Android and web. |
| Tilt steering | Android launcher and sensor backend. |
| DOS audio driver `/sXX` | DOS16: AD, PC, MT, TD; SB selects AdLib FM. Other ports use their native audio backend. |
| Hercules/mode-4 request `/h` | DOS16. Other ports use their native display backend. |
| Dependency notices `--licenses` | SDL3 builds except DOS32. |

The legacy `/nd` switch has no effect. Compatibility handling of `/sXX` and
`/h` on other ports does not add alternate hardware drivers there. Features
without a platform backend are not offered as working launcher choices.

Desktop fullscreen names and values are case-insensitive. Supply this option
once; unknown values and repeated options report an error before creating the
game window. `--fullscreen:on` starts the first window in fullscreen, and
`--fullscreen:off` starts windowed. `RESTUNTS_BORDERLESS=1` still selects the
window style restored when leaving fullscreen. Alt+Enter toggles fullscreen
during play. The game ignores this option on SDL3 DOS32, Android, browser and
non-SDL3 builds, including invalid or repeated values. DOS32 always uses its
native fullscreen video modes; other ports keep their existing display behavior.

The web startup page has a separate **Fullscreen** checkbox, initially off. It
remembers your choice and requests browser fullscreen directly from the
**Start game** click. It does not pass a fullscreen argument to the game. If the
browser refuses fullscreen, the game continues in the page; the manual
**Fullscreen** button and **Alt+Enter** remain available.

See [Android](android.md), [web](wasm.md), [Amiga](amiga.md), and
[SDL3](sdl3.md) for platform controls and build instructions.
