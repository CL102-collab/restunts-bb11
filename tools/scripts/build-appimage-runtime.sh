#!/usr/bin/env bash
# Build the 32-bit AppImage loader without SSE/SSE2, including its static libc.
# The ordinary and no-SSE2 x86 packages share this lowest-common-denominator loader.
set -euo pipefail
umask 022
command -v rg > /dev/null
command -v qemu-i386 > /dev/null

target=${1:?Usage: build-appimage-runtime.sh TARGET}
case "$target" in
    linux-appimage-x86|linux-appimage-x86-no-sse2) ;;
    *) echo "No custom AppImage runtime is required for $target" >&2; exit 1 ;;
esac
tool_root=${RESTUNTS_TOOLCHAIN_ROOT:-${RUNNER_TEMP:-/tmp}/restunts-toolchains}
build_dir="$tool_root/appimage-runtime-i586"
prefix="$build_dir/prefix"
jobs=${RESTUNTS_BUILD_JOBS:-2}
mkdir -p "$tool_root/downloads" "$build_dir"

download() {
    local name=$1 url=$2 checksum=$3
    local archive="$tool_root/downloads/$name"
    if ! printf '%s  %s\n' "$checksum" "$archive" | sha256sum --check --status 2>/dev/null; then
        curl --fail --location --retry 3 --connect-timeout 20 "$url" --output "$archive"
    fi
    printf '%s  %s\n' "$checksum" "$archive" | sha256sum --check --status
}

download musl-1.2.5.tar.gz \
    https://codeload.github.com/ifduyue/musl/tar.gz/refs/tags/v1.2.5 \
    83ff394502d1c334b040ea9bc66ec48bba453585e25b05f4bde3741d8245d883
download zlib-1.3.1.tar.xz \
    https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.xz \
    38ef96b8dfe510d42707d9c781877914792541133e1870841463bfa73f883e32
download zstd-1.5.7.tar.gz \
    https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz \
    eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
download fuse-3.15.0.tar.xz \
    https://github.com/libfuse/libfuse/releases/download/fuse-3.15.0/fuse-3.15.0.tar.xz \
    70589cfd5e1cff7ccd6ac91c86c01be340b227285c5e200baa284e401eea2ca0
download squashfuse-0.5.2.tar.gz \
    https://github.com/vasi/squashfuse/archive/0.5.2.tar.gz \
    db0238c5981dabbd80ee09ae15387f390091668ca060a7bc38047912491443d3
download type2-runtime-dd6cebed.tar.gz \
    https://codeload.github.com/AppImage/type2-runtime/tar.gz/dd6cebedcbddde9c82f89b011e8e1d40b6e43868 \
    f5fec23be76e50e2445ed2d018bac49b367490fc483c62c4637e99ec705d27ba

# Every dependency is rebuilt with the same CPU baseline. This also removes
# optional SSE2 paths present in the upstream i686 binary from this loader.
cpu_flags='-m32 -march=i586 -mno-sse -mno-sse2 -mfpmath=387'
export CFLAGS="$cpu_flags -Os -ffunction-sections -fdata-sections"
export LDFLAGS='-m32 -static -Wl,--gc-sections'
export PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig"
export PKG_CONFIG_PATH=
rm -rf "$build_dir/src" "$prefix"
mkdir -p "$build_dir/src" "$prefix/bin"
for archive in musl-1.2.5.tar.gz zlib-1.3.1.tar.xz zstd-1.5.7.tar.gz fuse-3.15.0.tar.xz \
    squashfuse-0.5.2.tar.gz type2-runtime-dd6cebed.tar.gz; do
    tar -xf "$tool_root/downloads/$archive" -C "$build_dir/src"
done

