#!/usr/bin/env bash
# Rebuild PulseAudio dependencies without an optional host NIS ABI.
set -euo pipefail
umask 022
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$script_dir/package-toolchains.conf"
architecture=${1:?Usage: build-appimage-pulse-dependencies.sh ARCHITECTURE SYSROOT OUTPUT_PREFIX}
sysroot=$(realpath -- "${2:?Missing legacy sysroot}")
output=$(realpath -m -- "${3:?Missing runtime prefix}")
legacy_gcc_prefix=opt/rh/devtoolset-2/root/usr/lib/gcc
gcc_version=4.8.2
socket_compatibility='-Dss_family=__ss_family -Dss_len=__ss_len'
default_compiler=gcc
default_nm=nm
qemu_name=
case "$architecture" in
    x64) lib_directory=lib64; legacy_target=x86_64-redhat-linux
        host_triple=x86_64-linux-gnu; architecture_flags= ;;
    x86) lib_directory=lib; legacy_target=i386-redhat-linux; host_triple=i686-linux-gnu
        architecture_flags='-m32 -march=i586 -mno-sse -mno-sse2 -mno-avx -mfpmath=387' ;;
    armv7) host_triple=arm-linux-gnueabihf; legacy_target=$host_triple
        lib_directory="lib/$host_triple"; legacy_gcc_prefix=usr/lib/gcc; gcc_version=$APPIMAGE_ARMV7_GCC_VERSION
        architecture_flags='-march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard'
        socket_compatibility=; default_compiler=$host_triple-gcc; default_nm=$host_triple-nm
        qemu_name=qemu-arm ;;
    arm64) host_triple=aarch64-linux-gnu; legacy_target=$host_triple
        lib_directory="lib/$host_triple"; legacy_gcc_prefix=usr/lib/gcc; gcc_version=$APPIMAGE_ARM64_GCC_VERSION
        architecture_flags='-march=armv8-a -mno-outline-atomics'
        socket_compatibility=; default_compiler=$host_triple-gcc; default_nm=$host_triple-nm
        qemu_name=qemu-aarch64 ;;
    *) echo "Unsupported libwrap architecture: $architecture" >&2; exit 1 ;;
