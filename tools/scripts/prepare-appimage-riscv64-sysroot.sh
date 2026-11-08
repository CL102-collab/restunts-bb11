#!/usr/bin/env bash
# Isolate the first official Debian riscv64 release as the AppImage ABI baseline.
set -euo pipefail
umask 022

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$script_dir/package-toolchains.conf"
target=${1:?Usage: prepare-appimage-riscv64-sysroot.sh linux-appimage-riscv64}
[[ "$target" == linux-appimage-riscv64 ]] || {
    echo "Unknown RISC-V AppImage target: $target" >&2; exit 1;
}
tool_root=${RESTUNTS_TOOLCHAIN_ROOT:-${RUNNER_TEMP:-/tmp}/restunts-toolchains}
triple=riscv64-linux-gnu
architecture_flags='-march=rv64gc -mabi=lp64d'
baseline='Debian 13 Trixie (glibc 2.41)'
glibc_version=2.41
gcc_version=$APPIMAGE_RISCV64_GCC_VERSION
compiler=$(command -v "${CC:-$triple-gcc}")
cxx_compiler=$(command -v "${CXX:-$triple-g++}")
ar_tool=$(command -v "${AR:-$triple-ar}")
ranlib_tool=$(command -v "${RANLIB:-$triple-ranlib}")
qemu=${RESTUNTS_APPIMAGE_QEMU:-$(command -v qemu-riscv64-static || command -v qemu-riscv64)}
sdk_root="$tool_root/appimage-sysroots/riscv64"
downloads="$tool_root/downloads"
mkdir -p "$sdk_root" "$downloads"
exec 9> "$sdk_root/.prepare.lock"
flock 9
recipe=$(cat "$script_dir/prepare-appimage-riscv64-sysroot.sh" "$script_dir/appimage-sysroot.py" \
    "$script_dir/package-toolchains.conf" "$script_dir/appimage_runtime.py" \
    "$script_dir/../appimage/query-gtk-modules.c"; \
    "$compiler" --version; "$cxx_compiler" --version)