(
    cd "$build_dir/src/musl-1.2.5"
    CC=gcc AR=ar RANLIB=ranlib ./configure --prefix="$prefix" --target=i386 --disable-shared
    make -j"$jobs"
    make install
)
cat > "$prefix/bin/i586-musl-gcc" <<EOF
#!/bin/sh
exec gcc $cpu_flags -Wl,-m,elf_i386 -specs "$prefix/lib/musl-gcc.specs" "\$@"
EOF
chmod +x "$prefix/bin/i586-musl-gcc"
export CC="$prefix/bin/i586-musl-gcc"
export CPPFLAGS="-I$prefix/include"
export LDFLAGS="$LDFLAGS -L$prefix/lib"
(
    cd "$build_dir/src/zlib-1.3.1"
    ./configure --prefix="$prefix" --static
    make -j"$jobs"
    make install
)
(
    cd "$build_dir/src/zstd-1.5.7"
    make -C lib -j"$jobs" libzstd.a ZSTD_LEGACY_SUPPORT=0 ZSTD_MULTITHREAD=0 \
        CPPFLAGS="$CPPFLAGS -DZSTD_DISABLE_ASM -DDYNAMIC_BMI2=0"
    install -m 644 lib/libzstd.a "$prefix/lib/"
    install -m 644 lib/zstd.h lib/zstd_errors.h "$prefix/include/"
)
runtime_source="$build_dir/src/type2-runtime-dd6cebedcbddde9c82f89b011e8e1d40b6e43868"
(
    cd "$build_dir/src/fuse-3.15.0"
    # This is the exact helper-selection patch used by the pinned upstream runtime.
    patch -p1 < "$runtime_source/patches/libfuse/mount.c.diff"
    cat > cross.ini <<EOF
[binaries]
c = '$CC'
ar = 'ar'
strip = 'strip'
pkg-config = 'pkg-config'
exe_wrapper = ['qemu-i386', '-cpu', 'pentium3']
[host_machine]
system = 'linux'
cpu_family = 'x86'
cpu = 'i586'
endian = 'little'
[properties]
needs_exe_wrapper = true
[built-in options]
c_args = ['-Os', '-ffunction-sections', '-fdata-sections']
c_link_args = ['-static', '-Wl,--gc-sections']
EOF
    meson setup build --cross-file cross.ini --prefix="$prefix" --libdir=lib \
        --default-library=static -Dutils=false -Dexamples=false -Dtests=false \
        -Duseroot=false -Dinitscriptdir= -Ddisable-libc-symbol-version=true
    ninja -C build -j"$jobs" install
)
(
    cd "$build_dir/src/squashfuse-0.5.2"
    ./autogen.sh
    # Excluding unused codecs and mimalloc avoids importing prebuilt libraries
    # with a different CPU requirement. The pinned packager uses zstd.
    ./configure --prefix="$prefix" --host=i586-linux-musl --build="$(gcc -dumpmachine)" \
        --disable-shared --enable-static --disable-demo \
        --without-xz --without-lzo --without-lz4
    make -j"$jobs"
    make install
    mkdir -p "$prefix/include/squashfuse"
    install -m 644 ./*.h "$prefix/include/squashfuse/"
)
(
    cd "$runtime_source/src/runtime"
    mkdir -p "$build_dir/relink"
    "$CC" -std=gnu99 -Os -D_FILE_OFFSET_BITS=64 \
        '-DGIT_COMMIT="dd6cebed-i586"' -ffunction-sections -fdata-sections \
        -I"$prefix/include/squashfuse" -I"$prefix/include/fuse3" \
        -c runtime.c -o "$build_dir/relink/runtime.o"
    "$CC" -T data_sections.ld -Wl,--gc-sections -static -no-pie \
        "$build_dir/relink/runtime.o" -L"$prefix/lib" \
        -lsquashfuse -lsquashfuse_ll -lzstd -lz -lfuse3 \
        -o "$build_dir/runtime-i586"
    cp data_sections.ld "$build_dir/relink/"
)
strip --strip-debug --strip-unneeded "$build_dir/runtime-i586"
printf 'AI\002' | dd of="$build_dir/runtime-i586" bs=1 count=3 seek=8 conv=notrunc status=none
chmod +x "$build_dir/runtime-i586"
# A static loader must not import a host dynamic linker or target libgcc.
readelf -l "$build_dir/runtime-i586" > "$build_dir/runtime-i586.program-headers"
if rg --quiet INTERP "$build_dir/runtime-i586.program-headers"; then
    echo 'AppImage runtime unexpectedly requires a dynamic interpreter' >&2
    exit 1
fi
objdump -d "$build_dir/runtime-i586" > "$build_dir/runtime-i586.disassembly"
if rg --quiet '\b(xmm[0-9]+|ymm[0-9]+|zmm[0-9]+)\b' "$build_dir/runtime-i586.disassembly"; then
    echo 'AppImage runtime unexpectedly contains SSE/AVX instructions' >&2
    exit 1
fi
qemu-i386 -cpu pentium3 "$build_dir/runtime-i586" --appimage-version
(cd "$build_dir"; sha256sum runtime-i586 > runtime-i586.sha256)
mkdir -p "$build_dir/licenses"
cp "$build_dir/src/musl-1.2.5/COPYRIGHT" "$build_dir/licenses/musl-COPYRIGHT"
cp "$build_dir/src/zlib-1.3.1/LICENSE" "$build_dir/licenses/zlib-LICENSE"
cp "$build_dir/src/zstd-1.5.7/LICENSE" "$build_dir/licenses/zstd-LICENSE"
cp "$build_dir/src/fuse-3.15.0/LGPL2.txt" "$build_dir/licenses/libfuse-LGPL2.txt"
cp "$build_dir/src/squashfuse-0.5.2/LICENSE" "$build_dir/licenses/squashfuse-LICENSE"
cp "$runtime_source/LICENSE" "$build_dir/licenses/type2-runtime-LICENSE"
cp "$prefix/lib/"{libc,libzstd,libz,libfuse3,libsquashfuse,libsquashfuse_ll}.a "$build_dir/relink/"
cat > "$build_dir/relink/README.txt" <<'EOF'
These relocatable objects and static libraries permit relinking this AppImage
runtime with a modified libfuse under the LGPL. The enclosing directory contains
all corresponding pinned sources and build-appimage-runtime.sh, including the
unmodified upstream runtime's libfuse patch. Run the build script to create the
i586 musl compiler, replace libfuse3.a here, and use that compiler to link:

  i586-musl-gcc -T data_sections.ld -Wl,--gc-sections -static -no-pie \
    runtime.o libsquashfuse.a libsquashfuse_ll.a libzstd.a libz.a libfuse3.a -o runtime-i586

Then strip the runtime and write the three AppImage magic bytes AI\002 at offset
8, as shown by the build script, before passing it to appimagetool --runtime-file.
EOF
printf '%s\n' "$build_dir/runtime-i586"
