#!/bin/bash

set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: bash tools/scripts/build-macos.sh [options] [-- CMake options...]

  --arch native|arm64|x86_64|universal  Target CPUs (default: native).
  --build-dir DIR                    Default: out/sdl3-macos-<arch>.
  --package-dir DIR                  Default: out/package-macos-<arch>.
  --jobs COUNT                       Parallel build jobs (default: 2).
  --test                             Run native regressions before packaging.
  --help                             Show this help.

Requires macOS, Xcode Command Line Tools, CMake 3.25+, Ninja, and Git.
Builds Release binaries and installs the complete Runtime package. The default
deployment target is macOS 11.0; MACOSX_DEPLOYMENT_TARGET can override it.
Tests require the original game data in the checkout's stunts/ directory.
Relative directory arguments are resolved from the current working directory.
USAGE
}

fail() {
    echo "Error: $*" >&2
    exit 1
}

require_value() {
    [[ $# -ge 2 && -n "$2" ]] || fail "Missing value for $1."
}

architecture=native
build_dir=
package_dir=
default_jobs=2
jobs="$default_jobs"
run_tests=false
# Keep the array nonempty: macOS Bash 3.2 treats empty arrays as unset with -u.
cmake_options=(-DCMAKE_BUILD_TYPE=Release)
while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch) require_value "$@"; architecture="$2"; shift 2 ;;
        --build-dir) require_value "$@"; build_dir="$2"; shift 2 ;;
        --package-dir) require_value "$@"; package_dir="$2"; shift 2 ;;
        --jobs) require_value "$@"; jobs="$2"; shift 2 ;;
        --test) run_tests=true; shift ;;
        --help|-h) usage; exit 0 ;;
        --) shift; cmake_options+=("$@"); break ;;
        *) fail "Unknown option: $1 (use --help)." ;;
    esac
done

[[ "$(uname -s)" == Darwin ]] || fail "Build on macOS with the Apple SDK and compiler."
if [[ "$architecture" == native ]]; then
    architecture="$(uname -m)"
fi
case "$architecture" in
    arm64|x86_64) cmake_architectures="$architecture" ;;
    universal) cmake_architectures='arm64;x86_64' ;;
    *) fail "Unsupported architecture: $architecture." ;;
esac
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || fail "--jobs must be a positive integer."

for tool in cmake ninja git xcrun; do
    command -v "$tool" >/dev/null 2>&1 || fail "Missing $tool; see readme.md for prerequisites."
done
cxx_compiler="$(xcrun --find clang++)" || fail "Install Xcode Command Line Tools: xcode-select --install"
compiler="$(xcrun --find clang)" || fail "Install Xcode Command Line Tools: xcode-select --install"
sdk="$(xcrun --sdk macosx --show-sdk-path)" || fail "Cannot locate the macOS SDK."

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd -- "$script_dir/../.." && pwd)"
build_dir="${build_dir:-$repo_dir/out/sdl3-macos-$architecture}"
package_dir="${package_dir:-$repo_dir/out/package-macos-$architecture}"
default_deployment_target=11.0
deployment_target="${MACOSX_DEPLOYMENT_TARGET:-$default_deployment_target}"

cmake -S "$repo_dir" -B "$build_dir" -G Ninja \
    "-DCMAKE_C_COMPILER=$compiler" \
    "-DCMAKE_CXX_COMPILER=$cxx_compiler" \
    "-DCMAKE_OSX_SYSROOT=$sdk" \
    "-DCMAKE_OSX_ARCHITECTURES=$cmake_architectures" \
    "-DCMAKE_OSX_DEPLOYMENT_TARGET=$deployment_target" \
    "${cmake_options[@]}"
cmake --build "$build_dir" --parallel "$jobs"
if [[ "$run_tests" == true ]]; then
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        ctest --test-dir "$build_dir" --output-on-failure --no-tests=error
fi
cmake --install "$build_dir" --prefix "$package_dir" --component Runtime
printf 'Runtime package: %s\n' "$package_dir"
printf 'Run: bash "%s/run-restunts.sh" --data-dir "/path/to/Stunts"\n' "$package_dir"