recipe_sha=$(printf '%s' "$recipe" | sha256sum | cut -d ' ' -f 1)
sysroot="$sdk_root/sysroot"
deps="$sdk_root/deps"
cached_recipe=
if [[ -f "$sdk_root/recipe.sha256" ]]; then read -r cached_recipe < "$sdk_root/recipe.sha256"; fi
if [[ "$cached_recipe" != "$recipe_sha" ]]; then
    stage=$(mktemp -d "$sdk_root/.prepare.XXXXXXXX")
    trap 'rm -rf -- "$stage"' EXIT
    echo "Preparing pinned RISC-V AppImage SDK: $baseline."
    python3 "$script_dir/appimage-sysroot.py" debian-sdk \
        --index-url "$APPIMAGE_RISCV64_INDEX_URL" --index-sha256 "$APPIMAGE_RISCV64_INDEX_SHA256" \
        --archive-root "$APPIMAGE_RISCV64_ARCHIVE_ROOT" --cache "$downloads" \
        --destination "$stage/sysroot" \
        libgtk2.0-dev libglib2.0-dev libpango1.0-dev fontconfig-config shared-mime-info libpulse-dev \
        libasound2-dev libx11-dev libxext-dev libxrender-dev libsm-dev \
        libc6-dev linux-libc-dev "libgcc-$gcc_version-dev" "libstdc++-$gcc_version-dev" \
        "gcc-$gcc_version-base"
    # Trixie packages use merged /usr; the ELF interpreter still starts with /lib.
    [[ -e "$stage/sysroot/lib" ]] || ln -s usr/lib "$stage/sysroot/lib"
    target_gcc="$stage/sysroot/usr/lib/gcc/$triple/$gcc_version"
    cxx_headers="$stage/sysroot/usr/include/c++/$gcc_version"
    target_headers="$stage/sysroot/usr/include/$triple/c++/$gcc_version"
    [[ -f "$target_gcc/libstdc++.a" && -f "$target_headers/bits/c++config.h" ]] || {
        echo 'Pinned RISC-V SDK is missing its C++ headers/static runtime.' >&2; exit 1;
    }
    include_arguments_text=$(python3 "$script_dir/appimage-sysroot.py" compiler-includes \
        "$compiler" "$stage/sysroot" "$triple")
    mapfile -t include_arguments <<< "$include_arguments_text"
    common_arguments=("--sysroot=$stage/sysroot" "-B$target_gcc/"
        "-B$stage/sysroot/usr/lib/$triple/" "${include_arguments[@]}"
        -march=rv64gc -mabi=lp64d -fno-pie -static-libgcc)
    header_macros=$("$compiler" "${common_arguments[@]}" -dM -E -include features.h -x c /dev/null)
    header_version=$(awk '$2 == "__GLIBC__" {major=$3} $2 == "__GLIBC_MINOR__" {minor=$3}
        END {print major "." minor}' <<< "$header_macros")
    [[ "$header_version" == "$glibc_version" ]] || {
        echo "Cross compiler selected glibc $header_version headers; expected $glibc_version." >&2
        exit 1
    }
    link_arguments=("-L$target_gcc" "-L$stage/sysroot/usr/lib/$triple"
        "-Wl,-rpath-link,$stage/sysroot/usr/lib/$triple")
    python3 "$script_dir/appimage-sysroot.py" debian-runtime "$stage/sysroot" "$stage/deps" "$triple"
    python3 "$script_dir/appimage-sysroot.py" prune-runtime "$stage/deps/lib"
    while IFS= read -r -d '' library; do patchelf --remove-rpath "$library"; done \
        < <(find "$stage/deps/lib" -type f -name '*.so*' -print0)
    gtk_flags=$(PKG_CONFIG_PATH= PKG_CONFIG_SYSROOT_DIR="$stage/sysroot" \
        PKG_CONFIG_LIBDIR="$stage/sysroot/usr/lib/$triple/pkgconfig:$stage/sysroot/usr/share/pkgconfig" \
        pkg-config --cflags --libs gmodule-2.0 pango gdk-pixbuf-2.0)
    read -r -a gtk_arguments <<< "$gtk_flags"
    mkdir -p "$stage/deps/bin"
    "$compiler" "${common_arguments[@]}" "${link_arguments[@]}" -O2 -no-pie \
        "$script_dir/../appimage/query-gtk-modules.c" "${gtk_arguments[@]}" \
        '-Wl,-rpath,$ORIGIN/../lib' -o "$stage/deps/bin/restunts-gtk-modules"
    module_directory="$stage/deps/lib/gdk-pixbuf-2.0/2.10.0/loaders"
    if [[ -d "$module_directory" ]]; then
        mapfile -d '' -t gtk_modules < <(find "$module_directory" -type f -name '*.so' -print0)
        if (( ${#gtk_modules[@]} > 0 )); then
            "$qemu" -L "$stage/sysroot" "$stage/deps/bin/restunts-gtk-modules" pixbuf \
                "${gtk_modules[@]}" > "$stage/query-gtk-modules.log"
        fi
    fi
    python3 - "$stage/deps" <<'PY'
from pathlib import Path
import sys
for config in (Path(sys.argv[1]) / 'share/alsa').rglob('*.conf'):
    text = config.read_text()
    config.write_text(text.replace('{ @func datadir }', '{ @func getenv vars [ RESTUNTS_ALSA_CONFIG_DIR ] default "/usr/share/alsa" }'))
PY
    cp "$script_dir/../appimage/query-gtk-modules.c" "$stage/deps/share/sources/"
    mkdir -p "$stage/deps/share/licenses/gcc-runtime"
    cp "$script_dir/../../third_party/gcc-runtime/COPYING3.txt" \
        "$script_dir/../../third_party/gcc-runtime/COPYING.RUNTIME.txt" \
        "$stage/deps/share/licenses/gcc-runtime/"
    {
        printf 'Baseline: %s\nIndex: %s\nIndex SHA256: %s\n' \
            "$baseline" "$APPIMAGE_RISCV64_INDEX_URL" "$APPIMAGE_RISCV64_INDEX_SHA256"
        printf 'Recipe SHA256: %s\nArchitecture flags: %s\n' "$recipe_sha" "$architecture_flags"
        printf 'Static C++ runtime: GCC %s; target libraries and headers from pinned packages\n' "$gcc_version"
        "$compiler" --version | head -n 1
    } > "$stage/deps/share/sources/appimage-build-info.txt"
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
target_gcc="$sysroot/usr/lib/gcc/$triple/$gcc_version"
cxx_headers="$sysroot/usr/include/c++/$gcc_version"
target_headers="$sysroot/usr/include/$triple/c++/$gcc_version"
include_flags=$(python3 "$script_dir/appimage-sysroot.py" compiler-includes \
    "$compiler" "$sysroot" "$triple" --shell)
toolchain="$sdk_root/toolchain.cmake"
cat > "$toolchain" <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR riscv64)
set(CMAKE_C_COMPILER [==[$compiler]==])
set(CMAKE_CXX_COMPILER [==[$cxx_compiler]==])
set(CMAKE_AR [==[$ar_tool]==])
set(CMAKE_RANLIB [==[$ranlib_tool]==])
set(CMAKE_SYSROOT [==[$sysroot]==])
set(CMAKE_CROSSCOMPILING_EMULATOR [==[$qemu]==] -L [==[$sysroot]==])
set(RESTUNTS_SDK_INCLUDE_FLAGS [==[$include_flags]==])
set(ENV{PKG_CONFIG_PATH} "")
set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH FALSE)
set(ENV{PKG_CONFIG_SYSROOT_DIR} "\${CMAKE_SYSROOT}")
set(ENV{PKG_CONFIG_LIBDIR} "\${CMAKE_SYSROOT}/usr/lib/$triple/pkgconfig:\${CMAKE_SYSROOT}/usr/share/pkgconfig")
string(CONCAT CMAKE_C_FLAGS_INIT "$architecture_flags -D_GNU_SOURCE "
    "-B\"$target_gcc/\" -B\"\${CMAKE_SYSROOT}/usr/lib/$triple/\" "
    "\${RESTUNTS_SDK_INCLUDE_FLAGS} -fno-pie -static-libgcc")
string(CONCAT CMAKE_CXX_FLAGS_INIT "$architecture_flags -D_GNU_SOURCE -nostdinc++ "
    "-isystem\"$cxx_headers\" -isystem\"$target_headers\" -isystem\"$cxx_headers/backward\" "
    "-B\"$target_gcc/\" -B\"\${CMAKE_SYSROOT}/usr/lib/$triple/\" "
    "\${RESTUNTS_SDK_INCLUDE_FLAGS} -fno-pie -static-libgcc -static-libstdc++")
string(CONCAT CMAKE_SHARED_LINKER_FLAGS_INIT "-L\"$target_gcc\" "
    "-L\"\${CMAKE_SYSROOT}/usr/lib/$triple\" "
    "-Wl,-rpath-link,\"\${CMAKE_SYSROOT}/usr/lib/$triple\"")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-no-pie \${CMAKE_SHARED_LINKER_FLAGS_INIT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(RESTUNTS_WX_TOOLKIT gtk2 CACHE STRING "Baseline-compatible native controls" FORCE)
set(RESTUNTS_APPIMAGE_REQUIRE_PULSEAUDIO ON CACHE BOOL "Require desktop sound server backend" FORCE)
set(SDL_PULSEAUDIO ON CACHE BOOL "Native desktop sound server" FORCE)
set(SDL_PULSEAUDIO_SHARED ON CACHE BOOL "Relocatable PulseAudio client" FORCE)
foreach(feature X11_XCURSOR X11_XINPUT X11_XFIXES X11_XRANDR X11_XTEST X11_XSCRNSAVER WAYLAND KMSDRM VULKAN GPU_VULKAN CAMERA)
    set(SDL_\${feature} OFF CACHE BOOL "Portable AppImage SDK" FORCE)
endforeach()
EOF
env_file="$tool_root/appimage-sysroots/$target.env"
{
    printf 'export RESTUNTS_APPIMAGE_SYSROOT=%q\n' "$sysroot"
    printf 'export RESTUNTS_APPIMAGE_DEPS_PREFIX=%q\n' "$deps"
    printf 'export RESTUNTS_APPIMAGE_CMAKE_TOOLCHAIN=%q\n' "$toolchain"
    printf 'export RESTUNTS_APPIMAGE_QEMU=%q\n' "$qemu"
    printf 'export RESTUNTS_APPIMAGE_BASELINE=%q\n' "$baseline"
    printf 'export RESTUNTS_APPIMAGE_GLIBC_VERSION=%q\n' "$glibc_version"
    printf 'export PKG_CONFIG_PATH=\n'
    printf 'export PKG_CONFIG_SYSROOT_DIR=%q\n' "$sysroot"
    printf 'export PKG_CONFIG_LIBDIR=%q\n' "$sysroot/usr/lib/$triple/pkgconfig:$sysroot/usr/share/pkgconfig"
} > "$env_file"
echo "Prepared RISC-V AppImage SDK environment: $env_file"
