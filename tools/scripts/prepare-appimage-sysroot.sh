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
        legacy_cxx_target=x86_64-redhat-linux
        sdk_image=$APPIMAGE_LEGACY_X64_IMAGE; glibc_image=$APPIMAGE_GLIBC_X64_IMAGE
        base_url=$APPIMAGE_LEGACY_X64_BASE_URL; base_sha=$APPIMAGE_LEGACY_X64_BASE_SHA256
        development_url=$APPIMAGE_LEGACY_X64_DEVELOPMENT_URL
        development_sha=$APPIMAGE_LEGACY_X64_DEVELOPMENT_SHA256
        runtime_url=$APPIMAGE_GLIBC_X64_URL; runtime_sha=$APPIMAGE_GLIBC_X64_SHA256
        gtk_index_url=$APPIMAGE_GTK_X64_INDEX_URL; gtk_index_sha=$APPIMAGE_GTK_X64_INDEX_SHA256
        architecture_flags=; dependency_flags=; elf_class=ELF64
        ;;
    linux-appimage-x86|linux-appimage-x86-no-sse2)
        architecture=x86; lib_directory=lib; host_triple=i686-linux-gnu
        legacy_cxx_target=i386-redhat-linux
        sdk_image=$APPIMAGE_LEGACY_X86_IMAGE; glibc_image=$APPIMAGE_GLIBC_X86_IMAGE
        base_url=$APPIMAGE_LEGACY_X86_BASE_URL; base_sha=$APPIMAGE_LEGACY_X86_BASE_SHA256
        development_url=$APPIMAGE_LEGACY_X86_DEVELOPMENT_URL
        development_sha=$APPIMAGE_LEGACY_X86_DEVELOPMENT_SHA256
        runtime_url=$APPIMAGE_GLIBC_X86_URL; runtime_sha=$APPIMAGE_GLIBC_X86_SHA256
        gtk_index_url=$APPIMAGE_GTK_X86_INDEX_URL; gtk_index_sha=$APPIMAGE_GTK_X86_INDEX_SHA256
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
cxx_compiler=${CXX:-g++}
recipe=$(cat "$script_dir/prepare-appimage-sysroot.sh" "$script_dir/appimage-sysroot.py" \
    "$script_dir/package-toolchains.conf" "$script_dir/appimage_runtime.py" \
    "$script_dir/../appimage/query-gtk-modules.c" \
    "$script_dir/../appimage/legacy-cxx-runtime.cpp" \
    "$script_dir/../appimage/legacy-compiler-runtime.c" \
    "$script_dir/../appimage/test-legacy-cxx-runtime.cpp" \
    "$script_dir/build-appimage-pulse-dependencies.sh" "$script_dir/../appimage/test-libwrap-runtime.c" \
    "$script_dir/../appimage/test-asyncns-runtime.c"; \
    "$compiler" -dumpmachine; "$compiler" --version; "$cxx_compiler" --version)
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
    command -v patchelf > /dev/null || {
        echo 'AppImage SDK preparation requires the host patchelf build tool.' >&2
        exit 1
    }
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
    python3 "$script_dir/appimage-sysroot.py" gtk \
        --index-url "$gtk_index_url" --index-sha256 "$gtk_index_sha" \
        --cache "$downloads" --destination "$stage/gtk"
    cp -a "$stage/gtk/." "$stage/sysroot/"
    if [[ "$lib_directory" == lib64 ]]; then
        # Debian uses /usr/lib on amd64; the manylinux SDK uses /usr/lib64.
        # Keep both layouts so pkg-config and compiler searches agree.
        cp -a "$stage/gtk/usr/lib/." "$stage/sysroot/usr/lib64/"
        cp -a "$stage/gtk/lib/." "$stage/sysroot/lib64/"
    fi
    # The base SDK keeps some old development aliases in /lib. Prefer the
    # matching GTK development files in /usr/lib without stale ABI mixtures.
    python3 - "$stage/gtk/usr/lib" "$stage/sysroot" "$lib_directory" <<'PYGTK'
from pathlib import Path
import sys
gtk, sysroot, directory = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
base, development = sysroot / directory, sysroot / 'usr' / directory
if base.resolve() != development.resolve():
    for library in gtk.iterdir():
        if library.name.endswith(('.so', '.a', '.la')):
            (base / library.name).unlink(missing_ok=True)
