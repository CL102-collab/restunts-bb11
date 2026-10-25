# Native desktop startup window

Desktop builds include `restunts-launcher` (`restunts-launcher.exe` on Windows).
Run it to select your writable original Stunts 1.1 game folder and startup
options, then choose **Start game**. The launcher validates the game's version before
starting the sibling `restunts` executable. Keep the complete runtime package
together. The original game data is supplied separately.

The launcher uses native Windows and Cocoa controls, GTK on Linux and the BSDs,
and wxQt with the Haiku Qt platform/style plugins on Haiku. The same C++ source
implements all seven desktop platforms. Web and Android keep their existing
startup interfaces; DOS builds do not depend on wxWidgets.

**New MIDI** starts enabled and **HyperVision** starts at **Auto**. **Fullscreen**
controls the game's initial window state. Selections, including the game folder,
are saved per user on Start. Cancel leaves saved preferences unchanged. The game
runs as a separate process and remains running after the launcher closes. The
AppImage hides the startup window and waits for the game to exit so its mounted
assets remain available.

Use the launcher from your desktop application menu where installed. macOS
packages include `Restunts.app`; keep it beside the package's `bin`, `lib` and
`share` directories. `run-restunts.sh` also opens the window with no arguments.
The AppImage opens the startup window with no arguments. Passing game arguments
to the AppImage or calling `bin/restunts` directly retains command-line use:

```sh
bin/restunts --data-dir /path/to/Stunts --fullscreen:on
```

`restunts-launcher --data-dir FOLDER` overrides the initial folder. `--game PATH`
selects another compatible game executable instead of the sibling executable.
`--wait-for-game` hides the window after Start and keeps the launcher process
alive until the game exits; the AppImage uses this mode automatically.

## Per-user settings

Preferences are stored in `launcher.ini` at these locations:

| Platform | Directory |
| --- | --- |
| Windows | `%APPDATA%\Restunts` |
| macOS | `~/Library/Application Support/Restunts` |
| Linux, FreeBSD, OpenBSD, NetBSD | `$XDG_CONFIG_HOME/restunts`, or `~/.config/restunts` |
| Haiku | The user's native settings directory, normally `/boot/home/config/settings/Restunts` |

All startup choices and the selected folder are saved on Start. Deleting this
file restores the initial defaults. The desktop launcher does not store preferences
in the game directory or change another user's choices. The game continues to
read and write tracks, replays and other game files in the selected game folder.

## Building

CMake enables `RESTUNTS_BUILD_LAUNCHER` by default for Windows, Linux, macOS,
FreeBSD, OpenBSD, NetBSD and Haiku game builds. A C++11 compiler is required.
Except on Haiku, CMake downloads the official wxWidgets 3.2.12 release archive
and verifies its SHA-256 hash. Only the static core/base libraries are linked,
with web, media, networking, OpenGL and optional image codecs disabled.
Haiku defaults to the HaikuPorts wxQt 3.2.4 system package, which carries
platform fixes absent from the upstream archive. Its core/base libraries remain
system runtime dependencies. The package includes the matching upstream license
texts downloaded from the official 3.2.4 tag with verified SHA-256 hashes.

Linux and BSD builds additionally need GTK 3 development packages, and users
need the matching GTK runtime. For example, install `libgtk-3-dev` on Debian,
`gtk3` on FreeBSD, `gtk3+` on NetBSD, or `gtk+3` on OpenBSD. Haiku builds need
`wxqt_devel`, `qt5_devel`, `libsdl_devel` and `qthaikuplugins`, including matching
Qt platform and style plugins at runtime. On the hybrid system use
`wxqt_x86_devel`, `qt5_x86_devel`, `libsdl_x86_devel` and `qthaikuplugins_x86`.
The dependency installer selects these packages automatically. macOS uses the Xcode SDK and does not
need an external GUI toolkit. The AppImage SDK selects GTK 2 to retain its
historical distribution baseline and bundles its GUI runtime dependencies.

Windows x86 release builds use Debian 12's GCC/G++ 12 MinGW-w64 win32-threading
MSVCRT toolchain. GCC 13's win32 C++ runtime imports Vista-only condition-variable
APIs even when XP macros are set. CMake audits each x86 launcher after linking
for XP-incompatible imports; no-SSE2 builds additionally reject SIMD instructions.
A failing audit fails the build. Windows x64 and ARM64 retain their existing
Windows 7 and Windows 10 baselines.

For an installed toolkit during local development, configure with
`-DRESTUNTS_SYSTEM_WX=ON`. This requires wxWidgets 3.2 or newer, and uses its
installed version and configuration. This is already the default on Haiku;
other release builds use the pinned source. `RESTUNTS_WX_TOOLKIT` can select a
specific bundled backend, for example `gtk2` for the historical AppImage SDK.
The optional `RESTUNTS_SYSTEM_WX=OFF` wxQt source build on Haiku has not been
validated and omits the HaikuPorts patches.

For a game/tools-only or headless development build, use
`-DRESTUNTS_BUILD_LAUNCHER=OFF`. This removes the C++ GUI dependency. Web, Android,
DOS, Amiga and standalone generator builds exclude the launcher automatically.

With native regressions enabled, CTest includes `launcher-options` and
`launcher-game-data`. They test settings/arguments and game-resource validation
without opening a display. A successful build still needs target-system GUI
verification for folder selection, DPI scaling, process startup and persistence.
The production launcher has been exercised on Linux and Windows XP SP3 with
QEMU's Pentium III CPU model (no SSE2), including native folder selection, process
arguments, saved preferences and Cancel. macOS, BSD and Haiku runtime validation
remains outstanding; shared-source support alone does not establish a successful
native GUI test on those systems.
