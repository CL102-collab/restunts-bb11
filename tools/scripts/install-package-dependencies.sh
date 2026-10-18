#!/usr/bin/env bash
# Install one release target's cross compiler and native driver headers.
set -euo pipefail
# Ubuntu's driver dependencies include tzdata; CI must never prompt for a zone.
export DEBIAN_FRONTEND=noninteractive

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$script_dir/package-toolchains.conf"
target=${1:?Usage: install-package-dependencies.sh TARGET}
tool_root=${RESTUNTS_TOOLCHAIN_ROOT:-${RUNNER_TEMP:-/tmp}/restunts-toolchains}
mkdir -p "$tool_root/downloads"

download() {
    local url=$1 checksum=$2 destination=$3
    curl --fail --location --retry 3 --connect-timeout 20 "$url" --output "$destination"
    printf '%s  %s\n' "$checksum" "$destination" | sha256sum --check --status
}

case "$target" in
    linux-*)
        case "$target" in
            linux-x64|linux-appimage-x64) architecture=amd64 ;;
            linux-x86|linux-x86-no-sse2) architecture=i386; compiler=gcc-multilib ;;
            linux-arm32) architecture=armhf; compiler=gcc-arm-linux-gnueabihf ;;
            linux-arm64) architecture=arm64; compiler=gcc-aarch64-linux-gnu ;;
            *) echo "Unknown Linux target: $target" >&2; exit 1 ;;
        esac
        if [[ "$architecture" != amd64 ]]; then
            dpkg --add-architecture "$architecture"
        fi
        apt-get update
        if [[ -n "${compiler:-}" ]]; then
            apt-get install -y --no-install-recommends "$compiler"
        fi
        apt-get install -y --no-install-recommends \
            "libasound2-dev:$architecture" "libpulse-dev:$architecture" \
            "libx11-dev:$architecture" "libxext-dev:$architecture" \
            "libxrandr-dev:$architecture" "libxcursor-dev:$architecture" \
            "libxi-dev:$architecture" "libxfixes-dev:$architecture" \
            "libxss-dev:$architecture" "libxtst-dev:$architecture" \
            "libwayland-dev:$architecture" "libxkbcommon-dev:$architecture" \
            "libudev-dev:$architecture" "libdrm-dev:$architecture" "libgbm-dev:$architecture" \
            "libegl1-mesa-dev:$architecture" "libgl1-mesa-dev:$architecture"
        if [[ "$target" == linux-appimage-x64 ]]; then
            download "$APPIMAGETOOL_URL" "$APPIMAGETOOL_SHA256" "$tool_root/downloads/appimagetool.AppImage"
            download "$APPIMAGE_RUNTIME_URL" "$APPIMAGE_RUNTIME_SHA256" "$tool_root/downloads/appimage-runtime-x86_64"
            chmod +x "$tool_root/downloads/appimagetool.AppImage"
            mkdir -p "$tool_root/appimagetool"
            (cd "$tool_root/appimagetool"; "$tool_root/downloads/appimagetool.AppImage" --appimage-extract > /dev/null)
        fi
        ;;
    windows-x86|windows-x86-no-sse2|windows-x64)
        apt-get update
        # Debian's GCC/MinGW targets msvcrt; UCRT would raise the XP minimum.
        apt-get install -y --no-install-recommends mingw-w64
        ;;
    windows-arm64)
        download "$LLVM_MINGW_URL" "$LLVM_MINGW_SHA256" "$tool_root/downloads/llvm-mingw.tar.xz"
        mkdir -p "$tool_root/llvm-mingw"
        tar -xJf "$tool_root/downloads/llvm-mingw.tar.xz" -C "$tool_root/llvm-mingw" --strip-components=1
        ;;
    dos32)
        download "$DJGPP_URL" "$DJGPP_SHA256" "$tool_root/downloads/djgpp.tar.bz2"
        mkdir -p "$tool_root/djgpp"
        tar -xjf "$tool_root/downloads/djgpp.tar.bz2" -C "$tool_root/djgpp" --strip-components=1
        download "$CWSDPMI_URL" "$CWSDPMI_SHA256" "$tool_root/downloads/csdpmi7b.zip"
        ;;
    browser)
        download "$EMSDK_URL" "$EMSDK_SHA256" "$tool_root/downloads/emsdk.tar.gz"
        mkdir -p "$tool_root/emsdk"
        tar -xzf "$tool_root/downloads/emsdk.tar.gz" -C "$tool_root/emsdk" --strip-components=1
        # Seed and verify every binary archive before invoking the SDK installer.
        # KEEP_DOWNLOADS makes emsdk consume these files without downloading again.
        mkdir -p "$tool_root/emsdk/downloads"
        download "$EMSCRIPTEN_URL" "$EMSCRIPTEN_SHA256" \
            "$tool_root/emsdk/downloads/$EMSCRIPTEN_REVISION-wasm-binaries.tar.xz"
        download "$EMSDK_NODE_URL" "$EMSDK_NODE_SHA256" \
            "$tool_root/emsdk/downloads/$EMSDK_NODE_ARCHIVE"
        EMSDK_KEEP_DOWNLOADS=1 "$tool_root/emsdk/emsdk" install "$EMSDK_VERSION"
        "$tool_root/emsdk/emsdk" activate "$EMSDK_VERSION"
        ;;
    *) echo "Unknown package target: $target" >&2; exit 1 ;;
esac
