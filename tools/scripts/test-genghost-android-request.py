#!/usr/bin/env python3
"""Run Android generator request and car resource tests on a host JDK, without an Android SDK."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
INTENT = """package android.content;
import java.util.HashMap;
import java.util.Map;
public final class Intent {
    private final Map<String, Object> values = new HashMap<>();
    public Intent putExtra(String key, String value) { values.put(key, value); return this; }
    public Intent putExtra(String key, int value) { values.put(key, value); return this; }
    public Intent putExtra(String key, String[] value) { values.put(key, value); return this; }
    public String getStringExtra(String key) { return (String)values.get(key); }
    public int getIntExtra(String key, int fallback) {
        return values.containsKey(key) ? (Integer)values.get(key) : fallback;
    }
    public String[] getStringArrayExtra(String key) { return (String[])values.get(key); }
}
"""


def main():
    java = shutil.which("java")
    if not java and os.environ.get("JAVA_HOME"):
        candidate = Path(os.environ["JAVA_HOME"]) / "bin/java"
        java = str(candidate) if candidate.is_file() else None
    if not java:
        java = next((str(path) for path in sorted(Path("/usr/lib/jvm").glob("*/bin/java"))
                     if path.is_file()), None)
    if not java:
        print("SKIP: a JDK is needed for Android generator request tests")
        return 77
    modules = subprocess.run([java, "--list-modules"], capture_output=True, text=True)
    if modules.returncode or "jdk.compiler@" not in modules.stdout:
        print("SKIP: a JDK with the compiler module is needed")
        return 77
    with tempfile.TemporaryDirectory(prefix="genghost-android-request-") as directory:
        temporary = Path(directory)
        stub = temporary / "Intent.java"
        stub.write_text(INTENT)
        classes = temporary / "classes"
        classes.mkdir()
        game = temporary / "game"
        game.mkdir()
        subprocess.run([java, "-m", "jdk.compiler/com.sun.tools.javac.Main", "-source", "8", "-target", "8", "-Xlint:-options",
                        "-d", str(classes), str(stub),
                        str(ROOT / "android/genghost/src/main/java/org/restunts/genghost/GenerationRequest.java"),
                        str(ROOT / "android/genghost/src/main/java/org/restunts/genghost/CarResources.java"),
                        str(ROOT / "android/app/src/main/java/org/restunts/android/GameDataImport.java"),
                        str(ROOT / "android/app/src/main/java/org/restunts/android/GameDataSync.java"),
                        str(ROOT / "android/app/src/main/java/org/restunts/android/GameDataVersion.java"),
                        str(ROOT / "android/tests/GenerationRequestTest.java"),
                        str(ROOT / "android/tests/CarResourcesTest.java")], check=True)
        subprocess.run([java, "-ea", "-cp", str(classes),
                        "org.restunts.genghost.GenerationRequestTest", str(game)], check=True)
        subprocess.run([java, "-ea", "-cp", str(classes),
                        "org.restunts.genghost.CarResourcesTest"], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
