# Genghost for Android

The ARMv7 debug APK installs **Genghost**, an interface for the same headless
offline optimizer as the desktop tool. It supports Android 5.0 (API 21) or
later on devices that can run 32-bit ARM apps. Devices that support only
64-bit apps cannot install this build. No game renderer or original game
resources are included, and the app does not request Internet access.

## Generate a ghost

1. Install `genghost-android-armv7-debug.apk` and open **Genghost**.
2. Choose a folder containing your original game resources, car resources,
   and `.TRK` or `.RPL` files. Grant the requested folder access. The app
   stages the files in private storage for the native engine.
3. Select the track/replay and car, then choose the search time (30 minutes
   by default), transmission, and physics options.
4. Start the search. Progress appears in the app and an ongoing notification.
   You can leave the interface while the foreground service works. Cancel
   asks the generator to preserve its best completed replay.
5. Export the resulting `.RPL` and its JSON report. Completed files are also
   published to the selected game folder when its storage provider allows it.

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

Install the updated APK over the previous build to keep the selected folder
and settings. It uses the same application ID and debug signing certificate.
A physical Galaxy S5 is still needed to confirm the device-specific artifacts
are gone; native engine tests do not validate screen rendering.

## Build

Install Java 17, Python 3, and the Android SDK components pinned in
`android/toolchain.properties`. Set `JAVA_HOME` and `ANDROID_HOME`, then run:

```sh
tools/scripts/build-genghost-android.sh --no-daemon
```

The APK is `android/genghost/build/outputs/apk/debug/genghost-debug.apk`.
Use `-PrestuntsAndroidBuildRoot=/absolute/build/path` to place build output
elsewhere; the APK is then under `genghost/build/outputs/apk/debug/` there.
The build downloads the pinned, checksum-verified SDL source and its Gradle
wrapper. Gradle resolves its pinned Android plugin through the configured
repositories. A local Android debug key signs the APK.

The CMake option `RESTUNTS_GENGHOST_ONLY=ON` builds the generator independently
of the game. Android packages it as `lib/armeabi-v7a/libgenghost_exec.so`, a
32-bit ARM PIE executable with statically linked SDL and C++ runtime. It is
extracted to Android's native library directory and run as a separate child
process. Each search gets fresh simulator globals. The Activity and foreground
service use Java, with no SDL Activity, JNI simulation bridge, or rendering.
