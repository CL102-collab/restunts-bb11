#!/usr/bin/env bash
# Recreate the Ubuntu 10.04-compatible SDK and the separately shipped libraries.
# Run on a modern x86-64 Linux host; old container executables are never used.
set -euo pipefail
umask 022

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$script_dir/package-toolchains.conf"
target=${1:?Usage: prepare-appimage-sysroot.sh TARGET}
tool_root=${RESTUNTS_TOOLCHAIN_ROOT:-${RUNNER_TEMP:-/tmp}/restunts-toolchains}
jobs=${RESTUNTS_BUILD_JOBS:-2}
case "$target" in
    linux-appimage-x64)
        architecture=x64; lib_directory=lib64; host_triple=x86_64-linux-gnu
        sdk_image=$APPIMAGE_LEGACY_X64_IMAGE; glibc_image=$APPIMAGE_GLIBC_X64_IMAGE
        base_url=$APPIMAGE_LEGACY_X64_BASE_URL; base_sha=$APPIMAGE_LEGACY_X64_BASE_SHA256
        development_url=$APPIMAGE_LEGACY_X64_DEVELOPMENT_URL
        development_sha=$APPIMAGE_LEGACY_X64_DEVELOPMENT_SHA256
        runtime_url=$APPIMAGE_GLIBC_X64_URL; runtime_sha=$APPIMAGE_GLIBC_X64_SHA256
        architecture_flags=; dependency_flags=; elf_class=ELF64
        ;;
    linux-appimage-x86|linux-appimage-x86-no-sse2)
        architecture=x86; lib_directory=lib; host_triple=i686-linux-gnu
        sdk_image=$APPIMAGE_LEGACY_X86_IMAGE; glibc_image=$APPIMAGE_GLIBC_X86_IMAGE
        base_url=$APPIMAGE_LEGACY_X86_BASE_URL; base_sha=$APPIMAGE_LEGACY_X86_BASE_SHA256
        development_url=$APPIMAGE_LEGACY_X86_DEVELOPMENT_URL
        development_sha=$APPIMAGE_LEGACY_X86_DEVELOPMENT_SHA256
        runtime_url=$APPIMAGE_GLIBC_X86_URL; runtime_sha=$APPIMAGE_GLIBC_X86_SHA256
        architecture_flags='-m32 -march=i586'
        # Share these dependencies between both x86 targets. Neither they nor
        # the no-SSE2 game may accidentally acquire an SSE2 runtime dependency.
        dependency_flags='-mno-sse -mno-sse2 -mno-avx -mfpmath=387'
        elf_class=ELF32
        ;;
    *) echo "Unknown AppImage target: $target" >&2; exit 1 ;;
esac

sdk_root="$tool_root/appimage-sysroots/$architecture"
downloads="$tool_root/downloads"
mkdir -p "$sdk_root" "$downloads"
# Normal x86 and x86-no-sse2 intentionally use the same prepared libraries.
exec 9> "$sdk_root/.prepare.lock"
flock 9
compiler=${CC:-gcc}
recipe=$(cat "$script_dir/prepare-appimage-sysroot.sh" "$script_dir/appimage-sysroot.py" \
    "$script_dir/package-toolchains.conf"; "$compiler" -dumpmachine; "$compiler" --version)
recipe_sha=$(printf '%s' "$recipe" | sha256sum | cut -d ' ' -f 1)
sysroot="$sdk_root/sysroot"
deps="$sdk_root/deps"
fetch() {
    python3 "$script_dir/appimage-sysroot.py" download "$1" "$2" "$3"
}

cached_recipe=
if [[ -f "$sdk_root/recipe.sha256" ]]; then
    read -r cached_recipe < "$sdk_root/recipe.sha256"
