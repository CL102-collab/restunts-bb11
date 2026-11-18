#!/bin/sh
# Build pinned Linux-hosted Haiku tools and import a matching native SDK.
set -eu

script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
target=${1:?Usage: prepare-haiku-cross.sh TARGET SDK_ROOT GUEST_SYSROOT_ARCHIVE}
sdk=${2:?Usage: prepare-haiku-cross.sh TARGET SDK_ROOT GUEST_SYSROOT_ARCHIVE}
guest_archive=${3:?Usage: prepare-haiku-cross.sh TARGET SDK_ROOT GUEST_SYSROOT_ARCHIVE}
case "$(uname -s):$(uname -m)" in
    Linux:x86_64) ;;
    *) echo "Haiku cross SDK preparation requires an x86-64 Linux host." >&2; exit 1 ;;
esac
case "$target" in
    haiku-x64) machine=x86_64-unknown-haiku; secondary= ;;
    haiku-x86) machine=i586-pc-haiku; secondary=x86 ;;
    *) echo "Unsupported Haiku cross target: $target" >&2; exit 1 ;;
esac
jobs=${RESTUNTS_BUILD_JOBS:-$(nproc)}
case "$jobs" in ''|*[!0-9]*|0) echo "Invalid build job count: $jobs" >&2; exit 1 ;; esac
for required in python3 curl sha256sum tar make gcc g++ bison flex gawk makeinfo; do
    command -v "$required" >/dev/null || {
        echo "Missing Haiku cross SDK prerequisite: $required" >&2
        exit 1
    }
done
test -f "$guest_archive"
mkdir -p "$sdk"
sdk=$(cd -- "$sdk" && pwd -P)
case "$sdk" in *[[:space:]]*) echo "The Haiku toolchain requires an SDK path without whitespace." >&2; exit 1 ;; esac
. "$script_dir/haiku-cross-toolchains.conf"
toolchain_identity="$target:$HAIKU_CROSS_HAIKU_SHA256:$HAIKU_CROSS_BUILDTOOLS_SHA256"
toolchain_marker="$sdk/toolchain/restunts-toolchain-id"
cache_root=${RESTUNTS_TOOLCHAIN_ROOT:-${RUNNER_TEMP:-/tmp}/restunts-toolchains}
download_dir="$cache_root/downloads"
mkdir -p "$download_dir"

download_source() {
    source_url=$1
    source_checksum=$2
    source_archive="$download_dir/$source_checksum.tar.gz"
    if [ ! -f "$source_archive" ] || \
            [ "$(sha256sum "$source_archive" | cut -d ' ' -f 1)" != "$source_checksum" ]; then
        curl --fail --location --retry 3 --connect-timeout 30 \
            "$source_url" --output "$source_archive.part"
        if [ "$(sha256sum "$source_archive.part" | cut -d ' ' -f 1)" != "$source_checksum" ]; then
            rm -f -- "$source_archive.part"
            echo "Haiku cross source checksum mismatch: $source_url" >&2
            exit 1
        fi
        mv "$source_archive.part" "$source_archive"
    fi
}

if [ ! -f "$toolchain_marker" ] || [ "$(cat "$toolchain_marker")" != "$toolchain_identity" ]; then
    if [ -e "$sdk/toolchain" ]; then
        echo "Remove the incomplete or mismatched cached Haiku toolchain: $sdk/toolchain" >&2
        exit 1
    fi
    download_source "$HAIKU_CROSS_HAIKU_URL" "$HAIKU_CROSS_HAIKU_SHA256"
    download_source "$HAIKU_CROSS_BUILDTOOLS_URL" "$HAIKU_CROSS_BUILDTOOLS_SHA256"
    build_root="$sdk/build"
    mkdir -p "$build_root/haiku" "$build_root/buildtools"
    tar -xzf "$download_dir/$HAIKU_CROSS_HAIKU_SHA256.tar.gz" \
        --strip-components=1 -C "$build_root/haiku"
    tar -xzf "$download_dir/$HAIKU_CROSS_BUILDTOOLS_SHA256.tar.gz" \
        --strip-components=1 -C "$build_root/buildtools"
    # Build only modern GCC/binutils, using the official compiler-only helper.
    # SECONDARY_ARCH selects hybrid x86 without building the unused GCC 2 tools.
    MAKE=make CC=gcc CXX=g++ SECONDARY_ARCH="$secondary" \
        HAIKU_USE_SYSROOT="$sdk/compiler-sysroot" HAIKU_USE_GCC_PIPE=1 \
        sh "$build_root/haiku/build/scripts/build_cross_tools_gcc4" \
        "$machine" "$build_root/haiku" "$build_root/buildtools" \
        "$sdk/toolchain" "-j$jobs"
    printf '%s\n' "$toolchain_identity" > "$toolchain_marker"
    rm -rf -- "$build_root" "$sdk/compiler-sysroot"
fi

# Refresh target files even when Linux-hosted compilers came from a cache.
python3 "$script_dir/haiku-cross-sdk.py" "$target" "$sdk" "$guest_archive"
printf '%s\n' "$target" > "$sdk/restunts-target"
printf '%s\n' "$machine" > "$sdk/restunts-machine"
actual_machine=$("$sdk/bin/cc" -dumpmachine)
if [ "$actual_machine" != "$machine" ]; then
    echo "The prepared compiler targets $actual_machine instead of $machine." >&2
    exit 1
fi
printf '%s\n' "$actual_machine"
"$sdk/bin/c++" -dumpversion
"$sdk/bin/wx-config" --version
echo "Prepared $target cross SDK at $sdk"
