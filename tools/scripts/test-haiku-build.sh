#!/usr/bin/env bash
# Run Haiku's native checks using the local or transferred CMake build tree.
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "$script_dir/../.." && pwd)
target=${1:?Usage: test-haiku-build.sh TARGET [--cross-built]}
mode=${2:-}
build_dir="$repo_dir/build/packages/$target"
jobs=${RESTUNTS_BUILD_JOBS:-2}
timeout_seconds=${RESTUNTS_TEST_TIMEOUT_SECONDS:-600}
python_command=${PYTHON:-python3}

case "$target:$(uname -s):$(uname -m)" in
    haiku-x64:Haiku:x86_64) ;;
    haiku-x86:Haiku:BePC|haiku-x86:Haiku:x86) export PATH="$(setarch -p x86)" ;;
    *) echo "$target tests require a matching Haiku host." >&2; exit 1 ;;
esac
export LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8

SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    ctest --test-dir "$build_dir" --output-on-failure --no-tests=error \
        --parallel "$jobs" --timeout "$timeout_seconds"

if [[ "$mode" == --cross-built ]]; then
    "$python_command" "$script_dir/test-nuked-package.py" --build-directory "$build_dir" --native-rebuild
    sh "$script_dir/test-relocated-runtime.sh" "$repo_dir/build/package-runtime/$target"
else
    "$python_command" "$script_dir/test-nuked-package.py" --build-directory "$build_dir"
fi
