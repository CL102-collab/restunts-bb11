# Genghost for Android

The Android APKs install **Genghost**, an interface for the same headless
offline optimizer as the desktop tool. Choose **android-armv7** for 32-bit ARM
(`armeabi-v7a`), including the Galaxy S5, or **android-arm64** for 64-bit ARM
(`arm64-v8a`), including devices that support only 64-bit apps. Both require
Android 5.0 (API 21) or later. Each APK contains only its selected architecture.
No game renderer or original game resources are included, and the app does not
request Internet access. The Tesla Roadster and McMurtry custom cars are bundled
in the app's private `cars/` folder. Additional car resources can remain loose in
your game folder or go in its `cars/` subfolder; Refresh discovers all three locations.

## Generate a ghost

1. Install the Genghost APK matching your Android ARM architecture and open
   **Genghost**.
2. Choose a folder containing your original game resources, car resources,
   and `.TRK`, `.RPL`, `.CSTRK` or `.CSRPL` files. Grant the requested folder access. The app
   stages the files in private storage for the native engine.
3. Select the track/replay and Search mode, then choose the search time (30 minutes
   by default), CPU core count, search method, and physics options.
   The core count defaults to `1`; `0` uses automatic CPU selection.
   **Route search** is the default. **State search (preview)** explores driving
   states with a shared search across the selected cores. The app remembers
   your selection.
   Choose **Generate replay** and select a car to search from a track or replay.
   For a completed `.RPL` or `.CSRPL`, choose **Fine-tune replay** to refine all driving
   inputs, or **Fine-tune gears only** to optimize shift timing while preserving
   throttle, braking, steering, and the complete input timeline. The single mode
   selector makes these choices mutually exclusive. Both fine-tuning modes use
   the replay's car automatically, so the car selector is hidden.
   All generated replays use manual transmission. Fine-tuning converts
   an automatic replay's shifts before optimizing earlier and later
   upshifts and downshifts. The engine verifies the replay under your selected
   driving rules before refinement. Fine-tuning is unavailable for track-only `.TRK` and `.CSTRK` files.
   The app saves a separate replay and reports any time saved; if no faster replay
   is found, it keeps the original time.
4. Start the search. Progress appears in the app and an ongoing notification.
   You can leave the interface while the foreground service works. Cancel
   asks the generator to preserve its best completed replay.
5. Export the resulting `.RPL` or `.CSRPL` and its JSON report. Completed files are also
   published to the selected game folder when its storage provider allows it.

The **Driving rules** checkboxes stay together in a vertical stack, one per
row, without horizontal scrolling: **Power Gear**, **Original Collision Bugs**,
**Left Corner Bias**, **Dual-Way Switching**, **Chicane Fast-Grass**,
**Amiga Grass**, and **OWOOT**. The first five default to enabled; OWOOT and
Amiga Grass default to disabled. All seven selections are remembered.
Uncheck **Chicane Fast-Grass** to use `--cfg:off`, giving green chicane areas
ordinary grass grip and drag. The report records this choice for replay
verification, including after an interrupted search. **Amiga Grass** uses
the original Amiga grass grip for stock cars, and is included in the report and
in verification after an interrupted search.

The app requires the same resource files as the desktop tool. A replay can
supply its embedded track, including when its car differs from the selected
car. Refresh the folder after adding tracks or cars. Play generated replays
in Restunts with the physics options recorded in the JSON report; the standard
replay format cannot store those options. See [the optimizer documentation](genghost.md)
for the search algorithm and its limits.

Long searches keep the CPU awake while running. Android or the device vendor
can still stop background work; the best completed files saved so far remain
in private storage. A search without a valid finish produces no completed ghost.
The debug package keeps native debug symbols while compiling the C engine
with optimization for search throughput.

## Display compatibility