PYGTK
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
    legacy_gcc="$stage/sysroot/opt/rh/devtoolset-2/root/usr/lib/gcc/$legacy_cxx_target/4.8.2"
    if [[ "$architecture" == x86 ]]; then
        "$compiler" --sysroot="$stage/sysroot" -B"$legacy_gcc/" \
            $architecture_flags $dependency_flags -O2 -fPIC \
            -c "$script_dir/../appimage/legacy-compiler-runtime.c" \
            -o "$stage/legacy-compiler-runtime.o"
        ar rcs "$legacy_gcc/libgcc.a" "$stage/legacy-compiler-runtime.o"
    fi
    legacy_flags="--sysroot=$stage/sysroot -B$legacy_gcc/"
    legacy_flags+=" -B$stage/sysroot/usr/$lib_directory/ $architecture_flags"
    legacy_link_flags="-B$legacy_gcc/ -B$stage/sysroot/usr/$lib_directory/ -L$legacy_gcc"
    legacy_link_flags+=" -L$stage/sysroot/$lib_directory -L$stage/sysroot/usr/$lib_directory"
    legacy_link_flags+=" -Wl,-rpath-link,$stage/sysroot/$lib_directory"
    legacy_link_flags+=" -Wl,-rpath-link,$stage/sysroot/usr/$lib_directory -static-libgcc"
    # Historical libtool drops -B and -static-libgcc from shared-library
    # links. A host wrapper keeps the pinned CRT/runtime selection intact.
    {
        printf '#!/usr/bin/env bash\nexec '
        printf '%q ' "$compiler" "--sysroot=$stage/sysroot" "-B$legacy_gcc/" \
            "-B$stage/sysroot/usr/$lib_directory/" -static-libgcc
        printf '"$@"\n'
    } > "$stage/legacy-cc"
    chmod 755 "$stage/legacy-cc"
    (
        cd "$stage/alsa-build"
        # ALSA's historical configure resets CC when a prefixed compiler is
        # present, so target/sysroot options also belong in CFLAGS.
        CC="$stage/legacy-cc" \
            CFLAGS="$legacy_flags $dependency_flags -O2 -fPIC -fno-stack-protector" \
            LDFLAGS="$legacy_link_flags" \
            "$stage/alsa-lib-1.0.27.2/configure" --prefix=/usr \
                --libdir="/usr/$lib_directory" --host="$host_triple" --disable-python \
                > configure.log 2>&1 &&
        make -j "$jobs" > build.log 2>&1 &&
        make DESTDIR="$stage/sysroot" install > install.log 2>&1
    ) || {
        tail -n 50 "$stage/alsa-build/configure.log" "$stage/alsa-build/build.log" \
            "$stage/alsa-build/install.log" >&2 2>/dev/null
        exit 1
    }
    cxx_headers="$stage/sysroot/opt/rh/devtoolset-2/root/usr/include/c++/4.8.2"
    "$cxx_compiler" --sysroot="$stage/sysroot" -B"$legacy_gcc/" \
        -B"$stage/sysroot/usr/$lib_directory/" $architecture_flags $dependency_flags \
        -std=c++11 -O2 -fPIC -nostdinc++ -isystem "$cxx_headers" \
        -isystem "$cxx_headers/$legacy_cxx_target" -isystem "$cxx_headers/backward" \
        -c "$script_dir/../appimage/legacy-cxx-runtime.cpp" -o "$stage/legacy-cxx-runtime.o"
    ar rcs "$legacy_gcc/libstdc++.a" "$stage/legacy-cxx-runtime.o"
    # Catch compiler/runtime ABI gaps before the larger GUI build. The linked
    # regression can also run on a host with the matching native ELF loader.
    "$cxx_compiler" --sysroot="$stage/sysroot" -B"$legacy_gcc/" \
        -B"$stage/sysroot/usr/$lib_directory/" $architecture_flags $dependency_flags \
        -std=c++11 -O2 -fno-pie -no-pie -static-libgcc -static-libstdc++ -nostdinc++ \
        -isystem "$cxx_headers" -isystem "$cxx_headers/$legacy_cxx_target" \
        -isystem "$cxx_headers/backward" "$script_dir/../appimage/test-legacy-cxx-runtime.cpp" \
        -L"$legacy_gcc" -L"$stage/sysroot/$lib_directory" -L"$stage/sysroot/usr/$lib_directory" \
        -o "$stage/test-legacy-cxx-runtime"
    mkdir -p "$stage/deps/lib" "$stage/deps/share/licenses" "$stage/deps/share/sources"
    python3 - "$stage/gtk" "$stage/deps" "$stage/runtime" <<'PY'
