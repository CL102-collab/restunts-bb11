#!/usr/bin/env bash
# Build historical font libraries with 64-bit filesystem metadata on 32-bit Linux.
set -euo pipefail
umask 022
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$script_dir/package-toolchains.conf"
architecture=${1:?Usage: build-appimage-font-dependencies.sh ARCHITECTURE SYSROOT OUTPUT_PREFIX}
sysroot=$(realpath -- "${2:?Missing legacy sysroot}")
output=$(realpath -m -- "${3:?Missing runtime prefix}")
case "$architecture" in
    x86)
        host_triple=i686-linux-gnu; lib_directory=lib; fontconfig_version=2.8.0; freetype_version=2.4.2
        legacy_gcc="$sysroot/opt/rh/devtoolset-2/root/usr/lib/gcc/i386-redhat-linux/4.8.2"
        architecture_flags='-m32 -march=i586 -mno-sse -mno-sse2 -mno-avx -mfpmath=387'
        compiler=${CC:-gcc}; qemu=${RESTUNTS_APPIMAGE_QEMU:-}; pin_arch=X86
        ;;
    armv7)
        host_triple=arm-linux-gnueabihf; lib_directory="lib/$host_triple"
        fontconfig_version=2.9.0; freetype_version=2.4.9
        legacy_gcc="$sysroot/usr/lib/gcc/$host_triple/$APPIMAGE_ARMV7_GCC_VERSION"
        architecture_flags='-march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard'
        compiler=${CC:-$host_triple-gcc}; pin_arch=ARMV7
        qemu=${RESTUNTS_APPIMAGE_QEMU:-$(command -v qemu-arm-static || command -v qemu-arm)}
        ;;
    *) echo "No 32-bit font rebuild is required for $architecture."; exit 0 ;;
esac
tool_root=${RESTUNTS_TOOLCHAIN_ROOT:-${RUNNER_TEMP:-/tmp}/restunts-toolchains}
downloads="$tool_root/downloads"
mkdir -p "$downloads" "$output/lib" "$output/share/sources" "$output/share/licenses/fontconfig" \
    "$output/share/licenses/freetype"
