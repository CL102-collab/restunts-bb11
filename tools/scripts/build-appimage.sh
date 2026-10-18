#!/usr/bin/env bash
# Wrap the complete installed runtime, including the replaceable OPL library.
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "$script_dir/../.." && pwd)
runtime_dir=$(cd -- "${1:?Usage: build-appimage.sh RUNTIME_DIRECTORY}" && pwd)
tool_root=${RESTUNTS_TOOLCHAIN_ROOT:-${RUNNER_TEMP:-/tmp}/restunts-toolchains}
source "$script_dir/package-toolchains.conf"

# Reject accidental builds on a newer libc, which would lose Fedora 34 support.
source /etc/os-release
if [[ "$ID:$VERSION_ID:$(uname -m)" != ubuntu:20.04:x86_64 ]]; then
    echo 'The AppImage target must be built in Ubuntu 20.04 x86-64.' >&2
    exit 1
fi
if [[ -e "$runtime_dir/bin/restunts.AppImage" ]]; then
    echo 'Refusing to embed an existing AppImage in itself.' >&2
    exit 1
fi
printf '%s  %s\n' "$APPIMAGE_RUNTIME_SHA256" "$tool_root/downloads/appimage-runtime-x86_64" \
    | sha256sum --check --status
appdir_parent=$(mktemp -d "${TMPDIR:-/tmp}/restunts-appdir.XXXXXX")
trap 'rm -rf -- "$appdir_parent"' EXIT
appdir="$appdir_parent/Restunts.AppDir"
mkdir -p "$appdir/usr"
cp -a "$runtime_dir/." "$appdir/usr/"
cp "$repo_dir/tools/appimage/AppRun" "$appdir/AppRun"
chmod +x "$appdir/AppRun"
cp "$repo_dir/tools/appimage/restunts.desktop" "$appdir/"
cp "$repo_dir/site/assets/favicon.svg" "$appdir/restunts.svg"

# Extracted appimagetool and a pinned static runtime avoid FUSE and network use.
ARCH=x86_64 "$tool_root/appimagetool/squashfs-root/AppRun" --no-appstream \
    --runtime-file "$tool_root/downloads/appimage-runtime-x86_64" \
    --mksquashfs-opt -processors --mksquashfs-opt "${RESTUNTS_BUILD_JOBS:-2}" \
    "$appdir" "$runtime_dir/bin/restunts.AppImage"
chmod +x "$runtime_dir/bin/restunts.AppImage"
