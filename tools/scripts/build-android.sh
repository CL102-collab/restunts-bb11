#!/usr/bin/env bash
set -euo pipefail
restunts_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
target=all
build_type=${RESTUNTS_ANDROID_BUILD_TYPE:-debug}
case "${1:-}" in
    android-armv7|android-arm64) target=$1; shift ;;
    all) shift ;;
    -h|--help)
        echo 'Usage: build-android.sh [android-armv7|android-arm64|all] [--release] [GRADLE_OPTIONS...]'
        exit 0
        ;;
    ""|-*) ;;
    *) echo "Unknown Android build target: $1" >&2; exit 1 ;;
esac
gradle_options=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --release) build_type=release ;;
        --) shift; gradle_options+=("$@"); break ;;
        *) gradle_options+=("$1") ;;
    esac
    shift
done
case "$build_type" in
    debug) variant=Debug ;;
    release)
        variant=Release
        for name in ANDROID_KEYSTORE_FILE ANDROID_KEYSTORE_PASSWORD \
                ANDROID_KEY_ALIAS ANDROID_KEY_PASSWORD; do
            if [[ -z "${!name:-}" ]]; then
                echo "Signed Android release builds require $name." >&2
                exit 1
            fi
        done
        if [[ ! -f "$ANDROID_KEYSTORE_FILE" || ! -r "$ANDROID_KEYSTORE_FILE" ]]; then
            echo 'The Android release keystore must be a readable file.' >&2
            exit 1
        fi
        ANDROID_KEYSTORE_FILE=$(python3 -c \
            'import os; print(os.path.abspath(os.environ["ANDROID_KEYSTORE_FILE"]))')
        export ANDROID_KEYSTORE_FILE
        gradle_options+=(--no-daemon)
        ;;
    *) echo "Unknown Android build type: $build_type" >&2; exit 1 ;;
esac
case "$target" in
    android-armv7) task=assembleArmv7$variant ;;
    android-arm64) task=assembleArm64$variant ;;
    all) task=assemble$variant ;;
esac
python3 "$restunts_root/tools/scripts/prepare-android.py"
# Bash 3.2 treats an empty array expansion as unset with nounset.
if [[ ${#gradle_options[@]} -gt 0 ]]; then
    exec "$restunts_root/out/android-gradle/gradlew" -p "$restunts_root/android" \
        ":app:$task" "${gradle_options[@]}"
fi
exec "$restunts_root/out/android-gradle/gradlew" -p "$restunts_root/android" ":app:$task"
