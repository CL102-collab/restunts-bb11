#!/bin/sh
# Prepare a target SDK on an amd64 OpenBSD host without running target programs.
set -eu

script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
target=${1:?Usage: prepare-openbsd-cross.sh TARGET SYSROOT}
sysroot=${2:?Usage: prepare-openbsd-cross.sh TARGET SYSROOT}
release=7.9
release_suffix=79
python_command=${PYTHON:-python3.13}

case "$target" in
    openbsd-arm64) sets_architecture=arm64; package_architecture=aarch64 ;;
    openbsd-riscv64) sets_architecture=riscv64; package_architecture=riscv64 ;;
    *) echo "Unsupported OpenBSD cross target: $target" >&2; exit 1 ;;
esac
case "$(uname -s):$(uname -m):$(uname -r)" in
    OpenBSD:amd64:"$release"|OpenBSD:x86_64:"$release") ;;
    *) echo "This helper requires an OpenBSD $release amd64 host." >&2; exit 1 ;;
esac
pkg_add -I bash cmake ninja git 'python%3' 'llvm%19'
# The base compiler contains only its host CPU backend. The ports compiler
# contains both target backends and uses OpenBSD's patched ELF linker.
compiler=/usr/local/bin/clang-19
case "$target" in
    openbsd-arm64) compiler_backend=aarch64 ;;
    openbsd-riscv64) compiler_backend=riscv64 ;;
esac
if ! "$compiler" --print-targets | grep -q "$compiler_backend"; then
    echo "The installed LLVM compiler does not support $compiler_backend." >&2
    exit 1
fi
if [ -e "$sysroot" ] && { [ ! -d "$sysroot" ] || [ -n "$(ls -A "$sysroot")" ]; }; then
    echo "Cross SDK directory must be empty: $sysroot" >&2
    exit 1
fi
mkdir -p "$sysroot"
sysroot=$(cd -- "$sysroot" && pwd)
download_dir=$(mktemp -d /tmp/restunts-openbsd-cross.XXXXXXXX)
trap 'rm -rf -- "$download_dir"' 0
trap 'exit 1' 1 2 15
sets_url="https://cdn.openbsd.org/pub/OpenBSD/$release/$sets_architecture"

for set_name in base comp xbase xshare; do
    archive="$set_name$release_suffix.tgz"
    checksum=$(awk -v name="$sets_architecture/$archive" \
        '$2 == "(" name ")" { print $4 }' "$script_dir/openbsd-cross-sets.sha256")
    if [ -z "$checksum" ]; then
        echo "Missing pinned checksum for $sets_architecture/$archive." >&2
        exit 1
    fi
    curl --fail --location --retry 3 --connect-timeout 20 \
        "$sets_url/$archive" --output "$download_dir/$archive"
    if [ "$(sha256 -q "$download_dir/$archive")" != "$checksum" ]; then
        echo "Checksum mismatch for $sets_architecture/$archive." >&2
        exit 1
    fi
    tar -xzpf "$download_dir/$archive" -C "$sysroot"
done
"$python_command" "$script_dir/extract-openbsd-sdk-packages.py" \
    "$sysroot" --normalize-links-only

# Dry-run mode fetches and verifies the complete dependency closure into
# PKG_CACHE. It does not run the target's package scripts or ldconfig.
mkdir -p "$download_dir/packages"
PKG_PATH="https://cdn.openbsd.org/pub/OpenBSD/$release/packages/$package_architecture/" \
    PKG_CACHE="$download_dir/packages" \
    pkg_add -n -I -A "$package_architecture" -B "$sysroot" gtk+3
"$python_command" "$script_dir/extract-openbsd-sdk-packages.py" \
    "$sysroot" "$download_dir/packages"
printf '%s\n' "$target" > "$sysroot/restunts-target"