from pathlib import Path
import shutil, sys
gtk, output, runtime = map(Path, sys.argv[1:])
for relative in ('lib', 'usr/lib'):
    library_root = gtk / relative
    for library in library_root.rglob('*'):
        if library.is_file() and (library.name.endswith('.so') or '.so.' in library.name):
            target = output / 'lib' / library.relative_to(library_root)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(library.resolve(), target)
for source, target in ((gtk / 'etc/fonts', output / 'etc/fonts'),
                       (gtk / 'usr/share/libthai', output / 'share/libthai'),
                       (runtime / 'usr/share/common-licenses', output / 'share/licenses/debian')):
    if source.is_dir():
        shutil.copytree(source, target, symlinks=True, dirs_exist_ok=True)
for copyright in (gtk / 'usr/share/doc').rglob('copyright'):
    target = output / 'share/licenses/gtk2' / copyright.relative_to(gtk / 'usr/share/doc')
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(copyright.resolve(), target)
shutil.copyfile(gtk / 'packages.txt', output / 'share/sources/appimage-gtk-packages.txt')
PY
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
    mkdir -p "$stage/deps/share/licenses/gcc-runtime"
    cp "$stage/sysroot/opt/rh/devtoolset-2/root/usr/share/doc/devtoolset-2-gcc-4.8.2/"COPYING* \
        "$stage/deps/share/licenses/gcc-runtime/"
    mkdir "$stage/deps/share/licenses/alsa-lib"
    cp "$stage/alsa-lib-1.0.27.2/COPYING" "$stage/deps/share/licenses/alsa-lib/"
    cp "$downloads/alsa-lib_1.0.27.2.orig.tar.bz2" "$stage/deps/share/sources/"
    cp -a "$stage/sysroot/usr/share/alsa" "$stage/deps/share/alsa"
    cp -a "$stage/sysroot/usr/share/X11" "$stage/deps/share/X11"
    bash "$script_dir/build-appimage-pulse-dependencies.sh" "$architecture" "$stage/sysroot" "$stage/deps"
    python3 "$script_dir/appimage-sysroot.py" prune-runtime "$stage/deps/lib"
    # Packaged libraries can carry distribution build paths. Remove those
    # only from our copied runtime; AppRun supplies the relocated library path.
    while IFS= read -r -d '' library; do
        patchelf --remove-rpath "$library"
    done < <(find "$stage/deps/lib" -type f -name '*.so*' -print0)
    mkdir -p "$stage/deps/bin"
    # Build our cache generator with the SDK; never execute packaged old tools.
    gtk_flags=$(PKG_CONFIG_PATH= \
        PKG_CONFIG_SYSROOT_DIR="$stage/sysroot" \
        PKG_CONFIG_LIBDIR="$stage/sysroot/usr/$lib_directory/pkgconfig" \
        pkg-config --cflags --libs gmodule-2.0 pango gdk-pixbuf-2.0)
    read -r -a gtk_arguments <<< "$gtk_flags"
    "$compiler" --sysroot="$stage/sysroot" -B"$legacy_gcc/" \
        -B"$stage/sysroot/usr/$lib_directory/" \
        $architecture_flags $dependency_flags -O2 -fno-pie -no-pie -static-libgcc \
        "$script_dir/../appimage/query-gtk-modules.c" "${gtk_arguments[@]}" \
        -L"$legacy_gcc" -L"$stage/sysroot/$lib_directory" -L"$stage/sysroot/usr/$lib_directory" \
        -Wl,-rpath-link,"$stage/sysroot/$lib_directory" \
        -Wl,-rpath-link,"$stage/sysroot/usr/$lib_directory" \
        '-Wl,-rpath,$ORIGIN/../lib' -o "$stage/deps/bin/restunts-gtk-modules"
    cp "$script_dir/../appimage/query-gtk-modules.c" \
        "$script_dir/../appimage/legacy-cxx-runtime.cpp" \
        "$script_dir/../appimage/legacy-compiler-runtime.c" "$stage/deps/share/sources/"
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
    chmod 755 "$stage/deps/bin/restunts-gtk-modules"
    {
        printf 'Legacy SDK image: %s\nLink-only glibc image: %s\n' "$sdk_image" "$glibc_image"
        printf 'Recipe SHA256: %s\nALSA source SHA256: %s\n' "$recipe_sha" "$APPIMAGE_ALSA_SHA256"
        printf 'ALSA configure: --prefix=/usr --libdir=/usr/%s --host=%s --disable-python\n' "$lib_directory" "$host_triple"
        printf 'Architecture flags: %s %s\n' "$architecture_flags" "$dependency_flags"
        printf 'C++ runtime: %s\n' \
            'devtoolset-2 GCC 4.8.2 static archive with C++11 array exception support'
        printf 'PulseAudio client: %s\n' \
            'pinned Squeeze 0.9.21; native SDL backend and matching client dependency closure'
        printf 'GTK/GLib headers: %s\n' \
            'pinned Squeeze packages; local g_slist_free_full convenience wrapper'
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

legacy_cxx_root="$sysroot/opt/rh/devtoolset-2/root/usr"
legacy_cxx_headers="$legacy_cxx_root/include/c++/4.8.2"
legacy_cxx_library="$legacy_cxx_root/lib/gcc/$legacy_cxx_target/4.8.2"
[[ -f "$legacy_cxx_library/libstdc++.a" \
    && -f "$legacy_cxx_headers/$legacy_cxx_target/bits/c++config.h" ]] || {
    echo 'Pinned development layer is missing its C++11 SDK/static runtime.' >&2
    exit 1
}
toolchain="$sdk_root/toolchain.cmake"
cat > "$toolchain" <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR $([[ "$architecture" == x64 ]] && echo x86_64 || echo i586))
set(CMAKE_C_COMPILER [==[$compiler]==])
set(CMAKE_CXX_COMPILER [==[$cxx_compiler]==])
set(CMAKE_SYSROOT [==[$sysroot]==])
set(ENV{PKG_CONFIG_PATH} "")
set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH FALSE)
set(ENV{PKG_CONFIG_SYSROOT_DIR} "\${CMAKE_SYSROOT}")
set(ENV{PKG_CONFIG_LIBDIR}
    "\${CMAKE_SYSROOT}/usr/$lib_directory/pkgconfig:\${CMAKE_SYSROOT}/usr/share/pkgconfig")
