#!/usr/bin/env bash
# Rebuild PulseAudio dependencies without an optional host NIS ABI.
set -euo pipefail
umask 022
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$script_dir/package-toolchains.conf"
architecture=${1:?Usage: build-appimage-pulse-dependencies.sh ARCHITECTURE SYSROOT OUTPUT_PREFIX}
sysroot=$(realpath -- "${2:?Missing legacy sysroot}")
output=$(realpath -m -- "${3:?Missing runtime prefix}")
case "$architecture" in
    x64) lib_directory=lib64; legacy_target=x86_64-redhat-linux
        host_triple=x86_64-linux-gnu; architecture_flags= ;;
    x86) lib_directory=lib; legacy_target=i386-redhat-linux; host_triple=i686-linux-gnu
        architecture_flags='-m32 -march=i586 -mno-sse -mno-sse2 -mno-avx -mfpmath=387' ;;
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
legacy_gcc="$sysroot/opt/rh/devtoolset-2/root/usr/lib/gcc/$legacy_target/4.8.2"
compiler=${CC:-gcc}
read -r -a baseline_arguments <<< "$architecture_flags"
{
    printf '#!/usr/bin/env bash\nexec '
    printf '%q ' "$compiler" "--sysroot=$sysroot" "-B$legacy_gcc/" \
        "-B$sysroot/usr/$lib_directory/" -static-libgcc -Wl,--as-needed "${baseline_arguments[@]}"
    printf '"$@"\n'
} > "$work/compiler"
chmod 755 "$work/compiler"
(
    cd "$work/source"
    make -j "${RESTUNTS_BUILD_JOBS:-2}" "CC=$work/compiler" \
        "COPTS=$architecture_flags -O2 -fPIC -fcommon -std=gnu89 -fno-stack-protector" \
        LIBS= AUX_OBJ=weak_symbols.o NETGROUP= TLI= VSYSLOG= BUGS= \
        'EXTRA_CFLAGS=-DSYS_ERRLIST_DEFINED -DHAVE_STRERROR -DHAVE_WEAKSYMS -D_REENTRANT -DINET6=1 -Dss_family=__ss_family -Dss_len=__ss_len' \
        config-check shared/libwrap.so.0.7.6 > "$work/build.log" 2>&1
) || { tail -n 50 "$work/build.log" >&2; exit 1; }
# Reject accidental restoration of NIS, rather than relying on host libnsl.
if readelf -d "$work/source/shared/libwrap.so.0.7.6" | grep -q 'libnsl'; then
    echo 'libwrap must not require the optional host libnsl ABI.' >&2
    exit 1
fi
python3 - "$work/source/debian/libwrap0.symbols" "$work/source/shared/libwrap.so.0.7.6" <<'PYAPI'
from pathlib import Path
import subprocess, sys
required = {line.strip().split("@", 1)[0] for line in Path(sys.argv[1]).read_text().splitlines()
            if line.startswith(" ")}
exported = {line.split()[-1].split("@", 1)[0] for line in subprocess.check_output(
    ["nm", "-D", "--defined-only", sys.argv[2]], text=True).splitlines()}
if required - exported:
    raise RuntimeError("Missing real libwrap API: " + ", ".join(sorted(required - exported)))
PYAPI
cp "$work/source/shared/libwrap.so.0.7.6" "$output/lib/libwrap.so.0"
"$work/compiler" $architecture_flags -O2 -fno-pie -no-pie \
    -I "$work/source" "$script_dir/../appimage/test-libwrap-runtime.c" \
    "$output/lib/libwrap.so.0" -o "$work/test-libwrap-runtime"
LD_LIBRARY_PATH="$output/lib" "$work/test-libwrap-runtime"
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
(
    cd "$work/asyncns"
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
LD_LIBRARY_PATH="$output/lib" "$work/test-asyncns-runtime"
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
    tools/scripts/prepare-appimage-sysroot.sh tools/scripts/appimage-sysroot.py \
    tools/scripts/appimage_runtime.py tools/scripts/package-toolchains.conf \
    tools/appimage/test-libwrap-runtime.c tools/appimage/test-asyncns-runtime.c \
    tools/appimage/query-gtk-modules.c tools/appimage/legacy-cxx-runtime.cpp \
    tools/appimage/legacy-compiler-runtime.c tools/appimage/test-legacy-cxx-runtime.cpp \
    src/restunts/c/legacy.h; do
    mkdir -p "$context/$(dirname -- "$relative")"
    cp "$repo_directory/$relative" "$context/$relative"
done
printf 'Rebuild: cd appimage-pulse-build-context; RESTUNTS_TOOLCHAIN_ROOT=TOOLCHAIN_ROOT bash tools/scripts/build-appimage-pulse-dependencies.sh %s SDK_ROOT OUTPUT_PREFIX\n' \
    "$architecture" > "$output/share/sources/appimage-pulse-rebuild.txt"
