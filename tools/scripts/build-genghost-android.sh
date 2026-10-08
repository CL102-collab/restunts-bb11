#!/usr/bin/env bash
# Build the headless ARMv7 generator and its installable debug Android interface.
set -euo pipefail
restunts_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
if [[ "${1:-}" == -h || "${1:-}" == --help ]]; then
    echo 'Usage: build-genghost-android.sh [GRADLE_OPTIONS...]'
    echo 'Requires Java 17 and the SDK/NDK/CMake versions in android/toolchain.properties.'
    exit 0
fi
python3 "$restunts_root/tools/scripts/prepare-genghost-android.py"
exec "$restunts_root/out/android-gradle/gradlew" -p "$restunts_root/android" \
    :genghost:assembleDebug "$@"