work=$(mktemp -d "${TMPDIR:-/tmp}/restunts-fonts.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
fetch_pin() {
    local url_variable=$1 checksum_variable=$2 destination_variable=$3
    local url=${!url_variable} checksum=${!checksum_variable} destination
    destination="$downloads/${url##*/}"
    python3 "$script_dir/appimage-sysroot.py" download "$url" "$checksum" "$destination"
    printf -v "$destination_variable" '%s' "$destination"
    cp "$destination" "$output/share/sources/"
}
fetch_pin "APPIMAGE_FREETYPE_${pin_arch}_SOURCE_URL" "APPIMAGE_FREETYPE_${pin_arch}_SOURCE_SHA256" freetype_archive
mkdir "$work/freetype"
tar -xf "$freetype_archive" -C "$work/freetype" --strip-components=1
if [[ "$architecture" == x86 ]]; then
    fetch_pin APPIMAGE_FREETYPE_X86_PATCHES_URL APPIMAGE_FREETYPE_X86_PATCHES_SHA256 freetype_patches
    gzip -dc "$freetype_patches" | patch -d "$work/freetype" -p1 --batch --forward > "$work/freetype-packaging.log"
fi
tar -xf "$work/freetype/freetype-$freetype_version.tar.bz2" -C "$work/freetype"
apply_series() {
    local source=$1 patches=$2 line patch_name
    while IFS= read -r line || [[ -n "$line" ]]; do
        read -r -a arguments <<< "$line"
        (( ${#arguments[@]} )) || continue
        [[ "${arguments[0]}" == \#* ]] && continue
        patch_name=${arguments[0]}
        patch -d "$source" -p1 "${arguments[@]:1}" --batch --forward < "$patches/$patch_name"
    done < "$patches/series"
}
apply_series "$work/freetype/freetype-$freetype_version" "$work/freetype/debian/patches-freetype" \
    > "$work/freetype-patches.log"
read -r -a baseline_arguments <<< "$architecture_flags"
include_arguments=()
if [[ "$architecture" == armv7 ]]; then
    include_arguments_text=$(python3 "$script_dir/appimage-sysroot.py" compiler-includes "$compiler" "$sysroot" "$host_triple")
    mapfile -t include_arguments <<< "$include_arguments_text"
fi
link_arguments=("-L$legacy_gcc" "-L$output/lib" "-L$sysroot/$lib_directory" "-L$sysroot/usr/$lib_directory"
    "-Wl,-rpath-link,$output/lib" "-Wl,-rpath-link,$sysroot/$lib_directory" "-Wl,-rpath-link,$sysroot/usr/$lib_directory")
{
    printf '#!/usr/bin/env bash\nexec '
    printf '%q ' "$compiler" "--sysroot=$sysroot" "-B$legacy_gcc/" "-B$sysroot/usr/$lib_directory/" \
        -static-libgcc -Wl,--as-needed "${baseline_arguments[@]}" "${include_arguments[@]}" "${link_arguments[@]}"
    printf '"$@"\n'
} > "$work/compiler"
chmod 755 "$work/compiler"
(
    cd "$work/freetype/freetype-$freetype_version"
    CC="$work/compiler" CPPFLAGS=-D_FILE_OFFSET_BITS=64 \
        CFLAGS="$architecture_flags -O2 -fPIC -fno-stack-protector -fno-strict-aliasing" \
        ./configure --host="$host_triple" --prefix=/usr --libdir="/usr/$lib_directory" \
            --disable-static > "$work/freetype-configure.log" 2>&1
    make -j "${RESTUNTS_BUILD_JOBS:-2}" > "$work/freetype-build.log" 2>&1
) || { tail -n 50 "$work/freetype-"*.log >&2; exit 1; }
cp -L "$work/freetype/freetype-$freetype_version/objs/.libs/libfreetype.so.6" "$output/lib/libfreetype.so.6"
cp "$work/freetype/freetype-$freetype_version/docs/FTL.TXT" \
    "$work/freetype/freetype-$freetype_version/docs/"GPL*.TXT \
    "$work/freetype/debian/copyright" "$output/share/licenses/freetype/"

fetch_pin "APPIMAGE_FONTCONFIG_${pin_arch}_SOURCE_URL" "APPIMAGE_FONTCONFIG_${pin_arch}_SOURCE_SHA256" fontconfig_archive
fetch_pin "APPIMAGE_FONTCONFIG_${pin_arch}_PATCHES_URL" "APPIMAGE_FONTCONFIG_${pin_arch}_PATCHES_SHA256" fontconfig_patches
mkdir "$work/fontconfig"
tar -xf "$fontconfig_archive" -C "$work/fontconfig" --strip-components=1
if [[ "$architecture" == x86 ]]; then
    gzip -dc "$fontconfig_patches" | patch -d "$work/fontconfig" -p1 --batch --forward > "$work/fontconfig-packaging.log"
else
    tar -xf "$fontconfig_patches" -C "$work/fontconfig"
fi
if [[ -f "$work/fontconfig/debian/patches/series" ]]; then
    apply_series "$work/fontconfig" "$work/fontconfig/debian/patches" > "$work/fontconfig-patches.log"
fi
# Historical configure runs freetype-config. This host shim uses only the
# pinned target .pc file, without executing a target SDK program.
{
    printf '#!/usr/bin/env bash\nexport PKG_CONFIG_PATH=\n'
    printf 'export PKG_CONFIG_SYSROOT_DIR=%q\n' "$sysroot"
    printf 'export PKG_CONFIG_LIBDIR=%q\n' "$sysroot/usr/$lib_directory/pkgconfig"
    printf 'case "$1" in --version) exec pkg-config --modversion freetype2;; *) exec pkg-config "$@" freetype2;; esac\n'
} > "$work/freetype-config"
chmod 755 "$work/freetype-config"
(
    cd "$work/fontconfig"
    CC="$work/compiler" CC_FOR_BUILD=gcc CPPFLAGS=-D_FILE_OFFSET_BITS=64 \
        CFLAGS="$architecture_flags -O2 -fPIC -fno-stack-protector" \
        ./configure --host="$host_triple" --prefix=/usr --libdir="/usr/$lib_directory" \
            --with-arch="$([[ "$architecture" == x86 ]] && echo le32d4 || echo le32d8)" \
            --with-freetype-config="$work/freetype-config" --disable-static --disable-docs \
            > "$work/fontconfig-configure.log" 2>&1
    make -j "${RESTUNTS_BUILD_JOBS:-2}" > "$work/fontconfig-build.log" 2>&1
) || { tail -n 50 "$work/fontconfig-"*.log >&2; exit 1; }
cp -L "$work/fontconfig/src/.libs/libfontconfig.so.1" "$output/lib/libfontconfig.so.1"
cp "$work/fontconfig/COPYING" "$work/fontconfig/debian/copyright" "$output/share/licenses/fontconfig/"
mkdir "$work/fonts"
cp "$work/fontconfig/test/4x6.pcf" "$work/fonts/"
mkdir "$work/font-config" "$work/font-cache"
python3 - "$work" <<'PY'
from pathlib import Path
import html, sys
root = Path(sys.argv[1])
(root / 'font-config/00-fonts.conf').write_text('<fontconfig><dir>' + html.escape(str(root / 'fonts')) +
    '</dir><cachedir>' + html.escape(str(root / 'font-cache')) + '</cachedir></fontconfig>')
(root / 'fonts.conf').write_text('<fontconfig><include>' + html.escape(str(root / 'font-config')) +
    '</include></fontconfig>')
PY
"$work/compiler" -D_FILE_OFFSET_BITS=64 -O2 -fno-pie -no-pie -I "$sysroot/usr/include/freetype2" \
    "$script_dir/../appimage/test-font-runtime.c" "$output/lib/libfontconfig.so.1" "$output/lib/libfreetype.so.6" \
    -o "$work/test-font-runtime"
# Keep the shim narrow even when the compiler defaults to 64-bit file/time types.
"$work/compiler" -U_TIME_BITS -U_FILE_OFFSET_BITS -D_FILE_OFFSET_BITS=32 \
    -O2 -shared -fPIC "$script_dir/../appimage/test-font-overflow.c" \
    -o "$work/test-font-overflow.so"
run_test() {
    local preload=$1
    if [[ -n "$qemu" ]]; then
        "$qemu" -L "$sysroot" -E "LD_LIBRARY_PATH=$output/lib" -E "LD_PRELOAD=$preload" \
            "$work/test-font-runtime" "$work/fonts" "$work/fonts/4x6.pcf" "$work/fonts.conf"
    else
        LD_LIBRARY_PATH="$output/lib" LD_PRELOAD="$preload" \
            "$work/test-font-runtime" "$work/fonts" "$work/fonts/4x6.pcf" "$work/fonts.conf"
    fi
}
run_test ''
run_test "$work/test-font-overflow.so"
cp "$script_dir/build-appimage-font-dependencies.sh" "$script_dir/../appimage/test-font-runtime.c" \
    "$script_dir/../appimage/test-font-overflow.c" "$output/share/sources/"
{
    printf 'FreeType: %s; Fontconfig: %s; matching distribution patches applied\n' "$freetype_version" "$fontconfig_version"
    printf 'Architecture flags: %s\n' "$architecture_flags"
    printf 'FreeType and Fontconfig: _FILE_OFFSET_BITS=64\n'
    printf 'Fontconfig cache metadata clients: _FILE_OFFSET_BITS=64\n'
    printf 'Real font file loading and directory enumeration: regression tested\n'
} > "$output/share/sources/appimage-font-build-info.txt"
printf 'Rebuild: cd appimage-pulse-build-context; RESTUNTS_TOOLCHAIN_ROOT=TOOLCHAIN_ROOT bash tools/scripts/build-appimage-font-dependencies.sh %s SDK_ROOT OUTPUT_PREFIX\n' \
    "$architecture" > "$output/share/sources/appimage-font-rebuild.txt"
chmod 644 "$output/lib/"{libfontconfig.so.1,libfreetype.so.6} "$output/share/licenses/fontconfig/"* \
    "$output/share/licenses/freetype/"* "$output/share/sources/appimage-font-build-info.txt"