fi
if [[ "$cached_recipe" != "$recipe_sha" ]]; then
    stage=$(mktemp -d "$sdk_root/.prepare.XXXXXXXX")
    cleanup() {
        python3 -c 'import shutil,sys; shutil.rmtree(sys.argv[1], ignore_errors=True)' "$stage"
    }
    trap cleanup EXIT
    echo "Preparing pinned legacy AppImage SDK ($architecture)."
    python3 "$script_dir/appimage-sysroot.py" prepare \
        --cache "$downloads" --sysroot "$stage/sysroot" --runtime "$stage/runtime" \
        --lib-directory "$lib_directory" \
        --base-url "$base_url" --base-sha256 "$base_sha" \
        --development-url "$development_url" --development-sha256 "$development_sha" \
        --runtime-url "$runtime_url" --runtime-sha256 "$runtime_sha"
    fetch "$APPIMAGE_SCHED_HEADER_URL" "$APPIMAGE_SCHED_HEADER_SHA256" "$downloads/appimage-sched.h"
    fetch "$APPIMAGE_SCHED_BITS_URL" "$APPIMAGE_SCHED_BITS_SHA256" "$downloads/appimage-sched-bits.h"
    cp "$downloads/appimage-sched.h" "$stage/sysroot/usr/include/sched.h"
    cp "$downloads/appimage-sched-bits.h" "$stage/sysroot/usr/include/bits/sched.h"
    fetch "$APPIMAGE_LINUX_HEADERS_URL" "$APPIMAGE_LINUX_HEADERS_SHA256" "$downloads/appimage-linux-headers.deb"
    dpkg-deb -x "$downloads/appimage-linux-headers.deb" "$stage/linux-headers"
    python3 - "$stage" <<'PY'
from pathlib import Path
import shutil, sys
stage = Path(sys.argv[1])
include = stage / 'sysroot/usr/include'
for name, relative in [('linux', 'linux'), ('asm-generic', 'asm-generic'), ('asm', 'x86_64-linux-gnu/asm')]:
    target = include / name
    if target.is_symlink():
        target.unlink()
    elif target.exists():
        shutil.rmtree(target)
    shutil.copytree(stage / 'linux-headers/usr/include' / relative, target)
PY
    fetch "$APPIMAGE_ALSA_URL" "$APPIMAGE_ALSA_SHA256" "$downloads/alsa-lib_1.0.27.2.orig.tar.bz2"
    mkdir "$stage/alsa-build"
    tar -xjf "$downloads/alsa-lib_1.0.27.2.orig.tar.bz2" -C "$stage"
    legacy_flags="--sysroot=$stage/sysroot -B$stage/sysroot/usr/$lib_directory/ $architecture_flags"
    legacy_link_flags="-L$stage/sysroot/$lib_directory -L$stage/sysroot/usr/$lib_directory -Wl,-rpath-link,$stage/sysroot/$lib_directory -Wl,-rpath-link,$stage/sysroot/usr/$lib_directory -static-libgcc"
    (
        cd "$stage/alsa-build"
        # ALSA's historical configure resets CC when a prefixed compiler is
        # present, so target/sysroot options also belong in CFLAGS.
        CC="$compiler" CFLAGS="$legacy_flags $dependency_flags -O2 -fPIC -fno-stack-protector" \
            LDFLAGS="$legacy_link_flags" \
            "$stage/alsa-lib-1.0.27.2/configure" --prefix=/usr \
                --libdir="/usr/$lib_directory" --host="$host_triple" --disable-python \
                > configure.log 2>&1
        make -j "$jobs" > build.log 2>&1
        make DESTDIR="$stage/sysroot" install > install.log 2>&1
    ) || {
        tail -n 50 "$stage/alsa-build/configure.log" "$stage/alsa-build/build.log" 2>/dev/null >&2
        exit 1
    }
    mkdir -p "$stage/deps/lib" "$stage/deps/share/licenses" "$stage/deps/share/sources"
    for library in libX11.so.6 libXext.so.6 libXau.so.6 libXdmcp.so.6 libXrender.so.1 libasound.so.2; do
        cp -L "$stage/sysroot/usr/$lib_directory/$library" "$stage/deps/lib/$library"
        readelf -h "$stage/deps/lib/$library" | grep -q "Class:.*$elf_class" || {
            echo "Wrong architecture for $library" >&2; exit 1;
        }
    done
    for library in libX11-1.0.3 libXext-1.0.1 libXau-1.0.1 libXdmcp-1.0.1 libXrender-0.9.1; do
        mkdir "$stage/deps/share/licenses/$library"
        cp "$stage/sysroot/usr/share/doc/$library/COPYING" "$stage/deps/share/licenses/$library/"
    done
    mkdir "$stage/deps/share/licenses/alsa-lib"
    cp "$stage/alsa-lib-1.0.27.2/COPYING" "$stage/deps/share/licenses/alsa-lib/"
    cp "$downloads/alsa-lib_1.0.27.2.orig.tar.bz2" "$stage/deps/share/sources/"
    cp -a "$stage/sysroot/usr/share/alsa" "$stage/deps/share/alsa"
    cp -a "$stage/sysroot/usr/share/X11" "$stage/deps/share/X11"
    python3 - "$stage/deps" <<'PY'
