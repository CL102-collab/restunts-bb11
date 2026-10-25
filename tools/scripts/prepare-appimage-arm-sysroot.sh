#!/usr/bin/env bash
# Build ARM AppImage SDKs from verified historical distribution packages.
set -euo pipefail
umask 022

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$script_dir/package-toolchains.conf"
target=${1:?Usage: prepare-appimage-arm-sysroot.sh TARGET}
tool_root=${RESTUNTS_TOOLCHAIN_ROOT:-${RUNNER_TEMP:-/tmp}/restunts-toolchains}
jobs=${RESTUNTS_BUILD_JOBS:-2}
case "$target" in
    linux-appimage-armv7)
        architecture=armv7; triple=arm-linux-gnueabihf; processor=armv7
        archive_root=$APPIMAGE_ARMV7_ARCHIVE_ROOT
        index_url=$APPIMAGE_ARMV7_INDEX_URL; index_sha=$APPIMAGE_ARMV7_INDEX_SHA256
        gcc_version=$APPIMAGE_ARMV7_GCC_VERSION
        gcc_package=$APPIMAGE_ARMV7_GCC_PACKAGE; cxx_package=$APPIMAGE_ARMV7_CXX_PACKAGE
        architecture_flags='-march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard'
        cxx_target_headers="usr/include/c++/$gcc_version/$triple"
        qemu_name=qemu-arm; baseline='Raspbian Wheezy (glibc 2.13)'; glibc_version=2.13
        ;;
    linux-appimage-arm64)
        architecture=arm64; triple=aarch64-linux-gnu; processor=aarch64
        archive_root=$APPIMAGE_ARM64_ARCHIVE_ROOT
        index_url=$APPIMAGE_ARM64_INDEX_URL; index_sha=$APPIMAGE_ARM64_INDEX_SHA256
        gcc_version=$APPIMAGE_ARM64_GCC_VERSION
        gcc_package=$APPIMAGE_ARM64_GCC_PACKAGE; cxx_package=$APPIMAGE_ARM64_CXX_PACKAGE
        architecture_flags='-march=armv8-a -mno-outline-atomics'
        cxx_target_headers="usr/include/$triple/c++/$gcc_version"
        qemu_name=qemu-aarch64; baseline='Debian Jessie (glibc 2.19)'; glibc_version=2.19
        ;;
    *) echo "Unknown ARM AppImage target: $target" >&2; exit 1 ;;
esac
compiler=$(command -v "${CC:-$triple-gcc}")
cxx_compiler=$(command -v "${CXX:-$triple-g++}")
ar_tool=$(command -v "${AR:-$triple-ar}")
ranlib_tool=$(command -v "${RANLIB:-$triple-ranlib}")
qemu=${RESTUNTS_APPIMAGE_QEMU:-$(command -v "$qemu_name-static" || command -v "$qemu_name")}
export RESTUNTS_APPIMAGE_QEMU="$qemu"
export NM="${NM:-$triple-nm}"
config_directory=${RESTUNTS_APPIMAGE_CONFIG_DIRECTORY:-$script_dir/gnu-config}
if [[ -z "${RESTUNTS_APPIMAGE_CONFIG_DIRECTORY:-}" && ! -f "$config_directory/config.sub" ]]; then
    config_directory=/usr/share/misc
fi
export RESTUNTS_APPIMAGE_CONFIG_DIRECTORY="$config_directory"
[[ -f "$config_directory/config.sub" && -f "$config_directory/config.guess" ]] || {
    echo 'ARM SDK preparation requires the host autotools-dev configuration scripts.' >&2
    exit 1
}
sdk_root="$tool_root/appimage-sysroots/$architecture"
downloads="$tool_root/downloads"
mkdir -p "$sdk_root" "$downloads"
exec 9> "$sdk_root/.prepare.lock"
flock 9
recipe=$(cat "$script_dir/prepare-appimage-arm-sysroot.sh" "$script_dir/appimage-sysroot.py" \
    "$script_dir/package-toolchains.conf" "$script_dir/appimage_runtime.py" \
    "$script_dir/build-appimage-pulse-dependencies.sh" \
    "$script_dir/../appimage/legacy-cxx-runtime.cpp" \
    "$script_dir/../appimage/test-legacy-cxx-runtime.cpp" \
    "$script_dir/../appimage/query-gtk-modules.c" \
    "$script_dir/../appimage/test-libwrap-runtime.c" \
    "$script_dir/../appimage/test-asyncns-runtime.c" "$script_dir/../../src/restunts/c/legacy.h" \
    "$config_directory/config.sub" "$config_directory/config.guess"; \
    "$compiler" --version; "$cxx_compiler" --version)
