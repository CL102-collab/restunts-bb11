#!/usr/bin/env bash
# Extract a pinned Linux x86-64 Amiga GCC toolchain without installing host packages.
set -euo pipefail

destination=${1:-/tmp/restunts-amiga-toolchain}
image=amigadev/crosstools@sha256:8ac8d60123403a119ee5870e539e93e9d8558574ad18027fc7032adac5086b74
marker=restunts-toolchain.version

if [[ -f "$destination/$marker" ]] && [[ $(cat "$destination/$marker") == "$image" ]] \
    && [[ -x "$destination/bin/m68k-amigaos-gcc" ]]; then
    echo "Amiga toolchain is already installed in $destination"
    exit 0
fi
if [[ -e "$destination" ]]; then
    echo "Refusing to replace $destination; choose an empty destination." >&2
    exit 1
fi
if [[ $(uname -s) != Linux || $(uname -m) != x86_64 ]]; then
    echo "This toolchain package requires Linux x86-64 and Docker." >&2
    exit 1
fi

staging=$(mktemp -d)
container=
cleanup() {
    if [[ -n "$container" ]]; then docker rm "$container" >/dev/null; fi
    rm -rf -- "$staging"
}
trap cleanup EXIT
docker pull "$image"
container=$(docker create --entrypoint /bin/true "$image")
mkdir "$staging/toolchain"
docker cp "$container:/opt/m68k-amigaos/." "$staging/toolchain/"
"$staging/toolchain/bin/m68k-amigaos-gcc" --version
printf '%s\n' "$image" > "$staging/toolchain/$marker"
mkdir -p -- "$(dirname -- "$destination")"
mv -- "$staging/toolchain" "$destination"
echo "Installed Amiga GCC in $destination"
