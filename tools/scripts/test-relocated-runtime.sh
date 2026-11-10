#!/bin/sh
# Check installed loader paths after moving the complete runtime.
set -eu

runtime=${1:?Usage: test-relocated-runtime.sh RUNTIME_DIRECTORY}
smoke_dir=$(mktemp -d "${TMPDIR:-/tmp}/restunts-runtime.XXXXXX")
trap 'rm -rf -- "$smoke_dir"' 0
trap 'exit 1' 1 2 15
cp -RP "$runtime/." "$smoke_dir/"
# --licenses does not require a display, audio device, or game data.
(unset LD_LIBRARY_PATH LIBRARY_PATH ADDON_PATH; "$smoke_dir/bin/restunts" --licenses > /dev/null)
