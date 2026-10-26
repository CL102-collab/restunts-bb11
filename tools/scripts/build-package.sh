#!/usr/bin/env bash
# Build and install one target. Archive creation validates the resulting tree.
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "$script_dir/../.." && pwd)
target=${1:?Usage: build-package.sh TARGET [DOS_EXECUTABLE_DIRECTORY]}
tool_root=${RESTUNTS_TOOLCHAIN_ROOT:-${RUNNER_TEMP:-/tmp}/restunts-toolchains}
build_dir="$repo_dir/build/packages/$target"
package_dir="$repo_dir/build/package-runtime/$target"
archive_dir="$repo_dir/dist/packages"
default_jobs=2
jobs=${RESTUNTS_BUILD_JOBS:-$default_jobs}
default_test_timeout_seconds=600
test_timeout_seconds=${RESTUNTS_TEST_TIMEOUT_SECONDS:-$default_test_timeout_seconds}
cmake_options=(-DCMAKE_BUILD_TYPE=Release -DRESTUNTS_BUILD_TESTS=OFF)
python_command=${PYTHON:-python3}

verify_bsd_host() {
    local host_system host_architecture
    host_system=$(uname -s)
    host_architecture=$(uname -m)
    case "$target:$host_system:$host_architecture" in
        freebsd-x64:FreeBSD:amd64|freebsd-x64:FreeBSD:x86_64|\
        openbsd-x64:OpenBSD:amd64|openbsd-x64:OpenBSD:x86_64|\
        netbsd-x64:NetBSD:amd64|netbsd-x64:NetBSD:x86_64|\
        netbsd-x86:NetBSD:i386|netbsd-x86-no-sse2:NetBSD:i386) ;;
        *)
            echo "$target requires its matching native BSD userspace; found $host_system $host_architecture." >&2
            exit 1
            ;;
    esac
}

verify_haiku_host() {
    local host_system host_architecture
    host_system=$(uname -s)
    host_architecture=$(uname -m)
    case "$target:$host_system:$host_architecture" in
        haiku-x64:Haiku:x86_64|haiku-x86:Haiku:BePC|haiku-x86:Haiku:x86) ;;
        *)
            echo "$target requires its matching native Haiku userspace; found $host_system $host_architecture." >&2
            exit 1
            ;;
    esac
}

case "$target" in
    freebsd-*|openbsd-*|netbsd-*)
        verify_bsd_host
        python_command=${PYTHON:-python3.13}
        ;;
    haiku-*)
        verify_haiku_host
        if [[ "$target" == haiku-x86 ]]; then
            # Hybrid Haiku defaults to GCC 2; select the modern x86 toolchain.
            PATH=$(setarch -p x86)
            export PATH
        fi
        ;;
esac

verify_clean_source() {
    if ! git -c safe.directory="$repo_dir" -C "$repo_dir" diff --quiet HEAD --; then
        echo "Refusing to package modified tracked source; the archive must match its source commit." >&2
        exit 1
    fi
}

verify_clean_source
mkdir -p "$package_dir" "$archive_dir"
# A stale installation must not hide a missing file or retain removed content.
if [[ -n "$(ls -A "$package_dir")" ]]; then
    echo "Package directory must be empty: $package_dir" >&2
    exit 1
fi

