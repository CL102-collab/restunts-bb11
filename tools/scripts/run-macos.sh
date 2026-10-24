#!/bin/bash

set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: bash run-restunts.sh [--data-dir DIR] [--runtime-dir DIR] [-- game arguments...]

  --data-dir DIR     Writable folder containing original Broderbund Stunts 1.1 data.
  --runtime-dir DIR  Runtime package or CMake build directory.
  --help            Show this help.

With no arguments, open the native startup window. With --data-dir, start the
game directly. In a runtime package, find bin/restunts beside this script. From a
source checkout, the default is out/package-macos-<native architecture>.
Relative paths are resolved from the current working directory. All arguments
after -- are passed to the game, for example: -- --nointro --hv:medium
USAGE
}

fail() {
    echo "Error: $*" >&2
    exit 1
}

data_dir=
runtime_dir=
while [[ $# -gt 0 ]]; do
    case "$1" in
        --data-dir|--runtime-dir)
            [[ $# -ge 2 && -n "$2" ]] || fail "Missing value for $1."
            if [[ "$1" == --data-dir ]]; then
                data_dir="$2"
            else
                runtime_dir="$2"
            fi
            shift 2
            ;;
        --help|-h) usage; exit 0 ;;
        --) shift; break ;;
        *) fail "Unknown option: $1 (put game arguments after --)." ;;
    esac
done

[[ "$(uname -s)" == Darwin ]] || fail "This launcher requires macOS."

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ -z "$runtime_dir" ]]; then
    if [[ -f "$script_dir/bin/restunts" ]]; then
        runtime_dir="$script_dir"
    else
        runtime_dir="$script_dir/../../out/package-macos-$(uname -m)"
    fi
fi
[[ -d "$runtime_dir" ]] || fail "Runtime directory does not exist: $runtime_dir"
runtime_dir="$(cd -- "$runtime_dir" && pwd)"
if [[ -f "$runtime_dir/bin/restunts" ]]; then
    executable="$runtime_dir/bin/restunts"
    library="$runtime_dir/lib/libnuked-opl2.dylib"
else
    executable="$runtime_dir/restunts"
    library="$runtime_dir/libnuked-opl2.dylib"
fi
if [[ -z "$data_dir" && $# -eq 0 ]]; then
    native_launcher="$(dirname -- "$executable")/restunts-launcher"
    [[ -x "$native_launcher" ]] || fail "Missing desktop launcher: $native_launcher"
    exec "$native_launcher"
fi
[[ -n "$data_dir" ]] || fail "Specify --data-dir with your Stunts 1.1 game folder."
[[ -d "$data_dir" && -w "$data_dir" ]] || fail "Game data directory must exist and be writable: $data_dir"
data_dir="$(cd -- "$data_dir" && pwd)"
[[ -x "$executable" ]] || fail "Missing executable or execute permission: $executable"
[[ -f "$library" ]] || fail "Missing audio library: $library. Keep the complete runtime package together."
exec "$executable" --data-dir "$data_dir" "$@"