esac
tool_root=${RESTUNTS_TOOLCHAIN_ROOT:-${RUNNER_TEMP:-/tmp}/restunts-toolchains}
downloads="$tool_root/downloads"
mkdir -p "$downloads" "$output/lib" "$output/share/licenses/libwrap" "$output/share/sources"
fetch() {
    python3 "$script_dir/appimage-sysroot.py" download "$1" "$2" "$3"
}
source_archive="$downloads/tcp-wrappers_7.6.q.orig.tar.gz"
patch_archive="$downloads/tcp-wrappers_7.6.q-19.debian.tar.gz"
fetch "$APPIMAGE_LIBWRAP_SOURCE_URL" "$APPIMAGE_LIBWRAP_SOURCE_SHA256" "$source_archive"
fetch "$APPIMAGE_LIBWRAP_PATCHES_URL" "$APPIMAGE_LIBWRAP_PATCHES_SHA256" "$patch_archive"
work=$(mktemp -d "${TMPDIR:-/tmp}/restunts-libwrap.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
mkdir "$work/source"
tar -xzf "$source_archive" -C "$work/source" --strip-components=1
tar -xzf "$patch_archive" -C "$work/source"
while IFS= read -r patch_name; do
    [[ -z "$patch_name" || "$patch_name" == \#* ]] && continue
    patch -d "$work/source" -p1 --batch --forward < "$work/source/debian/patches/$patch_name" \
        >> "$work/patches.log"
done < "$work/source/debian/patches/series"
legacy_gcc="$sysroot/$legacy_gcc_prefix/$legacy_target/$gcc_version"
compiler=${CC:-$default_compiler}
nm_tool=${NM:-$default_nm}
if [[ -n "$qemu_name" && -z "${RESTUNTS_APPIMAGE_QEMU:-}" ]]; then
    RESTUNTS_APPIMAGE_QEMU=$(command -v "$qemu_name-static" || command -v "$qemu_name")
fi
run_target() {
    if [[ -n "${RESTUNTS_APPIMAGE_QEMU:-}" ]]; then
        "$RESTUNTS_APPIMAGE_QEMU" -L "$sysroot" -E "LD_LIBRARY_PATH=$output/lib" "$@"
    else
        LD_LIBRARY_PATH="$output/lib" "$@"
    fi
}
read -r -a baseline_arguments <<< "$architecture_flags"
include_arguments=()
if [[ "$architecture" == armv7 || "$architecture" == arm64 ]]; then
    include_arguments_text=$(python3 "$script_dir/appimage-sysroot.py" compiler-includes \
        "$compiler" "$sysroot" "$host_triple")
    mapfile -t include_arguments <<< "$include_arguments_text"
fi
{
    printf '#!/usr/bin/env bash\nexec '
    printf '%q ' "$compiler" "--sysroot=$sysroot" "-B$legacy_gcc/" \
        "-B$sysroot/usr/$lib_directory/" -static-libgcc -Wl,--as-needed \
        "${baseline_arguments[@]}" "${include_arguments[@]}"
    printf '"$@"\n'
} > "$work/compiler"
chmod 755 "$work/compiler"
(
    cd "$work/source"
    make -j "${RESTUNTS_BUILD_JOBS:-2}" "CC=$work/compiler" \
        "COPTS=$architecture_flags -O2 -fPIC -fcommon -std=gnu89 -fno-stack-protector" \
        LIBS= AUX_OBJ=weak_symbols.o NETGROUP= TLI= VSYSLOG= BUGS= \
        "EXTRA_CFLAGS=-DSYS_ERRLIST_DEFINED -DHAVE_STRERROR -DHAVE_WEAKSYMS -D_REENTRANT -DINET6=1 $socket_compatibility" \
        config-check shared/libwrap.so.0.7.6 > "$work/build.log" 2>&1
) || { tail -n 50 "$work/build.log" >&2; exit 1; }
# Reject accidental restoration of NIS, rather than relying on host libnsl.
if readelf -d "$work/source/shared/libwrap.so.0.7.6" | grep -q 'libnsl'; then
    echo 'libwrap must not require the optional host libnsl ABI.' >&2
    exit 1
fi
python3 - "$work/source/debian/libwrap0.symbols" "$work/source/shared/libwrap.so.0.7.6" "$nm_tool" <<'PYAPI'
from pathlib import Path
import subprocess, sys
required = {line.strip().split("@", 1)[0] for line in Path(sys.argv[1]).read_text().splitlines()
            if line.startswith(" ")}
exported = {line.split()[-1].split("@", 1)[0] for line in subprocess.check_output(
    [sys.argv[3], "-D", "--defined-only", sys.argv[2]], text=True).splitlines()}
if required - exported:
    raise RuntimeError("Missing real libwrap API: " + ", ".join(sorted(required - exported)))
PYAPI
cp "$work/source/shared/libwrap.so.0.7.6" "$output/lib/libwrap.so.0"
"$work/compiler" $architecture_flags -O2 -fno-pie -no-pie \
    -I "$work/source" "$script_dir/../appimage/test-libwrap-runtime.c" \
    "$output/lib/libwrap.so.0" -o "$work/test-libwrap-runtime"
run_target "$work/test-libwrap-runtime"
cp "$work/source/DISCLAIMER" "$work/source/debian/copyright" "$output/share/licenses/libwrap/"
cp "$source_archive" "$patch_archive" "$script_dir/build-appimage-pulse-dependencies.sh" \
    "$script_dir/../appimage/test-libwrap-runtime.c" "$output/share/sources/"
{
    printf 'TCP wrappers: 7.6.q-19; official Debian patches applied in series order\n'
    printf 'Source SHA256: %s\nPatches SHA256: %s\n' \
        "$APPIMAGE_LIBWRAP_SOURCE_SHA256" "$APPIMAGE_LIBWRAP_PATCHES_SHA256"
    printf 'Architecture flags: %s\n' "$architecture_flags"
    printf 'Optional NIS/netgroup support: disabled (NETGROUP=, LIBS=)\n'
    printf 'IPv4, IPv6, CIDR and host access policies: enabled and regression tested\n'
    printf 'Compiler compatibility: -std=gnu89 -fcommon; pinned CRT and static libgcc\n'
} > "$output/share/sources/appimage-libwrap-build-info.txt"
chmod 644 "$output/lib/libwrap.so.0" "$output/share/licenses/libwrap/"* \
    "$output/share/sources/appimage-libwrap-build-info.txt"

# The historical resolver build unconditionally linked -lnsl after probing
# gethostbyname, although all used resolver APIs are supplied by libc/resolv.
# --as-needed keeps the actual source implementation and its genuine closure.
asyncns_archive="$downloads/libasyncns_0.3.orig.tar.gz"
asyncns_patches="$downloads/libasyncns_0.3-1.1.diff.gz"
fetch "$APPIMAGE_ASYNCNS_SOURCE_URL" "$APPIMAGE_ASYNCNS_SOURCE_SHA256" "$asyncns_archive"
fetch "$APPIMAGE_ASYNCNS_PATCHES_URL" "$APPIMAGE_ASYNCNS_PATCHES_SHA256" "$asyncns_patches"
mkdir "$work/asyncns"
tar -xzf "$asyncns_archive" -C "$work/asyncns" --strip-components=1
gzip -dc "$asyncns_patches" | patch -d "$work/asyncns" -p1 --batch --forward \
    > "$work/asyncns-patches.log"
if [[ "$architecture" == armv7 || "$architecture" == arm64 ]]; then
    # The source predates AArch64. The host autotools package supplies current
    # canonical target names; preserve its scripts in the rebuild context.
    config_directory=${RESTUNTS_APPIMAGE_CONFIG_DIRECTORY:-$script_dir/gnu-config}
    if [[ -z "${RESTUNTS_APPIMAGE_CONFIG_DIRECTORY:-}" && ! -f "$config_directory/config.sub" ]]; then
        config_directory=/usr/share/misc
    fi
    cp "$config_directory/config.sub" "$config_directory/config.guess" "$work/asyncns/"
    cp "$config_directory/config.sub" "$config_directory/config.guess" "$output/share/sources/"
fi
(
    cd "$work/asyncns"
    # Every pinned target uses glibc's non-null malloc(0). Autoconf otherwise
    # guesses no while cross-compiling and requests a nonexistent rpl_malloc.
    ac_cv_func_malloc_0_nonnull=yes \
        CC="$work/compiler" CFLAGS="$architecture_flags -O2 -fPIC -fno-stack-protector" \
        LDFLAGS="-Wl,--as-needed -L$legacy_gcc -L$sysroot/$lib_directory -L$sysroot/usr/$lib_directory -Wl,-rpath-link,$sysroot/$lib_directory -Wl,-rpath-link,$sysroot/usr/$lib_directory" \
        ./configure --host="$host_triple" --prefix=/usr --libdir="/usr/$lib_directory" \
            --disable-static --disable-lynx > "$work/asyncns-configure.log" 2>&1 &&
    make -j "${RESTUNTS_BUILD_JOBS:-2}" > "$work/asyncns-build.log" 2>&1
) || {
    tail -n 50 "$work/asyncns-configure.log" "$work/asyncns-build.log" >&2 2>/dev/null
    exit 1
}
if readelf -d "$work/asyncns/libasyncns/.libs/libasyncns.so.0" | grep -q 'libnsl'; then
    echo 'libasyncns must not require the optional host libnsl ABI.' >&2
    exit 1
fi
cp -L "$work/asyncns/libasyncns/.libs/libasyncns.so.0" "$output/lib/libasyncns.so.0"
"$work/compiler" $architecture_flags -O2 -fno-pie -no-pie \
    -I "$work/asyncns/libasyncns" "$script_dir/../appimage/test-asyncns-runtime.c" \
    "$output/lib/libasyncns.so.0" -o "$work/test-asyncns-runtime"
run_target "$work/test-asyncns-runtime"
mkdir -p "$output/share/licenses/libasyncns"
cp "$work/asyncns/LICENSE" "$work/asyncns/debian/copyright" "$output/share/licenses/libasyncns/"
cp "$asyncns_archive" "$asyncns_patches" "$script_dir/../appimage/test-asyncns-runtime.c" \
    "$output/share/sources/"
{
    printf 'libasyncns: 0.3-1.1; official Debian source patch applied\n'
    printf 'Source SHA256: %s\nPatches SHA256: %s\n' \
        "$APPIMAGE_ASYNCNS_SOURCE_SHA256" "$APPIMAGE_ASYNCNS_PATCHES_SHA256"
    printf 'Architecture flags: %s\nLinker: --as-needed\n' "$architecture_flags"
    printf 'Numeric IPv4/IPv6 asynchronous resolver queries: regression tested\n'
} > "$output/share/sources/appimage-asyncns-build-info.txt"
chmod 644 "$output/lib/libasyncns.so.0" "$output/share/licenses/libasyncns/"* \
    "$output/share/sources/appimage-asyncns-build-info.txt"

# Keep the exact project files needed to recreate both the link-only SDK and
# these libraries, including the relative legacy type header used by tests.
repo_directory=$(cd -- "$script_dir/../.." && pwd)
context="$output/share/sources/appimage-pulse-build-context"
for relative in tools/scripts/build-appimage-pulse-dependencies.sh \
    tools/scripts/build-appimage-font-dependencies.sh tools/appimage/test-font-runtime.c \
    tools/appimage/test-font-overflow.c \
    tools/scripts/prepare-appimage-sysroot.sh tools/scripts/prepare-appimage-arm-sysroot.sh \
    tools/scripts/appimage-sysroot.py \
    tools/scripts/appimage_runtime.py tools/scripts/package-toolchains.conf \
    tools/appimage/test-libwrap-runtime.c tools/appimage/test-asyncns-runtime.c \
    tools/appimage/query-gtk-modules.c tools/appimage/legacy-cxx-runtime.cpp \
    tools/appimage/legacy-compiler-runtime.c tools/appimage/test-legacy-cxx-runtime.cpp \
    third_party/gcc-runtime/COPYING3.txt third_party/gcc-runtime/COPYING.RUNTIME.txt \
    src/restunts/c/legacy.h; do
    mkdir -p "$context/$(dirname -- "$relative")"
    cp "$repo_directory/$relative" "$context/$relative"
done
if [[ "$architecture" == armv7 || "$architecture" == arm64 ]]; then
    mkdir -p "$context/tools/scripts/gnu-config"
    cp "$config_directory/config.sub" "$config_directory/config.guess" \
        "$context/tools/scripts/gnu-config/"
fi
printf 'Rebuild: cd appimage-pulse-build-context; RESTUNTS_TOOLCHAIN_ROOT=TOOLCHAIN_ROOT bash tools/scripts/build-appimage-pulse-dependencies.sh %s SDK_ROOT OUTPUT_PREFIX\n' \
    "$architecture" > "$output/share/sources/appimage-pulse-rebuild.txt"