case "$target" in
    android-armv7|android-arm64)
        bash "$script_dir/build-android-package.sh" "$target" "$package_dir"
        ;;
    dos16)
        dos_directory=${2:?The DOS16 target requires the downloaded DOS executable artifact}
        for executable in restunts restunto repldump repldumo pixldump pixldumo; do
            cp -- "$dos_directory/$executable.exe" "$package_dir/$executable.exe"
        done
        mkdir -p "$package_dir/cars"
        for car_code in MCMU TERR; do
            for car_file in "CAR$car_code.RES" "ST$car_code.3SH" "STDA$car_code.VSH" "STDB$car_code.VSH"; do
                cp -- "$repo_dir/assets/cars/$car_file" "$package_dir/cars/$car_file"
            done
        done
        ;;
    macos-universal)
        bash "$script_dir/build-macos.sh" --arch universal --build-dir "$build_dir" \
            --package-dir "$package_dir" --jobs "$jobs" -- -DRESTUNTS_BUILD_TESTS=OFF
        for binary in bin/restunts bin/restunts-launcher bin/genghost bin/genghost-gui \
            bin/repldump bin/pixldump lib/libnuked-opl2.dylib; do
            lipo "$package_dir/$binary" -verify_arch arm64 x86_64
        done
        ;;
    *)
        case "$target" in
            linux-x64) ;;
            linux-appimage-x64|linux-appimage-x86|linux-appimage-x86-no-sse2|\
            linux-appimage-armv7|linux-appimage-arm64)
                appimage_environment="$tool_root/appimage-sysroots/$target.env"
                [[ -f "$appimage_environment" ]] || {
                    echo "Missing legacy AppImage environment; run install-package-dependencies.sh $target first." >&2
                    exit 1
                }
                # shellcheck source=/dev/null
                source "$appimage_environment"
                cmake_options+=("-DCMAKE_TOOLCHAIN_FILE=$RESTUNTS_APPIMAGE_CMAKE_TOOLCHAIN"
                    -DSDL_X11_XCURSOR=OFF -DSDL_X11_XINPUT=OFF -DSDL_X11_XFIXES=OFF
                    -DSDL_X11_XRANDR=OFF -DSDL_X11_XTEST=OFF -DSDL_X11_XSCRNSAVER=OFF
                    -DSDL_WAYLAND=OFF -DSDL_VULKAN=OFF -DSDL_GPU_VULKAN=OFF)
                if [[ "$target" == linux-appimage-x86-no-sse2 ]]; then
                    cmake_options+=(-DRESTUNTS_SSE2=OFF)
                else
                    cmake_options+=(-DRESTUNTS_SSE2=ON)
                fi
                ;;
            linux-x86) cmake_options+=(-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/linux-x86.cmake) ;;
            linux-x86-no-sse2)
                cmake_options+=(-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/linux-x86.cmake -DRESTUNTS_SSE2=OFF) ;;
            linux-arm32) cmake_options+=(-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/linux-arm32.cmake) ;;
            linux-arm64) cmake_options+=(-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/linux-arm64.cmake) ;;
            freebsd-x64|openbsd-x64) ;;
            haiku-x64|haiku-x86) cmake_options+=(-DRESTUNTS_BUILD_TESTS=ON) ;;
            netbsd-x64|netbsd-x86|netbsd-x86-no-sse2)
                # NetBSD's ASLR prevents GCC from reusing SDL's precompiled header.
                cmake_options+=('-DCMAKE_PREFIX_PATH=/usr/pkg;/usr/X11R7' -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON)
                export PKG_CONFIG_PATH="/usr/pkg/lib/pkgconfig:/usr/X11R7/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
                if [[ "$target" == netbsd-x86 ]]; then
                    cmake_options+=(-DRESTUNTS_SSE2=ON)
                elif [[ "$target" == netbsd-x86-no-sse2 ]]; then
                    cmake_options+=(-DRESTUNTS_SSE2=OFF)
                fi
                ;;
            windows-x86) cmake_options+=(-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-x86.cmake) ;;
            windows-x86-no-sse2)
                cmake_options+=(-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-x86.cmake -DRESTUNTS_SSE2=OFF) ;;
            windows-x64) cmake_options+=(-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-x64.cmake) ;;
            windows-arm64)
                export PATH="$tool_root/llvm-mingw/bin:$PATH"
                cmake_options+=(-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-arm64.cmake) ;;
            dos32)
                export PATH="$tool_root/djgpp/bin:$PATH"
                cmake_options+=(-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/djgpp.cmake) ;;
            browser)
                # emsdk_env.sh expects unset optional environment variables.
                set +u
                source "$tool_root/emsdk/emsdk_env.sh"
                set -u
                cmake_options+=("-DCMAKE_TOOLCHAIN_FILE=$tool_root/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake") ;;
            *) echo "Unknown package target: $target" >&2; exit 1 ;;
        esac
        cmake -S "$repo_dir" -B "$build_dir" -G Ninja "${cmake_options[@]}"
        cmake --build "$build_dir" --parallel "$jobs"
        if [[ "$target" == haiku-* ]]; then
            SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
                ctest --test-dir "$build_dir" --output-on-failure --no-tests=error \
                    --timeout "$test_timeout_seconds"
            "$python_command" "$script_dir/test-nuked-package.py" --build-directory "$build_dir"
        fi
        cmake --install "$build_dir" --prefix "$package_dir" --component Runtime
        if [[ "$target" == dos32 ]]; then
            unzip -p "$tool_root/downloads/csdpmi7b.zip" bin/CWSDPMI.EXE > "$package_dir/bin/CWSDPMI.EXE"
            mkdir -p "$package_dir/share/licenses/restunts"
            unzip -p "$tool_root/downloads/csdpmi7b.zip" bin/cwsdpmi.doc \
                > "$package_dir/share/licenses/restunts/CWSDPMI.DOC"
        fi
        ;;
esac

if [[ "$target" == linux-appimage-* ]]; then
    bash "$script_dir/build-appimage.sh" "$package_dir" "$target"
fi

case "$target" in
    freebsd-*|openbsd-*|netbsd-*|haiku-*)
        # Check the installed loader paths after moving the complete runtime.
        # --licenses does not require a display, audio device, or game data.
        smoke_dir=$(mktemp -d "${TMPDIR:-/tmp}/restunts-runtime.XXXXXX")
        trap 'rm -rf -- "$smoke_dir"' EXIT
        cp -RP "$package_dir/." "$smoke_dir/"
        (unset LD_LIBRARY_PATH LIBRARY_PATH ADDON_PATH; "$smoke_dir/bin/restunts" --licenses > /dev/null)
        rm -rf -- "$smoke_dir"
        trap - EXIT
        ;;
esac

verify_clean_source
"$python_command" "$script_dir/release-packages.py" create --target "$target" \
    --runtime "$package_dir" --directory "$archive_dir" --commit "${GITHUB_SHA:?Missing source commit}"