string(CONCAT CMAKE_C_FLAGS_INIT
    "$architecture_flags -D_GNU_SOURCE -B\\"$legacy_cxx_library/\\" "
    "-B\\"\${CMAKE_SYSROOT}/usr/$lib_directory/\\" -fno-pie -static-libgcc")
string(CONCAT CMAKE_CXX_FLAGS_INIT
    "$architecture_flags -D_GNU_SOURCE -nostdinc++ -isystem\\"$legacy_cxx_headers\\" "
    "-isystem\\"$legacy_cxx_headers/$legacy_cxx_target\\" "
    "-isystem\\"$legacy_cxx_headers/backward\\" "
    "-B\\"$legacy_cxx_library/\\" -B\\"\${CMAKE_SYSROOT}/usr/$lib_directory/\\" "
    "-fno-pie -static-libgcc -static-libstdc++")
string(CONCAT CMAKE_EXE_LINKER_FLAGS_INIT
    "-no-pie -L\\"$legacy_cxx_library\\" "
    "-L\\"\${CMAKE_SYSROOT}/$lib_directory\\" "
    "-L\\"\${CMAKE_SYSROOT}/usr/$lib_directory\\" "
    "-Wl,-rpath-link,\\"\${CMAKE_SYSROOT}/$lib_directory\\" "
    "-Wl,-rpath-link,\\"\${CMAKE_SYSROOT}/usr/$lib_directory\\"")
string(CONCAT CMAKE_SHARED_LINKER_FLAGS_INIT
    "-L\\"\${CMAKE_SYSROOT}/$lib_directory\\" "
    "-L\\"\${CMAKE_SYSROOT}/usr/$lib_directory\\" "
    "-Wl,-rpath-link,\\"\${CMAKE_SYSROOT}/$lib_directory\\" "
    "-Wl,-rpath-link,\\"\${CMAKE_SYSROOT}/usr/$lib_directory\\"")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(RESTUNTS_WX_TOOLKIT gtk2 CACHE STRING "Ubuntu 10.04-compatible native controls" FORCE)
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
    printf 'export PKG_CONFIG_PATH=\n'
    printf 'export PKG_CONFIG_SYSROOT_DIR=%q\n' "$sysroot"
    printf 'export PKG_CONFIG_LIBDIR=%q\n' "$sysroot/usr/$lib_directory/pkgconfig:$sysroot/usr/share/pkgconfig"
} > "$env_file"
echo "Prepared AppImage SDK environment: $env_file"
