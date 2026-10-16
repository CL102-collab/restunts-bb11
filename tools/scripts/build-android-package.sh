#!/usr/bin/env bash
# Build both ABI-specific Android APKs and their source/license runtime tree.
set -euo pipefail
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "$script_dir/../.." && pwd)
target=${1:?Usage: build-android-package.sh TARGET RUNTIME_DIRECTORY}
runtime_dir=${2:?Specify the empty runtime directory}
build_type=${RESTUNTS_ANDROID_BUILD_TYPE:-debug}
case "$build_type" in
    debug) native_configuration=Debug ;;
    release) native_configuration=Release ;;
    *) echo "Unknown Android build type: $build_type" >&2; exit 1 ;;
esac
export RESTUNTS_ANDROID_BUILD_TYPE=$build_type
source "$repo_dir/android/toolchain.properties"
apk_alignment_bytes=4
apk_page_alignment_kb=16
case "$target" in
    android-armv7) flavor=armv7; abi=armeabi-v7a; minimum_sdk=$ANDROID_ARMV7_MIN_SDK ;;
    android-arm64) flavor=arm64; abi=arm64-v8a; minimum_sdk=$ANDROID_ARM64_MIN_SDK ;;
    *) echo "Unknown Android package target: $target" >&2; exit 1 ;;
esac
build_root=${RESTUNTS_ANDROID_BUILD_ROOT:-${RUNNER_TEMP:-/tmp}/restunts-$target}
sdk_root=${ANDROID_HOME:-${ANDROID_SDK_ROOT:?Set ANDROID_HOME or ANDROID_SDK_ROOT}}
build_tools="$sdk_root/build-tools/$ANDROID_BUILD_TOOLS"
mkdir -p "$runtime_dir"
if [[ -n "$(ls -A "$runtime_dir")" ]]; then
    echo "Android runtime directory must be empty: $runtime_dir" >&2
    exit 1
fi
bash "$script_dir/build-android.sh" "$target" --no-daemon \
    "-PrestuntsAndroidBuildRoot=$build_root"
bash "$script_dir/build-genghost-android.sh" "$target" --no-daemon \
    "-PrestuntsAndroidBuildRoot=$build_root"

expected_digest=
if [[ "$build_type" == release ]]; then
    certificate=$(mktemp "${RUNNER_TEMP:-${TMPDIR:-/tmp}}/restunts-signing-certificate.XXXXXX")
    trap 'rm -f -- "$certificate"' EXIT
    "${JAVA_HOME:?Set JAVA_HOME to Java 17}/bin/keytool" -exportcert -keystore "$ANDROID_KEYSTORE_FILE" \
        -storepass:env ANDROID_KEYSTORE_PASSWORD -alias "$ANDROID_KEY_ALIAS" -file "$certificate"
    expected_digest=$(python3 -c \
        'import hashlib, sys; print(hashlib.sha256(open(sys.argv[1], "rb").read()).hexdigest())' \
        "$certificate")
fi

verify_apk() {
    local apk=$1 application_id=$2
    local verification badging
    verification=$("${JAVA_HOME:?Set JAVA_HOME to Java 17}/bin/java" -jar "$build_tools/lib/apksigner.jar" \
        verify --verbose --print-certs "$apk")
    printf '%s\n' "$verification"
    for signature in 'v1 scheme (JAR signing)' 'v2 scheme (APK Signature Scheme v2)'; do
        if ! grep --fixed-strings --quiet "Verified using $signature: true" <<< "$verification"; then
            echo "Android APK is missing its required $signature signature: $apk" >&2
            exit 1
        fi
    done
    "$build_tools/zipalign" -c -P "$apk_page_alignment_kb" "$apk_alignment_bytes" "$apk"
    badging=$("$build_tools/aapt" dump badging "$apk")
    if ! [[ "$badging" == *"package: name='$application_id'"* && \
            "$badging" == *"sdkVersion:'$minimum_sdk'"* && \
            "$badging" == *"targetSdkVersion:'$ANDROID_COMPILE_SDK'"* && \
            "$badging" == *"native-code: '$abi'"* ]]; then
        echo "Android APK has incorrect application ID, SDK or native ABI requirements: $apk" >&2
        exit 1
    fi
    if [[ "$build_type" == release ]]; then
        if grep --quiet '^application-debuggable' <<< "$badging"; then
            echo "Refusing to package a debuggable Android release APK: $apk" >&2
            exit 1
        fi
        if ! grep --fixed-strings --line-regexp --quiet 'Number of signers: 1' <<< "$verification" || \
                ! grep --fixed-strings --line-regexp --quiet \
                    "Signer #1 certificate SHA-256 digest: $expected_digest" <<< "$verification"; then
            echo "Android release APK was not signed with the configured release key: $apk" >&2
            exit 1
        fi
    fi
}

game_apk="$build_root/app/build/outputs/apk/$flavor/$build_type/app-$flavor-$build_type.apk"
generator_apk="$build_root/genghost/build/outputs/apk/$flavor/$build_type/genghost-$flavor-$build_type.apk"
verify_apk "$game_apk" org.restunts.android
verify_apk "$generator_apk" org.restunts.genghost

# A reused build root can retain the previous minimum SDK's configuration hash.
# Only install the native tree matching the selected ABI and minimum API.
native_builds=()
for cache in "$build_root/app/.cxx/$native_configuration/"*"/$abi/CMakeCache.txt"; do
    if [[ -f "$cache" ]] && \
            grep --quiet --extended-regexp "^ANDROID_PLATFORM:[^=]+=android-$minimum_sdk$" "$cache" && \
            grep --quiet --extended-regexp "^ANDROID_ABI:[^=]+=$abi$" "$cache" && \
            grep --quiet --extended-regexp "^CMAKE_BUILD_TYPE:[^=]+=$native_configuration$" "$cache"; then
        native_builds+=("$cache")
    fi
done
if [[ "${#native_builds[@]}" != 1 ]]; then
    echo "Expected one $native_configuration Android native build for $abi at API $minimum_sdk." >&2
    exit 1
fi
"$sdk_root/cmake/$ANDROID_CMAKE/bin/cmake" --install "${native_builds[0]%/CMakeCache.txt}" \
    --prefix "$runtime_dir" --component Runtime
mkdir -p "$runtime_dir/bin" "$runtime_dir/share/docs/restunts"
cp -- "$game_apk" "$runtime_dir/bin/restunts.apk"
cp -- "$generator_apk" "$runtime_dir/bin/genghost.apk"
cp -- "$repo_dir/docs/genghost.md" "$repo_dir/docs/genghost-android.md" \
    "$runtime_dir/share/docs/restunts/"