recipe_sha=$(printf '%s' "$recipe" | sha256sum | cut -d ' ' -f 1)
sysroot="$sdk_root/sysroot"
deps="$sdk_root/deps"
cached_recipe=
if [[ -f "$sdk_root/recipe.sha256" ]]; then read -r cached_recipe < "$sdk_root/recipe.sha256"; fi
if [[ "$cached_recipe" != "$recipe_sha" ]]; then
    stage=$(mktemp -d "$sdk_root/.prepare.XXXXXXXX")
    trap 'rm -rf -- "$stage"' EXIT
    echo "Preparing pinned ARM AppImage SDK: $baseline."
    python3 "$script_dir/appimage-sysroot.py" debian-sdk \
        --index-url "$index_url" --index-sha256 "$index_sha" --archive-root "$archive_root" \
        --cache "$downloads" --destination "$stage/sysroot" \
        libgtk2.0-dev libglib2.0-dev libpango1.0-dev fontconfig-config libpulse-dev \
        libasound2-dev libx11-dev libxext-dev libxrender-dev libsm-dev \
        libc6-dev linux-libc-dev "$gcc_package" "$cxx_package" "gcc-$gcc_version-base"
    legacy_gcc="$stage/sysroot/usr/lib/gcc/$triple/$gcc_version"
    cxx_headers="$stage/sysroot/usr/include/c++/$gcc_version"
    target_headers="$stage/sysroot/$cxx_target_headers"
    [[ -f "$legacy_gcc/libstdc++.a" && -f "$target_headers/bits/c++config.h" ]] || {
        echo 'Pinned ARM SDK is missing its C++ headers/static runtime.' >&2; exit 1;
    }
    read -r -a architecture_arguments <<< "$architecture_flags"
    include_arguments_text=$(python3 "$script_dir/appimage-sysroot.py" compiler-includes \
        "$compiler" "$stage/sysroot" "$triple")
    mapfile -t include_arguments <<< "$include_arguments_text"
    common_arguments=("--sysroot=$stage/sysroot" "-B$legacy_gcc/"
        "-B$stage/sysroot/usr/lib/$triple/" "${include_arguments[@]}"
        "${architecture_arguments[@]}" -fno-stack-protector -static-libgcc)
    header_macros=$("$compiler" "${common_arguments[@]}" -dM -E -include features.h -x c /dev/null)
    header_version=$(awk '$2 == "__GLIBC__" {major=$3} $2 == "__GLIBC_MINOR__" {minor=$3}
        END {print major "." minor}' <<< "$header_macros")
    [[ "$header_version" == "$glibc_version" ]] || {
        echo "Cross compiler selected glibc $header_version headers; expected $glibc_version." >&2
        exit 1
    }
    link_arguments=("-L$legacy_gcc" "-L$stage/sysroot/lib/$triple"
        "-L$stage/sysroot/usr/lib/$triple" "-Wl,-rpath-link,$stage/sysroot/lib/$triple"
        "-Wl,-rpath-link,$stage/sysroot/usr/lib/$triple")
    cxx_arguments=(-std=c++11 -nostdinc++ "-isystem$cxx_headers"
        "-isystem$target_headers" "-isystem$cxx_headers/backward")
    # GCC 4.7 lacks the C++11 array-length exception emitted by modern compilers.
    if [[ "$architecture" == armv7 ]]; then
        python3 "$script_dir/appimage-sysroot.py" patch-cxx "$stage/sysroot" \
            "usr/include/c++/$gcc_version/new"
        "$cxx_compiler" "${common_arguments[@]}" "${cxx_arguments[@]}" -O2 -fPIC \
            -c "$script_dir/../appimage/legacy-cxx-runtime.cpp" -o "$stage/legacy-cxx-runtime.o"
        "$ar_tool" rcs "$legacy_gcc/libstdc++.a" "$stage/legacy-cxx-runtime.o"
    fi
    "$cxx_compiler" "${common_arguments[@]}" "${cxx_arguments[@]}" "${link_arguments[@]}" \
        -O2 -fno-pie -no-pie -static-libstdc++ \
        "$script_dir/../appimage/test-legacy-cxx-runtime.cpp" -o "$stage/test-legacy-cxx-runtime"
    "$qemu" -L "$stage/sysroot" "$stage/test-legacy-cxx-runtime"
    # Modern SDL needs the ALSA 1.0.27 declarations on Wheezy too. Rebuild the
    # same verified source against each baseline rather than taking host headers.
    python3 "$script_dir/appimage-sysroot.py" download "$APPIMAGE_ALSA_URL" \
        "$APPIMAGE_ALSA_SHA256" "$downloads/alsa-lib_1.0.27.2.orig.tar.bz2"
    tar -xjf "$downloads/alsa-lib_1.0.27.2.orig.tar.bz2" -C "$stage"
    mkdir "$stage/alsa-build"
    {
        printf '#!/usr/bin/env bash\nexec '
        printf '%q ' "$compiler" "${common_arguments[@]}" "${link_arguments[@]}"
        printf '"$@"\n'
    } > "$stage/compiler"
    chmod 755 "$stage/compiler"
    (
        cd "$stage/alsa-build"
        # Parse kernel typedefs before ALSA's userspace type aliases, including
        # on Wheezy's 32-bit headers with 64-bit file offsets enabled.
        CC="$stage/compiler" CFLAGS="$architecture_flags -O2 -fPIC -include linux/types.h" \
            "$stage/alsa-lib-1.0.27.2/configure" --prefix=/usr \
                --libdir="/usr/lib/$triple" --host="$triple" --disable-python \
                > configure.log 2>&1 &&
        make -j "$jobs" > build.log 2>&1 &&
        make DESTDIR="$stage/sysroot" install > install.log 2>&1
    ) || {
        tail -n 50 "$stage/alsa-build/"*.log >&2
        exit 1
    }
    python3 "$script_dir/appimage-sysroot.py" debian-runtime "$stage/sysroot" "$stage/deps" "$triple"
    CC="$compiler" bash "$script_dir/build-appimage-pulse-dependencies.sh" \
        "$architecture" "$stage/sysroot" "$stage/deps"
    python3 "$script_dir/appimage-sysroot.py" prune-runtime "$stage/deps/lib"
    while IFS= read -r -d '' library; do patchelf --remove-rpath "$library"; done \
        < <(find "$stage/deps/lib" -type f -name '*.so*' -print0)
    gtk_flags=$(PKG_CONFIG_PATH= PKG_CONFIG_SYSROOT_DIR="$stage/sysroot" \
        PKG_CONFIG_LIBDIR="$stage/sysroot/usr/lib/$triple/pkgconfig:$stage/sysroot/usr/share/pkgconfig" \
        pkg-config --cflags --libs gmodule-2.0 pango gdk-pixbuf-2.0)
    read -r -a gtk_arguments <<< "$gtk_flags"
    mkdir -p "$stage/deps/bin"
    "$compiler" "${common_arguments[@]}" "${link_arguments[@]}" -O2 -fno-pie -no-pie \
        "$script_dir/../appimage/query-gtk-modules.c" "${gtk_arguments[@]}" \
        '-Wl,-rpath,$ORIGIN/../lib' -o "$stage/deps/bin/restunts-gtk-modules"
    python3 - "$stage/deps" <<'PY'
from pathlib import Path
import sys
for config in (Path(sys.argv[1]) / 'share/alsa').rglob('*.conf'):
    text = config.read_text()
    config.write_text(text.replace('{ @func datadir }', '{ @func getenv vars [ RESTUNTS_ALSA_CONFIG_DIR ] default "/usr/share/alsa" }'))
PY
    cp "$downloads/alsa-lib_1.0.27.2.orig.tar.bz2" "$stage/deps/share/sources/"
    cp "$script_dir/../appimage/query-gtk-modules.c" \
        "$script_dir/../appimage/legacy-cxx-runtime.cpp" "$stage/deps/share/sources/"
    cp "$stage/alsa-lib-1.0.27.2/COPYING" "$stage/deps/share/licenses/ALSA-COPYING"
    mkdir -p "$stage/deps/share/licenses/gcc-runtime"
    cp "$script_dir/../../third_party/gcc-runtime/COPYING3.txt" \
        "$script_dir/../../third_party/gcc-runtime/COPYING.RUNTIME.txt" \
        "$stage/deps/share/licenses/gcc-runtime/"
    {
        printf 'Baseline: %s\nIndex: %s\nIndex SHA256: %s\n' "$baseline" "$index_url" "$index_sha"
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
legacy_gcc="$sysroot/usr/lib/gcc/$triple/$gcc_version"
cxx_headers="$sysroot/usr/include/c++/$gcc_version"
target_headers="$sysroot/$cxx_target_headers"
include_flags=$(python3 "$script_dir/appimage-sysroot.py" compiler-includes \
    "$compiler" "$sysroot" "$triple" --shell)
toolchain="$sdk_root/toolchain.cmake"
cat > "$toolchain" <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR $processor)
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
    "-B\\"$legacy_gcc/\\" -B\\"\${CMAKE_SYSROOT}/usr/lib/$triple/\\" "
    "\${RESTUNTS_SDK_INCLUDE_FLAGS} -fno-pie -static-libgcc")
string(CONCAT CMAKE_CXX_FLAGS_INIT "$architecture_flags -D_GNU_SOURCE -nostdinc++ "
    "-isystem\\"$cxx_headers\\" -isystem\\"$target_headers\\" -isystem\\"$cxx_headers/backward\\" "
    "-B\\"$legacy_gcc/\\" -B\\"\${CMAKE_SYSROOT}/usr/lib/$triple/\\" "
    "\${RESTUNTS_SDK_INCLUDE_FLAGS} -fno-pie -static-libgcc -static-libstdc++")
string(CONCAT CMAKE_SHARED_LINKER_FLAGS_INIT "-L\\"$legacy_gcc\\" "
    "-L\\"\${CMAKE_SYSROOT}/lib/$triple\\" -L\\"\${CMAKE_SYSROOT}/usr/lib/$triple\\" "
    "-Wl,-rpath-link,\\"\${CMAKE_SYSROOT}/lib/$triple\\" -Wl,-rpath-link,\\"\${CMAKE_SYSROOT}/usr/lib/$triple\\"")
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
    set(SDL_\${feature} OFF CACHE BOOL "Legacy AppImage SDK" FORCE)
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
echo "Prepared ARM AppImage SDK environment: $env_file"