from pathlib import Path
import sys
prefix = Path(sys.argv[1])
for config in (prefix / 'share/alsa').rglob('*.conf'):
    text = config.read_text()
    config.write_text(text.replace('{ @func datadir }', '{ @func getenv vars [ RESTUNTS_ALSA_CONFIG_DIR ] default "/usr/share/alsa" }'))
for path in prefix.rglob('*'):
    if path.is_dir():
        path.chmod(0o755)
    elif path.is_file():
        path.chmod(0o644)
PY
    {
        printf 'Legacy SDK image: %s\nLink-only glibc image: %s\n' "$sdk_image" "$glibc_image"
        printf 'Recipe SHA256: %s\nALSA source SHA256: %s\n' "$recipe_sha" "$APPIMAGE_ALSA_SHA256"
        printf 'ALSA configure: --prefix=/usr --libdir=/usr/%s --host=%s --disable-python\n' "$lib_directory" "$host_triple"
        printf 'Architecture flags: %s %s\n' "$architecture_flags" "$dependency_flags"
        "$compiler" --version | head -n 1
    } > "$stage/deps/share/sources/appimage-build-info.txt"
    # Atomically replace only this script's generated SDK, never the downloads.
    python3 - "$stage" "$sdk_root" <<'PY'
from pathlib import Path
import shutil, sys
stage, output = map(Path, sys.argv[1:])
for name in ('sysroot', 'deps'):
    target = output / name
    if target.exists():
        shutil.rmtree(target)
    (stage / name).replace(target)
PY
    printf '%s\n' "$recipe_sha" > "$sdk_root/recipe.sha256"
fi

toolchain="$sdk_root/toolchain.cmake"
cat > "$toolchain" <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR $([[ "$architecture" == x64 ]] && echo x86_64 || echo i586))
set(CMAKE_C_COMPILER [==[$compiler]==])
set(CMAKE_SYSROOT [==[$sysroot]==])
set(CMAKE_C_FLAGS_INIT "$architecture_flags -B\"\${CMAKE_SYSROOT}/usr/$lib_directory/\" -fno-pie -static-libgcc")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-no-pie -L\"\${CMAKE_SYSROOT}/$lib_directory\" -L\"\${CMAKE_SYSROOT}/usr/$lib_directory\" -Wl,-rpath-link,\"\${CMAKE_SYSROOT}/$lib_directory\" -Wl,-rpath-link,\"\${CMAKE_SYSROOT}/usr/$lib_directory\"")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-L\"\${CMAKE_SYSROOT}/$lib_directory\" -L\"\${CMAKE_SYSROOT}/usr/$lib_directory\" -Wl,-rpath-link,\"\${CMAKE_SYSROOT}/$lib_directory\" -Wl,-rpath-link,\"\${CMAKE_SYSROOT}/usr/$lib_directory\"")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
foreach(feature X11_XCURSOR X11_XINPUT X11_XFIXES X11_XRANDR X11_XTEST X11_XSCRNSAVER WAYLAND VULKAN GPU_VULKAN CAMERA)
    set(SDL_\${feature} OFF CACHE BOOL "Legacy AppImage SDK" FORCE)
endforeach()
EOF
env_file="$tool_root/appimage-sysroots/$target.env"
{
    printf 'export RESTUNTS_APPIMAGE_SYSROOT=%q\n' "$sysroot"
    printf 'export RESTUNTS_APPIMAGE_DEPS_PREFIX=%q\n' "$deps"
    printf 'export RESTUNTS_APPIMAGE_CMAKE_TOOLCHAIN=%q\n' "$toolchain"
    printf 'export PKG_CONFIG_SYSROOT_DIR=%q\n' "$sysroot"
    printf 'export PKG_CONFIG_LIBDIR=%q\n' "$sysroot/usr/$lib_directory/pkgconfig:$sysroot/usr/share/pkgconfig"
} > "$env_file"
echo "Prepared AppImage SDK environment: $env_file"
