#!/usr/bin/env bash
# Build static AppImage loaders for targets without a suitable upstream binary.
# The ordinary and no-SSE2 x86 packages share the i586 loader.
set -euo pipefail
umask 022
command -v rg > /dev/null

target=${1:?Usage: build-appimage-runtime.sh TARGET}
case "$target" in
    linux-appimage-x86|linux-appimage-x86-no-sse2)
        runtime_arch=i586
        compiler=gcc
        archiver=ar
        ranlib=ranlib
        stripper=strip
        disassembler=objdump
        musl_target=i386
        configure_host=i586-linux-musl
        cpu_family=x86
        cpu_flags='-m32 -march=i586 -mno-sse -mno-sse2 -mfpmath=387'
        abi_link_flags=-m32
        wrapper_linker_flags='-Wl,-m,elf_i386'
        emulator=(qemu-i386 -cpu pentium3)
        meson_exe_wrapper="['qemu-i386', '-cpu', 'pentium3']"
        ;;
    linux-appimage-riscv64)
        runtime_arch=riscv64
        compiler=riscv64-linux-gnu-gcc
        archiver=riscv64-linux-gnu-ar
        ranlib=riscv64-linux-gnu-ranlib
        stripper=riscv64-linux-gnu-strip
        disassembler=riscv64-linux-gnu-objdump
        musl_target=riscv64
        configure_host=riscv64-linux-musl
        cpu_family=riscv64
        cpu_flags='-march=rv64gc -mabi=lp64d'
        abi_link_flags="$cpu_flags"
        wrapper_linker_flags=
        emulator=(qemu-riscv64)
        meson_exe_wrapper="['qemu-riscv64']"
        ;;
    *) echo "No custom AppImage runtime is required for $target" >&2; exit 1 ;;
esac
command -v "${emulator[0]}" > /dev/null
command -v "$compiler" > /dev/null
tool_root=${RESTUNTS_TOOLCHAIN_ROOT:-${RUNNER_TEMP:-/tmp}/restunts-toolchains}
build_dir="$tool_root/appimage-runtime-$runtime_arch"
prefix="$build_dir/prefix"
runtime="$build_dir/runtime-$runtime_arch"
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

# Every dependency is rebuilt with the same CPU baseline. The i586 build also
# removes optional SSE2 paths present in the upstream i686 binary.
export CFLAGS="$cpu_flags -Os -ffunction-sections -fdata-sections"
export LDFLAGS="$abi_link_flags -static -Wl,--gc-sections"
export AR="$archiver" RANLIB="$ranlib"
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
    CC="$compiler" ./configure --prefix="$prefix" --target="$musl_target" --disable-shared
    make -j"$jobs"
    make install
)
cat > "$prefix/bin/$runtime_arch-musl-gcc" <<EOF
#!/bin/sh
exec $compiler $cpu_flags $wrapper_linker_flags -specs "$prefix/lib/musl-gcc.specs" "\$@"
EOF
chmod +x "$prefix/bin/$runtime_arch-musl-gcc"
export CC="$prefix/bin/$runtime_arch-musl-gcc"
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
ar = '$archiver'
strip = '$stripper'
pkg-config = 'pkg-config'
exe_wrapper = $meson_exe_wrapper
[host_machine]
system = 'linux'
cpu_family = '$cpu_family'
cpu = '$runtime_arch'
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
    ./configure --prefix="$prefix" --host="$configure_host" --build="$(gcc -dumpmachine)" \
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
        "-DGIT_COMMIT=\"dd6cebed-$runtime_arch\"" -ffunction-sections -fdata-sections \
        -I"$prefix/include/squashfuse" -I"$prefix/include/fuse3" \
        -c runtime.c -o "$build_dir/relink/runtime.o"
    "$CC" -T data_sections.ld -Wl,--gc-sections -static -no-pie \
        "$build_dir/relink/runtime.o" -L"$prefix/lib" \
        -lsquashfuse -lsquashfuse_ll -lzstd -lz -lfuse3 \
        -o "$runtime"
    cp data_sections.ld "$build_dir/relink/"
)
"$stripper" --strip-debug --strip-unneeded "$runtime"
printf 'AI\002' | dd of="$runtime" bs=1 count=3 seek=8 conv=notrunc status=none
chmod +x "$runtime"
# A static loader must not import a host dynamic linker or target libgcc.
readelf --wide --file-header --program-headers --dynamic "$runtime" > "$runtime.program-headers"
if rg --quiet 'INTERP|\(NEEDED\)' "$runtime.program-headers"; then
    echo 'AppImage runtime unexpectedly requires a dynamic interpreter' >&2
    exit 1
fi
if [[ "$runtime_arch" == i586 ]]; then
    "$disassembler" -d "$runtime" > "$runtime.disassembly"
    if rg --quiet '\b(xmm[0-9]+|ymm[0-9]+|zmm[0-9]+)\b' "$runtime.disassembly"; then
        echo 'AppImage runtime unexpectedly contains SSE/AVX instructions' >&2
        exit 1
    fi
else
    if ! rg --quiet 'Class:[[:space:]]+ELF64' "$runtime.program-headers" \
        || ! rg --quiet 'Machine:[[:space:]]+RISC-V' "$runtime.program-headers" \
        || ! rg --quiet 'Flags:.*double-float ABI' "$runtime.program-headers"; then
        echo 'AppImage runtime must use the RISC-V 64-bit double-float ABI' >&2
        exit 1
    fi
fi
"${emulator[@]}" "$runtime" --appimage-version
(cd "$build_dir"; sha256sum "runtime-$runtime_arch" > "runtime-$runtime_arch.sha256")
mkdir -p "$build_dir/licenses"
cp "$build_dir/src/musl-1.2.5/COPYRIGHT" "$build_dir/licenses/musl-COPYRIGHT"
cp "$build_dir/src/zlib-1.3.1/LICENSE" "$build_dir/licenses/zlib-LICENSE"
cp "$build_dir/src/zstd-1.5.7/LICENSE" "$build_dir/licenses/zstd-LICENSE"
cp "$build_dir/src/fuse-3.15.0/LGPL2.txt" "$build_dir/licenses/libfuse-LGPL2.txt"
cp "$build_dir/src/squashfuse-0.5.2/LICENSE" "$build_dir/licenses/squashfuse-LICENSE"
cp "$runtime_source/LICENSE" "$build_dir/licenses/type2-runtime-LICENSE"
cp "$prefix/lib/"{libc,libzstd,libz,libfuse3,libsquashfuse,libsquashfuse_ll}.a "$build_dir/relink/"
cat > "$build_dir/relink/README.txt" <<EOF
These relocatable objects and static libraries permit relinking this AppImage
runtime with a modified libfuse under the LGPL. The enclosing directory contains
all corresponding pinned sources and build-appimage-runtime.sh, including the
unmodified upstream runtime's libfuse patch. Run the build script to create the
$runtime_arch musl compiler, replace libfuse3.a here, and use that compiler to link:

  $runtime_arch-musl-gcc -T data_sections.ld -Wl,--gc-sections -static -no-pie \\
    runtime.o libsquashfuse.a libsquashfuse_ll.a libzstd.a libz.a libfuse3.a -o runtime-$runtime_arch

Then strip the runtime and write the three AppImage magic bytes AI\002 at offset
8, as shown by the build script, before appending the SquashFS payload.
EOF
printf '%s\n' "$runtime"