Version 1.0.1 introduced software drawing for the launcher window and an opaque
background for both the scrolling viewport and its contents. This avoids the
hardware redraw path associated with smeared text on older Android 9 devices,
including the reported Galaxy S5, and paints exposed pixels during startup
and scrolling. Version 1.0.2 retains this display fix and includes the
[Ayrton DEFAULT search improvements](genghost-default-study.md). Version 1.0.3
also includes the [complete stunt tactic catalog](genghost-stunt-catalog.md),
with collision-aware shortcuts as part of the default search.
Version 1.0.4 adds the CPU core control and process-isolated parallel search.
Version 1.0.5 adds the state search preview alongside the existing route search.
Version 1.0.6 corrects slalom center clearance and steering; state jump proposals
require actual native airborne contact loss and remain disabled under OWOOT.
Version 1.0.7 adds the optional Fine-tune replay control for completed replays.
Both search methods use the same native physics and respect the selected rules.

Install the updated APK over the previous build to keep the selected folder
and settings. It uses the same application ID and debug signing certificate.
A physical Galaxy S5 is still needed to confirm the device-specific artifacts
are gone; native engine tests do not validate screen rendering.

CSTRK tracks produce CSRPL replays containing the full track. CSRPL selection
restores its recorded rules, car and 10/20 Hz timing; changes to the rule controls
are passed explicitly to the generator. Extended searches support up to 120,000
inputs and the same slopes and stunt modifiers as the desktop tool. See
[extended tracks and stunts](genghost.md#extended-tracks-and-stunts).

## Build

Install Java 17, Python 3, and the Android SDK components pinned in
`android/toolchain.properties`. Set `JAVA_HOME` and `ANDROID_HOME`, then run:

```sh
tools/scripts/build-genghost-android.sh android-armv7 --no-daemon
tools/scripts/build-genghost-android.sh android-arm64 --no-daemon
```

Omit the target (or use `all`) to build both debug APKs. The outputs are:

- `android/genghost/build/outputs/apk/armv7/debug/genghost-armv7-debug.apk`
- `android/genghost/build/outputs/apk/arm64/debug/genghost-arm64-debug.apk`

Use `-PrestuntsAndroidBuildRoot=/absolute/build/path` to place build output
elsewhere; the APKs are then under `genghost/build/outputs/apk/<flavor>/debug/`
there. The build downloads the pinned, checksum-verified SDL source and its
Gradle wrapper. Gradle resolves its pinned Android plugin through the configured
repositories. A local Android debug key signs debug APKs.

For signed release builds, set `ANDROID_KEYSTORE_FILE`, `ANDROID_KEYSTORE_PASSWORD`,
`ANDROID_KEY_ALIAS`, and `ANDROID_KEY_PASSWORD` to the same persistent release key
used for Chocolate Stunts. Then run:

```sh
tools/scripts/build-genghost-android.sh all --release
```

The helper requires every signing setting and a readable keystore before building.
It produces non-debuggable, signed APKs under `outputs/apk/<flavor>/release/`,
named `genghost-<flavor>-release.apk`; it does not fall back to a debug or unsigned
build. `RESTUNTS_ANDROID_BUILD_TYPE=release` selects the same release mode.
Keep using the same signing key to install updates without removing app data.

The shared PR/release Android CI matrix builds both Chocolate Stunts and Genghost
for **android-armv7** and **android-arm64**. Each ABI's `restunts-android-<cpu>.zip`
contains `bin/restunts.apk` and `bin/genghost.apk`. PR validation builds debug APKs;
the trusted Release workflow signs both apps with the configured release key.
Package checks verify signatures, SDK levels, native ABI, dependency notices and
exclusion of original game resources for both APKs.

The CMake option `RESTUNTS_GENGHOST_ONLY=ON` builds the generator independently
of the game. Android packages its native worker as
`lib/<abi>/libgenghost_exec.so`, a PIE executable with statically linked SDL and
C++ runtime. CMake keeps its output in a separate directory for each ABI and
variant so Gradle includes exactly the matching worker. It is extracted to
Android's native library directory and run as a separate child process. Each
search gets fresh simulator globals. The Activity and foreground service use
Java, with no SDL Activity, JNI simulation bridge, or rendering.
