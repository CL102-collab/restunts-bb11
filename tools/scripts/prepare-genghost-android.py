#!/usr/bin/env python3
"""Prepare only the pinned SDL source, Gradle wrapper, and genghost notices."""
import importlib.util
import os
from pathlib import Path
import re
import shutil


def prepare(root):
    helper = root / "tools/scripts/prepare-android.py"
    spec = importlib.util.spec_from_file_location("prepare_android", helper)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    sdl = module.dependency(root, "SDL3", "CMakeLists.txt", "sdl")
    wrapper = root / "out/android-gradle"
    wrapper.mkdir(parents=True, exist_ok=True)
    shutil.copy2(sdl / "android-project/gradlew", wrapper / "gradlew")
    (wrapper / "gradlew").chmod(0o755)
    shutil.copytree(sdl / "android-project/gradle", wrapper / "gradle", dirs_exist_ok=True)
    assets = root / "android/genghost/generated-assets"
    notices = assets / "licenses"
    notices.mkdir(parents=True, exist_ok=True)
    shutil.copy2(sdl / "LICENSE.txt", notices / "SDL-LICENSE.txt")
    sdk = os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT")
    if not sdk:
        raise RuntimeError("Set ANDROID_HOME or ANDROID_SDK_ROOT to the installed Android SDK")
    pins = (root / "android/toolchain.properties").read_text()
    version = re.search(r"^ANDROID_NDK=(.+)$", pins, re.MULTILINE).group(1).strip()
    ndk = Path(sdk) / "ndk" / version
    for name in ("NOTICE", "NOTICE.toolchain"):
        shutil.copy2(ndk / name, notices / ("Android-NDK-" + name + ".txt"))
    shutil.copy2(root / "docs/genghost.md", assets / "genghost.md")
    shutil.copy2(root / "docs/genghost-android.md", assets / "genghost-android.md")


if __name__ == "__main__":
    prepare(Path(__file__).resolve().parents[2])
