#!/usr/bin/env bash
# Wrap the complete installed runtime, including the replaceable OPL library.
set -euo pipefail
umask 022

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "$script_dir/../.." && pwd)
runtime_dir=$(cd -- "${1:?Usage: build-appimage.sh RUNTIME_DIRECTORY [TARGET]}" && pwd)
target=${2:-linux-appimage-x64}
tool_root=${RESTUNTS_TOOLCHAIN_ROOT:-${RUNNER_TEMP:-/tmp}/restunts-toolchains}
source "$script_dir/package-toolchains.conf"
case "$target" in
    linux-appimage-x64)
        architecture=x86_64
        runtime="$tool_root/downloads/appimage-runtime-x86_64"
        printf '%s  %s\n' "$APPIMAGE_RUNTIME_SHA256" "$runtime" | sha256sum --check --status
        ;;
    linux-appimage-x86|linux-appimage-x86-no-sse2)
        architecture=i686
        runtime="$tool_root/appimage-runtime-i586/runtime-i586"
        (cd "$tool_root/appimage-runtime-i586"; sha256sum --check --status runtime-i586.sha256)
        ;;
    linux-appimage-armv7)
        architecture=armhf
        runtime="$tool_root/downloads/appimage-runtime-armhf"
        printf '%s  %s\n' "$APPIMAGE_RUNTIME_ARMV7_SHA256" "$runtime" | sha256sum --check --status
        ;;
    linux-appimage-arm64)
        architecture=aarch64
        runtime="$tool_root/downloads/appimage-runtime-aarch64"
        printf '%s  %s\n' "$APPIMAGE_RUNTIME_ARM64_SHA256" "$runtime" | sha256sum --check --status
        ;;
    *) echo "Unknown AppImage target: $target" >&2; exit 1 ;;
esac
# shellcheck source=/dev/null
source "$tool_root/appimage-sysroots/$target.env"
deps_prefix=${RESTUNTS_APPIMAGE_DEPS_PREFIX:?Missing legacy dependency prefix}
if [[ -e "$runtime_dir/bin/restunts.AppImage" ]]; then
    echo 'Refusing to embed an existing AppImage in itself.' >&2
    exit 1
fi
appdir_parent=$(mktemp -d "${TMPDIR:-/tmp}/restunts-appdir.XXXXXX")
trap 'rm -rf -- "$appdir_parent"' EXIT
appdir="$appdir_parent/Restunts.AppDir"
mkdir -p "$appdir/usr"
cp -a "$runtime_dir/." "$appdir/usr/"
cp -a "$deps_prefix/." "$appdir/usr/"
# Apply the graphics policy here too, including when reusing a cached SDK.
python3 - "$script_dir" "$appdir/usr/lib" <<'PY'
from pathlib import Path
import sys
sys.path.insert(0, sys.argv[1])
from appimage_runtime import is_system_graphics_library

for path in Path(sys.argv[2]).rglob('*'):
    if (path.is_file() or path.is_symlink()) and is_system_graphics_library(path.name):
        path.unlink()
PY
# The host libc and ELF loader must always come from the user's distribution.
if compgen -G "$appdir/usr/lib/libc.so*" > /dev/null \
    || compgen -G "$appdir/usr/lib/ld-linux*" > /dev/null; then
    echo 'Refusing to bundle glibc or its dynamic loader.' >&2
    exit 1
fi
if [[ "$architecture" == i686 ]]; then
    mkdir -p "$appdir/usr/share/licenses/appimage-runtime" "$appdir/usr/share/sources/appimage-runtime"
    cp -a "$tool_root/appimage-runtime-i586/licenses/." "$appdir/usr/share/licenses/appimage-runtime/"
    for archive in musl-1.2.5.tar.gz zlib-1.3.1.tar.xz zstd-1.5.7.tar.gz fuse-3.15.0.tar.xz \
        squashfuse-0.5.2.tar.gz type2-runtime-dd6cebed.tar.gz; do
        cp "$tool_root/downloads/$archive" "$appdir/usr/share/sources/appimage-runtime/"
    done
    cp "$script_dir/build-appimage-runtime.sh" "$appdir/usr/share/sources/appimage-runtime/"
    cp -a "$tool_root/appimage-runtime-i586/relink" "$appdir/usr/share/sources/appimage-runtime/"
fi
cp "$repo_dir/tools/appimage/AppRun" "$appdir/AppRun"
chmod +x "$appdir/AppRun"
cp "$repo_dir/tools/appimage/restunts.desktop" "$appdir/"
cp "$repo_dir/site/assets/favicon.svg" "$appdir/restunts.svg"
find "$appdir" -type d -exec chmod 755 {} +
find "$appdir" -type f -exec chmod a+r {} +
chmod 755 "$appdir/AppRun" \
    "$appdir/usr/bin/"{restunts,restunts-launcher,repldump,pixldump,genghost,restunts-gtk-modules}
python3 "$script_dir/check-appimage-abi.py" "$appdir" "$target" --runtime "$runtime"

# Extracted appimagetool and a pinned static runtime avoid FUSE and network use.
ARCH="$architecture" "$tool_root/appimagetool/squashfs-root/AppRun" --no-appstream \
    --runtime-file "$runtime" \
    --mksquashfs-opt -processors --mksquashfs-opt "${RESTUNTS_BUILD_JOBS:-2}" \
    "$appdir" "$runtime_dir/bin/restunts.AppImage"
chmod +x "$runtime_dir/bin/restunts.AppImage"
